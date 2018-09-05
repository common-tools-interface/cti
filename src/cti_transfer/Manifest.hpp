#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <set>

// forward declare Session
class Session;

class Manifest {
public:
	enum class DepsPolicy {
		Ignore = 0,
		Stage
	};

private:
	std::map<std::string, std::set<std::string>> folders;

public:
	const std::weak_ptr<Session> sessionPtr;

	Manifest(std::shared_ptr<Session> sessionPtr_) : sessionPtr(sessionPtr_) {}
	void addBinary(const std::string& rawName, DepsPolicy depsPolicy = DepsPolicy::Stage);
	void addLibrary(const std::string& rawName, DepsPolicy depsPolicy = DepsPolicy::Stage);
	void addLibDir(const std::string& rawName);
	void addFile(const std::string& rawName);
};

