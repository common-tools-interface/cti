/******************************************************************************\
 * Frontend.cpp - Flux specific frontend library functions.
 *
 * Copyright 2021 Hewlett Packard Enterprise Development LP.
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
#include <type_traits>

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

[[ noreturn ]] static void unimplemented(std::string const& functionName)
{
    throw std::runtime_error("unimplemented: " + functionName);
}

// Leverage Flux's dry-run mode to generate jobspec for API
static std::string make_jobspec(char const* launcher_name, char const* const launcher_args[],
    char const* const env_list[])
{
    // Build Flux dry run arguments
    auto fluxArgv = cti::ManagedArgv { launcher_name, "mini", "run", "--dry-run" };

    // Add environment arguments, if provided
    if (env_list != nullptr) {
        for (int i = 0; env_list[i] != nullptr; i++) {

            // --env=VAR=VAL will set VAR to VAL in the job environment
            fluxArgv.add("--env=" + std::string{env_list[i]});
        }
    }

    // Add launcher arguments
    if (launcher_args != nullptr) {
        for (int i = 0; launcher_args[i] != nullptr; i++) {
            fluxArgv.add(launcher_args[i]);
        }
    }

    // Run jobspec generator
    auto fluxOutput = cti::Execvp{launcher_name, (char* const*)fluxArgv.get(), cti::Execvp::stderr::Pipe};
    auto result = std::string{std::istreambuf_iterator<char>(fluxOutput.stream()), {}};

    // Check output code
    if (fluxOutput.getExitStatus() != 0) {
        throw std::runtime_error("The Flux launcher failed to validate the provided launcher \
arguments: \n" + result);
    }

    return result;
}

/* FluxFrontend implementation */

struct FluxFrontend::LibFlux
{
    using FluxOpen = flux_t*(char const*, int);
    using FluxClose = void(flux_t*);
    using FluxFatality = bool(flux_t*);

    using FluxSend = int(flux_t*, flux_msg_t const *, int);
    using FluxRecv = flux_msg_t*(flux_t*, flux_match, int);
    using FluxMsgCreate = flux_msg_t*(int);
    using FluxMsgDestroy = void(flux_msg_t*);

    using FluxFutureErrorString = char const*(flux_future_t*);

    using FluxJobSubmit = flux_future_t*(flux_t*, const char*, int, int);
    using FluxJobSubmitGetId = int(flux_future_t*, flux_jobid_t*);
    using FluxJobIdParse = int(const char*, flux_jobid_t*);
    using FluxJobIdEncode = int(flux_jobid_t, const char*, char*, size_t);

    using FluxReactorCreate = flux_reactor_t*(int);
    using FluxReactorDestroy = void(flux_reactor_t*);

    using FluxWatcherStart = void(flux_watcher_t*);
    using FluxWatcherStop = void(flux_watcher_t*);
    using FluxWatcherDestroy = void(flux_watcher_t*);
    using FluxHandleWatcherCreate = flux_watcher_t*(flux_reactor_t*, flux_t*, int, flux_watcher_f, void*);
    using FluxFdWatcherCreate = flux_watcher_t*(flux_reactor_t*, int, int, flux_watcher_f, void*);
    using FluxBufferReadWatcherCreate = flux_watcher_t*(flux_reactor_t*, int, int, flux_watcher_f, int, void*);
    using FluxBufferWriteWatcherCreate = flux_watcher_t*(flux_reactor_t*, int, int, flux_watcher_f, int, void*);
    using FluxBufferWriteWatcherClose = int(flux_watcher_t*);

    using FluxBufferReadWatcherGetBuffer = flux_buffer_t*(flux_watcher_t*);
    using FluxBufferWriteWatcherGetBuffer = flux_buffer_t*(flux_watcher_t*);

    cti::Dlopen::Handle libFluxHandle;

    std::function<FluxOpen> flux_open;
    std::function<FluxClose> flux_close;
    std::function<FluxFatality> flux_fatality;

    std::function<FluxSend> flux_send;
    std::function<FluxRecv> flux_recv;

    std::function<FluxMsgCreate> flux_msg_create;
    std::function<FluxMsgDestroy> flux_msg_destroy;

    std::function<FluxFutureErrorString> flux_future_error_string;

    std::function<FluxJobSubmit> flux_job_submit;
    std::function<FluxJobSubmitGetId> flux_job_submit_get_id;
    std::function<FluxJobIdParse> flux_job_id_parse;
    std::function<FluxJobIdEncode> flux_job_id_encode;

    std::function<FluxReactorCreate> flux_reactor_create;
    std::function<FluxReactorDestroy> flux_reactor_destroy;

    std::function<FluxWatcherStart> flux_watcher_start;
    std::function<FluxWatcherStop> flux_watcher_stop;
    std::function<FluxWatcherDestroy> flux_watcher_destroy;
    std::function<FluxHandleWatcherCreate> flux_handle_watcher_create;
    std::function<FluxFdWatcherCreate> flux_fd_watcher_create;
    std::function<FluxBufferReadWatcherCreate> flux_buffer_read_watcher_create;
    std::function<FluxBufferWriteWatcherCreate> flux_buffer_write_watcher_create;
    std::function<FluxBufferWriteWatcherClose> flux_buffer_write_watcher_close;
    std::function<FluxBufferReadWatcherGetBuffer> flux_buffer_read_watcher_get_buffer;
    std::function<FluxBufferWriteWatcherGetBuffer> flux_buffer_write_watcher_get_buffer;

    LibFlux(std::string const& libFluxName);
};

FluxFrontend::LibFlux::LibFlux(std::string const& libFluxName)
    : libFluxHandle{libFluxName}
    , flux_open{libFluxHandle.load<FluxOpen>("flux_open")}
    , flux_close{libFluxHandle.load<FluxClose>("flux_close")}
    , flux_fatality{libFluxHandle.load<FluxFatality>("flux_fatality")}
    , flux_send{libFluxHandle.load<FluxSend>("flux_send")}
    , flux_recv{libFluxHandle.load<FluxRecv>("flux_recv")}
    , flux_msg_create{libFluxHandle.load<FluxMsgCreate>("flux_msg_create")}
    , flux_msg_destroy{libFluxHandle.load<FluxMsgDestroy>("flux_msg_destroy")}
    , flux_job_submit{libFluxHandle.load<FluxJobSubmit>("flux_job_submit")}
    , flux_job_submit_get_id{libFluxHandle.load<FluxJobSubmitGetId>("flux_job_submit_get_id")}
    , flux_job_id_parse{libFluxHandle.load<FluxJobIdParse>("flux_job_id_parse")}
    , flux_job_id_encode{libFluxHandle.load<FluxJobIdEncode>("flux_job_id_encode")}
    , flux_reactor_create{libFluxHandle.load<FluxReactorCreate>("flux_reactor_create")}
    , flux_watcher_start{libFluxHandle.load<FluxWatcherStart>("flux_watcher_start")}
    , flux_watcher_stop{libFluxHandle.load<FluxWatcherStop>("flux_watcher_stop")}
    , flux_watcher_destroy{libFluxHandle.load<FluxWatcherDestroy>("flux_watcher_destroy")}
    , flux_handle_watcher_create{libFluxHandle.load<FluxHandleWatcherCreate>("flux_handle_watcher_create")}
    , flux_fd_watcher_create{libFluxHandle.load<FluxFdWatcherCreate>("flux_fd_watcher_create")}
    , flux_buffer_read_watcher_create{libFluxHandle.load<FluxBufferReadWatcherCreate>("flux_buffer_read_watcher_create")}
    , flux_buffer_write_watcher_create{libFluxHandle.load<FluxBufferWriteWatcherCreate>("flux_buffer_write_watcher_create")}
    , flux_buffer_write_watcher_close{libFluxHandle.load<FluxBufferWriteWatcherClose>("flux_buffer_write_watcher_close")}
    , flux_buffer_read_watcher_get_buffer{libFluxHandle.load<FluxBufferReadWatcherGetBuffer>("flux_buffer_read_watcher_get_buffer")}
    , flux_buffer_write_watcher_get_buffer{libFluxHandle.load<FluxBufferWriteWatcherGetBuffer>("flux_buffer_write_watcher_get_buffer")}
{}

std::weak_ptr<App>
FluxFrontend::launch(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
    CStr inputFile, CStr chdirPath, CArgArray env_list)
{
    // Launch application using API
    auto const job_id = launchApp(launcher_argv, inputFile, stdout_fd, stderr_fd,
        chdirPath, env_list);

    // Create and track new application object
    auto ret = m_apps.emplace(std::make_shared<FluxApp>(*this, job_id));
    if (!ret.second) {
        throw std::runtime_error("Failed to create new App object.");
    }
    return *ret.first;
}

std::weak_ptr<App>
FluxFrontend::launchBarrier(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
    CStr inputFile, CStr chdirPath, CArgArray env_list)
{
    unimplemented(__func__);
}

std::weak_ptr<App>
FluxFrontend::registerJob(size_t numIds, ...)
{
    if (numIds != 1) {
        throw std::logic_error("expecting single job ID argument to register app");
    }

    va_list idArgs;
    va_start(idArgs, numIds);

    char const* f58_job_id = va_arg(idArgs, char const*);

    va_end(idArgs);

    // Convert F58-formatted job ID to internal job ID
    auto job_id = flux_jobid_t{};
    if (m_libFlux->flux_job_id_parse(f58_job_id, &job_id) < 0) {
        throw std::runtime_error("failed to parse Flux job ID: " + std::string{f58_job_id});
    }

    // Create new application instance with job ID
    auto ret = m_apps.emplace(std::make_shared<FluxApp>(*this, job_id));
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
    // Cache the launcher name result.
    auto static launcherName = std::string{cti::getenvOrDefault(CTI_LAUNCHER_NAME_ENV_VAR, "flux")};
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
    auto const launcher_path = cti::take_pointer_ownership(_cti_pathFind(launcherName.c_str(), nullptr), ::free);

    // Ensure launcher was found in path
    if (launcher_path == nullptr) {
        throw std::runtime_error("Could not find Flux launcher '" + launcherName + "' in PATH. \
Ensure the Flux launcher is accessible and executable");
    }

    // LDD launcher binary to find path to libflux library
    char const* ldd_argv[] = { "ldd", launcher_path.get(), nullptr };
    auto lddOutput = cti::Execvp{"ldd", (char* const*)ldd_argv, cti::Execvp::stderr::Ignore};

    // Capture LDD output
    auto& lddStream = lddOutput.stream();
    auto libraryPathMap = std::string{};
    while (std::getline(lddStream, libraryPathMap)) {
        auto const [library, sep, path, address] = cti::split::string<4>(libraryPathMap, ' ');

        // Accept library if it begins with LIBFLUX_NAME prefix
        if (cti::split::removeLeadingWhitespace(library).rfind(LIBFLUX_NAME, 0) == 0) {
            return path;
        }
    }

    throw std::runtime_error("Could not find the path to " LIBFLUX_NAME " in the launcher's \
dependencies. Try setting " LIBFLUX_PATH_ENV_VAR " to the path to the Flux runtime library");
}

// Normal cti::take_pointer_ownership cannot deal with std::function types,
// this wrapper enables capture of std::function destructors
template <typename T, typename Destr>
static auto make_unique_del(T*&& expiring, Destr&& destr)
{
    auto bound_destr = [&destr](T* ptr) {
        std::invoke(destr, ptr);
    };
    return std::unique_ptr<T, decltype(bound_destr)>{std::move(expiring), bound_destr};
}

uint64_t FluxFrontend::launchApp(const char* const launcher_args[],
    const char* input_file, int stdout_fd, int stderr_fd, const char *chdir_path,
    const char * const env_list[])
{
    if ((input_file != nullptr) || (stdout_fd >= 0) || (stderr_fd >= 0)
     || (chdir_path != nullptr)) {
        unimplemented(__func__);
    }

    // Generate jobspec string
    auto const jobspec = make_jobspec(getLauncherName().c_str(), launcher_args, env_list);

    // Submit jobspec to API
    auto job_future = m_libFlux->flux_job_submit(m_fluxHandle, jobspec.c_str(), 16, 0);

    // Wait for job to launch and receive job ID
    auto job_id = flux_jobid_t{};
    if (m_libFlux->flux_job_submit_get_id(job_future, &job_id)) {
        auto const flux_error = m_libFlux->flux_future_error_string(job_future);
        throw std::runtime_error("Flux job launch failed: " + std::string{(flux_error)
            ? flux_error
            : "(no error provided)"});
    }

    return job_id;
}

FluxFrontend::FluxFrontend()
    : m_libFluxPath{findLibFluxPath(cti::getenvOrDefault(CTI_LAUNCHER_NAME_ENV_VAR, "flux"))}
    , m_libFlux{std::make_unique<LibFlux>(m_libFluxPath)}
    // Flux will read socket information in environment
    , m_fluxHandle{m_libFlux->flux_open(nullptr, 0)}
{
    if (m_fluxHandle == nullptr) {
        throw std::runtime_error{"Flux initialization failed: " + std::string{strerror(errno)}};
    }
}

FluxFrontend::~FluxFrontend()
{
    m_libFlux->flux_close(m_fluxHandle);
    m_fluxHandle = nullptr;
}

/* FluxApp implementation */

std::string
FluxApp::getJobId() const
{
    // Convert numerical job ID to compact F58 encoding
    char buf[64];
    if (m_libFluxRef.flux_job_id_encode(m_jobId, "f58", buf, sizeof(buf) - 1) < 0) {
        throw std::runtime_error("failed to encode Flux job id: " + std::string{strerror(errno)});
    }
    buf[sizeof(buf) - 1] = '\0';

    return std::string{buf};
}

std::string
FluxApp::getLauncherHostname() const
{
    unimplemented(__func__);
}

bool
FluxApp::isRunning() const
{
    unimplemented(__func__);
}

std::vector<std::string>
FluxApp::getHostnameList() const
{
    unimplemented(__func__);
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

    unimplemented(__func__);

    m_atBarrier = false;
}

void
FluxApp::kill(int signal)
{
    unimplemented(__func__);
}

void
FluxApp::shipPackage(std::string const& tarPath) const
{
    unimplemented(__func__);
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

    unimplemented(__func__);
}

FluxApp::FluxApp(FluxFrontend& fe, uint64_t job_id)
    : App{fe}
    , m_libFluxRef{*fe.m_libFlux}
    , m_jobId{job_id}
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
    unimplemented(__func__);
}

FluxApp::~FluxApp()
{}
