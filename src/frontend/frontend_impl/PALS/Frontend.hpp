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
#include <future>

#include "frontend/Frontend.hpp"

#include "useful/cti_wrappers.hpp"

class PALSFrontend final : public Frontend
{
public: // inherited interface
    static char const* getName()        { return CTI_WLM_TYPE_PALS_STR; }

    cti_wlm_type_t getWLMType() const override { return CTI_WLM_PALS; }

    std::weak_ptr<App> launch(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
        CStr inputFile, CStr chdirPath, CArgArray env_list) override;

    std::weak_ptr<App> launchBarrier(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
        CStr inputFile, CStr chdirPath, CArgArray env_list) override;

    std::weak_ptr<App> registerJob(size_t numIds, ...) override;

    std::string getHostname() const override;

public: // pals specific types
    enum class LaunchBarrierMode
        { Disabled = 0
        , Enabled  = 1
    };

    struct PalsApiInfo
    {
        std::string hostname;
        std::string username;
        std::string accessToken;
        std::string endpointBase;
    };

    struct PalsLaunchInfo
    {
        std::string apId;
        std::vector<CTIHost> hostsPlacement;
        std::map<std::string, std::vector<int>> binaryRankMap;
        int stdinFd, stdoutFd, stderrFd;
        bool started, atBarrier;
    };

    // Forward-declare heavy Boost structures
    struct CtiWSSImpl;

private: // pals specific members
    PalsApiInfo m_palsApiInfo;
    friend class PALSApp;

public: // pals specific interface
    // Get the default launcher binary name, or, if provided, from the environment.
    std::string getLauncherName() const;

    // Get API authentication information
    auto const& getApiInfo() const { return m_palsApiInfo; }

    // Use PALS API to get application and node placement information
    PalsLaunchInfo getPalsLaunchInfo(std::string const& apId);

    // Launch and extract application and node placement information
    PalsLaunchInfo launchApp(const char * const launcher_argv[], int stdout_fd,
        int stderr_fd, const char *inputFile, const char *chdirPath, const char * const env_list[],
        LaunchBarrierMode const launchBarrierMode);

    // Read out the currently running apid from a craycli instance
    std::string getApid(pid_t craycliPid) const;

public: // constructor / destructor interface
    PALSFrontend();
    ~PALSFrontend() = default;
    PALSFrontend(const PALSFrontend&) = delete;
    PALSFrontend& operator=(const PALSFrontend&) = delete;
    PALSFrontend(PALSFrontend&&) = delete;
    PALSFrontend& operator=(PALSFrontend&&) = delete;
};

class PALSApp final : public App
{
private: // variables
    std::string m_apId;
    bool m_beDaemonSent; // Have we already shipped over the backend daemon?
    size_t m_numPEs;
    std::vector<CTIHost> m_hostsPlacement;
    std::map<std::string, std::vector<int>> m_binaryRankMap;
    PALSFrontend::PalsApiInfo const m_palsApiInfo;

    std::string m_apinfoPath;  // Backend path where the libpals apinfo file is located
    std::string m_toolPath;    // Backend path where files are unpacked
    std::string m_attribsPath; // Backend Cray-specific directory
    std::string m_stagePath;   // Local directory where files are staged before transfer to BE
    std::vector<std::string> m_extraFiles; // List of extra support files to transfer to BE

    // Redirect websocket output to stdout/err_fd and inputFile to websocket input
    std::unique_ptr<PALSFrontend::CtiWSSImpl> m_stdioStream; // stdin / out / err stream
    int m_queuedInFd;  // Where to source stdin after barrier release
    int m_queuedOutFd; // Where to redirect stdout after barrier release
    int m_queuedErrFd; // Where to redirect stderr after barrier release
    std::future<int> m_stdioInputFuture;  // Task relaying input from stdin to stdio stream
    std::future<int> m_stdioOutputFuture; // Task relaying output from stdio stream to stdout/err
    bool m_atBarrier; // Flag that the application is at the startup barrier.

    std::vector<std::string> m_toolIds; // PALS IDs of running tool helpers

public: // app interaction interface
    std::string getJobId()            const override;
    std::string getLauncherHostname() const override;
    std::string getToolPath()         const override { return m_toolPath; }
    std::string getAttribsPath()      const override { return m_attribsPath; }

    std::vector<std::string> getExtraFiles() const override { return m_extraFiles; }

    bool   isRunning()       const override;
    size_t getNumPEs()       const override { return m_numPEs; }
    size_t getNumHosts()     const override { return m_hostsPlacement.size(); }
    std::vector<std::string> getHostnameList()   const override;
    std::vector<CTIHost>     getHostsPlacement() const override { return  m_hostsPlacement; }
    std::map<std::string, std::vector<int>> getBinaryRankMap() const override;

    void releaseBarrier() override;
    void kill(int signal) override;
    void shipPackage(std::string const& tarPath) const override;
    void startDaemon(const char* const args[]) override;

public: // pals specific interface
    // PALS allows shipping to an arbitrarily-named destination
    void shipPackage(std::string const& tarPath, std::string const& remoteName) const;

public: // constructor / destructor interface
    PALSApp(PALSFrontend& fe, PALSFrontend::PalsLaunchInfo&& palsLaunchInfo);
    ~PALSApp();
    PALSApp(const PALSApp&) = delete;
    PALSApp& operator=(const PALSApp&) = delete;
    PALSApp(PALSApp&&) = delete;
    PALSApp& operator=(PALSApp&&) = delete;
};
