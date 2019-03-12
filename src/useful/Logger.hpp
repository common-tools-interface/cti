#pragma once

#include <string>
#include <memory>

#include "cti_useful.h"

class Logger
{
private: // types
	using LogPtr = std::unique_ptr<cti_log_t, int(*)(cti_log_t*)>;

private: // variables
	LogPtr logFile;

public: // interface
	Logger(std::string const& filename, int suffix) : logFile{nullptr, _cti_close_log}
	{
		// determine if logging mode is enabled
		if (getenv(DBG_ENV_VAR)) {
			logFile = LogPtr{_cti_create_log(filename.c_str(), suffix), _cti_close_log};
		}
	}

	template <typename... Args>
	void write(char const* fmt, Args&&... args)
	{
		if (logFile) {
			_cti_write_log(logFile.get(), fmt, std::forward<Args>(args)...);
		}
	}
};
