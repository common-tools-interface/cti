#include <stdexcept>
#include <sstream>
#include <memory>

#include <string.h>

#include "cti_transfer/Manifest.hpp"
#include "cti_transfer/Session.hpp"

extern "C" {
	#include "cti_fe.h"
	#include "cti_error.h"
}

#include "cti_transfer.h"

std::map<cti_session_id_t, Session> sessions;
static const cti_session_id_t SESSION_ERROR = 0;
static cti_session_id_t newSessionId() noexcept {
	static cti_session_id_t nextId = 1;
	return nextId++;
}

// manifest objects are created by and owned by session objects
std::map<cti_manifest_id_t, std::weak_ptr<Manifest>> manifests;
static const cti_manifest_id_t MANIFEST_ERROR = 0;
static cti_manifest_id_t newManifestId() noexcept {
	static cti_manifest_id_t nextId = 1;
	return nextId++;
}

/* session implementations */

// run code that can throw and use it to set cti error instead
static int
runSafely(std::function<void()> f) noexcept {
	try { // try to run the function
		f();
		return 0;
	} catch (const std::exception& ex) {
		// if we get an exception, set cti error instead
		_cti_set_error(ex.what());
		return 1;
	}
}

cti_session_id_t cti_createSession(cti_session_id_t appId, bool stageDeps) {
	cti_session_id_t sid = newSessionId();

	// construct throwing lambda that can be called by runSafely
	auto emplaceSession = [&]() {
		// get appPtr from appId
		if (appEntry_t *appPtr = _cti_findAppEntry(appId)) {
			// emplace new session in the list
			sessions.emplace(std::piecewise_construct,
				std::forward_as_tuple(sid),
				std::forward_as_tuple(appPtr));
		} else {
			throw std::runtime_error(
				"cannot create session: appId %d not found" + std::to_string(appId));
		}
	};

	return runSafely(emplaceSession) ? SESSION_ERROR : sid;
}

int cti_sessionIsValid(cti_session_id_t sid) {
	return sessions.find(sid) != sessions.end();
}

Session& getSessionHandle(const std::string& caller, cti_session_id_t sid) {
	if (!cti_sessionIsValid(sid)) {
		throw std::runtime_error(caller + ": invalid session id " + std::to_string(sid));
	}

	return sessions.at(sid);
}

char** cti_getSessionLockFiles(cti_session_id_t sid) {
	char **result;

	// construct throwing lambda that can be called by runSafely
	auto getLockFiles = [&]() {
		Session& session = getSessionHandle("cti_getSessionLockFiles", sid);

		// ensure there's at least one manifest instance
		if (session.getNumManifests() == 0) {
			throw std::runtime_error("cti_getSessionLockFiles: backend not initialized for session id " + std::to_string(sid));
		}

		// create return array
		result = (char**)malloc(sizeof(char*) * (session.getNumManifests() + 1));
		if (result == nullptr) {
			throw std::runtime_error("cti_getSessionLockFiles: malloc failed for session id " + std::to_string(sid));
		}

		// create the strings
		for (size_t i = 0; i < session.getNumManifests(); i++) {
			std::stringstream ss;
			ss << session.toolPath << "/.lock_" << session.stagePath << "_" << i;
			result[i] = strdup(ss.str().c_str());
		}
		result[session.getNumManifests()] = nullptr;
	};

	return runSafely(getLockFiles) ? nullptr : result;
}

// fill in a heap string pointer to session root path plus subdirectory
static char* sessionPathAppend(const std::string& caller, 
	cti_session_id_t sid, const std::string& str) {
	char *result;

	auto constructPath = [&]() {
		// get session and construct string
		Session& session = getSessionHandle(caller, sid);
		std::stringstream ss;
		ss << session.toolPath << "/" << session.stagePath << str;
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
	auto emplaceManifest = [&]() {
		// get session and emplace manifest
		Session& session = getSessionHandle("cti_createManifest", sid);

		manifests.emplace(std::piecewise_construct,
			std::forward_as_tuple(mid),
			std::forward_as_tuple(session.createManifest()));
	};

	return runSafely(emplaceManifest) ? MANIFEST_ERROR : mid;
}

int cti_manifestIsValid(cti_manifest_id_t mid) {
	return manifests.find(mid) != manifests.end();
}

std::shared_ptr<Manifest>
getManifestHandle(const std::string& caller, cti_manifest_id_t mid) {
	// lookup manifest in mid map
	auto it = manifests.find(mid);
	if (it != manifests.end()) {
		// try and promote weak manifest pointer to shared pointer
		if (auto manifestPtr = manifests.at(mid).lock()) {
			return manifestPtr;
		}
	}

	throw std::runtime_error(caller + ": invalid manifest id " + std::to_string(mid));
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

int cti_sendManifest(cti_manifest_id_t mid);

/* tool daemon prototypes */
int cti_execToolDaemon(cti_manifest_id_t mid, const char *daemon_path,
	const char * const daemon_args[], const char * const env_vars[]);

#ifdef TRANSITION_DEFS
void _cti_setStageDeps(bool stageDeps) {
	throw std::runtime_error("not implemented: _cti_setStageDeps");
}

void _cti_transfer_init(void) {
	throw std::runtime_error("not implemented: _cti_transfer_init");
}

void _cti_transfer_fini(void) {
	throw std::runtime_error("not implemented: _cti_transfer_fini");
}

void _cti_consumeSession(void *) {
	throw std::runtime_error("not implemented: _cti_consumeSession");
}
#endif