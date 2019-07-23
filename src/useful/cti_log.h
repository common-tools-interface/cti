/******************************************************************************\
 * cti_log.h - Header file for the log interface.
 *
 * Copyright 2011-2014 Cray Inc.  All Rights Reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
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

namespace cti {

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

} /* namespace cti */

#endif /* __cplusplus */

#endif /* _CTI_LOG_H */
