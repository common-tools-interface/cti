#pragma once

/* wrappers for helper functions that use C-style allocation and error handling */

#include <cstring>
#include <unistd.h>

// automatic c-style string management
#include <functional>
#include <memory>
template <typename T>
using UniquePtrDestr = std::unique_ptr<T, std::function<void(T*)>>;

// cti frontend definitions
#include "cti_defs.h"
#include "frontend/cti_fe.h"
#include "cti_transfer.h"

#include "useful/cti_useful.h"
#include "ld_val/ld_val.h"
#include "frontend/cti_error.h"

// debug declares
#ifdef DEBUG
	#include <iostream>
	#define DEBUG_PRINT(x) do { std::cerr << x; } while (0)
#else
	#define DEBUG_PRINT(x)
#endif

/* cstring wrappers */

namespace cstr {
	// managed c-style string
	namespace {
		using cstr_type = std::unique_ptr<char, decltype(::free)*>;
	}
	class handle : cstr_type {
		handle(char* str) : cstr_type{str, ::free} {}
	};

	// lifted asprintf
	template <typename... Args>
	static inline std::string asprintf(char const* const formatCStr, Args&&... args) {
		char *rawResult = nullptr;
		if (::asprintf(&rawResult, formatCStr, std::forward<Args>(args)...) < 0) {
			throw std::runtime_error("asprintf failed.");
		}
		auto const result = handle{rawResult};
		return std::string(result.get());
	}

	// lifted mkdtemp
	static inline std::string mkdtemp(std::string const& pathTemplate) {
		auto rawPathTemplate = handle{strdup(pathTemplate.c_str())};

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
	static inline auto try_open(std::string const& path, char const* mode) -> UniquePtrDestr<FILE> {
		if (auto ufp = UniquePtrDestr<FILE>(fopen(path.c_str(), mode), ::fclose)) {
			return ufp;
		} else {
			return nullptr;
		}
	};

	// open a file path and return a unique FILE* or throw
	static inline auto open(std::string const& path, char const* mode) -> UniquePtrDestr<FILE> {
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
	namespace {
		using StringArray = UniquePtrDestr<char*>;
		auto stringArrayDeleter = [](char** arr){
			for (char** elem = arr; *elem != nullptr; elem++) { free(*elem); }
		};
	}

	static inline StringArray getFileDependencies(const std::string& filePath) {
		if (_cti_stage_deps) {
			return StringArray(_cti_ld_val(filePath.c_str(), _cti_getLdAuditPath().c_str()),
				stringArrayDeleter);
		} else {
			return nullptr;
		}
	}
}

namespace cti {

	/* cti_useful wrappers */

	static inline std::string findPath(std::string const& fileName) {
		if (auto fullPath = cstr::handle{_cti_pathFind(fileName.c_str(), nullptr)}) {
			return std::string{fullPath.get()};
		} else { // _cti_pathFind failed with nullptr result
			throw std::runtime_error(fileName + ": Could not locate in PATH.");
		}
	}

	static inline std::string findLib(std::string const& fileName) {
		if (auto fullPath = cstr::handle{_cti_libFind(fileName.c_str())}) {
			return std::string{fullPath.get()};
		} else { // _cti_libFind failed with nullptr result
			throw std::runtime_error(fileName + ": Could not locate in LD_LIBRARY_PATH or system location.");
		}
	}
	static inline std::string getNameFromPath(std::string const& filePath) {
		if (auto realName = cstr::handle{_cti_pathToName(filePath.c_str())}) {
			return std::string{realName.get()};
		} else { // _cti_pathToName failed with nullptr result
			throw std::runtime_error("Could not convert the fullname to realname.");
		}
	}

	static inline std::string getRealPath(std::string const& filePath) {
		if (auto realPath = cstr::handle{realpath(filePath.c_str(), nullptr)}) {
			return std::string{realPath.get()};
		} else { // realpath failed with nullptr result
			throw std::runtime_error("realpath failed.");
		}
	}

	static inline void myListAdd(cti_list_t *list, void *elem) {
		if (_cti_list_add(list, elem)) {
			throw std::runtime_error("_cti_list_add failed.");
		}
	}

	static inline void myListRemove(cti_list_t *list, void *elem) {
		_cti_list_remove(list, elem);
	}

	/* cti_error wrappers */

	static inline std::string getErrorString() {
		return std::string{cti_error_str()};
	}
}