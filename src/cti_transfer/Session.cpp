#include "Session.hpp"
#include "Manifest.hpp"

#include "useful/cti_wrappers.hpp"

// getpid
#include <sys/types.h>
#include <unistd.h>
// valid chars array used in seed generation
static const char _cti_valid_char[] {
	'0','1','2','3','4','5','6','7','8','9',
	'A','B','C','D','E','F','G','H','I','J','K','L','M',
	'N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
	'a','b','c','d','e','f','g','h','i','j','k','l','m',
	'n','o','p','q','r','s','t','u','v','w','x','y','z' };
class CTIPRNG {
	char _cti_r_state[256];
public:
	CTIPRNG() {
		// We need to generate a good seed to avoid collisions. Since this
		// library can be used by automated tests, it is vital to have a
		// good seed.
		struct timespec		tv;
		unsigned int		pval;
		unsigned int		seed;
		
		// get the current time from epoch with nanoseconds
		if (clock_gettime(CLOCK_REALTIME, &tv)) {
			throw std::runtime_error("clock_gettime failed.");
		}
		
		// generate an appropriate value from the pid, we shift this to
		// the upper 16 bits of the int. This should avoid problems with
		// collisions due to slight variations in nano time and adding in
		// pid offsets.
		pval = (unsigned int)getpid() << ((sizeof(unsigned int) * CHAR_BIT) - 16);
		
		// Generate the seed. This is not crypto safe, but should have enough
		// entropy to avoid the case where two procs are started at the same
		// time that use this interface.
		seed = (tv.tv_sec ^ tv.tv_nsec) + pval;
		
		// init the state
		initstate(seed, (char *)_cti_r_state, sizeof(_cti_r_state));

		// set the PRNG state
		if (setstate((char *)_cti_r_state) == NULL) {
			throw std::runtime_error("setstate failed.");
		}
	}

	char genChar() {
		unsigned int oset;

		// Generate a random offset into the array. This is random() modded 
		// with the number of elements in the array.
		oset = random() % (sizeof(_cti_valid_char)/sizeof(_cti_valid_char[0]));
		// assing this char
		return _cti_valid_char[oset];
	}
};

std::string Session::generateStagePath() {
	std::string stageName;

	// check to see if the caller set a staging directory name, otherwise generate one
	if (const char* customStagePath = getenv(DAEMON_STAGE_VAR)) {
		stageName = customStagePath;
	} else {
		// remove placeholder Xs from DEFAULT_STAGE_DIR
		const std::string stageFormat(DEFAULT_STAGE_DIR);
		stageName = stageFormat.substr(0, stageFormat.find("X"));

		// now start replacing the 'X' characters in the stage_name string with
		// randomness
		CTIPRNG prng;
		size_t numChars = stageFormat.length() - stageName.length();
		for (size_t i = 0; i < numChars; i++) {
			stageName.push_back(prng.genChar());
		}
	}

	return stageName;
}

Session::Session(Frontend const& frontend, Frontend::AppId appId) :
	m_frontend(frontend),
	m_appId(appId),
	m_configPath(_cti_getCfgDir()),
	m_stageName(generateStagePath()),
	m_attribsPath(m_frontend.getApp(m_appId).getAttribsPath()),
	m_toolPath(m_frontend.getApp(m_appId).getToolPath()),
	m_jobId(m_frontend.getApp(m_appId).getJobId()), 
	m_wlmEnum(std::to_string(m_frontend.getWLMType())),
	m_ldLibraryPath(m_toolPath + "/" + m_stageName + "/lib") // default libdir /tmp/cti_daemonXXXXXX/lib
{}

#include "ArgvDefs.hpp"
void Session::launchCleanup() {
	DEBUG_PRINT("launchCleanup: creating daemonArgv for cleanup" << std::endl);

	// create DaemonArgv
	OutgoingArgv<DaemonArgv> daemonArgv("cti_daemon");
	{ using DA = DaemonArgv;
		daemonArgv.add(DA::ApID,         m_jobId);
		daemonArgv.add(DA::ToolPath,     m_toolPath);
		if (!m_attribsPath.empty()) { daemonArgv.add(DA::PMIAttribsPath, m_attribsPath); }
		daemonArgv.add(DA::WLMEnum,      m_wlmEnum);
		daemonArgv.add(DA::Directory,    m_stageName);
		daemonArgv.add(DA::InstSeqNum,   std::to_string(m_shippedManifests + 1));
		daemonArgv.add(DA::Clean);
		if (getenv(DBG_ENV_VAR)) { daemonArgv.add(DA::Debug); };
	}

	// call cleanup function with DaemonArgv
	// wlm_startDaemon adds the argv[0] automatically, so argv.get() + 1 for arguments.
	DEBUG_PRINT("launchCleanup: launching daemon for cleanup" << std::endl);
	startDaemon(daemonArgv.get() + 1);

	// session is finalized, no changes can be made
	invalidate();
}

void Session::startDaemon(char * const argv[]) {
	m_frontend.getApp(m_appId).startDaemon(argv);
}

std::shared_ptr<Manifest> Session::createManifest() {
	m_manifests.push_back(std::make_shared<Manifest>(m_manifests.size(), *this));
	return m_manifests.back();
}

static bool isSameFile(const std::string& filePath, const std::string& candidatePath) {
	// todo: could do something with file hashing?
	return !(filePath.compare(candidatePath));
}

Session::Conflict Session::hasFileConflict(const std::string& folderName,
	const std::string& realName, const std::string& candidatePath) const {

	// has /folderName/realName been shipped to the backend?
	const std::string fileArchivePath(folderName + "/" + realName);
	auto namePathPair = m_sourcePaths.find(fileArchivePath);
	if (namePathPair != m_sourcePaths.end()) {
		if (isSameFile(namePathPair->first, candidatePath)) {
			return Conflict::AlreadyAdded;
		} else {
			return Conflict::NameOverwrite;
		}
	}

	return Conflict::None;
}

std::vector<FolderFilePair>
Session::mergeTransfered(const FoldersMap& newFolders, const PathMap& newPaths) {
	std::vector<FolderFilePair> toRemove;

	for (auto folderContentsPair : newFolders) {
		const std::string& folderName = folderContentsPair.first;
		const std::set<std::string>& folderContents = folderContentsPair.second;

		for (auto fileName : folderContents) {
			// mark fileName to be located at /folderName/fileName
			m_folders[folderName].insert(fileName);

			// map /folderName/fileName to source file path newPaths[fileName]
			const std::string fileArchivePath(folderName + "/" + fileName);
			if (m_sourcePaths.find(fileArchivePath) != m_sourcePaths.end()) {
				throw std::runtime_error(
					std::string("tried to merge transfered file ") + fileArchivePath +
					" but it was already in the session!");
			} else {
				if (isSameFile(m_sourcePaths[fileArchivePath], newPaths.at(fileName))) {
					// duplicate, tell manifest to not bother shipping
					toRemove.push_back(std::make_pair(folderName, fileName));
				} else {
					// register new file as coming from Manifest's source
					m_sourcePaths[fileArchivePath] = newPaths.at(fileName);
				}
			}
		}
	}
	m_shippedManifests++;

	return toRemove;
}