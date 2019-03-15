/******************************************************************************\
 * Manifest.hpp - In-progress file list that is owned by a session. Call
 * finalizeAndShip() to produce a RemotePackage representing a tarball that
 * is present on compute nodes.
 *
 * Copyright 2013-2019 Cray Inc.    All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
 *
 ******************************************************************************/

#pragma once

#include <string>

// pointer management
#include <memory>

#include "Session.hpp"
#include "RemotePackage.hpp"

class Manifest final {
public: // types
	enum class DepsPolicy {
		Ignore = 0,
		Stage
	};

private: // variables
	std::weak_ptr<Session> m_sessionPtr;
	size_t const m_instanceCount;

	FoldersMap m_folders;
	PathMap    m_sourcePaths;

	std::string m_ldLibraryOverrideFolder;

public: // variables
	std::string const m_lockFilePath;

private: // helper functions
	inline bool empty() { return m_sourcePaths.empty(); }
	inline void invalidate() { m_sessionPtr.reset(); }

	// add dynamic library dependencies to manifest
	void addLibDeps(const std::string& filePath);

	// if no session conflicts, add to manifest (otherwise throw)
	void checkAndAdd(const std::string& folder, const std::string& filePath,
		const std::string& realName);

	// create manifest archive with libarchive and ship package with WLM transfer function
	RemotePackage createAndShipArchive(const std::string& archiveName,
		std::shared_ptr<Session>& liveSession);

public: // interface
	Manifest(size_t instanceCount, Session& owningSession) :
		m_sessionPtr(owningSession.shared_from_this()),
		m_instanceCount(instanceCount),
		m_lockFilePath(owningSession.m_toolPath + "/.lock_" + owningSession.m_stageName +
			"_" + std::to_string(m_instanceCount)) {}

	// add files and optionally their dependencies to manifest
	void addBinary(const std::string& rawName, DepsPolicy depsPolicy = DepsPolicy::Stage);
	void addLibrary(const std::string& rawName, DepsPolicy depsPolicy = DepsPolicy::Stage);
	void addLibDir(const std::string& rawPath);
	void addFile(const std::string& rawName);

	// package files from manifest and ship, return remotely extractable archive object
	RemotePackage finalizeAndShip();
};

