#include <stdexcept>
#include <sstream>
#include <memory>

#include <string.h>

#include "cti_transfer/Manifest.hpp"
#include "cti_transfer/Session.hpp"

#include "cti_fe.h"
#include "cti_error.h"

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

cti_session_id_t cti_createSession(cti_session_id_t appId) {
	cti_session_id_t sid = newSessionId();

	// get appPtr from appId
	appEntry_t *appPtr = _cti_findAppEntry(appId);
	if (appPtr == nullptr) {
		_cti_set_error("cannot create session: appId %d not found", appId);
		return SESSION_ERROR;
	}

	// emplace new session in the list
	try {
		sessions.emplace(std::piecewise_construct,
			std::forward_as_tuple(sid),
			std::forward_as_tuple(appPtr));
	} catch (std::exception& ex) {
		if (ex.what()[0]) {
			_cti_set_error(ex.what());
		}
		return SESSION_ERROR;
	}

	return sid;
}

int cti_sessionIsValid(cti_session_id_t sid) {
	return sessions.find(sid) != sessions.end();
}

char** cti_getSessionLockFiles(cti_session_id_t sid) {
	if (!cti_sessionIsValid(sid)) {
		_cti_set_error("cti_getSessionLockFiles: invalid session id %d", sid);
		return NULL;
	}

	// fetch session handle
	Session& session = sessions.at(sid);

	// ensure there's at least one manifest instance
	if (session.getNumManifests() == 0) {
		_cti_set_error("cti_getSessionLockFiles: backend not initialized for session id %d", sid);
		return NULL;
	}

	// create return array
	char **result = (char**)malloc(sizeof(char*) * (session.getNumManifests() + 1));
	if (result == nullptr) {
		_cti_set_error("cti_getSessionLockFiles: malloc failed for session id %d", sid);
		return NULL;
	}

	// create the strings
	for (size_t i = 0; i < session.getNumManifests(); i++) {
		std::stringstream ss;
		ss << session.toolPath << "/.lock_" << session.stagePath << "_" << i;
		result[i] = strdup(ss.str().c_str());
	}
	result[session.getNumManifests()] = nullptr;

	return result;
}

// return a heap string pointer to session root path plus subdirectory
static char* sessionPathAppend(const std::string& caller, cti_session_id_t sid,
		const std::string& str) {
	if (!cti_sessionIsValid(sid)) {
		_cti_set_error("%s: invalid session id %d", caller.c_str(), sid);
		return NULL;
	}

	Session& session = sessions.at(sid);
	std::stringstream ss;
	ss << session.toolPath << "/" << session.stagePath << str;
	return strdup(ss.str().c_str());
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
	if (!cti_sessionIsValid(sid)) {
		_cti_set_error("cti_createManifest: invalid session id %d", sid);
		return MANIFEST_ERROR;
	}

	// fetch session handle
	Session& session = sessions.at(sid);
	cti_manifest_id_t mid = newManifestId();

	// emplace new manifest in the list
	try {
		manifests.emplace(std::piecewise_construct,
			std::forward_as_tuple(mid),
			std::forward_as_tuple(session.createManifest()));
	} catch (std::exception& ex) {
		if (ex.what()[0]) {
			_cti_set_error(ex.what());
		}
		return MANIFEST_ERROR;
	}

	return mid;
}

int cti_manifestIsValid(cti_manifest_id_t mid) {
	return manifests.find(mid) != manifests.end();
}

int cti_addManifestBinary(cti_manifest_id_t mid, const char * path);
int cti_addManifestLibrary(cti_manifest_id_t mid, const char * path);
int cti_addManifestLibDir(cti_manifest_id_t mid, const char * path);
int cti_addManifestFile(cti_manifest_id_t mid, const char * path);

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