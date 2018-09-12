#pragma once

/* wrappers for CTI helper functions */

// automatic c-style string management
#include <functional>
#include <memory>
template <typename T>
using UniquePtrDestr = std::unique_ptr<T, std::function<void(T*)>>;
using CharPtr = UniquePtrDestr<char>;
using StringArray = UniquePtrDestr<char*>;
auto stringArrayDeleter = [](char** arr){
	for (char** elem = arr; *elem != nullptr; elem++) { free(*elem); }
};

// cti frontend definitions
#include "cti_defs.h"
#include "frontend/cti_fe.h"

// debug declares
#define DEBUG
#ifdef DEBUG
	#include <iostream>
	#define DEBUG_PRINT(x) do { std::cerr << x; } while (0)
#else
	#define DEBUG_PRINT(x)
#endif

/* implementations */

#include "useful/cti_useful.h"

static inline CharPtr findPath(const std::string& fileName) {
	if (auto fullPath = CharPtr(_cti_pathFind(fileName.c_str(), nullptr), free)) {
		return fullPath;
	} else { // _cti_pathFind failed with nullptr result
		throw std::runtime_error(fileName + ": Could not locate in PATH.");
	}
}

static inline CharPtr findLib(const std::string& fileName) {
	if (auto fullPath = CharPtr(_cti_libFind(fileName.c_str()), free)) {
		return fullPath;
	} else { // _cti_libFind failed with nullptr result
		throw std::runtime_error(fileName + ": Could not locate in LD_LIBRARY_PATH or system location.");
	}
}
static inline CharPtr getNameFromPath(const std::string& filePath) {
	if (auto realName = CharPtr(_cti_pathToName(filePath.c_str()), free)) {
		return realName;
	} else { // _cti_pathToName failed with nullptr result
		throw std::runtime_error("Could not convert the fullname to realname.");
	}
}

static inline CharPtr getRealPath(const std::string& filePath) {
	if (auto realPath = CharPtr(realpath(filePath.c_str(), nullptr), free)) {
		return realPath;
	} else { // realpath failed with nullptr result
		throw std::runtime_error("realpath failed.");
	}
}

#include "ld_val/ld_val.h"

static inline StringArray getFileDependencies(const std::string& filePath) {
	return StringArray(_cti_ld_val(filePath.c_str(), _cti_getLdAuditPath()),
		stringArrayDeleter);
}