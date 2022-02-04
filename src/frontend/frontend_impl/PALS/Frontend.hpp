/******************************************************************************\
 * Frontend.hpp - A header file for the PALS specific frontend interface.
 *
 * Copyright 2020 Hewlett Packard Enterprise Development LP.
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

class HPCMPALSFrontend : public Frontend
{
public: // types
    struct PalsLaunchInfo
    {
        FE_daemon::DaemonAppId daemonAppId;
        std::string apId;
        MPIRProctable procTable;
        BinaryRankMap binaryRankMap;
        bool atBarrier;
    };

public: // inherited interface
    static char const* getName() { return CTI_WLM_TYPE_PALS_STR; }

    cti_wlm_type_t getWLMType() const override { return CTI_WLM_PALS; }

    std::weak_ptr<App> launch(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
        CStr inputFile, CStr chdirPath, CArgArray env_list) override;

    std::weak_ptr<App> launchBarrier(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
        CStr inputFile, CStr chdirPath, CArgArray env_list) override;

    std::weak_ptr<App> registerJob(size_t numIds, ...) override;

    std::string getHostname() const override;

public: // PALS-specific interface
    // Get the default launcher binary name, or, if provided, from the environment.
    static std::string getLauncherName();

    // Extract apid string from launcher process
    std::string getApid(pid_t launcher_pid);

    // Use PALS API to get application and node placement information
    PalsLaunchInfo getPalsLaunchInfo(std::string const& apId);

    // Launch an app under MPIR control and hold at barrier.
    PalsLaunchInfo launchApp(const char * const launcher_argv[],
        int stdoutFd, int stderrFd, const char *inputFile, const char *chdirPath, const char * const env_list[]);
};

class HPCMPALSApp : public App
{
    FE_daemon::DaemonAppId const m_daemonAppId; // used for util registry and MPIR release
    std::string m_apId;
    bool m_beDaemonSent;
    MPIRProctable m_procTable;
    BinaryRankMap m_binaryRankMap;
    std::set<std::string> m_hosts;

    std::string m_apinfoPath;  // Backend path where the libpals apinfo file is located
    std::string m_toolPath;    // Backend path where files are unpacked
    std::string m_attribsPath; // Backend Cray-specific directory
    std::string m_stagePath;   // Local directory where files are staged before transfer to BE
    std::vector<std::string> m_extraFiles; // List of extra support files to transfer to BE

    bool m_atBarrier; // At startup barrier or not


public:
    HPCMPALSApp(HPCMPALSFrontend& fe, HPCMPALSFrontend::PalsLaunchInfo&& palsLaunchInfo);
    ~HPCMPALSApp();
    HPCMPALSApp(const HPCMPALSApp&) = delete;
    HPCMPALSApp& operator=(const HPCMPALSApp&) = delete;
    HPCMPALSApp(HPCMPALSApp&&) = delete;
    HPCMPALSApp& operator=(HPCMPALSApp&&) = delete;

    std::string getJobId()            const override { return m_apId; }
    std::string getLauncherHostname() const override;
    std::string getToolPath()         const override { return m_toolPath;    }
    std::string getAttribsPath()      const override { return m_attribsPath; }

    std::vector<std::string> getExtraFiles() const override { return m_extraFiles; }

    bool   isRunning()       const override;
    size_t getNumPEs()       const override;
    size_t getNumHosts()     const override;
    std::vector<std::string> getHostnameList()   const override;
    std::vector<CTIHost>     getHostsPlacement() const override;
    std::map<std::string, std::vector<int>> getBinaryRankMap() const override;

    void releaseBarrier() override;
    void kill(int signal) override;
    void shipPackage(std::string const& tarPath) const override;
    void startDaemon(const char* const args[]) override;
};
