/******************************************************************************\
 * cti_wrappers.hpp - A header file for utility wrappers. This is for helper
 *                    wrappers to C-style allocation and error handling routines.
 *
 * Copyright 2019 Cray Inc.  All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
 *
 ******************************************************************************/
#pragma once

#include <cstring>
#include <unistd.h>

#include <functional>
#include <memory>
#include <string>
#include <type_traits>

// cti frontend definitions
#include "cti_defs.h"
#include "frontend/cti_fe_iface.h"

#include "useful/cti_useful.h"
#include "ld_val/ld_val.h"

namespace cti {

// there is an std::make_unique<T> which constructs a unique_ptr of type T from its arguments.
// however, there is no equivalent that accepts a custom destructor function. normally, one would
// have to explicitly provide the types of T and its destructor function:
//     std::unique_ptr<T, decltype(&destructor)>{new T{}, destructor}
// this is a helper function to perform this deduction:
//     make_unique_destr(new T{}, destructor)
// for example:
//     auto const cstr = make_unique_destr(strdup(...), std::free);
template <typename T, typename Destr>
inline static auto
make_unique_destr(T*&& expiring, Destr&& destructor) -> std::unique_ptr<T, decltype(&destructor)>
{
	// type of Destr&& is deduced at the same time as Destr -> universal reference
	static_assert(!std::is_rvalue_reference<decltype(destructor)>::value);

	// type of T is deduced from T* first, then parameter as T*&& -> rvalue reference
	static_assert(std::is_rvalue_reference<decltype(expiring)>::value);

	return std::unique_ptr<T, decltype(&destructor)>
		{ std::move(expiring) // then we take ownership of the expiring raw pointer
		, destructor          // and merely capture a reference to the destructor
	};
}

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
} /* namespace cti::cstr */

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
} /* namespace cti::file */

template <typename T>
static void free_ptr_list(T* head) {
	auto elem = head;
	while (*elem != nullptr) {
		free(*elem);
		elem++;
	}
	free(head);
}

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
} /* namespace cti::ld_val */

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

// generate a temporary file and remove it on destruction
class temp_file_handle
{
private:
	std::unique_ptr<char, decltype(&::free)> m_path;

public:
	temp_file_handle(std::string const& templ)
		: m_path{strdup(templ.c_str()), ::free}
	{
		// use template to generate filename
		mktemp(m_path.get());
		if (m_path.get()[0] == '\0') {
			throw std::runtime_error("mktemp failed");
		}
	}

	temp_file_handle(temp_file_handle&& moved)
		: m_path{std::move(moved.m_path)}
	{
		moved.m_path.reset();
	}

	~temp_file_handle()
	{
		if (m_path && remove(m_path.get()) < 0) {
			// could have been destroyed without file being opened
			std::cerr << "warning: remove " << std::string{m_path.get()} << " failed" << std::endl;
		}
	}

	char const* get() const { return m_path.get(); }
};

} /* namespace cti */
