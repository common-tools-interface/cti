#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <set>

#include "Session.hpp"

using FoldersMap = std::map<std::string, std::set<std::string>>;
using PathMap = std::map<std::string, std::string>;

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

	// promote session pointer to a shared pointer (otherwise throw)
	inline std::shared_ptr<Session> getSessionHandle() {
		if (auto liveSession = sessionPtr.lock()) { return liveSession; }
		throw std::runtime_error("Manifest is not valid, already shipped.");
	}

	// add dynamic library dependencies to manifest
	void addLibDeps(const std::string& filePath);

	// if no session conflicts, add to manifest (otherwise throw)
	void checkAndAdd(const std::shared_ptr<Session>& liveSession,
		const std::string& folder, const std::string& filePath,
		const std::string& realName);

	// package files from manifest and ship, return name of new archive
	std::string shipAndFinalize();

public: // interface
	Manifest(size_t instanceCount_, std::shared_ptr<Session> sessionPtr_) :
		sessionPtr(sessionPtr_),
		instanceCount(instanceCount_),
		lockFilePath(sessionPtr_->toolPath + "/.lock_" + sessionPtr_->stageName +
			"_" + std::to_string(instanceCount)) {}

	void addBinary(const std::string& rawName, DepsPolicy depsPolicy = DepsPolicy::Stage);
	void addLibrary(const std::string& rawName, DepsPolicy depsPolicy = DepsPolicy::Stage);
	void addLibDir(const std::string& rawPath);
	void addFile(const std::string& rawName);

	void finalizeAndExtract();
	void finalizeAndRun(const char * const daemonPath, const char * const daemonArgs[], const char * const envVars[]);
};

