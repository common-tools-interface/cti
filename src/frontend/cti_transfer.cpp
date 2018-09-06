#include <stdexcept>
#include <sstream>
#include <memory>

#include <string.h>

#include "cti_fe.h"
#include "cti_error.h"

#include "cti_transfer/Manifest.hpp"
#include "cti_transfer/Session.hpp"

#include "cti_transfer.h"

std::map<cti_session_id_t, std::shared_ptr<Session>> sessions;
static const cti_session_id_t SESSION_ERROR = 0;
static cti_session_id_t newSessionId() noexcept {
	static cti_session_id_t nextId = 1;
	return nextId++;
}

// manifest objects are created by and owned by session objects
std::map<cti_manifest_id_t, std::shared_ptr<Manifest>> manifests;
static const cti_manifest_id_t MANIFEST_ERROR = 0;
static cti_manifest_id_t newManifestId() noexcept {
	static cti_manifest_id_t nextId = 1;
	return nextId++;
}

/* session implementations */

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

cti_session_id_t cti_createSession(cti_app_id_t appId) {
	cti_session_id_t sid = newSessionId();

	// construct throwing lambda that can be called by runSafely
	auto insertSession = [&]() {
		// get appPtr from appId
		if (appEntry_t *appPtr = _cti_findAppEntry(appId)) {
			sessions.insert({sid, std::shared_ptr<Session>(new Session(appPtr))});
		} else {
			throw std::runtime_error(
				"cannot create session: appId %d not found" + std::to_string(appId));
		}
	};

	return runSafely(insertSession) ? SESSION_ERROR : sid;
}

int cti_sessionIsValid(cti_session_id_t sid) {
	return sessions.find(sid) != sessions.end();
}

int cti_destroySession(cti_session_id_t sid) {
	if (!cti_sessionIsValid(sid)) {
		throw std::runtime_error("cti_destroySession: invalid session id " + std::to_string(sid));
	}

	sessions.erase(sid);

	return 0;
}

static std::shared_ptr<Session>
getSessionHandle(const std::string& caller, cti_session_id_t sid) {
	if (!cti_sessionIsValid(sid)) {
		throw std::runtime_error(caller + ": invalid session id " + std::to_string(sid));
	}

	return sessions.at(sid);
}

char** cti_getSessionLockFiles(cti_session_id_t sid) {
	char **result;

	// construct throwing lambda that can be called by runSafely
	auto getLockFiles = [&]() {
		auto sessionPtr = getSessionHandle("cti_getSessionLockFiles", sid);

		// ensure there's at least one manifest instance
		if (sessionPtr->getNumManifests() == 0) {
			throw std::runtime_error("cti_getSessionLockFiles: backend not initialized for session id " + std::to_string(sid));
		}

		// create return array
		result = (char**)malloc(sizeof(char*) * (sessionPtr->getNumManifests() + 1));
		if (result == nullptr) {
			throw std::runtime_error("cti_getSessionLockFiles: malloc failed for session id " + std::to_string(sid));
		}

		// create the strings
		for (size_t i = 0; i < sessionPtr->getNumManifests(); i++) {
			std::stringstream ss;
			ss << sessionPtr->toolPath << "/.lock_" << sessionPtr->stagePath << "_" << i;
			result[i] = strdup(ss.str().c_str());
		}
		result[sessionPtr->getNumManifests()] = nullptr;
	};

	return runSafely(getLockFiles) ? nullptr : result;
}

// fill in a heap string pointer to session root path plus subdirectory
static char* sessionPathAppend(const std::string& caller, 
	cti_session_id_t sid, const std::string& str) {
	char *result;

	auto constructPath = [&]() {
		// get session and construct string
		auto sessionPtr = getSessionHandle(caller, sid);
		std::stringstream ss;
		ss << sessionPtr->toolPath << "/" << sessionPtr->stagePath << str;
		result = strdup(ss.str().c_str());
	};

	return runSafely(constructPath) ? nullptr : result;
}

char* cti_getSessionRootDir(cti_session_id_t sid) {
	return sessionPathAppend("cti_getSessionRootDir", sid, "");
}

char* cti_getSessionBinDir(cti_session_id_t sid) {
	return sessionPathAppend("cti_getSessionBinDir", sid, "/bin");
}

char* cti_getSessionLibDir(cti_session_id_t sid) {
	return sessionPathAppend("cti_getSessionLibDir", sid, "/lib");
}

char* cti_getSessionFileDir(cti_session_id_t sid) {
	return sessionPathAppend("cti_getSessionFileDir", sid, "");
}

char* cti_getSessionTmpDir(cti_session_id_t sid) {
	return sessionPathAppend("cti_getSessionTmpDir", sid, "/tmp");
}

/* manifest implementations */

cti_manifest_id_t cti_createManifest(cti_session_id_t sid) {
	cti_manifest_id_t mid = newManifestId();

	// construct throwing lambda that can be called by runSafely
	auto insertManifest = [&]() {
		// get session and create manifest
		auto sessionPtr = getSessionHandle("cti_createManifest", sid);
		manifests.insert({mid, sessionPtr->createManifest()});
	};

	return runSafely(insertManifest) ? MANIFEST_ERROR : mid;
}

int cti_manifestIsValid(cti_manifest_id_t mid) {
	return manifests.find(mid) != manifests.end();
}

static std::shared_ptr<Manifest>
getManifestHandle(const std::string& caller, cti_manifest_id_t mid) {
	if (!cti_manifestIsValid(mid)) {
		throw std::runtime_error(caller + ": invalid manifest id " + std::to_string(mid));
	}

	return manifests.at(mid);
}

int cti_addManifestBinary(cti_manifest_id_t mid, const char * rawName) {
	return runSafely([&](){
		getManifestHandle("cti_addManifestBinary", mid)->addBinary(rawName);
	});
}

int cti_addManifestLibrary(cti_manifest_id_t mid, const char * rawName) {
	return runSafely([&](){
		getManifestHandle("cti_addManifestLibrary", mid)->addLibrary(rawName);
	});
}

int cti_addManifestLibDir(cti_manifest_id_t mid, const char * rawName) {
	return runSafely([&](){
		getManifestHandle("cti_addManifestLibDir", mid)->addLibDir(rawName);
	});
}

int cti_addManifestFile(cti_manifest_id_t mid, const char * rawName) {
	return runSafely([&](){
		getManifestHandle("cti_addManifestFile", mid)->addFile(rawName);
	});
}

int cti_sendManifest(cti_manifest_id_t mid) {
	return runSafely([&](){
		getManifestHandle("cti_sendManifest", mid)->send();
	});
}

/* tool daemon prototypes */
int cti_execToolDaemon(cti_manifest_id_t mid, const char *daemon_path,
	const char * const daemon_args[], const char * const env_vars[]) {
	cti_sendManifest(mid);
	throw std::runtime_error("not implemented: cti_execToolDaemon");
}

#ifdef TRANSITION_DEFS
#include <iostream>
void _cti_setStageDeps(bool stageDeps) {
	std::cerr << "deprecated: _cti_setStageDeps" << std::endl;
}

void _cti_transfer_init(void) {
	std::cerr << "deprecated: _cti_setStageDeps" << std::endl;
}

void _cti_transfer_fini(void) {
	std::cerr << "deprecated: _cti_setStageDeps" << std::endl;
}

void _cti_consumeSession(void *) {
	std::cerr << "deprecated: _cti_consumeSession" << std::endl;
}
#endif