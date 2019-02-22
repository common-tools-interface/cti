#include <stdexcept>
#include <sstream>
#include <memory>

#include <string.h>

#include "cti_fe.h"
#include "cti_error.h"

#include "frontend/Frontend.hpp"
#include "cti_transfer/Manifest.hpp"
#include "cti_transfer/Session.hpp"

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
template <typename FuncType, typename ReturnType = decltype(std::declval<FuncType>()())>
static ReturnType runSafely(std::string const& caller, FuncType&& func, ReturnType const onError) {
	try {
		return std::forward<FuncType>(func)();
	} catch (std::exception const& ex) {
		_cti_set_error((caller + ": " + ex.what()).c_str());
		return onError;
	}
}

/* session implementations */

// create and add wlm basefiles to manifest. run this after creating a Session
static void shipWLMBaseFiles(Session& liveSession) {
	auto& frontend = _cti_getCurrentFrontend();

	auto baseFileManifest = liveSession.createManifest();
	for (auto const& path : frontend.getApp(liveSession.m_appId).getExtraBinaries()) {
		baseFileManifest->addBinary(path);
	}
	for (auto const& path : frontend.getApp(liveSession.m_appId).getExtraLibraries()) {
		baseFileManifest->addLibrary(path);
	}
	for (auto const& path : frontend.getApp(liveSession.m_appId).getExtraLibDirs()) {
		baseFileManifest->addLibDir(path);
	}
	for (auto const& path : frontend.getApp(liveSession.m_appId).getExtraFiles()) {
		baseFileManifest->addFile(path);
	}

	// ship basefile manifest and run remote extraction
	baseFileManifest->finalizeAndShip().extract();
}

cti_session_id_t
cti_createSession(cti_app_id_t appId) {
	return runSafely("cti_createSession", [&](){
		auto const sid = newSessionId();

		// create session instance
		auto newSession = std::make_shared<Session>(_cti_getCurrentFrontend(), appId);
		shipWLMBaseFiles(*newSession);
		sessions.insert(std::make_pair(sid, newSession));
		return sid;
	}, SESSION_ERROR);
}

int
cti_sessionIsValid(cti_session_id_t sid) {
	return runSafely("cti_sessionIsValid", [&](){
		return sessions.find(sid) != sessions.end();
	}, false);
}

static Session& getSession(cti_session_id_t sid) {
	if (!cti_sessionIsValid(sid)) {
		throw std::runtime_error("invalid session id " + std::to_string(sid));
	}
	return *(sessions.at(sid));
}

char**
cti_getSessionLockFiles(cti_session_id_t sid) {
	return runSafely("cti_getSessionLockFiles", [&](){
		auto const& manifests = getSession(sid).getManifests();

		// ensure there's at least one manifest instance
		if (manifests.size() == 0) {
			throw std::runtime_error("backend not initialized for session id " + std::to_string(sid));
		}

		// create return array
		auto result = (char**)malloc(sizeof(char*) * (manifests.size() + 1));
		if (result == nullptr) {
			throw std::runtime_error("malloc failed for session id " + std::to_string(sid));
		}

		// create the strings
		for (size_t i = 0; i < manifests.size(); i++) {
			result[i] = strdup(manifests[i]->m_lockFilePath.c_str());
		}
		result[manifests.size()] = nullptr;
		return result;
	}, (char**)nullptr);
}

// fill in a heap string pointer to session root path plus subdirectory
static char* sessionPathAppend(std::string const& caller, cti_session_id_t sid, const std::string& str) {
	return runSafely(caller, [&](){
		// get session and construct string
		auto const& session = getSession(sid);
		std::stringstream ss;
		ss << session.m_toolPath << "/" << session.m_stageName << str;
		return strdup(ss.str().c_str());
	}, (char*)nullptr);
}

char*
cti_getSessionRootDir(cti_session_id_t sid) {
	return sessionPathAppend("cti_getSessionRootDir", sid, "");
}

char*
cti_getSessionBinDir(cti_session_id_t sid) {
	return sessionPathAppend("cti_getSessionBinDir", sid, "/bin");
}

char*
cti_getSessionLibDir(cti_session_id_t sid) {
	return sessionPathAppend("cti_getSessionLibDir", sid, "/lib");
}

char*
cti_getSessionFileDir(cti_session_id_t sid) {
	return sessionPathAppend("cti_getSessionFileDir", sid, "");
}

char*
cti_getSessionTmpDir(cti_session_id_t sid) {
	return sessionPathAppend("cti_getSessionTmpDir", sid, "/tmp");
}

/* manifest implementations */

cti_manifest_id_t
cti_createManifest(cti_session_id_t sid) {
	return runSafely("cti_createManifest", [&](){
		auto const mid = newManifestId();
		manifests.insert({mid, getSession(sid).createManifest()});
		return mid;
	}, MANIFEST_ERROR);
}

int
cti_manifestIsValid(cti_manifest_id_t mid) {
	return runSafely("cti_manifestIsValid", [&](){
		return manifests.find(mid) != manifests.end();
	}, false);
}

namespace {
	static constexpr auto SUCCESS = int{0};
	static constexpr auto FAILURE = int{1};
}

int
cti_destroySession(cti_session_id_t sid) {
	return runSafely("cti_destroySession", [&](){
		getSession(sid).launchCleanup();
		sessions.erase(sid);
		return SUCCESS;
	}, FAILURE);
}

static Manifest& getManifest(cti_manifest_id_t mid) {
	if (!cti_manifestIsValid(mid)) {
		throw std::runtime_error("invalid manifest id " + std::to_string(mid));
	}
	return *(manifests.at(mid));
}

int
cti_addManifestBinary(cti_manifest_id_t mid, const char * rawName) {
	return runSafely("cti_addManifestBinary", [&](){
		getManifest(mid).addBinary(rawName);
		return SUCCESS;
	}, FAILURE);
}

int
cti_addManifestLibrary(cti_manifest_id_t mid, const char * rawName) {
	return runSafely("cti_addManifestLibrary", [&](){
		getManifest(mid).addLibrary(rawName);
		return SUCCESS;
	}, FAILURE);
}

int
cti_addManifestLibDir(cti_manifest_id_t mid, const char * rawName) {
	return runSafely("cti_addManifestLibDir", [&](){
		getManifest(mid).addLibDir(rawName);
		return SUCCESS;
	}, FAILURE);
}

int
cti_addManifestFile(cti_manifest_id_t mid, const char * rawName) {
	return runSafely("cti_addManifestFile", [&](){
		getManifest(mid).addFile(rawName);
		return SUCCESS;
	}, FAILURE);
}

int
cti_sendManifest(cti_manifest_id_t mid) {
	return runSafely("cti_sendManifest", [&](){
		auto remotePackage = getManifest(mid).finalizeAndShip();
		remotePackage.extract();
		manifests.erase(mid);
		return SUCCESS;
	}, FAILURE);
}

/* tool daemon prototypes */
int
cti_execToolDaemon(cti_manifest_id_t mid, const char *daemonPath,
	const char * const daemonArgs[], const char * const envVars[])
{
	return runSafely("cti_execToolDaemon", [&](){
		{ auto& manifest = getManifest(mid);
			manifest.addBinary(daemonPath);
			auto remotePackage = manifest.finalizeAndShip();
			remotePackage.extractAndRun(daemonPath, daemonArgs, envVars);
		}
		manifests.erase(mid);
		return SUCCESS;
	}, FAILURE);
}

bool _cti_stage_deps = true; // extern defined in cti_transfer.h
void
_cti_setStageDeps(bool stageDeps) {
	_cti_stage_deps = stageDeps;
}

void
_cti_consumeSession(void* rawSidPtr) {
	if (rawSidPtr == nullptr) {
		return;
	}

	auto sidPtr = static_cast<cti_session_id_t*>(rawSidPtr);
	cti_destroySession(*sidPtr);
	delete sidPtr;
}

void _cti_transfer_init(void) { /* no-op */ }

void
_cti_transfer_fini(void) {
	sessions.clear();
}