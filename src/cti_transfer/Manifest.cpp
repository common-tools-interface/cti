#include <unistd.h>
#include <fcntl.h>

#include <string>

#include "Manifest.hpp"
#include "Session.hpp"

extern "C" {
	#include "cti_fe.h"
	#include "useful/cti_useful.h"
	#include "ld_val/ld_val.h"
}

static CharPtr getPath(const std::string& fileName) {
	if (auto fullPath = CharPtr(_cti_pathFind(fileName.c_str(), nullptr), free)) {
		return fullPath;
	} else { // _cti_pathFind failed with nullptr result
		throw std::runtime_error("Could not locate the specified file in PATH.");
	}
}

static CharPtr getRealName(const std::string& filePath) {
	if (auto realName = CharPtr(_cti_pathToName(filePath.c_str()), free)) {
		return realName;
	} else { // _cti_pathToName failed with nullptr result
		throw std::runtime_error("Could not convert the fullname to realname.");
	}
}

static void addLibDeps(Manifest& manifest, const std::string& filePath) {
	// get array of library paths using ld_val libArray
	if (auto libArray = StringArray(
		_cti_ld_val(filePath.c_str(), _cti_getLdAuditPath())
		)) {

		// add to manifest
		for (char** elem = libArray.get(); elem != nullptr; elem++) {
			manifest.addLibrary(*elem, Manifest::DepsPolicy::Ignore);
		}
	} else { // _cti_ld_val failed with nullptr result
		throw std::runtime_error("ld_val audit failed to get dependencies");
	}
}

void Manifest::addBinary(const std::string& rawName, DepsPolicy depsPolicy) {
	if (auto liveSession = sessionPtr.lock()) {
		// get path and real name of file
		const std::string filePath(getPath(rawName).get());
		const std::string realName(getRealName(filePath).get());

		// check permissions
		if (access(filePath.c_str(), R_OK | X_OK)) {
			throw std::runtime_error("Specified binary does not have execute permissions.");
		}

		// check for conflicts in session
		{ using C = Session::Conflict;
			switch (liveSession->hasFileConflict("bin", realName, filePath)) {
			case C::None:          break;
			case C::AlreadyAdded:  return;
			case C::NameOverwrite:
				throw std::runtime_error(realName + ": session conflict");
			}
		}

		// add to manifest registry
		folders["bin"].emplace(realName);

		// add libraries if needed
		if (depsPolicy == DepsPolicy::Stage) {
			addLibDeps(*this, filePath);
		}
	} else { // could not promote session pointer
		throw std::runtime_error("Session is not valid.");
	}
}

void Manifest::addLibrary(const std::string& rawName, DepsPolicy depsPolicy) {
	throw std::runtime_error("not implemented");
}

void Manifest::addLibDir(const std::string& rawName) {
	throw std::runtime_error("not implemented");
}

void Manifest::addFile(const std::string& rawName) {
	throw std::runtime_error("not implemented");
}