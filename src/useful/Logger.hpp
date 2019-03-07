#pragma once

#include <string>
#include <memory>

#include "cti_useful.h"

class Logger
{
private: // types
	using LogPtr = std::unique_ptr<FILE, int(*)(FILE*)>;

private: // variables
	LogPtr logFile;

public: // interface
	Logger(std::string const& filename, int suffix) : logFile{nullptr, ::fclose}
	{
		// determine if logging mode is enabled
		if (getenv(DBG_ENV_VAR)) {
			logFile = LogPtr{_cti_create_log(filename.c_str(), suffix), ::fclose};
		}
	}

	template <typename... Args>
	void write(char const* fmt, Args&&... args)
	{
		if (logFile) {
			fprintf(logFile.get(), fmt, std::forward<Args>(args)...);
		}
	}
};
