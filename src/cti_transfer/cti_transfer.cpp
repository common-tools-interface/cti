#include <stdexcept>

#include "Manifest.hpp"
#include "Session.hpp"

#include "cti_transfer.h"

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