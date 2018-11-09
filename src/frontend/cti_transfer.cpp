#include <stdexcept>
#include <sstream>
#include <memory>

#include <string.h>

#include "cti_fe.h"
#include "cti_error.h"

#include "cti_transfer/Manifest.hpp"
#include "cti_transfer/Session.hpp"

#include "cti_transfer.h"

static std::unordered_map<cti_session_id_t, std::shared_ptr<Session>> sessions;
static const cti_session_id_t SESSION_ERROR = 0;
static cti_session_id_t newSessionId() noexcept {
	static cti_session_id_t nextId = 1;
	return nextId++;
}

// manifest objects are created by and owned by session objects
static std::unordered_map<cti_manifest_id_t, std::shared_ptr<Manifest>> manifests;
static const cti_manifest_id_t MANIFEST_ERROR = 0;
static cti_manifest_id_t newManifestId() noexcept {
	static cti_manifest_id_t nextId = 1;
	return nextId++;
}

// run code that can throw and use it to set cti error instead
static int runSafely(std::function<void()> f) noexcept {
	try { // try to run the function
		f();
		return 0;
	} catch (const std::exception& ex) {
		// if we get an exception, set cti error instead
		_cti_set_error(ex.what());
		return 1;
	}
}

/* session implementations */

cti_session_id_t cti_createSession(cti_app_id_t appId) {
	cti_session_id_t sid = newSessionId();

	// construct throwing lambda that can be called by runSafely
	auto insertSession = [&]() {
		// get appPtr from appId
		if (appEntry_t *appPtr = _cti_findAppEntry(appId)) {
			// create session instance
			auto newSession = std::make_shared<Session>(appPtr);
			newSession->shipWLMBaseFiles();
			sessions.insert(std::make_pair(sid, newSession));

			// add pointer to sid to appEntry's session list
			ctiListAdd(appPtr->sessions, new cti_session_id_t(sid));
		} else {
			throw std::runtime_error(
				"failed, appId not found: " + std::to_string(appId));
		}
	};

	return runSafely(insertSession) ? SESSION_ERROR : sid;
}

int cti_sessionIsValid(cti_session_id_t sid) {
	return sessions.find(sid) != sessions.end();
}

static std::shared_ptr<Session>
getSessionHandle(cti_session_id_t sid) {
	if (!cti_sessionIsValid(sid)) {
		throw std::runtime_error("invalid session id " + std::to_string(sid));
	}

	return sessions.at(sid);
}

int cti_destroySession(cti_session_id_t sid) {
	return runSafely([&]() {
		getSessionHandle(sid)->launchCleanup();
		sessions.erase(sid);
	});
}

char** cti_getSessionLockFiles(cti_session_id_t sid) {
	char **result;

	// construct throwing lambda that can be called by runSafely
	auto getLockFiles = [&]() {
		auto sessionPtr = getSessionHandle(sid);
		const auto& manifests = sessionPtr->getManifests();

		// ensure there's at least one manifest instance
		if (manifests.size() == 0) {
			throw std::runtime_error("backend not initialized for session id " + std::to_string(sid));
		}

		// create return array
		result = (char**)malloc(sizeof(char*) * (manifests.size() + 1));
		if (result == nullptr) {
			throw std::runtime_error("malloc failed for session id " + std::to_string(sid));
		}

		// create the strings
		for (size_t i = 0; i < manifests.size(); i++) {
			result[i] = strdup(manifests[i]->lockFilePath.c_str());
		}
		result[manifests.size()] = nullptr;
	};

	return runSafely(getLockFiles) ? nullptr : result;
}

// fill in a heap string pointer to session root path plus subdirectory
static char* sessionPathAppend(cti_session_id_t sid, const std::string& str) {
	char *result;

	auto constructPath = [&]() {
		// get session and construct string
		auto sessionPtr = getSessionHandle(sid);
		std::stringstream ss;
		ss << sessionPtr->toolPath << "/" << sessionPtr->stageName << str;
		result = strdup(ss.str().c_str());
	};

	return runSafely(constructPath) ? nullptr : result;
}

char* cti_getSessionRootDir(cti_session_id_t sid) {
	return sessionPathAppend(sid, "");
}

char* cti_getSessionBinDir(cti_session_id_t sid) {
	return sessionPathAppend(sid, "/bin");
}

char* cti_getSessionLibDir(cti_session_id_t sid) {
	return sessionPathAppend(sid, "/lib");
}

char* cti_getSessionFileDir(cti_session_id_t sid) {
	return sessionPathAppend(sid, "");
}

char* cti_getSessionTmpDir(cti_session_id_t sid) {
	return sessionPathAppend(sid, "/tmp");
}

/* manifest implementations */

cti_manifest_id_t cti_createManifest(cti_session_id_t sid) {
	cti_manifest_id_t mid = newManifestId();

	// construct throwing lambda that can be called by runSafely
	auto insertManifest = [&]() {
		manifests.insert({mid, getSessionHandle(sid)->createManifest()});
	};

	return runSafely(insertManifest) ? MANIFEST_ERROR : mid;
}

int cti_manifestIsValid(cti_manifest_id_t mid) {
	return manifests.find(mid) != manifests.end();
}

static std::shared_ptr<Manifest>
getManifestHandle(cti_manifest_id_t mid) {
	if (!cti_manifestIsValid(mid)) {
		throw std::runtime_error("invalid manifest id " + std::to_string(mid));
	}

	return manifests.at(mid);
}

int cti_addManifestBinary(cti_manifest_id_t mid, const char * rawName) {
	return runSafely([&](){
		getManifestHandle(mid)->addBinary(rawName);
	});
}

int cti_addManifestLibrary(cti_manifest_id_t mid, const char * rawName) {
	return runSafely([&](){
		getManifestHandle(mid)->addLibrary(rawName);
	});
}

int cti_addManifestLibDir(cti_manifest_id_t mid, const char * rawName) {
	return runSafely([&](){
		getManifestHandle(mid)->addLibDir(rawName);
	});
}

int cti_addManifestFile(cti_manifest_id_t mid, const char * rawName) {
	return runSafely([&](){
		getManifestHandle(mid)->addFile(rawName);
	});
}

int cti_sendManifest(cti_manifest_id_t mid) {
	return runSafely([&](){
		auto remotePackage = getManifestHandle(mid)->finalizeAndShip();
		remotePackage.extract();
		manifests.erase(mid);
	});
}

/* tool daemon prototypes */
int cti_execToolDaemon(cti_manifest_id_t mid, const char *daemonPath,
	const char * const daemonArgs[], const char * const envVars[]) {
	return runSafely([&](){
		auto manifestPtr = getManifestHandle(mid);

		manifestPtr->addBinary(daemonPath);
		auto remotePackage = manifestPtr->finalizeAndShip();
		remotePackage.extractAndRun(daemonPath, daemonArgs, envVars);
		manifests.erase(mid);
	});
}

bool _cti_stage_deps = true; // extern defined in cti_transfer.h
void _cti_setStageDeps(bool stageDeps) {
	_cti_stage_deps = stageDeps;
}

void _cti_consumeSession(void* rawSidPtr) {
	if (rawSidPtr == nullptr) {
		return;
	}

	auto sidPtr = static_cast<cti_session_id_t*>(rawSidPtr);
	runSafely([&]() {
		auto const& sessionHandle = getSessionHandle(*sidPtr);
		ctiListRemove(sessionHandle->appPtr->sessions, sidPtr);
	});
	cti_destroySession(*sidPtr);
	delete sidPtr;
}

void _cti_transfer_init(void) { /* no-op */ }

void _cti_transfer_fini(void) {
	sessions.clear();
}