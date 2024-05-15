/******************************************************************************\
 * Frontend.hpp - A header file for the Flux specific frontend interface.
 *
 * Copyright 2020 Hewlett Packard Enterprise Development LP.
 * SPDX-License-Identifier: Linux-OpenIB
 ******************************************************************************/

#pragma once

#include <stdint.h>
#include <sys/types.h>

#include <stdexcept>
#include <future>

#include "frontend/Frontend.hpp"
#include "frontend/transfer/Archive.hpp"

#include "useful/cti_wrappers.hpp"
#include "useful/cti_dlopen.hpp"

class FluxFrontend final : public Frontend
{
public: // inherited interface
    static char const* getName()        { return CTI_WLM_TYPE_FLUX_STR; }

    cti_wlm_type_t getWLMType() const override { return CTI_WLM_FLUX; }

    std::weak_ptr<App> launch(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
        CStr inputFile, CStr chdirPath, CArgArray env_list) override;

    std::weak_ptr<App> launchBarrier(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
        CStr inputFile, CStr chdirPath, CArgArray env_list) override;

    // Register application via job ID. Expecting one char const* parameter
    std::weak_ptr<App> registerJob(size_t numIds, ...) override;

    std::string getHostname() const override;

public: // flux specific types

    // Forward declare libflux handle
    struct flux_t;
    struct LibFlux;

    enum class LaunchBarrierMode
        { Disabled = 0
        , Enabled  = 1
    };

    struct LaunchInfo {
        uint64_t jobId;
        bool atBarrier;
        char const* input_file;
        int stdout_fd; int stderr_fd;
    };

    struct HostPlacement {
        std::string hostname;
        int node_id;
        size_t numPEs;
        std::vector<std::pair<int, pid_t>> rankPidPairs;
    };

private: // flux specific members
    std::string const m_libFluxPath;
    std::unique_ptr<LibFlux> m_libFlux;
    friend class FluxApp;

    // Implemented as pointer instead of unique_ptr to avoid requiring the definition
    // of the custom LibFlux destructor
    flux_t *m_fluxHandle;

public: // flux specific interface

    // Get the default launcher binary name, or, if provided, from the environment.
    std::string getLauncherName() const;

    // Use environment variable or flux launcher location to find Flux root directory
    static std::string findFluxInstallDir(std::string const& launcherName);

    // Use environment variable or flux launcher location to find libflux path
    static std::string findLibFluxPath(std::string const& launcherName);

    // Use Flux API to get application and node placement information
    LaunchInfo getLaunchInfo(uint64_t job_id);

    // Submit job launch to Flux API, get job ID
    LaunchInfo launchApp(const char* const launcher_argv[],
        const char* input_file, int stdout_fd, int stderr_fd, const char* chdir_path,
        const char* const env_list[],
        LaunchBarrierMode const launchBarrierMode);

public: // constructor / destructor interface
    FluxFrontend();
    ~FluxFrontend();
    FluxFrontend(const FluxFrontend&) = delete;
    FluxFrontend& operator=(const FluxFrontend&) = delete;
    FluxFrontend(FluxFrontend&&) = delete;
    FluxFrontend& operator=(FluxFrontend&&) = delete;
};

class FluxApp final : public App
{
private: // variables
    FluxFrontend::flux_t* m_fluxHandle;
    FluxFrontend::LibFlux& m_libFluxRef;
    uint64_t m_jobId;

    int m_leaderRank;
    std::string m_rpcService;
    std::string m_resourceSpec;

    bool m_beDaemonSent; // Have we already shipped over the backend daemon?
    size_t m_numPEs;
    std::vector<FluxFrontend::HostPlacement> m_hostsPlacement;

    bool m_allocBypassLoaded;
    std::string m_binaryName; // Flux does not support MPMD, so only need to store a single binary

    std::string m_toolPath;    // Backend path where files are unpacked
    std::string m_stagePath;   // Local directory where files are staged before transfer to BE
    std::vector<std::string> m_extraFiles; // List of extra support files to transfer to BE
    bool m_atBarrier; // Flag that the application is at the startup barrier.

    std::vector<uint64_t> m_daemonJobIds; // Daemon IDs to be cleaned up on exit

private: // member helpers
    void shipDaemon();

public: // app interaction interface
    std::string getJobId()            const override;
    std::string getLauncherHostname() const override;
    std::string getToolPath()         const override { return m_toolPath; }
    std::string getAttribsPath()      const override { return m_toolPath; }

    std::vector<std::string> getExtraFiles() const override { return m_extraFiles; }

    bool   isRunning()       const override;
    size_t getNumPEs()       const override { return m_numPEs; }
    size_t getNumHosts()     const override { return m_hostsPlacement.size(); }
    std::vector<std::string> getHostnameList()   const override;
    std::vector<CTIHost>     getHostsPlacement() const override;
    std::map<std::string, std::vector<int>> getBinaryRankMap() const override;

    void releaseBarrier() override;
    void kill(int signal) override;
    void shipPackage(std::string const& tarPath) const override;
    void startDaemon(const char* const args[], bool synchronous) override;
    std::set<std::string> checkFilesExist(std::set<std::string> const& paths) override;

public: // flux specific interface

public: // constructor / destructor interface
    FluxApp(FluxFrontend& fe, FluxFrontend::LaunchInfo&& launchInfo);
    ~FluxApp();
    FluxApp(const FluxApp&) = delete;
    FluxApp& operator=(const FluxApp&) = delete;
    FluxApp(FluxApp&&) = delete;
    FluxApp& operator=(FluxApp&&) = delete;
};
