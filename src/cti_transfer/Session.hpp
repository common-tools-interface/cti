#pragma once

#include <string>
#include <vector>
#include <map>

#include "Manifest.hpp"

#include "cti_defs.h"
#include "frontend/cti_fe.h"

class Session {
	const appEntry_t  *appPtr;
	const std::string stagePath;
	const std::string toolPath;

	std::vector<Manifest>    manifests;
	std::vector<std::string> transferedFilePaths;

	// generate a staging path according to CTI path rules
	static std::string generateStagePath();

public:
	Session(appEntry_t *appPtr_) :
		appPtr(appPtr_),
		stagePath(generateStagePath()),
		toolPath(appPtr->wlmProto->wlm_getToolPath(appPtr->_wlmObj)) {

	}
};