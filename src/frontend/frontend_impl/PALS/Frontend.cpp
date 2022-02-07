/******************************************************************************\
 * Frontend.cpp - PALS specific frontend library functions.
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
#include <algorithm>

#include <sstream>
#include <fstream>

#include "transfer/Manifest.hpp"

#include "PALS/Frontend.hpp"

#include "useful/cti_hostname.hpp"
#include "useful/cti_split.hpp"
#include "frontend/mpir_iface/Inferior.hpp"

std::string
HPCMPALSFrontend::getApid(pid_t launcher_pid)
{
    // MPIR attach to launcher
    auto const mpirData = Daemon().request_AttachMPIR(
        // Get path to launcher binary
        cti::take_pointer_ownership(
            _cti_pathFind(getLauncherName().c_str(), nullptr),
            std::free).get(),
        // Attach to existing launcher PID
        launcher_pid);

    // Extract apid string from launcher
    auto result = Daemon().request_ReadStringMPIR(mpirData.mpir_id, "totalview_jobid");

    // Release MPIR control
    Daemon().request_ReleaseMPIR(mpirData.mpir_id);

    return result;
}

HPCMPALSFrontend::PalsLaunchInfo
HPCMPALSFrontend::attachApp(std::string const& apId)
{
    // Create daemon ID for new application
    auto result = PalsLaunchInfo
        { .daemonAppId = Daemon().request_RegisterApp()
        , .apId = apId
        , .procTable = {}
        , .binaryRankMap = {}
        , .atBarrier = false
    };

    // Launch palstat MPIR query
    char const* palstat_argv[] = { "palstat", "-p", apId.c_str(), nullptr };
    auto palstatOutput = cti::Execvp{"palstat", (char* const*)palstat_argv, cti::Execvp::stderr::Ignore};

    // Parse MPIR output from palstat
    auto& palstatStream = palstatOutput.stream();
    auto line = std::string{};

    // Ignore header
    std::getline(palstatStream, line);

    writeLog("palstat MPIR entries:\n");

    // An empty line will terminate the loop
    while (std::getline(palstatStream, line)) {

        // <HOST> <EXECUTABLE> <PID>
        auto elem = MPIRProctableElem{};
        { auto ss = std::stringstream{line};
            auto rawPid = std::string{};
            ss >> std::skipws >> elem.hostname >> elem.executable >> rawPid;
            elem.pid = std::stoi(rawPid);
        }

        writeLog("%d %s %s\n", elem.pid, elem.hostname.c_str(), elem.executable.c_str());
        result.procTable.push_back(std::move(elem));
    }

    // Build binary-rank map
    auto rank = size_t{0};
    for (auto&& [pid, host, executable] : result.procTable) {
        result.binaryRankMap[executable].push_back(rank++);
    }

    return result;
}

HPCMPALSFrontend::PalsLaunchInfo
HPCMPALSFrontend::launchApp(const char * const launcher_argv[],
        int stdoutFd, int stderrFd, const char *inputFile, const char *chdirPath, const char * const env_list[])
{
    // Get the launcher path from CTI environment variable / default.
    if (auto const launcher_path = cti::take_pointer_ownership(_cti_pathFind(getLauncherName().c_str(), nullptr), std::free)) {
        // set up arguments and FDs
        if (inputFile == nullptr) { inputFile = "/dev/null"; }
        if (stdoutFd < 0) { stdoutFd = STDOUT_FILENO; }
        if (stderrFd < 0) { stderrFd = STDERR_FILENO; }

        // construct argv array & instance
        cti::ManagedArgv launcherArgv
            { launcher_path.get()
        };

        // Copy provided launcher arguments
        launcherArgv.add(launcher_argv);

        // Launch program under MPIR control.
        auto mpirData = Daemon().request_LaunchMPIR(
            launcher_path.get(), launcherArgv.get(),
            ::open(inputFile, O_RDONLY), stdoutFd, stderrFd,
            env_list);

        // Get application ID from launcher
        auto apid = Daemon().request_ReadStringMPIR(mpirData.mpir_id,
            "totalview_jobid");

        // Construct launch info struct
        return PalsLaunchInfo
            { .daemonAppId = mpirData.mpir_id
            , .apId = std::move(apid)
            , .procTable = std::move(mpirData.proctable)
            , .binaryRankMap = std::move(mpirData.binaryRankMap)
            , .atBarrier = true
        };

    } else {
        throw std::runtime_error("Failed to find launcher in path: " + getLauncherName());
    }
}

// Add the launcher's timeout environment variable to provided environment list
// Set timeout to five minutes
static inline auto setTimeoutEnvironment(std::string const& launcherName, CArgArray env_list)
{
    // Determine the timeout environment variable for PALS `mpiexec` or PALS `aprun` command
    // https://connect.us.cray.com/jira/browse/PE-34329
    auto const timeout_env = (launcherName == "aprun")
        ? "APRUN_RPC_TIMEOUT=300"
        : "PALS_RPC_TIMEOUT=300";

    // Add the launcher's timeout disable environment variable to a new environment list
    auto fixedEnvVars = cti::ManagedArgv{};

    // Copy provided environment list
    if (env_list != nullptr) {
        fixedEnvVars.add(env_list);
    }

    // Append timeout disable environment variable
    fixedEnvVars.add(timeout_env);

    return fixedEnvVars;
}

std::weak_ptr<App>
HPCMPALSFrontend::launch(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
    CStr inputFile, CStr chdirPath, CArgArray env_list)
{
    auto fixedEnvVars = setTimeoutEnvironment(getLauncherName(), env_list);

    auto appPtr = std::make_shared<HPCMPALSApp>(*this,
        launchApp(launcher_argv, stdout_fd, stderr_fd, inputFile, chdirPath, fixedEnvVars.get()));

    // Release barrier and continue launch
    appPtr->releaseBarrier();

    // Register with frontend application set
    auto resultInsertedPair = m_apps.emplace(std::move(appPtr));
    if (!resultInsertedPair.second) {
        throw std::runtime_error("Failed to insert new App object.");
    }

    return *resultInsertedPair.first;
}

std::weak_ptr<App>
HPCMPALSFrontend::launchBarrier(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
        CStr inputFile, CStr chdirPath, CArgArray env_list)
{
    auto fixedEnvVars = setTimeoutEnvironment(getLauncherName(), env_list);

    auto ret = m_apps.emplace(std::make_shared<HPCMPALSApp>(*this,
        launchApp(launcher_argv, stdout_fd, stderr_fd, inputFile, chdirPath, fixedEnvVars.get())));
    if (!ret.second) {
        throw std::runtime_error("Failed to create new App object.");
    }

    return *ret.first;
}

std::weak_ptr<App>
HPCMPALSFrontend::registerJob(size_t numIds, ...)
{
    if (numIds != 1) {
        throw std::logic_error("expecting single apid argument to register app");
    }

    va_list idArgs;
    va_start(idArgs, numIds);

    char const* apid = va_arg(idArgs, char const*);

    va_end(idArgs);

    auto ret = m_apps.emplace(std::make_shared<HPCMPALSApp>(*this,
        attachApp(apid)));
    if (!ret.second) {
        throw std::runtime_error("Failed to create new App object.");
    }

    return *ret.first;
}

// Current address can now be obtained using the `cminfo` tool.
std::string
HPCMPALSFrontend::getHostname() const
{
    static auto const nodeAddress = []() {

        // Run cminfo query
        auto const cminfo_query = [](char const* option) {
            char const* cminfoArgv[] = { "cminfo", option, nullptr };

            // Start cminfo
            try {
                auto cminfoOutput = cti::Execvp{"cminfo", (char* const*)cminfoArgv, cti::Execvp::stderr::Ignore};

                // Return first line of query
                auto& cminfoStream = cminfoOutput.stream();
                std::string line;
                if (std::getline(cminfoStream, line)) {
                    return line;
                }
            } catch (...) {
                return std::string{};
            }

            return std::string{};
        };

        // Get name of management network
        auto const managementNetwork = cminfo_query("--mgmt_net_name");
        if (!managementNetwork.empty()) {

            // Query management IP address
            auto const addressOption = "--" + managementNetwork + "_ip";
            auto const managementAddress = cminfo_query(addressOption.c_str());
            if (!managementAddress.empty()) {
                return managementAddress;
            }
        }

        // Delegate to shared implementation supporting both XC and Shasta
        return cti::detectFrontendHostname();
    }();

    return nodeAddress;
}

std::string
HPCMPALSFrontend::getLauncherName()
{
    // Cache the launcher name result. Assume mpiexec by default.
    auto static launcherName = std::string{cti::getenvOrDefault(CTI_LAUNCHER_NAME_ENV_VAR, "mpiexec")};
    return launcherName;
}

HPCMPALSApp::HPCMPALSApp(HPCMPALSFrontend& fe, HPCMPALSFrontend::PalsLaunchInfo&& palsLaunchInfo)
    : App{fe}
    , m_daemonAppId{palsLaunchInfo.daemonAppId}
    , m_apId{std::move(palsLaunchInfo.apId)}

    , m_beDaemonSent{false}
    , m_procTable{std::move(palsLaunchInfo.procTable)}
    , m_binaryRankMap{std::move(palsLaunchInfo.binaryRankMap)}

    , m_apinfoPath{"/var/run/palsd/" + m_apId + "/apinfo"}
    , m_toolPath{"/tmp/cti-" + m_apId}
    , m_attribsPath{"/var/run/palsd/" + m_apId} // BE daemon looks for <m_attribsPath>/pmi_attribs
    , m_stagePath{cti::cstr::mkdtemp(std::string{m_frontend.getCfgDir() + "/palsXXXXXX"})}
    , m_extraFiles{}

    , m_atBarrier{palsLaunchInfo.atBarrier}
{
    // Get set of hosts for application
    for (auto&& [pid, hostname, executable] : m_procTable) {
        m_hosts.emplace(hostname);
    }

    // Create remote toolpath directory
    { auto palscmdArgv = cti::ManagedArgv { "palscmd", m_apId,
            "mkdir", "-p", m_toolPath };

        if (!m_frontend.Daemon().request_ForkExecvpUtil_Sync(
            m_daemonAppId, "palscmd", palscmdArgv.get(),
            -1, -1, -1,
            nullptr)) {
            throw std::runtime_error("failed to create remote toolpath directory for apid " + m_apId);
        }
    }
}

HPCMPALSApp::~HPCMPALSApp()
{
    // Remove remote toolpath directory
    { auto palscmdArgv = cti::ManagedArgv { "palscmd", m_apId,
            "rm", "-rf", m_toolPath };

        // Ignore failures in destructor
        m_frontend.Daemon().request_ForkExecvpUtil_Sync(
            m_daemonAppId, "palscmd", palscmdArgv.get(),
            -1, -1, -1,
            nullptr);
    }
}

std::string
HPCMPALSApp::getLauncherHostname() const
{
    throw std::runtime_error{"not supported for PALS: " + std::string{__func__}};
}

bool
HPCMPALSApp::isRunning() const
{
    auto palstatArgv = cti::ManagedArgv{"palstat", m_apId};
    return m_frontend.Daemon().request_ForkExecvpUtil_Sync(
        m_daemonAppId, "palstat", palstatArgv.get(),
        -1, -1, -1,
        nullptr);
}

size_t
HPCMPALSApp::getNumPEs() const
{
    return m_procTable.size();
}

size_t
HPCMPALSApp::getNumHosts() const
{
    return m_hosts.size();
}

std::vector<std::string>
HPCMPALSApp::getHostnameList() const
{
    // Make vector from set
    auto result = std::vector<std::string>{};
    result.reserve(m_hosts.size());
    for (auto&& hostname : m_hosts) {
        result.emplace_back(hostname);
    }

    return result;
}

std::vector<CTIHost>
HPCMPALSApp::getHostsPlacement() const
{
    // Count PEs for each host
    auto hostnameCountMap = std::map<std::string, size_t>{};
    for (auto&& [pid, hostname, executable] : m_procTable) {
        hostnameCountMap[hostname]++;
    }

    // Make vector from map
    auto result = std::vector<CTIHost>{};
    for (auto&& [hostname, count] : hostnameCountMap) {
        result.emplace_back(CTIHost{std::move(hostname), count});
    }

    return result;
}

std::map<std::string, std::vector<int>>
HPCMPALSApp::getBinaryRankMap() const
{
    return m_binaryRankMap;
}

void
HPCMPALSApp::releaseBarrier()
{
    if (!m_atBarrier) {
        throw std::runtime_error("application is not at startup barrier");
    }

    m_frontend.Daemon().request_ReleaseMPIR(m_daemonAppId);
    m_atBarrier = false;
}

void
HPCMPALSApp::shipPackage(std::string const& tarPath) const
{
    auto const destinationName = cti::cstr::basename(tarPath);

    auto palscpArgv = cti::ManagedArgv{"palscp", "-f", tarPath, "-d", destinationName, m_apId};

    if (!m_frontend.Daemon().request_ForkExecvpUtil_Sync(
        m_daemonAppId, "palscp", palscpArgv.get(),
        -1, -1, -1,
        nullptr)) {
        throw std::runtime_error("failed to ship " + tarPath + " using palscp");
    }

    // Move shipped file from noexec filesystem to toolpath directory
    auto const palscpDestination = "/var/run/palsd/" + m_apId + "/files/" + destinationName;
    auto palscmdArgv = cti::ManagedArgv { "palscmd", m_apId,
            "mv", palscpDestination, m_toolPath };

    if (!m_frontend.Daemon().request_ForkExecvpUtil_Sync(
        m_daemonAppId, "palscmd", palscmdArgv.get(),
        -1, -1, -1,
        nullptr)) {
        throw std::runtime_error("failed to move shipped package for apid " + m_apId);
    }
}

void
HPCMPALSApp::startDaemon(const char* const args[])
{
    // sanity check
    if (args == nullptr) {
        throw std::runtime_error("args array is empty!");
    }

    // Send daemon if not already shipped
    if (!m_beDaemonSent) {
        // Get the location of the backend daemon
        if (m_frontend.getBEDaemonPath().empty()) {
            throw std::runtime_error("Unable to locate backend daemon binary. Try setting " + std::string(CTI_BASE_DIR_ENV_VAR) + " environment variable to the install location of CTI.");
        }

        // Copy the BE binary to its unique storage name
        auto const sourcePath = m_frontend.getBEDaemonPath();
        auto const destinationPath = m_frontend.getCfgDir() + "/" + getBEDaemonName();

        // Create the args for copy
        auto copyArgv = cti::ManagedArgv {
            "cp", sourcePath.c_str(), destinationPath.c_str()
        };

        // Run copy command
        if (!m_frontend.Daemon().request_ForkExecvpUtil_Sync(
            m_daemonAppId, "cp", copyArgv.get(),
            -1, -1, -1,
            nullptr)) {
            throw std::runtime_error("failed to copy " + sourcePath + " to " + destinationPath);
        }

        // Ship the unique backend daemon
        shipPackage(destinationPath);
        // set transfer to true
        m_beDaemonSent = true;
    }

    // Create the arguments for palscmd
    auto palscmdArgv = cti::ManagedArgv { "palscmd", m_apId };

    // Use location of existing launcher binary on compute node
    auto const launcherPath = m_toolPath + "/" + getBEDaemonName();
    palscmdArgv.add(launcherPath);

    // Copy provided launcher arguments
    palscmdArgv.add(args);

    // tell frontend daemon to launch palscmd, wait for it to finish
    if (!m_frontend.Daemon().request_ForkExecvpUtil_Sync(
        m_daemonAppId, "palscmd", palscmdArgv.get(),
        -1, -1, -1,
        nullptr)) {
        throw std::runtime_error("failed to launch tool daemon for apid " + m_apId);
    }
}

void HPCMPALSApp::kill(int signum)
{
    // create the args for palsig
    auto palsigArgv = cti::ManagedArgv { "palsig", "-s", std::to_string(signum),
        m_apId };

    // tell frontend daemon to launch palsig, wait for it to finish
    if (!m_frontend.Daemon().request_ForkExecvpUtil_Sync(
        m_daemonAppId, "palsig", palsigArgv.get(),
        -1, -1, -1,
        nullptr)) {
        throw std::runtime_error("failed to send signal to apid " + m_apId);
    }
}
