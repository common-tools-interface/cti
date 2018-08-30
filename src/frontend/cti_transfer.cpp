#include <stdexcept>

#include "cti_transfer/Manifest.hpp"
#include "cti_transfer/Session.hpp"

#include "cti_fe.h"
#include "cti_error.h"

#include "cti_transfer.h"

std::map<cti_session_id_t, Session> sessions;
static const cti_session_id_t SESSION_ERROR = 0;
static cti_session_id_t newId() noexcept {
	static cti_session_id_t nextId = 1;
	return nextId++;
}

cti_session_id_t
cti_createSession(cti_session_id_t appId) {
	cti_session_id_t id = newId();

	// get appPtr from appId
	appEntry_t *appPtr = _cti_findAppEntry(appId);
	if (appPtr == nullptr) {
		_cti_set_error("cannot create session: appId %d not found", appId);
		return SESSION_ERROR;
	}

	// emplace new session in the list
	try {
		sessions.emplace(id, appPtr);
	} catch (std::exception& ex) {
		if (ex.what()[0]) {
			_cti_set_error(ex.what());
		}
		return SESSION_ERROR;
	}

	return id;
}

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