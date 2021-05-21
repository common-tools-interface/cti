/******************************************************************************\
 * Frontend.cpp - Flux specific frontend library functions.
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

// This pulls in config.h
#include "cti_defs.h"
#include "cti_argv_defs.hpp"

#include <memory>
#include <thread>
#include <variant>

#include <sstream>
#include <fstream>

#include "transfer/Manifest.hpp"

#include <flux/core.h>

#include "Flux/Frontend.hpp"

#include <flux/core.h>

#include "useful/cti_execvp.hpp"
#include "useful/cti_hostname.hpp"
#include "useful/cti_split.hpp"

/* helper functions */

/* FluxFrontend implementation */

FluxFrontend::LibFlux::LibFlux(std::string const& libFluxName)
    : libFluxHandle{libFluxName}
    , flux_open{libFluxHandle.load<FluxOpen>("flux_open")}
    , flux_close{libFluxHandle.load<FluxClose>("flux_close")}
    , flux_fatality{libFluxHandle.load<FluxFatality>("flux_fatality")}
    , flux_send{libFluxHandle.load<FluxSend>("flux_send")}
    , flux_msg_create{libFluxHandle.load<FluxMsgCreate>("flux_msg_create")}
    , flux_msg_destroy{libFluxHandle.load<FluxMsgDestroy>("flux_msg_destroy")}
{}

flux_msg_t* FluxFrontend::LibFlux::flux_recv(flux_t* flux_handle, flux_match* match, int flags)
{
    using FluxRecv = flux_msg_t*(flux_t*, flux_match, int);
    auto static const flux_recv_non_forwarded = libFluxHandle.load<FluxRecv>("flux_recv");
    return flux_recv_non_forwarded(flux_handle, *match, flags);
}

std::weak_ptr<App>
FluxFrontend::launch(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
    CStr inputFile, CStr chdirPath, CArgArray env_list)
{
    auto ret = m_apps.emplace(std::make_shared<FluxApp>(*this));
    if (!ret.second) {
        throw std::runtime_error("Failed to create new App object.");
    }
    return *ret.first;
}

std::weak_ptr<App>
FluxFrontend::launchBarrier(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
    CStr inputFile, CStr chdirPath, CArgArray env_list)
{
    auto ret = m_apps.emplace(std::make_shared<FluxApp>(*this));
    if (!ret.second) {
        throw std::runtime_error("Failed to create new App object.");
    }
    return *ret.first;
}

std::weak_ptr<App>
FluxFrontend::registerJob(size_t numIds, ...)
{
    if (numIds != 1) {
        throw std::logic_error("expecting single job ID argument to register app");
    }

    va_list idArgs;
    va_start(idArgs, numIds);

    char const* job_id = va_arg(idArgs, char const*);

    va_end(idArgs);

    auto ret = m_apps.emplace(std::make_shared<FluxApp>(*this));
    if (!ret.second) {
        throw std::runtime_error("Failed to create new App object.");
    }
    return *ret.first;
}

std::string
FluxFrontend::getHostname() const
{
    // Delegate to shared implementation supporting both XC and Shasta
    return cti::detectFrontendHostname();
}

std::string
FluxFrontend::getLauncherName() const
{
    auto getenvOrDefault = [](char const* envVar, char const* defaultValue) {
        if (char const* envValue = getenv(envVar)) {
            return envValue;
        }
        return defaultValue;
    };

    // Cache the launcher name result.
    auto static launcherName = std::string{getenvOrDefault(CTI_LAUNCHER_NAME_ENV_VAR, "flux")};
    return launcherName;
}

std::string
FluxFrontend::findLibFluxPath(std::string const& launcherName)
{
    // Use setting if supplied
    if (auto const libflux_path = ::getenv(LIBFLUX_PATH_ENV_VAR)) {
       return std::string{libflux_path};
    }

    // Find libflux from launcher dependencies
    auto libFluxPath = std::string{};
    if (auto const launcher_path = cti::take_pointer_ownership(_cti_pathFind(launcherName.c_str(), nullptr), ::free)) {

        char const* ldd_argv[] = { "ldd", launcher_path.get(), nullptr };
        auto lddOutput = cti::Execvp{"ldd", (char* const*)ldd_argv, cti::Execvp::stderr::Ignore};

        auto& lddStream = lddOutput.stream();
        auto libraryPathMap = std::string{};
        while (std::getline(lddStream, libraryPathMap)) {
            auto const [library, sep, path, address] = cti::split::string<4>(libraryPathMap, ' ');
            if (library.find(LIBFLUX_NAME) != std::string::npos) {
                return path;
            }
        }

        throw std::runtime_error("Could not find the path to " LIBFLUX_NAME " in the launcher's \
dependencies. Try setting " LIBFLUX_PATH_ENV_VAR " to the path to the Flux runtime library");

    } else {
        throw std::runtime_error("Could not find Flux launcher '" + launcherName + "' in PATH. \
Ensure the Flux launcher is accessible and executable");
    }
}

FluxFrontend::FluxFrontend()
    : m_libFluxPath{findLibFluxPath((::getenv(CTI_LAUNCHER_NAME_ENV_VAR)) ? ::getenv(CTI_LAUNCHER_NAME_ENV_VAR) : "flux")}
    , m_libFlux{m_libFluxPath}
    // Flux will read socket information in environment
    , m_fluxHandle{m_libFlux.flux_open(nullptr, 0), m_libFlux.flux_close}
{
    if (m_fluxHandle == nullptr) {
        throw std::runtime_error{"Flux initialization failed: " + std::string{strerror(errno)}};
    }

    throw std::runtime_error{"not implemented: " + std::string{__func__}};
}

/* FluxApp implementation */

std::string
FluxApp::getJobId() const
{
    return m_jobId;
}

std::string
FluxApp::getLauncherHostname() const
{
    throw std::runtime_error{"not supported for Flux: " + std::string{__func__}};
}

bool
FluxApp::isRunning() const
{
    throw std::runtime_error{"not implemented: " + std::string{__func__}};
}

std::vector<std::string>
FluxApp::getHostnameList() const
{
    throw std::runtime_error{"not implemented: " + std::string{__func__}};
}

std::map<std::string, std::vector<int>>
FluxApp::getBinaryRankMap() const
{
    return m_binaryRankMap;
}

void
FluxApp::releaseBarrier()
{
    if (!m_atBarrier) {
        throw std::runtime_error("application is not at startup barrier");
    }

    throw std::runtime_error{"not implemented: " + std::string{__func__}};

    m_atBarrier = false;
}

void
FluxApp::kill(int signal)
{
    throw std::runtime_error{"not implemented: " + std::string{__func__}};
}

void
FluxApp::shipPackage(std::string const& tarPath) const
{
    throw std::runtime_error{"not implemented: " + std::string{__func__}};
}

void
FluxApp::startDaemon(const char* const args[])
{
    // Prepare to start daemon binary on compute node
    auto const remoteBEDaemonPath = m_toolPath + "/" + getBEDaemonName();

    // Send daemon if not already shipped
    if (!m_beDaemonSent) {
        // Get the location of the backend daemon
        if (m_frontend.getBEDaemonPath().empty()) {
            throw std::runtime_error("Unable to locate backend daemon binary. Try setting " + std::string(CTI_BASE_DIR_ENV_VAR) + " environment varaible to the install location of CTI.");
        }

        // Ship the BE binary to its unique storage name
        shipPackage(getBEDaemonName());

        // set transfer to true
        m_beDaemonSent = true;
    }

    throw std::runtime_error{"not implemented: " + std::string{__func__}};
}

FluxApp::FluxApp(FluxFrontend& fe)
    : App{fe}
    , m_jobId{}
    , m_beDaemonSent{}
    , m_numPEs{}
    , m_hostsPlacement{}
    , m_binaryRankMap{}

    , m_apinfoPath{}
    , m_toolPath{}
    , m_attribsPath{} // BE daemon looks for <m_attribsPath>/pmi_attribs
    , m_stagePath{}
    , m_extraFiles{}

    , m_atBarrier{}
{
    throw std::runtime_error{"not implemented: " + std::string{__func__}};
}

FluxApp::~FluxApp()
{}
