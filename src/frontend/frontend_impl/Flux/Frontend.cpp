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
#include <numeric>

#include <sstream>
#include <fstream>

#include "transfer/Manifest.hpp"

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

namespace pt = boost::property_tree;

/* helper functions */

[[ noreturn ]] static void unimplemented(std::string const& functionName)
{
    throw std::runtime_error("unimplemented: " + functionName);
}

// Leverage Flux's dry-run mode to generate jobspec for API
static std::string make_jobspec(char const* launcher_name, char const* const launcher_args[],
    std::string const& inputPath, std::string const& outputPath, std::string const& errorPath,
    std::string const& chdirPath, char const* const env_list[])
{
    // Build Flux dry run arguments
    auto fluxArgv = cti::ManagedArgv { launcher_name, "mini", "run", "--dry-run" };

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

static auto parseJson(std::string const& json)
{
    // Create stream from string source
    auto jsonSource = boost::iostreams::array_source{json.c_str(), json.length()};
    auto jsonStream = boost::iostreams::stream<boost::iostreams::array_source>{jsonSource};

    auto root = pt::ptree{};
    try {
        pt::read_json(jsonStream, root);
    } catch (pt::json_parser::json_parser_error const& parse_ex) {
        throw std::runtime_error("failed to parse JSON response: " + json);
    }

    return root;
}

// Convert numerical job ID to compact F58 encoding
static auto encode_job_id(FluxFrontend::LibFlux& libFluxRef, uint64_t job_id)
{
    char buf[64];
    if (libFluxRef.flux_job_id_encode(job_id, "f58", buf, sizeof(buf) - 1) < 0) {
        throw std::runtime_error("failed to encode Flux job id: " + std::string{strerror(errno)});
    }
    buf[sizeof(buf) - 1] = '\0';

    return std::string{buf};
}

struct Empty {};
struct Range
    { int64_t start, end;
};
struct RLE
    { int64_t value, count;
};
using RangeList = std::variant<Empty, Range, RLE>;

// Read next rangelist object and return new Range / RLE state
static auto parseRangeList(pt::ptree const& root, int64_t base)
{
    // Single element will be interpreted as a range of size 1
    if (!root.data().empty()) {
        return RangeList { RLE
            { .value = root.get_value<int64_t>()
            , .count = 1
        } };
    }

    // Multiple elements must be size 2 for range
    if (root.size() != 2) {
        throw std::runtime_error("Flux API rangelist must have size 2");
    }

    auto cursor = root.begin();

    // Add base offset to range start / RLE value
    auto const first = base + (cursor++)->second.get_value<int>();
    auto const second = (cursor++)->second.get_value<int>();

    // Negative first element indicates empty range
    if (first < 0) {
        return RangeList { Empty{} };
    }

    // Negative second element indicates run length encoding
    if (second < 0) {
        return RangeList { RLE
            { .value = first
            , .count = -second + 1
        } };

    // Otherwise, traditional range
    } else {
        return RangeList { Range
            { .start = first
            , .end = first + second
        } };
    }
}

static auto make_hostsPlacement(pt::ptree const& root)
{
    auto result = std::vector<CTIHost>{};

    /* Flux proctable format:
      prefix_rangelist: [ prefix_string, [ rangelist, ... ] ]
      "hosts": [ prefix_rangelist, ... ]
      "executables": [ prefix_rangelist, ... ]
      "ids": [ rangelist, ... ]
      "pids": [ rangelist, ... ]

      Example: running 2 ranks of a.out on node15, with PIDs 7991 and 7992
        { "hosts": [[ "node", [[15,-1]] ]]
        , "executables": [[ "/path/to/a.out", [[-1,-1]] ]]
        , "ids": [[0,1]]
        , "pids": [[7991,1]]
        }
    */

    /* "hosts" contains a prefix rangelist that expands to one instance of each
       hostname for every PE on that host.
       The rangelists [ 1, 3 ], [ 5, -1 ] will be parsed as the following:
       - Range of ints 1 to 3 inclusive
       - RLE with value 3 + 5 = 8 of length -(-1) + 1 = 2
       - Values 1, 2, 3, 8, 8
       The prefix rangelist data [ "node", [ [1,3], [5,-1] ] ] will then be
       computed as node1, node2, node3, node8, node8.
       Finally, nodes 1 through 3 will have 1 PE each, and node 8 will have 2
    */

    auto hostPECount = std::map<std::string, size_t>{};

    // { "hosts": { "": [ prefix_string, { "": [ rangelist, ... ], ... } ] } }
    for (auto&& prefixListArrayPair : root.get_child("hosts")) {

        // Next rangelist object's starting value is based on the ending value
        // of the previous range
        auto base = int64_t{};

        // { "": [ prefix_string, { "": [ rangelist, ... ], ... } ] }
        auto cursor = prefixListArrayPair.second.begin();
        auto const prefix = (cursor++)->second.get_value<std::string>();
        auto const rangeListObjectArray = (cursor++)->second;

        // { "": [ rangelist, ... ], ... }
        for (auto&& rangeListObjectPair : rangeListObjectArray) {

            // Parse inner rangelist object as either range or RLE
            auto const rangeList = parseRangeList(rangeListObjectPair.second, base);

            // Empty: there is a single hostname consisting solely of the prefix
            if (std::holds_alternative<Empty>(rangeList)) {
                hostPECount[prefix]++;

            // Range: increment PE count for every hostname constructed with prefix
            } else if (std::holds_alternative<Range>(rangeList)) {

                auto const [start, end] = std::get<Range>(rangeList);
                for (auto i = start; i <= end; i++) {
                     auto const hostname = prefix + std::to_string(i);
                     hostPECount[hostname]++;
                }

                base = end;

            // RLE: add run length to host's PE count
            } else if (std::holds_alternative<RLE>(rangeList)) {

                auto const [value, count] = std::get<RLE>(rangeList);
                auto const hostname = prefix + std::to_string(value);
                hostPECount[hostname] += count;

                base = value;
            }
        }
    }

    // Construct placement vector from count map
    for (auto&& [hostname, pe_count] : hostPECount) {
        result.emplace_back(CTIHost
            { .hostname = hostname
            , .numPEs =  pe_count
        });
    }

    return result;
}

// Parse executables rangelist into vector of strings
static auto make_binaryList(pt::ptree const& root)
{
    auto result = std::vector<std::string>{};

    /* Flux proctable format:
      prefix_rangelist: [ prefix_string, [ rangelist, ... ] ]
      "hosts": [ prefix_rangelist, ... ]
      "executables": [ prefix_rangelist, ... ]
      "ids": [ rangelist, ... ]
      "pids": [ rangelist, ... ]

      Example: running 2 ranks of a.out on node15, with PIDs 7991 and 7992
        { "hosts": [[ "node", [[15,-1]] ]]
        , "executables": [[ "/path/to/a.out", [[-1,-1]] ]]
        , "ids": [[0,1]]
        , "pids": [[7991,1]]
        }
    */

    // { "executables": { "": [ prefix_string, { "": [ rangelist, ... ], ... } ] } }
    for (auto&& prefixListArrayPair : root.get_child("executables")) {

        // Next rangelist object's starting value is based on the ending value
        // of the previous range
        auto base = int64_t{};

        // { "": [ prefix_string, { "": [ rangelist, ... ], ... } ] }
        auto cursor = prefixListArrayPair.second.begin();
        auto const prefix = (cursor++)->second.get_value<std::string>();
        auto const rangeListObjectArray = (cursor++)->second;

        // { "": [ rangelist, ... ], ... }
        for (auto&& rangeListObjectPair : rangeListObjectArray) {

            // Parse inner rangelist object as either range or RLE
            auto const rangeList = parseRangeList(rangeListObjectPair.second, base);

            // Empty: there is a single executable consisting solely of the prefix
            if (std::holds_alternative<Empty>(rangeList)) {
                 result.emplace_back(prefix);

            // Range: add executable name for every name constructed with prefix
            } else if (std::holds_alternative<Range>(rangeList)) {

                auto const [start, end] = std::get<Range>(rangeList);
                for (auto i = start; i <= end; i++) {
                     result.emplace_back(prefix + std::to_string(i));
                }

                base = end;

            // RLE: add run length value to form executable
            } else if (std::holds_alternative<RLE>(rangeList)) {

                auto const [value, count] = std::get<RLE>(rangeList);
                for (int64_t i = 0; i < count; i++) {
                    result.emplace_back(prefix + std::to_string(value));
                }

                base = value;
            }
        }
    }

    return result;
}

/* FluxFrontend implementation */

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
    // Get output, and error files from file descriptors
    auto const outputPath = (stdout_fd >= 0)
        ? "/proc/" + std::to_string(getpid()) + "/fd/" + std::to_string(stdout_fd)
        : std::string{};
    auto const errorPath = (stderr_fd >= 0)
        ? "/proc/" + std::to_string(getpid()) + "/fd/" + std::to_string(stderr_fd)
        : std::string{};

    // Generate jobspec string
    auto const jobspec = make_jobspec(getLauncherName().c_str(), launcher_args,
        (input_file != nullptr) ? input_file : "",
        outputPath, errorPath,
        (chdir_path != nullptr) ? chdir_path : "",
        env_list);

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
    static const auto jobIdF58 = encode_job_id(m_libFluxRef, m_jobId);
    return jobIdF58;
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
    , m_fluxHandle{fe.m_fluxHandle}
    , m_libFluxRef{*fe.m_libFlux}
    , m_jobId{job_id}

    , m_leaderRank{}
    , m_rpcService{}

    , m_beDaemonSent{false}
    , m_numPEs{}
    , m_hostsPlacement{}
    , m_binaryName{}

    , m_toolPath{}
    , m_attribsPath{} // BE daemon looks for <m_attribsPath>/pmi_attribs
    , m_stagePath{cti::cstr::mkdtemp(std::string{m_frontend.getCfgDir() + "/fluxXXXXXX"})}
    , m_extraFiles{}

    , m_atBarrier{}
{
    // Query event log for job leader rank and service key for RPC
    auto eventlog_future = make_unique_del(
        m_libFluxRef.flux_job_event_watch(m_fluxHandle, m_jobId, "guest.exec.eventlog", 0),
        m_libFluxRef.flux_future_destroy);
    if (eventlog_future == nullptr) {
        throw std::runtime_error("Flux job event query failed");
    }

    // Read event log responses
    while (true) {
        char const *eventlog_result = nullptr;
        auto const eventlog_rc = m_libFluxRef.flux_job_event_watch_get(eventlog_future.get(), &eventlog_result);
        if (eventlog_rc == ENODATA) {
            break;
        } else if (eventlog_rc) {
            auto const flux_error = m_libFluxRef.flux_future_error_string(eventlog_future.get());
            throw std::runtime_error("Flux job event query failed: " + std::string{(flux_error)
                ? flux_error
                : "(no error provided)"});
        }

        // Received a new event log result, parse it as JSON
        writeLog("eventlog: %s\n", eventlog_result);
        auto root = parseJson(eventlog_result);

        // Looking for shell.init event, will contain leader rank and service key
        if (root.get<std::string>("name") != "shell.init") {

            // Reset and wait for next event log result
            m_libFluxRef.flux_future_reset(eventlog_future.get());
            continue;
        }

        // Got shell.init, extract the job information
        auto context = root.get_child("context");
        m_leaderRank = context.get<int>("leader-rank");
        m_rpcService = context.get<std::string>("service");

        writeLog("extracted job info: leader rank %d, service key %s\n", m_leaderRank, m_rpcService.c_str());

        break;
    }

    // Start new proctable query
    { auto topic = m_rpcService + ".proctable";
        auto proctable_future = make_unique_del(
            m_libFluxRef.flux_rpc_raw(m_fluxHandle, topic.c_str(), "{}", 2, m_leaderRank, 0),
            m_libFluxRef.flux_future_destroy);
        if (proctable_future == nullptr) {
            throw std::runtime_error("Flux proctable query failed");
        }

        // Block and read until RPC returns proctable response
        char const *proctable_result = nullptr;
        auto const proctable_rc = m_libFluxRef.flux_rpc_get(proctable_future.get(), &proctable_result);
        if (proctable_rc) {
            auto const flux_error = m_libFluxRef.flux_future_error_string(proctable_future.get());
            throw std::runtime_error("Flux proctable query failed: " + std::string{(flux_error)
                ? flux_error
                : "(no error provided)"});
        }

        // Received proctable, parse response
        writeLog("proctable: %s\n", proctable_result);
        auto const proctable = parseJson(proctable_result);

        // Fill in hosts placement, PEs per node
        m_hostsPlacement = make_hostsPlacement(proctable);

        // Sum up number of PEs
        m_numPEs = std::accumulate(
            m_hostsPlacement.begin(), m_hostsPlacement.end(), size_t{},
            [](size_t total, CTIHost const& ctiHost) { return total + ctiHost.numPEs; });

        // Get list of binaries. As Flux does not support MPMD, this should only ever
        // be a single binary.
        auto binaryList = make_binaryList(proctable);
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

        // TODO: determine if Flux's PMI implementation provides pmi_attribs
    }

    unimplemented(__func__);
}

FluxApp::~FluxApp()
{}
