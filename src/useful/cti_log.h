/******************************************************************************\
 * cti_log.h - Header file for the log interface.
 *
 * Copyright 2011-2020 Hewlett Packard Enterprise Development LP.
 * SPDX-License-Identifier: Linux-OpenIB
 ******************************************************************************/

#ifndef _CTI_LOG_H
#define _CTI_LOG_H

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef FILE cti_log_t;

// if logging is enabled,
// create a new logfile in directory with format <filename>.<suffix>.log
// otherwise, returns NULL cti_log_t that can be passed to logging functions with no effect
cti_log_t* _cti_create_log(char const *directory, char const* filename, int suffix);

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
#include <stdexcept>

namespace cti {

class Logger
{
private: // types
    using LogPtr = std::unique_ptr<cti_log_t, int(*)(cti_log_t*)>;

private: // variables
    LogPtr logFile;

public: // interface
    Logger(bool enable, std::string const& directory, std::string const& filename, int suffix) : logFile{nullptr, _cti_close_log}
    {
        // determine if logging mode is enabled
        if (enable) {
            char const *dir = nullptr;
            if (!directory.empty()) {
                dir = directory.c_str();
            }
            logFile = LogPtr{_cti_create_log(dir, filename.c_str(), suffix), _cti_close_log};
        }
    }

    template <typename... Args>
    void write(char const* fmt, Args&&... args)
    {
        if (logFile) {
            _cti_write_log(logFile.get(), fmt, std::forward<Args>(args)...);
        }
    }

    void hook()
    {
        if (!logFile) {
            return;
        }

        if (_cti_hook_stdoe(logFile.get())) {
            throw std::runtime_error("failed to hook standard out / err");
        }
    }
};

} /* namespace cti */

#endif /* __cplusplus */

#endif /* _CTI_LOG_H */
