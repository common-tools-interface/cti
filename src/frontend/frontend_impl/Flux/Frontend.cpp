/******************************************************************************\
 * Frontend.cpp - Flux specific frontend library functions.
 *
 * Copyright 2021 Hewlett Packard Enterprise Development LP.
 * SPDX-License-Identifier: Linux-OpenIB
 ******************************************************************************/

// This pulls in config.h
#include "cti_defs.h"
#include "cti_argv_defs.hpp"

#include <memory>
#include <thread>
#include <variant>
#include <type_traits>
#include <numeric>
#include <filesystem>

#include <sstream>
#include <fstream>

#include "transfer/Manifest.hpp"

#define FLUX_SHELL_PLUGIN_NAME cti

#include <flux/core.h>
#include <flux/shell.h>

#include "Flux/Frontend.hpp"

// Boost JSON
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/algorithm/string/replace.hpp>

// Boost array stream
#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/stream.hpp>

#include "useful/cti_execvp.hpp"
#include "useful/cti_hostname.hpp"
#include "useful/cti_split.hpp"

#include "LibFlux.hpp"
#include "FluxAPI.hpp"

namespace pt = boost::property_tree;

/* helper functions */

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

// Leverage Flux's dry-run mode to generate jobspec for API
static std::string make_jobspec(char const* launcher_name, char const* const launcher_args[],
    std::string const& inputPath, std::string const& outputPath, std::string const& errorPath,
    std::string const& chdirPath, char const* const env_list[],
    std::map<std::string, std::string> const& jobAttributes)
{
    // Build Flux dry run arguments
    auto fluxArgv = cti::ManagedArgv { launcher_name, "submit", "--dry-run" };

    // Add input / output / error files, if provided
    if (!inputPath.empty()) {
        fluxArgv.add("--input=" + inputPath);
    }
    if (!outputPath.empty()) {
        fluxArgv.add("--output=" + outputPath);
    }
    if (!errorPath.empty()) {
        fluxArgv.add("--error=" + errorPath);
    }

    // Add cwd attribute for working directory, if provided
    if (!chdirPath.empty()) {
        fluxArgv.add("--setattr=system.cwd=" + chdirPath);
    }

    // Add environment arguments, if provided
    if (env_list != nullptr) {
        for (int i = 0; env_list[i] != nullptr; i++) {

            // --env=VAR=VAL will set VAR to VAL in the job environment
            fluxArgv.add("--env=" + std::string{env_list[i]});
        }
    }

    // Add additional job attributes
    for (auto&& [attr, setting] : jobAttributes) {
        fluxArgv.add("--setattr=" + attr + "=" + setting);
    }

    // Add launcher arguments
    if (launcher_args != nullptr) {
        for (int i = 0; launcher_args[i] != nullptr; i++) {
            fluxArgv.add(launcher_args[i]);
        }
    }

    // Run jobspec generator
    auto fluxOutput = cti::Execvp{launcher_name, (char* const*)fluxArgv.get(),
        cti::Execvp::stderr::Ignore};
    auto result = std::string{std::istreambuf_iterator<char>(fluxOutput.stream()), {}};

    // Check output code
    if (fluxOutput.getExitStatus() != 0) {
        throw std::runtime_error("The Flux launcher failed to validate the provided launcher \
arguments: \n" + result);
    }

    return result;
}

static auto get_flux_future_error(FluxFrontend::LibFlux& libFluxRef, flux_future_t* future)
{
    auto const flux_error = libFluxRef.flux_future_error_string(future);
    return std::string{(flux_error)
        ? flux_error
        : "(no error provided)"};
}

// Query event log for job leader rank and service key for RPC
static auto get_rpc_service(FluxFrontend::LibFlux& libFlux, FluxFrontend::flux_t* fluxHandle,
    uint64_t job_id)
{
    auto eventlog_future = make_unique_del(
        libFlux.flux_job_event_watch(fluxHandle, job_id, "guest.exec.eventlog", 0),
        libFlux.flux_future_destroy);
    if (eventlog_future == nullptr) {
        throw std::runtime_error("Flux job event query failed");
    }

    // Read event log responses
    while (true) {
        char const *eventlog_result = nullptr;
        auto const eventlog_rc = libFlux.flux_job_event_watch_get(eventlog_future.get(), &eventlog_result);
        if (eventlog_rc == ENODATA) {
            continue;
        } else if (eventlog_rc < 0) {
            throw std::runtime_error("Flux job event query failed: " + get_flux_future_error(libFlux, eventlog_future.get()));
        }

        // Received a new event log result, parse it as JSON
        auto root = parse_json(eventlog_result);

        // Looking for shell.init event, will contain leader rank and service key
        if (root.get<std::string>("name") == "shell.init") {

            // Got shell.init, extract the job information
            auto context = root.get_child("context");
            auto leaderRank = context.get<int>("leader-rank");
            auto rpcService = context.get<std::string>("service");

            if (rpcService.empty()) {
                throw std::runtime_error("Flux API returned empty RPC service key");
            }

            return std::make_pair(leaderRank, std::move(rpcService));
        }

        // Reset and wait for next event log result
        libFlux.flux_future_reset(eventlog_future.get());
    }
}

static std::string make_rpc_request(FluxFrontend::LibFlux& libFlux, FluxFrontend::flux_t* fluxHandle,
    int leader_rank, std::string const& topic, std::string const& content)
{
    // Create request future
    auto future = make_unique_del(
        libFlux.flux_rpc_raw(fluxHandle, topic.c_str(), content.c_str(), content.length() + 1, leader_rank, 0),
        libFlux.flux_future_destroy);
    if (future == nullptr) {
        throw std::runtime_error("Flux query failed");
    }

    // Block and read until RPC returns response
    char const *result = nullptr;
    auto const rc = libFlux.flux_rpc_get(future.get(), &result);
    if (rc < 0) {
        throw std::runtime_error("Flux query with topic " + topic + " failed: " + get_flux_future_error(libFlux, future.get()));
    }

    return std::string{(result) ? result : ""};
}

static const char utf8_prefix[] = u8"\u0192";

// Parse raw job ID string (f58 or otherwise) into numeric job ID
static inline auto parse_job_id(FluxFrontend::LibFlux& libFluxRef, char const* raw_job_id)
{
    // Determine if job ID is f58-formatted by checking for f58 prefix
    auto const f58_formatted = (::strncmp(utf8_prefix, raw_job_id, ::strlen(utf8_prefix)) == 0);

    // Convert F58-formatted job ID to internal job ID
    auto job_id = flux_jobid_t{};
    if (f58_formatted) {

        if (libFluxRef.flux_job_id_parse(raw_job_id, &job_id) < 0) {
            throw std::runtime_error("failed to parse Flux job ID: " + std::string{raw_job_id});
        }

    // Job ID was provided in numeric format
    } else {
        job_id = std::stol(raw_job_id);
    }

    return job_id;
}

// Convert numerical job ID to compact F58 encoding
static inline auto encode_job_id(FluxFrontend::LibFlux& libFluxRef, uint64_t job_id)
{
    // Job IDs are a maximum of 14 characters (12 characters, 2-byte f prefix, 1 terminator)
    char buf[64];

    // flux_job_id_encode will always output in ASCII-only, so will need to overwrite the
    // ASCII 'f' with UTF-8 prefix if not disabled
    auto const utf8_enabled = (!getenv("FLUX_F58_FORCE_ASCII"));
    auto const offset_len = (utf8_enabled)
        ? strlen(utf8_prefix) - 1 // Last byte of UTF-8 prefix will overwrite ASCII prefix
        : 0;

    if (libFluxRef.flux_job_id_encode(job_id, "f58", buf + offset_len, sizeof(buf) - offset_len) < 0) {
        throw std::runtime_error("failed to encode Flux job id: " + std::string{strerror(errno)});
    }
    buf[sizeof(buf) - 1] = '\0';

    // Replace ASCII 'f' with prefix
    if (utf8_enabled) {
        ::memcpy(buf, utf8_prefix, strlen(utf8_prefix));
    }

    return std::string{buf};
}

static int cancel_job(FluxFrontend::LibFlux& libFlux, FluxFrontend::flux_t* fluxHandle,
    flux_jobid_t id, char const* reason)
{
    // Create cancel future
    auto future = make_unique_del(
        libFlux.flux_job_cancel(fluxHandle, id, reason),
        libFlux.flux_future_destroy);
    if (future == nullptr) {
        return -1;
    }

    // Block and wait until canceled
    // TODO: this waiting process can be chained for multiple cancellations
    return libFlux.flux_future_wait_for(future.get(), 0);
}

/* FluxFrontend implementation */

std::weak_ptr<App>
FluxFrontend::launch(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
    CStr inputFile, CStr chdirPath, CArgArray env_list)
{
    // Launch application using API
    auto launchInfo = launchApp(launcher_argv, inputFile, stdout_fd, stderr_fd,
        chdirPath, env_list, LaunchBarrierMode::Disabled);

    // Create and track new application object
    auto ret = m_apps.emplace(std::make_shared<FluxApp>(*this, std::move(launchInfo)));
    if (!ret.second) {
        throw std::runtime_error("Failed to create new App object.");
    }

    return *ret.first;
}

std::weak_ptr<App>
FluxFrontend::launchBarrier(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
    CStr inputFile, CStr chdirPath, CArgArray env_list)
{
    // Launch application with barrier using API
    auto launchInfo = launchApp(launcher_argv, inputFile, stdout_fd, stderr_fd,
        chdirPath, env_list, LaunchBarrierMode::Enabled);

    // Create and track new application object
    auto ret = m_apps.emplace(std::make_shared<FluxApp>(*this, std::move(launchInfo)));
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

    char const* raw_job_id = va_arg(idArgs, char const*);

    va_end(idArgs);

    // Get attach information from Flux API
    auto launchInfo = LaunchInfo
        { .jobId = parse_job_id(*m_libFlux, raw_job_id)
        , .atBarrier = false
    };

    // Create new application instance with job ID
    auto ret = m_apps.emplace(std::make_shared<FluxApp>(*this, std::move(launchInfo)));
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
FluxFrontend::findFluxInstallDir(std::string const& launcherName)
{
    // Use setting if supplied
    if (auto const flux_install_dir = ::getenv(FLUX_INSTALL_DIR_ENV_VAR)) {
       return std::string{flux_install_dir};
    }

    // Find libflux from launcher location
    auto fluxInstallDir = std::string{};
    auto const launcher_path = cti::take_pointer_ownership(_cti_pathFind(launcherName.c_str(), nullptr), ::free);

    // Ensure launcher was found in path
    if (launcher_path == nullptr) {
        throw std::runtime_error("Could not find Flux launcher '" + launcherName + "' in PATH. \
Ensure the Flux launcher is accessible and executable");
    }

    // Flux root install dir will be parent directory of launcher root
    return cti::cstr::realpath(cti::cstr::dirname(launcher_path.get()) + "/../");
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

FluxFrontend::LaunchInfo FluxFrontend::launchApp(const char* const launcher_args[],
    const char* input_file, int stdout_fd, int stderr_fd, const char *chdir_path,
    const char * const env_list[], FluxFrontend::LaunchBarrierMode const launchBarrierMode)
{
    // Get output, and error files from file descriptors
    auto const outputPath = (stdout_fd >= 0)
        ? "/proc/" + std::to_string(getpid()) + "/fd/" + std::to_string(stdout_fd)
        : std::string{};
    auto const errorPath = (stderr_fd >= 0)
        ? "/proc/" + std::to_string(getpid()) + "/fd/" + std::to_string(stderr_fd)
        : std::string{};

    // Add barrier option if enabled
    auto jobAttributes = std::map<std::string, std::string>{};
    if (launchBarrierMode == LaunchBarrierMode::Enabled) {
        jobAttributes["system.shell.options.stop-tasks-in-exec"] = "1";
    }

    // Generate jobspec string
    auto const jobspec = make_jobspec(getLauncherName().c_str(), launcher_args,
        (input_file != nullptr) ? input_file : "",
        outputPath, errorPath,
        (chdir_path != nullptr) ? chdir_path : "",
        env_list,
        jobAttributes);

    // Submit jobspec to API
    auto job_future = m_libFlux->flux_job_submit(m_fluxHandle, jobspec.c_str(), 16, 0);

    // Wait for job to launch and receive job ID
    auto job_id = flux_jobid_t{};
    if (m_libFlux->flux_job_submit_get_id(job_future, &job_id) < 0) {
        throw std::runtime_error("Flux job launch failed: " + get_flux_future_error(*m_libFlux, job_future));
    }

    return LaunchInfo
        { .jobId = job_id
        , .atBarrier = (launchBarrierMode == LaunchBarrierMode::Enabled)
    };
}

FluxFrontend::FluxFrontend()
    : m_libFluxPath{findLibFluxPath(cti::getenvOrDefault(CTI_LAUNCHER_NAME_ENV_VAR, "flux"))}
    , m_libFlux{std::make_unique<LibFlux>(m_libFluxPath)}
    // Flux will read socket information in environment
    , m_fluxHandle{m_libFlux->flux_open(nullptr, 0)}
{
    while (m_fluxHandle == nullptr) {
        if (errno == EINTR) {
            m_fluxHandle = m_libFlux->flux_open(nullptr, 0);
            continue;
        } else {
            throw std::runtime_error{"Flux initialization failed: "
                + std::string{strerror(errno)}};
        }
    }

    // Check that flux runtime version matches header version (check can be removed after
    // libflux-core interface stabilizes in Flux 1.0 release)
    // Can be bypassed by setting environment variable
    if (::getenv(CTI_FLUX_DEBUG_ENV_VAR) == nullptr) {
        auto fluxArgv = cti::ManagedArgv{getLauncherName(), "--version"};
        auto fluxOutput = cti::Execvp{getLauncherName().c_str(), fluxArgv.get(), cti::Execvp::stderr::Ignore};

        // Read libflux-core version line
        auto& fluxStream = fluxOutput.stream();
        auto versionLine = std::string{};
        while (std::getline(fluxStream, versionLine)) {

            // Split line into each word
            auto const [key, value] = cti::split::string<2>(versionLine, ':');
            if (key == "libflux-core") {
                auto const runtime_version = cti::split::removeLeadingWhitespace(value);
                auto const [version, hash] = cti::split::string<2>(FLUX_CORE_VERSION_STRING, '-');

                if (runtime_version == version) {
                    break;
                } else {
                    throw std::runtime_error("Mismatch between system's libflux-core version (" +
runtime_version + ") and CTI's built version (" + version + "). libflux-core is still in \
development, and its interface is subject to change. To attempt to continue, set the environment \
variable " CTI_FLUX_DEBUG_ENV_VAR " and relaunch this application.");
                }
            }
        }
    }

    // Remove any existing jobtap plugins
    (void)make_rpc_request(*m_libFlux, m_fluxHandle, FLUX_NODEID_ANY,
        "job-manager.jobtap", "{\"remove\": \"all\"}");

    // Load alloc-bypass jobtap plugin to allow oversubscription
    { auto const fluxInstallDir = findFluxInstallDir(getLauncherName());
        auto allocBypassPaths = std::vector<std::string>
            { fluxInstallDir + "/lib64/flux/job-manager/plugins/alloc-bypass.so"
            , fluxInstallDir + "/lib/flux/job-manager/plugins/alloc-bypass.so"
        };

        // Try loading from potential alloc bypass library paths
        auto load_successful = false;
        for (auto&& allocBypassPath : allocBypassPaths) {
            auto loadRequest = R"({"load": ")" + allocBypassPath + R"("})";

            // Make load RPC request
            try {
                (void)make_rpc_request(*m_libFlux, m_fluxHandle, FLUX_NODEID_ANY,
                    "job-manager.jobtap", loadRequest);

                load_successful = true;
                break;

            // Try next path if failed
            } catch (std::exception const& ex) {
                continue;
            }
        }

        if (!load_successful) {

            // Build error message with different paths tried
            auto errorMsgStream = std::stringstream{};
            errorMsgStream << "failed to load Flux jobtap plugin from: \n";
            for (auto&& allocBypassPath : allocBypassPaths) {
                errorMsgStream << allocBypassPath << "\n";
            }
            errorMsgStream << "Set " FLUX_INSTALL_DIR_ENV_VAR " to the root of "
                "your Flux installation";

            throw std::runtime_error(errorMsgStream.str());
        }
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
    static const auto jobIdF58 = encode_job_id(m_libFluxRef, m_jobId);
    return jobIdF58;
}

std::string
FluxApp::getLauncherHostname() const
{
    throw std::runtime_error("not supported for WLM: getLauncherHostname");
}

bool
FluxApp::isRunning() const
{
    // Create request future
    auto future = make_unique_del(
        m_libFluxRef.flux_job_list_id(m_fluxHandle, m_jobId, "[\"state\"]"),
        m_libFluxRef.flux_future_destroy);
    if (future == nullptr) {
        throw std::runtime_error("Flux query failed: " + std::string{strerror(errno)});
    }

    // Block and read until API returns response
    char const *result = nullptr;
    auto const rc = m_libFluxRef.flux_rpc_get(future.get(), &result);
    if ((rc < 0) || (result == nullptr)) {
        throw std::runtime_error("Flux query failed: " + get_flux_future_error(m_libFluxRef, future.get()));
    }

    // Parse JSON
    auto root = parse_json(result);
    auto const state = root.get<int>("job.state");

    return (state == FLUX_JOB_STATE_RUN);
}

std::vector<std::string>
FluxApp::getHostnameList() const
{
    std::vector<std::string> result;

    // extract hostnames from each CTIHost
    std::transform(m_hostsPlacement.begin(), m_hostsPlacement.end(), std::back_inserter(result),
        [](FluxFrontend::HostPlacement const& placement) { return placement.hostname; });
    return result;
}

std::vector<CTIHost>
FluxApp::getHostsPlacement() const
{
    std::vector<CTIHost> result;

    // Extract hostnames and number of PEs from each CTIHost
    std::transform(m_hostsPlacement.begin(), m_hostsPlacement.end(), std::back_inserter(result),
        [](FluxFrontend::HostPlacement const& placement) {
            return CTIHost { .hostname = placement.hostname, .numPEs = placement.numPEs };
        } );

    return result;
}

std::map<std::string, std::vector<int>>
FluxApp::getBinaryRankMap() const
{
    // As Flux does not support MPMD, the binary / rank map can be generated with the single
    // binary name and number of PEs
    static const auto binaryRankMap = [this]() {
        auto result = std::map<std::string, std::vector<int>>{};

        auto allRanks = std::vector<int>{};
        allRanks.reserve(m_numPEs);
        for (size_t i = 0; i < m_numPEs; i++) {
            allRanks.push_back(i);
        }

        result[m_binaryName] = std::move(allRanks);

        return result;
    }();

    return binaryRankMap;
}

void
FluxApp::releaseBarrier()
{
    if (!m_atBarrier) {
        throw std::runtime_error("application is not at startup barrier");
    }

    // Send SIGCONT to job to release from barrier
    kill(SIGCONT);

    m_atBarrier = false;
}

void
FluxApp::kill(int signal)
{
    // Create signal future
    auto future = make_unique_del(
        m_libFluxRef.flux_job_kill(m_fluxHandle, m_jobId, signal),
        m_libFluxRef.flux_future_destroy);
    if (future == nullptr) {
        return;
    }

    // Block and wait until signal sent
    m_libFluxRef.flux_future_wait_for(future.get(), 0);
}

void
FluxApp::shipPackage(std::string const& tarPath) const
{
    auto const dirName = cti::cstr::dirname(tarPath);
    auto const packageName = cti::cstr::basename(tarPath);
    auto const destination = m_toolPath + "/" + packageName;
    writeLog("Flux shipping %s to '%s'\n", tarPath.c_str(), destination.c_str());

    if (!m_runningOnBroker) {

        // Make remote directory on broker
        writeLog("Broker: mkdir %s\n", m_toolPath.c_str());
        char const* exec_mkdir_argv[] = {"flux", "exec", "-r", "0", "mkdir", "-p",
            m_toolPath.c_str(), nullptr};
        if (cti::Execvp::runExitStatus("flux", (char* const*)exec_mkdir_argv)) {
            throw std::runtime_error("failed to make directory on rank 0 " + m_toolPath);
        }

        // Send file to broker
        writeLog("Broker: piping %s to %s\n", tarPath.c_str(), destination.c_str());
        { auto catCommand = "cat " + tarPath + " | flux exec -r 0 sed -n 'w " + destination + "'";
            char const* exec_cat_argv[] = {"bash", "-c", catCommand.c_str(), nullptr};

            // Input piping for large files can still fail on the first invocation,
            // but an immediate retry is usually successful. Deduplicated manifests
            // are small enough to not run into this issue for CDST products.
            // https://github.com/flux-framework/flux-core/issues/4572
            auto const max_retry = 5;
            for (int retry = 1; retry <= max_retry; retry++) {
                fprintf(stderr, "trying ship %s\n", destination.c_str());
                if (cti::Execvp::runExitStatus("bash", (char* const*)exec_cat_argv)) {
                    if (retry == max_retry) {
                        throw std::runtime_error("failed to send file to rank 0 " + destination);
                    }
                    writeLog("Broker: failed to pipe file (retry %d/%d)\n", retry, max_retry);
                } else {
                    break;
                }
            }
        }

        // Make file executable
        writeLog("Broker: chmod +x %s\n", destination.c_str());
        char const* exec_chmod_argv[] = {"flux", "exec", "-r", "0", "chmod", "+x",
            destination.c_str(), nullptr};
        if (cti::Execvp::runExitStatus("flux", (char* const*)exec_chmod_argv)) {
            throw std::runtime_error("failed to make directory on rank 0 " + m_toolPath);
        }
    }

    // Broadcast file from broker to other nodes
    if (m_runningOnBroker || (m_hostsPlacement.size() > 1)) {

        // Map file on broker node to central store
        writeLog("Broker: adding to filemap\n");
        char const* exec_map_argv[] = {"flux", "exec", "-r", "0", "flux", "filemap", "map",
            "--tags", m_toolPath.c_str(), "-C",
            (m_runningOnBroker) ? dirName.c_str() : m_toolPath.c_str(),
            packageName.c_str(), nullptr};
        if (cti::Execvp::runExitStatus("flux", (char* const*)exec_map_argv)) {
            throw std::runtime_error("failed to map file on rank 0 " + destination);
        }

        // Make remote directories and pull from filemap
        writeLog("%s: mkdir %s and pull %s\n",
            (m_runningOnBroker) ? "All ranks" : "Non-broker",
            m_toolPath.c_str(), packageName.c_str());
        auto mkdirGetCmd = "mkdir -p " + m_toolPath + "; flux filemap get --tags "
            + m_toolPath + " -C " + m_toolPath + " " + packageName;
        char const* exec_get_argv[] = {"flux", "exec", "-r",
            (m_runningOnBroker) ? "all" : m_nonBrokerRanks.c_str(),
            "bash", "-c", mkdirGetCmd.c_str(), nullptr};
        if (cti::Execvp::runExitStatus("flux", (char* const*)exec_get_argv)) {
            throw std::runtime_error("failed to get mapped file " + destination);
        }

        // Unmap file on broker node
        writeLog("Broker: unmapping file\n");
        char const* exec_unmap_argv[] = {"flux", "exec", "-r", "0", "flux", "filemap", "unmap",
            "--tags", m_toolPath.c_str(), nullptr};
        if (cti::Execvp::runExitStatus("flux", (char* const*)exec_unmap_argv)) {
            throw std::runtime_error("failed to unmap file on rank 0 " + destination);
        }
    }
}

void
FluxApp::startDaemon(const char* const args[], bool synchronous)
{
    // Prepare to start daemon binary on compute node
    auto const remoteBEDaemonPath = m_toolPath + "/" + getBEDaemonName();

    // Send daemon if not already shipped
    if (!m_beDaemonSent) {
        shipDaemon();
    }

    // Create daemon argument array
    auto launcherArgv = cti::ManagedArgv{};

    // Resource spec provides for number of nodes, match that to number of ranks when
    // launching the daemon
    launcherArgv.add("-n" + std::to_string(m_hostsPlacement.size()));

    // Add daemon and arguments if provided
    launcherArgv.add(remoteBEDaemonPath);
    if (args != nullptr) {
        for (const char* const* arg = args; *arg != nullptr; arg++) {
            launcherArgv.add(*arg);
        }
    }

    // Generate daemon jobspec string
    auto& fluxFrontend = dynamic_cast<FluxFrontend&>(m_frontend);
    auto const jobspec = make_jobspec(fluxFrontend.getLauncherName().c_str(), launcherArgv.get(),
        "", "", "", "", {}, // No input, output, error, chdir, environment settings
        { { "system.alloc-bypass.R", m_resourceSpec } });

    // Submit jobspec to API
    writeLog("Submitting daemon jobspec:\n%s\n", jobspec.c_str());
    auto job_submit_flags = (synchronous)
        ? (int)FluxFrontend::LibFlux::JobSubmitFlags::FluxJobWaitable
        : int{};
    auto daemon_job_future = m_libFluxRef.flux_job_submit(m_fluxHandle, jobspec.c_str(), 16,
        job_submit_flags);

    // Wait for job to launch and receive job ID
    auto daemon_job_id = flux_jobid_t{};
    if (m_libFluxRef.flux_job_submit_get_id(daemon_job_future, &daemon_job_id) < 0) {
        throw std::runtime_error("Flux daemon launch failed: " + get_flux_future_error(m_libFluxRef, daemon_job_future));
    }

    if (synchronous) {
        auto daemon_wait_future = m_libFluxRef.flux_job_wait(m_fluxHandle, daemon_job_id);
        auto daemon_succeeded = false;
        char const* daemon_err_msg = nullptr;
        if (m_libFluxRef.flux_job_wait_get_status(daemon_wait_future, &daemon_succeeded,
            &daemon_err_msg) < 0) {
            throw std::runtime_error("Flux daemon wait failed: " + get_flux_future_error(m_libFluxRef, daemon_wait_future));
        } else if (!daemon_succeeded) {
            throw std::runtime_error("Daemon failed: " + std::string{
                (daemon_err_msg) ? daemon_err_msg : "(no message)"});
        }
    }

    // Add job ID to daemon job IDs
    m_daemonJobIds.push_back(daemon_job_id);
}

std::set<std::string>
FluxApp::checkFilesExist(std::set<std::string> const& paths)
{
    auto result = std::set<std::string>{};

    // Send daemon if not already shipped
    if (!m_beDaemonSent) {
        shipDaemon();
    }

    // Build daemon launcher arguments
    auto launcherArgv = cti::ManagedArgv{"flux", "exec", m_toolPath + "/" + getBEDaemonName()};
    for (auto&& path : paths) {
        launcherArgv.add("--file=" + path);
    }

    // Launch duplicate checker
    auto filesOutput = cti::Execvp{"flux", (char* const*)launcherArgv.get(),
        cti::Execvp::stderr::Ignore};
    auto& filesStream = filesOutput.stream();

    // Track number of present files
    auto num_nodes = m_hostsPlacement.size();
    auto pathCountMap = std::map<std::string, size_t>{};

    // Read out all paths from daemon
    auto exit_count = num_nodes;
    auto line = std::string{};
    while ((exit_count > 0) && std::getline(filesStream, line)) {

        // Daemons will print an empty line when output is completed
        if (line.empty()) {
            exit_count--;

        // Ignore library version warning from Flux utility
        } else if (line.find("no version information") != std::string::npos) {
            continue;

        // Received path from daemon
        } else {

            // Increment count for path
            pathCountMap[line]++;

            // Add path to duplicate list if all nodes have file
            if (pathCountMap[line] == num_nodes) {
                result.emplace(std::move(line));
            }
        }
    }

    return result;
}

FluxApp::FluxApp(FluxFrontend& fe, FluxFrontend::LaunchInfo&& launchInfo)
    : App{fe}
    , m_fluxHandle{fe.m_fluxHandle}
    , m_libFluxRef{*fe.m_libFlux}
    , m_jobId{launchInfo.jobId}

    , m_leaderRank{}
    , m_rpcService{}
    , m_resourceSpec{}

    , m_beDaemonSent{false}
    , m_numPEs{}
    , m_hostsPlacement{}
    , m_runningOnBroker{true} // Currently only support running from same Flux instance
    , m_nonBrokerRanks{}
    , m_binaryName{}

    , m_toolPath{}
    , m_stagePath{cti::cstr::mkdtemp(std::string{m_frontend.getCfgDir() + "/fluxXXXXXX"})}
    , m_extraFiles{}

    , m_atBarrier{launchInfo.atBarrier}

    , m_daemonJobIds{}
{
    // Get API access information for this job
    std::tie(m_leaderRank, m_rpcService) = get_rpc_service(m_libFluxRef, m_fluxHandle, m_jobId);
    writeLog("extracted job info: leader rank %d, service key %s\n", m_leaderRank, m_rpcService.c_str());

    // Start resource spec query
    { auto lookupRequest = std::stringstream{};
        lookupRequest
            << "{ \"id\": " << m_jobId
            << ", \"keys\": [\"R\"]"
            << ", \"flags\": 0"
        << "}";
        auto root = parse_json(make_rpc_request(m_libFluxRef, m_fluxHandle, m_leaderRank,
            "job-info.lookup", lookupRequest.str()));
        m_resourceSpec = root.get<std::string>("R");
    }

    // Start new proctable query
    { auto const proctableResult = make_rpc_request(m_libFluxRef, m_fluxHandle, m_leaderRank,
        m_rpcService + ".proctable", "{}");

        // Received proctable, parse response
        writeLog("proctable: %s\n", proctableResult.c_str());
        auto const proctable = parse_json(proctableResult);

        // Fill in hosts placement, PEs per node
        m_hostsPlacement = flux::make_hostsPlacement(proctable);

        // Sum up number of PEs
        m_numPEs = std::accumulate(
            m_hostsPlacement.begin(), m_hostsPlacement.end(), size_t{},
            [](size_t total, FluxFrontend::HostPlacement const& placement) {
                return total + placement.numPEs;
            });

        // Get list of binaries. As Flux does not support MPMD, this should only ever
        // be a single binary.
        auto binaryList = flux::flatten_prefixList(proctable.get_child("executables"));
        if (binaryList.size() != 1) {
            throw std::runtime_error("expected a single binary launched with Flux. Got " + std::to_string(binaryList.size()));
        }
        m_binaryName = std::move(binaryList[0]);

        writeLog("binary %s running with %zu ranks\n", m_binaryName.c_str(), m_numPEs);
    }

    // Flux generates job's tmpdir as <handle_rundir>/jobtmp-<shellrank>-<jobidf58>
    { auto const rundir = m_libFluxRef.flux_attr_get(m_fluxHandle, "rundir");
        if (rundir == nullptr) {
            throw std::runtime_error("Flux getattr failed");
        }

        // Encode job ID and build toolpath
        auto const jobIdF58 = encode_job_id(m_libFluxRef, m_jobId);
        m_toolPath = std::string{rundir} + "/jobtmp-" + std::to_string(m_leaderRank) + "-" + jobIdF58;
        writeLog("tmpdir: %s\n", m_toolPath.c_str());
    }

    { // Generate list of first ranks for each node that aren't rank 0 (broker)
        auto hostnameFirstRankMap = std::map<std::string, int>{};

        char const* flux_exec_hostname_argv[] = {"flux", "exec", "-l", "hostname", nullptr};
        auto hostnameOutput = cti::Execvp{"flux", (char* const*)flux_exec_hostname_argv,
            cti::Execvp::stderr::Ignore};
        auto& hostnameStream = hostnameOutput.stream();
        auto line = std::string{};
        while (std::getline(hostnameStream, line)) {
            auto&& [rank, hostname] = cti::split::string<2>(line, ':');
            auto hostnameFirstRank = hostnameFirstRankMap.find(hostname);

            if (hostnameFirstRank != hostnameFirstRankMap.end()) {
                hostnameFirstRank->second
                    = std::min(std::stoi(rank), hostnameFirstRank->second);
            } else {
                hostnameFirstRankMap.insert({hostname, std::stoi(rank)});
            }
        }

        auto nonBrokerRanks = std::set<int>{};
        for (auto&& [hostname, rank] : hostnameFirstRankMap) {
            if (rank != 0) {
                nonBrokerRanks.insert(rank);
            }
        }

        // TODO: make rank ranges
        for (auto&& rank : nonBrokerRanks) {
            if (m_nonBrokerRanks.empty()) {
                m_nonBrokerRanks = std::to_string(rank);
            } else {
                m_nonBrokerRanks += "," + std::to_string(rank);
            }
        }

        writeLog("Non-broker ranks: %s\n", m_nonBrokerRanks.c_str());
    }
}

// Flux does not yet support cray-pmi, so backend information needs to be generated separately
std::vector<std::pair<std::string, std::string>>
FluxApp::generateHostAttribs()
{
    auto result = std::vector<std::pair<std::string, std::string>>{};

    auto const cfgDir = m_frontend.getCfgDir();

    // Create attribs file for each hostname
    for (auto&& placement : m_hostsPlacement) {

        // Create placement directory
        auto placementDir = cfgDir + "/" + placement.hostname;
        { struct stat st;
            if (::stat(placementDir.c_str(), &st)) {
                if (::mkdir(placementDir.c_str(), 0700)) {
                    throw std::runtime_error("failed to create directory at " + placementDir);
                }
            }
        }

        // Open attribute file for writing
        auto const attribsPath = placementDir + "/pmi_attribs";
        auto attribs_file = cti::take_pointer_ownership(::fopen(attribsPath.c_str(), "w"), ::fclose);
        if (!attribs_file) {
            throw std::runtime_error("failed to create file at " + attribsPath);
        }

        // Write attribs information to file
        fprintf(attribs_file.get(), "%d\n%d\n%d\n%ld\n",
            1, // PMI version 1
            0, // Node ID disabled
            0, // Flux does not support MPMD
            placement.numPEs); // Ranks on node

        for (auto&& [rank, pid] : placement.rankPidPairs) {
            fprintf(attribs_file.get(), "%d %d\n", rank, pid);
        }

        // Add PMI file to list
        result.emplace_back(placement.hostname, attribsPath);
    }

    return result;
}

static void
cleanupHostAttribs(std::string const& cfgDir,
    std::vector<std::pair<std::string, std::string>> const& hostnameAttribsPairs)
{
    // Create attribs file for each hostname
    for (auto&& [hostname, attribsPath] : hostnameAttribsPairs) {

        // Remove placement file
        ::unlink(attribsPath.c_str());

        // Remove placement directory
        auto placementDir = cfgDir + "/" + hostname;
        ::rmdir(placementDir.c_str());
    }
}

void
FluxApp::shipDaemon()
{
    // Get the location of the backend daemon
    if (m_frontend.getBEDaemonPath().empty()) {
        throw std::runtime_error("Unable to locate backend daemon binary. Try setting " + std::string(CTI_BASE_DIR_ENV_VAR) + " environment variable to the install location of CTI.");
    }

    // Copy the BE binary to its unique storage name and ship
    auto const sourcePath = m_frontend.getBEDaemonPath();
    auto const destinationPath = m_frontend.getCfgDir() + "/" + getBEDaemonName();
    std::filesystem::copy_file(sourcePath, destinationPath,
    std::filesystem::copy_options::overwrite_existing);
    shipPackage(destinationPath);

    // Generate attribute files
    auto const hostAttribs = generateHostAttribs();

    // Ship attribute files
    for (auto&& [hostname, attribPath] : hostAttribs) {
        // pmi_attribs file will be expected in m_toolPath/pmi_attribs
        shipPackage(attribPath);
    }

    // Remove attribute files
    cleanupHostAttribs(m_frontend.getCfgDir(), hostAttribs);

    // set transfer to true
    m_beDaemonSent = true;
}

FluxApp::~FluxApp()
{
    if (!Frontend::isOriginalInstance()) {
        writeLog("~FluxApp: forked PID %d exiting without cleanup\n", getpid());
        return;
    }

    try {
        // Terminate utilities launched by CTI
        for (auto&& id : m_daemonJobIds) {
            (void)cancel_job(m_libFluxRef, m_fluxHandle, id, "controlling application is terminating");
        }

    } catch (std::exception const& ex) {
        writeLog("~FluxApp: %s\n", ex.what());
    }
}
