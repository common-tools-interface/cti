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
	std::string stagePath;

	// check to see if the caller set a staging directory name, otherwise generate one
	if (const char* customStagePath = getenv(DAEMON_STAGE_VAR)) {
		stagePath = customStagePath;
	} else {
		// remove placeholder Xs from DEFAULT_STAGE_DIR
		const std::string stageFormat(DEFAULT_STAGE_DIR);
		stagePath = stageFormat.substr(0, stageFormat.find("X"));

		// now start replacing the 'X' characters in the stage_name string with
		// randomness
		CTIPRNG prng;
		for (size_t i = 0; i < (stageFormat.length() - stagePath.length()); i++) {
			stagePath.push_back(prng.genChar());
		}
	}

	return stagePath;
}

Session::Session(appEntry_t *appPtr_) :
	appPtr(appPtr_),
	stagePath(generateStagePath()),
	toolPath(appPtr->wlmProto->wlm_getToolPath(appPtr->_wlmObj)),
	jobId(CharPtr(appPtr->wlmProto->wlm_getJobId(appPtr->_wlmObj), free).get()), 
	wlmEnum(std::to_string(appPtr->wlmProto->wlm_type)) {
		DEBUG("stagePath: " << stagePath << std::endl);
	}

void Session::shipWLMBaseFiles() {
	auto baseFileManifest = createManifest();

	auto wlmProto = appPtr->wlmProto;
	auto wlmObj = appPtr->_wlmObj;
	if (const char * const *elem = wlmProto->wlm_extraBinaries(wlmObj)) {
		for (; *elem != nullptr; elem++) {
			baseFileManifest->addBinary(*elem);
		}
	}
	if (const char * const *elem = wlmProto->wlm_extraLibraries(wlmObj)) {
		for (; *elem != nullptr; elem++) {
			baseFileManifest->addLibrary(*elem);
		}
	}
	if (const char * const *elem = wlmProto->wlm_extraLibDirs(wlmObj)) {
		for (; *elem != nullptr; elem++) {
			baseFileManifest->addLibDir(*elem);
		}
	}
	if (const char * const *elem = wlmProto->wlm_extraFiles(wlmObj)) {
		for (; *elem != nullptr; elem++) {
			baseFileManifest->addFile(*elem);
		}
	}

	// ship basefile manifest
	baseFileManifest->ship();
}

int Session::startDaemon(char * const argv[]) {
	auto cti_argv = UniquePtrDestr<cti_args_t>(_cti_newArgs(), _cti_freeArgs);
	for (char * const* arg = argv; *arg != nullptr; arg++) {
		_cti_addArg(cti_argv.get(), *arg);
	}
	return appPtr->wlmProto->wlm_startDaemon(appPtr->_wlmObj, cti_argv.get());
}

std::shared_ptr<Manifest> Session::createManifest() {
	manifests.push_back(std::make_shared<Manifest>(
		manifests.size(), shared_from_this()
	));
	return manifests.back();
}

Session::Conflict Session::hasFileConflict(const std::string& folder,
	const std::string& realName, const std::string& candidatePath) const {

	auto namePathPair = sourcePaths.find(realName);
	if (namePathPair != sourcePaths.end()) {
		if (!namePathPair->first.compare(candidatePath)) {
			return Conflict::AlreadyAdded;
		} else {
			return Conflict::NameOverwrite;
		}
	}

	return Conflict::None;
}

void Session::mergeTransfered(const FoldersMap& newFolders, const PathMap& newPaths) {
	for (auto folderContentsPair : newFolders) {
		const std::string& folderName = folderContentsPair.first;
		const std::set<std::string>& folderContents = folderContentsPair.second;

		folders[folderName].insert(folderContents.begin(), folderContents.end());
	}

	for (auto namePathPair : newPaths) {
		const std::string& fileName = namePathPair.first;
		const std::string& filePath = namePathPair.first;

		if (sourcePaths.find(fileName) != sourcePaths.end()) {
			throw std::runtime_error(
				std::string("tried to merge transfered file ") + fileName +
				" but it was already in the session!");
		}

		sourcePaths[fileName] = filePath;
	}

	shippedManifests++;
}