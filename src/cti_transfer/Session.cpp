#include "Session.hpp"
#include "Manifest.hpp"

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

static std::string emptyFromNullPtr(const char* cstr) {
	return cstr ? std::string(cstr) : "";
}
Session::Session(appEntry_t *appPtr_) :
	appPtr(appPtr_),
	configPath(emptyFromNullPtr(_cti_getCfgDir())),
	stageName(generateStagePath()),
	attribsPath(emptyFromNullPtr(appPtr->wlmProto->wlm_getAttribsPath(appPtr->_wlmObj))),
	toolPath(appPtr->wlmProto->wlm_getToolPath(appPtr->_wlmObj)),
	jobId(CharPtr(appPtr->wlmProto->wlm_getJobId(appPtr->_wlmObj), free).get()), 
	wlmEnum(std::to_string(appPtr->wlmProto->wlm_type)) {}

#include "ArgvDefs.hpp"
void Session::launchCleanup() {
	DEBUG_PRINT("launchCleanup: creating daemonArgv for cleanup" << std::endl);

	// create DaemonArgv
	OutgoingArgv<DaemonArgv> daemonArgv("cti_daemon");
	{ using DA = DaemonArgv;
		daemonArgv.add(DA::ApID,         jobId);
		daemonArgv.add(DA::ToolPath,     toolPath);
		if (!attribsPath.empty()) { daemonArgv.add(DA::PMIAttribsPath, attribsPath); }
		daemonArgv.add(DA::WLMEnum,      wlmEnum);
		daemonArgv.add(DA::Directory,    stageName);
		daemonArgv.add(DA::InstSeqNum,   std::to_string(shippedManifests + 1));
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


void Session::shipWLMBaseFiles() {
	auto baseFileManifest = createManifest();

	auto wlmProto = appPtr->wlmProto;
	auto wlmObj = appPtr->_wlmObj;

	using CStr = const char * const;
	auto forEachCStrArr = [&](std::function<void(CStr)> adder, CStr* arr) {
		if (arr != nullptr) { for (; *arr != nullptr; arr++) { adder(*arr); } }
	};
	forEachCStrArr([&](CStr cstr) { baseFileManifest->addBinary(cstr);  },
		wlmProto->wlm_extraBinaries(wlmObj));
	forEachCStrArr([&](CStr cstr) { baseFileManifest->addLibrary(cstr); },
		wlmProto->wlm_extraLibraries(wlmObj));
	forEachCStrArr([&](CStr cstr) { baseFileManifest->addLibDir(cstr);  },
		wlmProto->wlm_extraLibDirs(wlmObj));
	forEachCStrArr([&](CStr cstr) { baseFileManifest->addFile(cstr);    },
		wlmProto->wlm_extraFiles(wlmObj));

	// ship basefile manifest and run remote extraction
	baseFileManifest->finalizeAndShip().extract();
}

int Session::startDaemon(char * const argv[]) {
	auto cti_argv = UniquePtrDestr<cti_args_t>(_cti_newArgs(), _cti_freeArgs);
	for (char * const* arg = argv; *arg != nullptr; arg++) {
		_cti_addArg(cti_argv.get(), *arg);
	}
	return getWLM()->wlm_startDaemon(appPtr->_wlmObj, cti_argv.get());
}

std::shared_ptr<Manifest> Session::createManifest() {
	manifests.push_back(std::make_shared<Manifest>(
		manifests.size(), shared_from_this()
	));
	return manifests.back();
}

static bool isSameFile(const std::string& filePath, const std::string& candidatePath) {
	// todo: could do something with file hashing?
	return !(filePath.compare(candidatePath));
}

Session::Conflict Session::hasFileConflict(const std::string& folderName,
	const std::string& realName, const std::string& candidatePath) const {

	// has /folderName/realName been shipped to the backend?
	const std::string fileArchivePath(folderName + "/" + realName);
	auto namePathPair = sourcePaths.find(fileArchivePath);
	if (namePathPair != sourcePaths.end()) {
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
			folders[folderName].insert(fileName);

			// map /folderName/fileName to source file path newPaths[fileName]
			const std::string fileArchivePath(folderName + "/" + fileName);
			if (sourcePaths.find(fileArchivePath) != sourcePaths.end()) {
				throw std::runtime_error(
					std::string("tried to merge transfered file ") + fileArchivePath +
					" but it was already in the session!");
			} else {
				if (isSameFile(sourcePaths[fileArchivePath], newPaths.at(fileName))) {
					// duplicate, tell manifest to not bother shipping
					toRemove.push_back(std::make_pair(folderName, fileName));
				} else {
					// register new file as coming from Manifest's source
					sourcePaths[fileArchivePath] = newPaths.at(fileName);
				}
			}
		}
	}
	shippedManifests++;

	return toRemove;
}