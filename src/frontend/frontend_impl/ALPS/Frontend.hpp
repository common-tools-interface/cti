/******************************************************************************\
 * Frontend.hpp - A header file for the ALPS specific frontend interface.
 *
 * Copyright 2014-2019 Cray Inc. All Rights Reserved.
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

#pragma once

#include <stdint.h>
#include <sys/types.h>

#include <stdexcept>

#include "frontend/Frontend.hpp"

#include "useful/cti_wrappers.hpp"
#include "useful/cti_dlopen.hpp"

#ifndef __APINFO_H__
// It is not possible to forward-declare anonymous typedefed C structs in C++ and then define the typedef in a later C include.
// Only forward-declare if the apInfo.h header has not been included.

struct appInfo_t;
struct cmdDetail_t;
struct placeNodeList_t;
#endif

class ALPSFrontend final : public Frontend
{
public: // inherited interface
    static char const* getName()        { return ALPS_WLM_TYPE_IMPL; }
    static char const* getDescription() { return ALPS_WLM_TYPE_STRING; }
    static bool isSupported();

    cti_wlm_type_t getWLMType() const override { return CTI_WLM_ALPS; }

    std::weak_ptr<App> launchBarrier(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
        CStr inputFile, CStr chdirPath, CArgArray env_list) override;

    std::weak_ptr<App> registerJob(size_t numIds, ...) override;

    std::string getHostname() const override;

private: // alps specific types
    struct LibAlps
    {
        using AlpsGetApidType = uint64_t(int, pid_t);
        using AlpsGetAppinfoVer2ErrType = int(uint64_t, appInfo_t *, cmdDetail_t **,
            placeNodeList_t **, char **, int *);
        using AlpsLaunchToolHelperType = const char*(uint64_t, int, int, int, int, char **);
        using AlpsGetOverlapOrdinalType = int(uint64_t, char **, int *);

        cti::Dlopen::Handle libAlpsHandle;
        std::function<AlpsGetApidType>           alps_get_apid;
        std::function<AlpsGetAppinfoVer2ErrType> alps_get_appinfo_ver2_err;
        std::function<AlpsLaunchToolHelperType>  alps_launch_tool_helper;
        std::function<AlpsGetOverlapOrdinalType> alps_get_overlap_ordinal;

        LibAlps(std::string const& libAlpsName)
            : libAlpsHandle{libAlpsName}
            , alps_get_apid{libAlpsHandle.load<AlpsGetApidType>("alps_get_apid")}
            , alps_get_appinfo_ver2_err{libAlpsHandle.load<AlpsGetAppinfoVer2ErrType>("alps_get_appinfo_ver2_err")}
            , alps_launch_tool_helper{libAlpsHandle.load<AlpsLaunchToolHelperType>("alps_launch_tool_helper")}
            , alps_get_overlap_ordinal{libAlpsHandle.loadFailable<AlpsGetOverlapOrdinalType>("alps_get_overlap_ordinal")}
        {}
    };

    struct AprunInfo
    {
        std::unique_ptr<appInfo_t> alpsAppInfo;
        std::vector<CTIHost>       hostsPlacement;
    };

private: // alps specific members
    std::string const m_libAlpsPath;
    LibAlps m_libAlps;
    friend class ALPSApp;

public: // alps specific interface
    // Use libALPS to get node placement information
    ALPSFrontend::AprunInfo getAprunInfo(uint64_t aprunId);

    // Attach and read aprun ID
    uint64_t getApid(pid_t aprunPid) const;

public: // constructor / destructor interface
    ALPSFrontend();
    ~ALPSFrontend() = default;
    ALPSFrontend(const ALPSFrontend&) = delete;
    ALPSFrontend& operator=(const ALPSFrontend&) = delete;
    ALPSFrontend(ALPSFrontend&&) = delete;
    ALPSFrontend& operator=(ALPSFrontend&&) = delete;
};

class ALPSApp final : public App
{
private: // variables
    pid_t    m_launcherPid; // job launcher PID
    FE_daemon::DaemonAppId m_daemonAppId; // used for util registry and MPIR release
    bool     m_beDaemonSent; // Have we already shipped over the backend daemon?

    std::unique_ptr<appInfo_t> m_alpsAppInfo;
    std::vector<CTIHost> m_hostsPlacement;
    ALPSFrontend::LibAlps& m_libAlpsRef;

    std::string m_toolPath;    // Backend path where files are unpacked
    std::string m_attribsPath; // Backend Cray-specific directory
    std::string m_stagePath;   // Local directory where files are staged before transfer to BE
    std::vector<std::string> m_extraFiles; // List of extra support files to transfer to BE

public: // app interaction interface
    std::string getJobId()            const override;
    std::string getLauncherHostname() const override;
    std::string getToolPath()         const override { return m_toolPath;    }
    std::string getAttribsPath()      const override { return m_attribsPath; }

    std::vector<std::string> getExtraFiles() const override { return m_extraFiles; }

    size_t getNumPEs()       const override;
    size_t getNumHosts()     const override;
    std::vector<std::string> getHostnameList()   const override;
    std::vector<CTIHost>     getHostsPlacement() const override { return  m_hostsPlacement; }

    void releaseBarrier() override;
    void kill(int signal) override;
    void shipPackage(std::string const& tarPath) const override;
    void startDaemon(const char* const args[]) override;

public: // alps specific interface
    uint64_t getApid() const;
    cti_aprunProc_t get_cti_aprunProc_t() const;

    int getAlpsOverlapOrdinal() const;

private: // delegated constructor
    ALPSApp(ALPSFrontend& fe, ALPSFrontend::AprunInfo&& aprunInfo, FE_daemon::MPIRResult&& mpirData);
public: // constructor / destructor interface
    // attach case
    ALPSApp(ALPSFrontend& fe, ALPSFrontend::AprunInfo&& aprunInfo);
    // launch case
    ALPSApp(ALPSFrontend& fe, const char * const launcher_argv[], int stdout_fd,
        int stderr_fd, const char *inputFile, const char *chdirPath, const char * const env_list[]);
    ~ALPSApp();
    ALPSApp(const ALPSApp&) = delete;
    ALPSApp& operator=(const ALPSApp&) = delete;
    ALPSApp(ALPSApp&&) = delete;
    ALPSApp& operator=(ALPSApp&&) = delete;
};
