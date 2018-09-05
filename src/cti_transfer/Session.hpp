#pragma once

#include <string>
#include <vector>
#include <set>
#include <map>
#include <memory>

#include "Manifest.hpp"

#include "cti_defs.h"
#include "frontend/cti_fe.h"

#include <functional>
template <typename T>
using UniquePtrDestr = std::unique_ptr<T, std::function<void(T*)>>;
using CharPtr = UniquePtrDestr<char>;
using StringArray = UniquePtrDestr<char*>;
auto stringArrayDeleter = [](char** arr){
	for (char** elem = arr; elem != nullptr; elem++) { free(*elem); }
};

class Session {

private:
	const appEntry_t  *appPtr;

	std::vector<std::shared_ptr<Manifest>> manifests;
	std::map<std::string, std::map<std::string, std::string>> transferedFolders;

	// generate a staging path according to CTI path rules
	static std::string generateStagePath();

public:
	const std::string stagePath;
	const std::string toolPath;

	Session(appEntry_t *appPtr_) :
		appPtr(appPtr_),
		stagePath(generateStagePath()),
		toolPath(appPtr->wlmProto->wlm_getToolPath(appPtr->_wlmObj)) {}

	inline size_t getNumManifests() const { return manifests.size(); }
	inline const cti_wlm_proto_t* getWLM() const { return appPtr->wlmProto; }

	std::weak_ptr<Manifest> createManifest();

	enum class Conflict {
		None = 0,     // file is not present in session
		AlreadyAdded, // same file already in session
		NameOverwrite // different file already in session; would overwrite
	};
	/* fileName: filename as provided by client
	   realName: basename following symlinks
	   conflict rules:
	   - realName not in the provided folder -> None
	   - realpath(fileName) == realpath(realName) -> AlreadyAdded
	   - realpath(fileName) != realpath(realName) -> NameOverwrite
	   */
	Conflict hasFileConflict(const std::string& folder, const std::string& realName, const std::string& filePath) const;
};