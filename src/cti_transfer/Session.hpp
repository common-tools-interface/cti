#pragma once

#include <string>
#include <vector>
#include <set>
#include <map>
#include <memory>

#include "cti_defs.h"
#include "frontend/cti_fe.h"

#include <iostream>
#define DEBUG(x) do { std::cerr << x; } while (0)
//#define DEBUG(x)

#include <functional>
template <typename T>
using UniquePtrDestr = std::unique_ptr<T, std::function<void(T*)>>;
using CharPtr = UniquePtrDestr<char>;
using StringArray = UniquePtrDestr<char*>;
auto stringArrayDeleter = [](char** arr){
	for (char** elem = arr; *elem != nullptr; elem++) { free(*elem); }
};

using FoldersMap = std::map<std::string, std::set<std::string>>;
using PathMap = std::map<std::string, std::string>;

// forward declare Manifest
class Manifest;

class Session final : public std::enable_shared_from_this<Session> {
public: // types
	enum class Conflict {
		None = 0,     // file is not present in session
		AlreadyAdded, // same file already in session
		NameOverwrite // different file already in session; would overwrite
	};

private: // variables
	const appEntry_t  *appPtr;
	std::vector<std::shared_ptr<Manifest>> manifests;
	size_t shippedManifests = 0;

	FoldersMap folders;
	PathMap sourcePaths;

private: // helper functions
	// generate a staging path according to CTI path rules
	static std::string generateStagePath();

public: // variables
	const std::string configPath;
	const std::string stageName;
	const std::string attribsPath;
	const std::string toolPath;
	const std::string jobId;
	const std::string wlmEnum;

public: // interface
	explicit Session(appEntry_t *appPtr_);
	~Session();

	inline const std::vector<std::shared_ptr<Manifest>>& getManifests() const {
		return manifests;
	}
	inline const cti_wlm_proto_t* getWLM() const { return appPtr->wlmProto; }

	// create and add wlm basefiles to manifest
	void shipWLMBaseFiles();

	// wlm / daemon wrappers
	int startDaemon(char * const argv[]);
	inline int shipPackage(const char *tar_name) {
		return getWLM()->wlm_shipPackage(appPtr->_wlmObj, tar_name);
	}

	// create new manifest and register ownership
	std::shared_ptr<Manifest> createManifest();

	/* fileName: filename as provided by client
	   realName: basename following symlinks
	   conflict rules:
	   - realName not in the provided folder -> None
	   - realpath(fileName) == realpath(realName) -> AlreadyAdded
	   - realpath(fileName) != realpath(realName) -> NameOverwrite
	   */
	Conflict hasFileConflict(const std::string& folder, const std::string& realName, const std::string& candidatePath) const;

	// merge manifest contents into directory of transfered files
	void mergeTransfered(const FoldersMap& folders, const PathMap& paths);
};