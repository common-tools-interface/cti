/******************************************************************************\
 * cti_log.h - Header file for the log interface.
 *
 * Copyright 2011-2014 Cray Inc.  All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
 *
 * $HeadURL$
 * $Date$
 * $Rev$
 * $Author$
 *
 ******************************************************************************/

#ifndef _CTI_LOG_H
#define _CTI_LOG_H

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef FILE cti_log_t;

// if logging is enabled,
// create a new logfile in temporary storage with format <filename>.<suffix>.log
// otherwise, returns NULL cti_log_t that can be passed to logging functions with no effect
cti_log_t* _cti_create_log(char const* filename, int suffix);

// finalize log and close its file (if nonnull)
int _cti_close_log(cti_log_t* log_file);

// write the given formatted string to the log file (if nonnull)
int _cti_write_log(cti_log_t* log_file, const char *fmt, ...);

// redirect standard out / err to the specified logfile (if nonnull)
int _cti_hook_stdoe(cti_log_t* log_file);

#ifdef __cplusplus
}

#include <string>
#include <memory>

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

#endif /* __cplusplus */

#endif /* _CTI_LOG_H */