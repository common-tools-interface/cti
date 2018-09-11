#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <set>

#include "Session.hpp"
#include "Archive.hpp"

using FoldersMap = std::map<std::string, std::set<std::string>>;
using PathMap = std::map<std::string, std::string>;

/* object created as a result of finalizing a manifest. represents a remote archive ready
	for the cti daemon to extract / run tooldaemon with
*/
class RemotePackage final {
private: // variables
	const std::string archiveName;
	std::weak_ptr<Session> sessionPtr;
	const size_t instanceCount;

private: // functions
	inline void invalidate() { sessionPtr.reset(); }

public: // interface

	// run WLM shipping routine to stage archivePath
	RemotePackage(Archive&& archiveToShip, const std::string& archiveName_,
		std::shared_ptr<Session> liveSession, size_t instanceCount_);

	// object finalized after running extraction routines
	void extract();
	void extractAndRun(const char * const daemonPath, const char * const daemonArgs[], 
		const char * const envVars[]);
};

/* in-progress file list that is owned by a session. finalize() into a tarball package
	that is present on compute nodes
 */
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

public: // interface
	Manifest(size_t instanceCount_, std::shared_ptr<Session> liveSession) :
		sessionPtr(liveSession),
		instanceCount(instanceCount_),
		lockFilePath(liveSession->toolPath + "/.lock_" + liveSession->stageName +
			"_" + std::to_string(instanceCount)) {}

	// add files and optionally their dependencies to manifest
	void addBinary(const std::string& rawName, DepsPolicy depsPolicy = DepsPolicy::Stage);
	void addLibrary(const std::string& rawName, DepsPolicy depsPolicy = DepsPolicy::Stage);
	void addLibDir(const std::string& rawPath);
	void addFile(const std::string& rawName);

	// package files from manifest and ship, return remotely extractable archive object
	RemotePackage finalizeAndShip();
};

