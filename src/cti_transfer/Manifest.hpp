#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <set>

// forward declare Session
class Session;

class Manifest final {
public: // types
	enum class DepsPolicy {
		Ignore = 0,
		Stage
	};

private: // variables
	const std::weak_ptr<Session> sessionPtr;

	std::map<std::string, std::set<std::string>> folders;
	std::map<std::string, std::string> sourcePaths;

private: // helper functions
	// add dynamic library dependencies to manifest
	void addLibDeps(const std::string& filePath);

	// if no session conflicts, add to manifest (otherwise throw)
	void checkAndAdd(const std::shared_ptr<Session>& liveSession,
		const std::string& folder, const std::string& filePath,
		const std::string& realName);

	// promote session pointer to a shared pointer (otherwise throw)
	inline std::shared_ptr<Session> getSessionHandle() {
		if (auto liveSession = sessionPtr.lock()) { return liveSession; }
		throw std::runtime_error("Manifest's session is not valid.");
	}

public: // interface
	Manifest(std::shared_ptr<Session> sessionPtr_) : sessionPtr(sessionPtr_) {}
	void addBinary(const std::string& rawName, DepsPolicy depsPolicy = DepsPolicy::Stage);
	void addLibrary(const std::string& rawName, DepsPolicy depsPolicy = DepsPolicy::Stage);
	void addLibDir(const std::string& rawPath);
	void addFile(const std::string& rawName);

	void send();
	void execToolDaemon(const char * const daemonPath, const char * const daemonArgs[], const char * const envVars[]);
};

