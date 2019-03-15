/*********************************************************************************\
 * Session: state object representing a remote staging directory where packages
 *  of files to support CTI programs are unpacked and stored. Manages conflicts
 *  between files present on remote systems and in-progress, unshipped file
 *  lists( Manifests).
 *
 * Copyright 2019 Cray Inc.  All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
 *
 *********************************************************************************/

#pragma once

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

class Manifest; // forward declare Manifest

class Session final : public std::enable_shared_from_this<Session> {
public: // types
	enum class Conflict {
		None = 0,     // file is not present in session
		AlreadyAdded, // same file already in session
		NameOverwrite // different file already in session; would overwrite
	};

private: // variables
	std::vector<std::shared_ptr<Manifest>> m_manifests;
	size_t m_shippedManifests = 0;

	FoldersMap m_folders;
	PathMap m_sourcePaths;

private: // helper functions
	// generate a staging path according to CTI path rules
	static std::string generateStagePath();

public: // variables
	App& m_activeApp;

	std::string const m_configPath;
	std::string const m_stageName;
	std::string const m_attribsPath;
	std::string const m_toolPath;
	std::string const m_jobId;
	std::string const m_wlmType;

private: // variables
	std::string m_ldLibraryPath;

public: // interface
	Session(cti_wlm_type const wlmType, App& activeApp);

	// accessors
	inline auto getManifests() const -> const decltype(m_manifests)& { return m_manifests; }
	inline const std::string& getLdLibraryPath() const { return m_ldLibraryPath; }
	inline void invalidate() { m_manifests.clear(); }

	// log function for Manifest / RemoteSession
	template <typename... Args>
	inline void writeLog(char const* fmt, Args&&... args) const {
		m_activeApp.writeLog(fmt, std::forward<Args>(args)...);
	}

	// launch cti_daemon to clean up the session stage directory. invalidates the session
	void launchCleanup();

	// wlm / daemon wrappers
	void startDaemon(char * const argv[]);
	inline void shipPackage(std::string const& tarPath) {
		m_activeApp.shipPackage(tarPath);
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
	inline void pushLdLibraryPath(std::string const& folderName) {
		std::string const remoteLibDirPath{m_toolPath + "/" + m_stageName + "/" + folderName};
		m_ldLibraryPath = remoteLibDirPath + ":" + m_ldLibraryPath;
	}
};
