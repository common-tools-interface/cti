/******************************************************************************\
 * Frontend.hpp - A header file for the Flux specific frontend interface.
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
#include "useful/cti_dlopen.hpp"

// Forward declare Flux types
#ifndef _FLUX_CORE_H
struct flux_t;
struct flux_msg_t;
struct flux_match;
#endif

class FluxFrontend final : public Frontend
{
public: // inherited interface
    static char const* getName()        { return CTI_WLM_TYPE_FLUX_STR; }

    cti_wlm_type_t getWLMType() const override { return CTI_WLM_FLUX; }

    std::weak_ptr<App> launch(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
        CStr inputFile, CStr chdirPath, CArgArray env_list) override;

    std::weak_ptr<App> launchBarrier(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
        CStr inputFile, CStr chdirPath, CArgArray env_list) override;

    std::weak_ptr<App> registerJob(size_t numIds, ...) override;

    std::string getHostname() const override;

public: // flux specific types
    struct LibFlux
    {
        using FluxOpen = flux_t*(char const*, int);
        using FluxClose = void(flux_t*);
        using FluxFatality = bool(flux_t*);
        using FluxSend = int(flux_t*, flux_msg_t const *, int);
        using FluxMsgCreate = flux_msg_t*(int);
        using FluxMsgDestroy = void(flux_msg_t*);

        cti::Dlopen::Handle libFluxHandle;
        std::function<FluxOpen> flux_open;
        std::function<FluxClose> flux_close;
        std::function<FluxFatality> flux_fatality;
        std::function<FluxSend> flux_send;
        std::function<FluxMsgCreate> flux_msg_create;
        std::function<FluxMsgDestroy> flux_msg_destroy;

        LibFlux(std::string const& libFluxName);

        flux_msg_t* flux_recv(flux_t* flux_handle, flux_match* match, int flags);
    };

private: // flux specific members
    std::string const m_libFluxPath;
    LibFlux m_libFlux;
    friend class FluxApp;

    std::unique_ptr<flux_t, std::function<LibFlux::FluxClose>> m_fluxHandle;

public: // flux specific interface

    // Get the default launcher binary name, or, if provided, from the environment.
    std::string getLauncherName() const;

    // Use environment variable or flux launcher location to find libflux path
    static std::string findLibFluxPath(std::string const& launcherName);


public: // constructor / destructor interface
    FluxFrontend();
    ~FluxFrontend() = default;
    FluxFrontend(const FluxFrontend&) = delete;
    FluxFrontend& operator=(const FluxFrontend&) = delete;
    FluxFrontend(FluxFrontend&&) = delete;
    FluxFrontend& operator=(FluxFrontend&&) = delete;
};

class FluxApp final : public App
{
private: // variables
    std::string m_jobId;
    bool m_beDaemonSent; // Have we already shipped over the backend daemon?
    size_t m_numPEs;
    std::vector<CTIHost> m_hostsPlacement;
    std::map<std::string, std::vector<int>> m_binaryRankMap;

    std::string m_apinfoPath;  // Backend path where the apinfo file is located
    std::string m_toolPath;    // Backend path where files are unpacked
    std::string m_attribsPath; // Backend Cray-specific directory
    std::string m_stagePath;   // Local directory where files are staged before transfer to BE
    std::vector<std::string> m_extraFiles; // List of extra support files to transfer to BE
    bool m_atBarrier; // Flag that the application is at the startup barrier.

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

public: // flux specific interface

public: // constructor / destructor interface
    FluxApp(FluxFrontend& fe);
    ~FluxApp();
    FluxApp(const FluxApp&) = delete;
    FluxApp& operator=(const FluxApp&) = delete;
    FluxApp(FluxApp&&) = delete;
    FluxApp& operator=(FluxApp&&) = delete;
};
