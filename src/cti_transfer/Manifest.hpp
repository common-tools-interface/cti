#pragma once

/* Manifest: in-progress file list that is owned by a session. Call finalizeAndShip() to 
	produce a RemotePackage representing a tarball that is present on compute nodes.
*/

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
	std::weak_ptr<Session> sessionPtr;
	const size_t instanceCount;

	FoldersMap folders;
	PathMap sourcePaths;

	std::string ldLibraryOverrideFolder;

public: // variables
	const std::string lockFilePath;

private: // helper functions
	inline bool empty() { return sourcePaths.empty(); }
	inline void invalidate() { sessionPtr.reset(); }

	// add dynamic library dependencies to manifest
	void addLibDeps(const std::string& filePath);

	// if no session conflicts, add to manifest (otherwise throw)
	void checkAndAdd(const std::string& folder, const std::string& filePath,
		const std::string& realName);

	// create manifest archive with libarchive and ship package with WLM transfer function
	RemotePackage createAndShipArchive(const std::string& archiveName,
		std::shared_ptr<Session>& liveSession);

public: // interface
	Manifest(size_t instanceCount_, Session& owningSession) :
		sessionPtr(owningSession.shared_from_this()),
		instanceCount(instanceCount_),
		lockFilePath(owningSession.toolPath + "/.lock_" + owningSession.stageName +
			"_" + std::to_string(instanceCount)) {}

	// add files and optionally their dependencies to manifest
	void addBinary(const std::string& rawName, DepsPolicy depsPolicy = DepsPolicy::Stage);
	void addLibrary(const std::string& rawName, DepsPolicy depsPolicy = DepsPolicy::Stage);
	void addLibDir(const std::string& rawPath);
	void addFile(const std::string& rawName);

	// package files from manifest and ship, return remotely extractable archive object
	RemotePackage finalizeAndShip();
};

