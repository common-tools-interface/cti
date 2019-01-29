#pragma once

/* Session: state object representing a remote staging directory where packages of files
	to support CTI programs are unpacked and stored. Manages conflicts between files
	present on remote systems and in-progress, unshipped file lists (Manifests).
*/

#include <string>
#include <vector>

// pointer management
#include <memory>

// file registry
#include <map>
#include <unordered_map>
#include <set>
using FoldersMap = std::map<std::string, std::set<std::string>>;
using PathMap = std::unordered_map<std::string, std::string>;
using FolderFilePair = std::pair<std::string, std::string>;

#include "frontend/Frontend.hpp"
#include "cti_wrappers.hpp"

class Manifest; // forward declare Manifest

class Session final : public std::enable_shared_from_this<Session> {
public: // types
	enum class Conflict {
		None = 0,     // file is not present in session
		AlreadyAdded, // same file already in session
		NameOverwrite // different file already in session; would overwrite
	};

private: // variables
	std::vector<std::shared_ptr<Manifest>> manifests;
	size_t shippedManifests = 0;

	FoldersMap folders;
	PathMap sourcePaths;

private: // helper functions
	// generate a staging path according to CTI path rules
	static std::string generateStagePath();
	std::string ldLibraryPath;

public: // variables
	Frontend const& frontend;
	Frontend::AppId const appId;

	const std::string configPath;
	const std::string stageName;
	const std::string attribsPath;
	const std::string toolPath;
	const std::string jobId;
	const std::string wlmEnum;

public: // interface
	explicit Session(Frontend const& frontend, Frontend::AppId appId);

	// accessors
	inline auto getManifests() const -> const decltype(manifests)& { return manifests; }
	inline const std::string& getLdLibraryPath() const { return ldLibraryPath; }
	inline void invalidate() { manifests.clear(); }

	// launch cti_daemon to clean up the session stage directory. invalidates the session
	void launchCleanup();

	// wlm / daemon wrappers
	void startDaemon(char * const argv[]);
	inline void shipPackage(std::string const& tarPath) {
		frontend.getApp(appId).shipPackage(tarPath);
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
	Conflict hasFileConflict(const std::string& folderName, const std::string& realName,
		const std::string& candidatePath) const;

	/* merge manifest contents into directory of transfered files, return list of
		duplicate files that don't need to be shipped
		*/
	std::vector<FolderFilePair> mergeTransfered(const FoldersMap& folders,
		const PathMap& paths);

	/* prepend a manifest's alternate lib directory path to daemon LD_LIBRARY_PATH
		override argument
		*/
	inline void pushLdLibraryPath(std::string folderName) {
		const std::string remoteLibDirPath(toolPath + "/" + stageName + "/" + folderName);
		ldLibraryPath = remoteLibDirPath + ":" + ldLibraryPath;
	}
};
