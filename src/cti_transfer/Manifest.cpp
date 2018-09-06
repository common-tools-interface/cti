#include <unistd.h>
#include <fcntl.h>

#include <string>

#include "Manifest.hpp"
#include "Session.hpp"

#include "cti_fe.h"
#include "useful/cti_useful.h"
#include "ld_val/ld_val.h"


/* wrappers for CTI helper functions */

static CharPtr findPath(const std::string& fileName) {
	if (auto fullPath = CharPtr(_cti_pathFind(fileName.c_str(), nullptr), free)) {
		return fullPath;
	} else { // _cti_pathFind failed with nullptr result
		throw std::runtime_error(fileName + ": Could not locate in PATH.");
	}
}

static CharPtr findLib(const std::string& fileName) {
	if (auto fullPath = CharPtr(_cti_libFind(fileName.c_str()), free)) {
		return fullPath;
	} else { // _cti_libFind failed with nullptr result
		throw std::runtime_error(fileName + ": Could not locate in LD_LIBRARY_PATH or system location.");
	}
}
static CharPtr getNameFromPath(const std::string& filePath) {
	if (auto realName = CharPtr(_cti_pathToName(filePath.c_str()), free)) {
		return realName;
	} else { // _cti_pathToName failed with nullptr result
		throw std::runtime_error("Could not convert the fullname to realname.");
	}
}

static CharPtr getRealPath(const std::string& filePath) {
	if (auto realPath = CharPtr(realpath(filePath.c_str(), nullptr), free)) {
		return realPath;
	} else { // realpath failed with nullptr result
		throw std::runtime_error("realpath failed.");
	}
}

// add dynamic library dependencies to manifest
void Manifest::addLibDeps(const std::string& filePath) {
	// get array of library paths using ld_val libArray
	if (auto libArray = StringArray(
			_cti_ld_val(filePath.c_str(), _cti_getLdAuditPath()),
		stringArrayDeleter)) {

		// add to manifest
		for (char** elem = libArray.get(); *elem != nullptr; elem++) {
			addLibrary(*elem, Manifest::DepsPolicy::Ignore);
		}
	}
}

/* manifest file add implementations */

// add file to manifest if session reports no conflict on realname / filepath
void Manifest::checkAndAdd(const std::shared_ptr<Session>& liveSession,
	const std::string& folder, const std::string& filePath, const std::string& realName) {

	// check for conflicts in session
	switch (liveSession->hasFileConflict(folder, realName, filePath)) {
		case Session::Conflict::None: break;
		case Session::Conflict::AlreadyAdded: return;
		case Session::Conflict::NameOverwrite:
			throw std::runtime_error(realName + ": session conflict");
	}

	// add to manifest registry
	folders[folder].emplace(realName);
	sourcePaths[realName] = filePath;
}

void Manifest::addBinary(const std::string& rawName, DepsPolicy depsPolicy) {
	// get path and real name of file
	const std::string filePath(findPath(rawName).get());
	const std::string realName(getNameFromPath(filePath).get());

	// check permissions
	if (access(filePath.c_str(), R_OK | X_OK)) {
		throw std::runtime_error("Specified binary does not have execute permissions.");
	}

	checkAndAdd(getSessionHandle(), "bin", filePath, realName);

	// add libraries if needed
	if (depsPolicy == DepsPolicy::Stage) {
		addLibDeps(filePath);
	}
}

void Manifest::addLibrary(const std::string& rawName, DepsPolicy depsPolicy) {
	auto liveSession = getSessionHandle();

	// get path and real name of file
	const std::string filePath(findLib(rawName).get());
	const std::string realName(getNameFromPath(filePath).get());

	/* TODO: We need to create a way to ship conflicting libraries. Since
		most libraries are sym links to their proper version, name collisions
		are possible. In the future, the launcher should be able to handle
		this by pointing its LD_LIBRARY_PATH to a custom directory containing
		the conflicting lib. Don't actually implement this until the need arises.
	*/

	checkAndAdd(liveSession, "lib", filePath, realName);

	// add libraries if needed
	if (depsPolicy == DepsPolicy::Stage) {
		addLibDeps(filePath);
	}
}

void Manifest::addLibDir(const std::string& rawPath) {
	// get real path and real name of directory
	const std::string realPath(getRealPath(rawPath).get());
	const std::string realName(getNameFromPath(realPath).get());

	checkAndAdd(getSessionHandle(), "lib", realPath, realName);
}

void Manifest::addFile(const std::string& rawName) {
	// get path and real name of file
	const std::string filePath(findPath(rawName).get());
	const std::string realName(getNameFromPath(filePath).get());

	checkAndAdd(getSessionHandle(), "", filePath, realName);
}

/* manifest finalizer implementations */

#include <iostream>
void Manifest::send() {
	for (auto folderIt : folders) {
		std::cerr << "directory '" << folderIt.first << "':" << std::endl;
		for (auto fileIt : folderIt.second) {
			std::cerr << "\t'" << fileIt << "' -> " << sourcePaths[fileIt] << std::endl;
		}
	}

	throw std::runtime_error("send not implemented");
}

void Manifest::execToolDaemon(const char * const daemonPath, const char * const daemonArgs[], const char * const envVars[]) {
	for (auto folderIt : folders) {
		std::cerr << "directory '" << folderIt.first << "':" << std::endl;
		for (auto fileIt : folderIt.second) {
			std::cerr << "\t'" << fileIt << "' -> " << sourcePaths[fileIt] << std::endl;
		}
	}

	throw std::runtime_error("execToolDaemon not implemented");
}