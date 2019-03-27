#pragma once

/* wrappers for helper functions that use C-style allocation and error handling */

#include <cstring>
#include <unistd.h>

// automatic c-style string management
#include <functional>

// cti frontend definitions
#include "cti_defs.h"
#include "frontend/cti_fe_iface.h"

#include "useful/cti_useful.h"
#include "ld_val/ld_val.h"
#include "useful/make_unique_destr.hpp"

/* cstring wrappers */

namespace cstr {
	// lifted asprintf
	template <typename... Args>
	static inline std::string asprintf(char const* const formatCStr, Args&&... args) {
		char *rawResult = nullptr;
		if (::asprintf(&rawResult, formatCStr, std::forward<Args>(args)...) < 0) {
			throw std::runtime_error("asprintf failed.");
		}
		auto const result = make_unique_destr(std::move(rawResult), std::free);
		return std::string(result.get());
	}

	// lifted mkdtemp
	static inline std::string mkdtemp(std::string const& pathTemplate) {
		auto rawPathTemplate = make_unique_destr(strdup(pathTemplate.c_str()), std::free);

		if (::mkdtemp(rawPathTemplate.get())) {
			return std::string(rawPathTemplate.get());
		} else {
			throw std::runtime_error("mkdtemp failed on " + pathTemplate);
		}
	}

	// lifted gethostname
	static inline std::string gethostname() {
		char buf[HOST_NAME_MAX + 1];
		if (::gethostname(buf, HOST_NAME_MAX) < 0) {
			throw std::runtime_error("gethostname failed");
		}
		return std::string{buf};
	}
}

namespace file {
	// open a file path and return a unique FILE* or nullptr
	static inline auto try_open(std::string const& path, char const* mode) ->
		std::unique_ptr<FILE, decltype(&std::fclose)>
	{
		return make_unique_destr(fopen(path.c_str(), mode), std::fclose);
	};

	// open a file path and return a unique FILE* or throw
	static inline auto open(std::string const& path, char const* mode) ->
		std::unique_ptr<FILE, decltype(&std::fclose)>
	{
		if (auto ufp = try_open(path, mode)) {
			return ufp;
		} else {
			throw std::runtime_error("failed to open path " + path);
		}
	};

	// write a POD to file
	template <typename T>
	static inline void writeT(FILE* fp, T const& data) {
		static_assert(std::is_pod<T>::value, "type cannot be written bytewise to file");
		if (fwrite(&data, sizeof(T), 1, fp) != 1) {
			throw std::runtime_error("failed to write to file");
		}
	}
};

/* ld_val wrappers */

namespace ld_val {
	static inline auto getFileDependencies(const std::string& filePath) ->
		std::unique_ptr<char*, decltype(&free_ptr_list<char*>)>
	{
		auto dependencyArray = _cti_stage_deps
			? _cti_ld_val(filePath.c_str(), _cti_getLdAuditPath().c_str())
			: nullptr;
		return make_unique_destr(std::move(dependencyArray), free_ptr_list<char*>);
	}
}

namespace cti {

	/* cti_useful wrappers */

	static inline std::string findPath(std::string const& fileName) {
		if (auto fullPath = make_unique_destr(_cti_pathFind(fileName.c_str(), nullptr), std::free)) {
			return std::string{fullPath.get()};
		} else { // _cti_pathFind failed with nullptr result
			throw std::runtime_error(fileName + ": Could not locate in PATH.");
		}
	}

	static inline std::string findLib(std::string const& fileName) {
		if (auto fullPath = make_unique_destr(_cti_libFind(fileName.c_str()), std::free)) {
			return std::string{fullPath.get()};
		} else { // _cti_libFind failed with nullptr result
			throw std::runtime_error(fileName + ": Could not locate in LD_LIBRARY_PATH or system location.");
		}
	}
	static inline std::string getNameFromPath(std::string const& filePath) {
		if (auto realName = make_unique_destr(_cti_pathToName(filePath.c_str()), std::free)) {
			return std::string{realName.get()};
		} else { // _cti_pathToName failed with nullptr result
			throw std::runtime_error("Could not convert the fullname to realname.");
		}
	}

	static inline std::string getRealPath(std::string const& filePath) {
		if (auto realPath = make_unique_destr(realpath(filePath.c_str(), nullptr), std::free)) {
			return std::string{realPath.get()};
		} else { // realpath failed with nullptr result
			throw std::runtime_error("realpath failed.");
		}
	}

	/* cti_error wrappers */

	static inline std::string getErrorString() {
		return std::string{cti_error_str()};
	}
}
