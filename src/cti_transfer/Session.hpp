#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>

#include "Manifest.hpp"

#include "cti_defs.h"
#include "frontend/cti_fe.h"

class Session {
	const appEntry_t  *appPtr;

	std::vector<std::shared_ptr<Manifest>> manifests;
	std::vector<std::string> transferedFilePaths;

	// generate a staging path according to CTI path rules
	static std::string generateStagePath();

public:
	const std::string stagePath;
	const std::string toolPath;

	Session(appEntry_t *appPtr_) :
		appPtr(appPtr_),
		stagePath(generateStagePath()),
		toolPath(appPtr->wlmProto->wlm_getToolPath(appPtr->_wlmObj)) {}

	size_t getNumManifests() const { return manifests.size(); }

	std::weak_ptr<Manifest> createManifest();
};