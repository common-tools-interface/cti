#pragma once

#include <functional>
#include <type_traits>

#include "useful/handle.hpp"

namespace cstr {
	// lifted asprintf
	template <typename... Args>
	static auto asprintf(char const* const formatCStr, Args&&... args) -> std::string {
		char *rawResult = nullptr;
		if (::asprintf(&rawResult, formatCStr, std::forward<Args>(args)...) < 0) {
			throw std::runtime_error("asprintf failed.");
		}
		auto const result = handle::cstr{rawResult};
		return std::string(result.get());
	}

	// lifted mkdtemp
	static auto mkdtemp(std::string const& pathTemplate) -> std::string {
		auto rawPathTemplate = handle::cstr{strdup(pathTemplate.c_str())};

		if (::mkdtemp(rawPathTemplate.get())) {
			return std::string(rawPathTemplate.get());
		} else {
			throw std::runtime_error("mkdtemp failed on " + pathTemplate);
		}
	}

	// lifted gethostname
	static auto gethostname() -> std::string {
		char buf[HOST_NAME_MAX + 1];
		if (::gethostname(buf, HOST_NAME_MAX) < 0) {
			throw std::runtime_error("gethostname failed");
		}
		return std::string{buf};
	}
}

namespace file {
	// open a file path and return a unique FILE* or nullptr
	static auto try_open(std::string const& path, char const* mode) -> UniquePtrDestr<FILE> {
		if (auto ufp = UniquePtrDestr<FILE>(fopen(path.c_str(), mode), ::fclose)) {
			return ufp;
		} else {
			return nullptr;
		}
	};

	// open a file path and return a unique FILE* or throw
	static auto open(std::string const& path, char const* mode) -> UniquePtrDestr<FILE> {
		if (auto ufp = try_open(path, mode)) {
			return ufp;
		} else {
			throw std::runtime_error("failed to open path " + path);
		}
	};

	// write a POD to file
	template <typename T>
	static void writeT(FILE* fp, T const& data) {
		static_assert(std::is_pod<T>::value, "type cannot be written bytewise to file");
		if (fwrite(&data, sizeof(T), 1, fp) != 1) {
			throw std::runtime_error("failed to write to file");
		}
	}
};