/******************************************************************************\
 * Frontend.cpp -  Frontend library functions for SSH based workload manager.
 *
 * Copyright 2017-2022 Hewlett Packard Enterprise Development LP.
 * SPDX-License-Identifier: Linux-OpenIB
 ******************************************************************************/

// This pulls in config.h
#include "cti_defs.h"
#include "cti_argv_defs.hpp"

#include <unordered_map>
#include <thread>
#include <future>
#include <algorithm>

// Pull in manifest to properly define all the forward declarations
#include "transfer/Manifest.hpp"

#include "GenericSSH/Frontend.hpp"

#include "daemon/cti_fe_daemon_iface.hpp"
#include "SSHSession/SSHSession.hpp"


GenericSSHApp::GenericSSHApp(GenericSSHFrontend& fe, FE_daemon::MPIRResult&& mpirData)
    : App(fe, mpirData.mpir_id)
    , m_username { fe.m_username }
    , m_homeDir { fe.m_homeDir }
    , m_launcherPid { mpirData.launcher_pid }
    , m_binaryRankMap { std::move(mpirData.binaryRankMap) }
    , m_stepLayout  { fe.fetchStepLayout(mpirData.proctable) }
    , m_beDaemonSent { false }
    , m_toolPath    { SSH_TOOL_DIR }
    , m_attribsPath { SSH_TOOL_DIR }
    , m_stagePath   { cti::cstr::mkdtemp(std::string{fe.getCfgDir() + "/" + SSH_STAGE_DIR}) }
    , m_extraFiles  { fe.createNodeLayoutFile(m_stepLayout, m_stagePath) }
{
    // Ensure there are running nodes in the job.
    if (m_stepLayout.nodes.empty()) {
        throw std::runtime_error("Application " + getJobId() + " does not have any nodes.");
    }

    // Ensure application has been registered with daemon
    if (!m_daemonAppId) {
        throw std::runtime_error("tried to create app with invalid daemon id: " + std::to_string(m_daemonAppId));
    }

    // If an active MPIR session was provided, extract the MPIR ProcTable and write the PID List File.
    m_extraFiles.push_back(fe.createPIDListFile(mpirData.proctable, m_stagePath));
}

GenericSSHApp::~GenericSSHApp()
{
    if (!Frontend::isOriginalInstance()) {
        writeLog("~GenericSSHApp: forked PID %d exiting without cleanup\n", getpid());
        return;
    }

    try {
        // Delete the staging directory if it exists.
        if (!m_stagePath.empty()) {
            _cti_removeDirectory(m_stagePath.c_str());
        }

        // Inform the FE daemon that this App is going away
        m_frontend.Daemon().request_DeregisterApp(m_daemonAppId);
    } catch (std::exception const& ex) {
        writeLog("~GenericSSHApp: %s\n", ex.what());
    }
}

/* running app info accessors */

std::string
GenericSSHApp::getJobId() const
{
    return std::to_string(m_launcherPid);
}

std::string
GenericSSHApp::getLauncherHostname() const
{
    throw std::runtime_error("not supported for WLM: getLauncherHostname");
}

bool
GenericSSHApp::isRunning() const
{
    return m_frontend.Daemon().request_CheckApp(m_daemonAppId);
}

std::vector<std::string>
GenericSSHApp::getHostnameList() const
{
    std::vector<std::string> result;
    // extract hostnames from each NodeLayout
    std::transform(m_stepLayout.nodes.begin(), m_stepLayout.nodes.end(), std::back_inserter(result),
        [](GenericSSHFrontend::NodeLayout const& node) { return node.hostname; });
    return result;
}

std::map<std::string, std::vector<int>>
GenericSSHApp::getBinaryRankMap() const
{
    return m_binaryRankMap;
}

std::vector<CTIHost>
GenericSSHApp::getHostsPlacement() const
{
    std::vector<CTIHost> result;
    // construct a CTIHost from each NodeLayout
    std::transform(m_stepLayout.nodes.begin(), m_stepLayout.nodes.end(), std::back_inserter(result),
        [](GenericSSHFrontend::NodeLayout const& node) {
            return CTIHost{node.hostname, node.pids.size()};
        });
    return result;
}

void
GenericSSHApp::releaseBarrier()
{
    // release MPIR barrier
    m_frontend.Daemon().request_ReleaseMPIR(m_daemonAppId);
}

void
GenericSSHApp::kill(int signal)
{
    // Connect through ssh to each node and send a kill command to every pid on that node
    for (auto&& node : m_stepLayout.nodes) {
        // kill -<sig> <pid> ... <pid>
        cti::ManagedArgv killArgv
            { "kill"
            , "-" + std::to_string(signal)
        };
        for (auto&& pid : node.pids) {
            killArgv.add(std::to_string(pid));
        }

        // run remote kill command
        SSHSession(node.hostname, m_username, m_homeDir).executeRemoteCommand(killArgv.get(), nullptr,
            /* synchronous */ true);
    }
}

void
GenericSSHApp::shipPackage(std::string const& tarPath) const
{
    auto packageName = cti::cstr::basename(tarPath);
    auto const destination = std::string{SSH_TOOL_DIR} + "/" + packageName;
    writeLog("GenericSSH shipping %s to '%s'\n", tarPath.c_str(), destination.c_str());

    // Send the package to each of the hosts using SCP
    for (auto&& node : m_stepLayout.nodes) {
        SSHSession(node.hostname, m_username, m_homeDir).sendRemoteFile(tarPath.c_str(), destination.c_str(), S_IRWXU | S_IRWXG | S_IRWXO);
    }
}

void
GenericSSHApp::startDaemon(const char* const args[], bool synchronous)
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

        // Create the args for link
        auto linkArgv = cti::ManagedArgv {
            "ln", "-s", sourcePath.c_str(), destinationPath.c_str()
        };

        // Run link command
        if (!m_frontend.Daemon().request_ForkExecvpUtil_Sync(
            m_daemonAppId, "ln", linkArgv.get(),
            -1, -1, -1,
            nullptr)) {
            throw std::runtime_error("failed to link " + sourcePath + " to " + destinationPath);
        }

        // Ship the unique backend daemon
        shipPackage(destinationPath);
        // set transfer to true
        m_beDaemonSent = true;
    }

    // Use location of existing launcher binary on compute node
    std::string const launcherPath{m_toolPath + "/" + getBEDaemonName()};

    // Prepare the launcher arguments
    cti::ManagedArgv launcherArgv { launcherPath };

    // Copy provided launcher arguments
    launcherArgv.add(args);

    // Execute the launcher on each of the hosts using SSH
    auto executeRemoteCommand = [](std::string const& hostname, std::string const& username,
        std::string const& homeDir, char const* const* argv, bool synchronous) {
        auto session = SSHSession{hostname, username, homeDir};
        session.executeRemoteCommand(argv, nullptr, synchronous);
    };

    if (synchronous) {

        // Synchronous launches run in parallel as future tasks
        auto launchFutures = std::vector<std::future<void>>{};
        for (auto&& node : m_stepLayout.nodes) {
            launchFutures.push_back(std::async(std::launch::async, executeRemoteCommand,
                node.hostname, m_username, m_homeDir, launcherArgv.get(),
                /* synchronous */ true));
        }
        for (auto&& future : launchFutures) {
            future.get();
        }

    } else {

        // Asynchronous launches can be started in parallel and continued to run
        for (auto&& node : m_stepLayout.nodes) {
            executeRemoteCommand(node.hostname, m_username, m_homeDir, launcherArgv.get(),
                /* asynchronous */ false);
        }
    }
}

/* SSH frontend implementation */

GenericSSHFrontend::GenericSSHFrontend()
    : m_username{getPwd().pw_name}
    , m_homeDir{getPwd().pw_dir}
{
    // Initialize the libssh2 library. Note that this isn't threadsafe.
    // FIXME: Address this.
    if ( libssh2_init(0) ) {
        throw std::runtime_error("Failed to initailize libssh2");
    }
}

GenericSSHFrontend::~GenericSSHFrontend()
{
    // Deinit the libssh2 library.
    libssh2_exit();
}

std::weak_ptr<App>
GenericSSHFrontend::launch(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
    CStr inputFile, CStr chdirPath, CArgArray env_list)
{
    auto appPtr = std::make_shared<GenericSSHApp>(*this,
        launchApp(launcher_argv, stdout_fd, stderr_fd, inputFile, chdirPath, env_list));

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
GenericSSHFrontend::launchBarrier(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
        CStr inputFile, CStr chdirPath, CArgArray env_list)
{
    auto ret = m_apps.emplace(std::make_shared<GenericSSHApp>(*this,
        launchApp(launcher_argv, stdout_fd, stderr_fd, inputFile, chdirPath, env_list)));
    if (!ret.second) {
        throw std::runtime_error("Failed to create new App object.");
    }
    return *ret.first;
}

std::weak_ptr<App>
GenericSSHFrontend::registerJob(size_t numIds, ...)
{
    if (numIds != 1) {
        throw std::logic_error("expecting single pid argument to register app");
    }

    va_list idArgs;
    va_start(idArgs, numIds);

    pid_t launcherPid = va_arg(idArgs, pid_t);

    va_end(idArgs);

    auto ret = m_apps.emplace(std::make_shared<GenericSSHApp>(*this,
        // MPIR attach to launcher
        Daemon().request_AttachMPIR(
            // Get path to launcher binary
            cti::take_pointer_ownership(
                _cti_pathFind(getLauncherName().c_str(), nullptr),
                std::free).get(),
            // Attach to existing launcherPid
            launcherPid)));
    if (!ret.second) {
        throw std::runtime_error("Failed to create new App object.");
    }
    return *ret.first;
}

std::string
GenericSSHFrontend::getHostname() const
{
    return cti::cstr::gethostname();
}

/* SSH frontend implementations */

std::string
GenericSSHFrontend::getLauncherName()
{
    // Cache the launcher name result. Assume mpiexec by default.
    auto static launcherName = std::string{cti::getenvOrDefault(CTI_LAUNCHER_NAME_ENV_VAR, "mpiexec")};
    return launcherName;
}

GenericSSHFrontend::StepLayout
GenericSSHFrontend::fetchStepLayout(MPIRProctable const& procTable)
{
    StepLayout layout;
    layout.numPEs = procTable.size();

    size_t nodeCount = 0;
    size_t peCount   = 0;

    std::unordered_map<std::string, size_t> hostNidMap;

    // For each new host we see, add a host entry to the end of the layout's host list
    // and hash each hostname to its index into the host list
    for (auto&& proc : procTable) {

        // Truncate hostname at first '.' in case the launcher has used FQDNs for hostnames
        auto const base_hostname = proc.hostname.substr(0, proc.hostname.find("."));

        size_t nid;
        auto const hostNidPair = hostNidMap.find(base_hostname);
        if (hostNidPair == hostNidMap.end()) {
            // New host, extend nodes array, and fill in host entry information
            nid = nodeCount++;
            layout.nodes.push_back(NodeLayout
                { .hostname = base_hostname
                , .pids = {}
                , .firstPE = peCount
            });
            hostNidMap[base_hostname] = nid;
        } else {
            nid = hostNidPair->second;
        }

        // add new pe to end of host's list
        layout.nodes[nid].pids.push_back(proc.pid);

        peCount++;
    }

    return layout;
}

std::string
GenericSSHFrontend::createNodeLayoutFile(GenericSSHFrontend::StepLayout const& stepLayout, std::string const& stagePath)
{
    // How a SSH Node Layout File entry is created from a SSH Node Layout entry:
    auto make_layoutFileEntry = [](NodeLayout const& node) {
        // Ensure we have good hostname information.
        auto const hostname_len = node.hostname.size() + 1;
        if (hostname_len > sizeof(cti_layoutFile_t::host)) {
            throw std::runtime_error("hostname too large for layout buffer");
        }

        // Extract PE and node information from Node Layout.
        auto layout_entry    = cti_layoutFile_t{};
        layout_entry.PEsHere = node.pids.size();
        layout_entry.firstPE = node.firstPE;

        memcpy(layout_entry.host, node.hostname.c_str(), hostname_len);

        return layout_entry;
    };

    // Create the file path, write the file using the Step Layout
    auto const layoutPath = std::string{stagePath + "/" + SSH_LAYOUT_FILE};
    if (auto const layoutFile = cti::file::open(layoutPath, "wb")) {

        // Write the Layout header.
        cti::file::writeT(layoutFile.get(), cti_layoutFileHeader_t
            { .numNodes = (int)stepLayout.nodes.size()
        });

        // Write a Layout entry using node information from each SSH Node Layout entry.
        for (auto const& node : stepLayout.nodes) {
            cti::file::writeT(layoutFile.get(), make_layoutFileEntry(node));
        }

        return layoutPath;
    } else {
        throw std::runtime_error("failed to open layout file path " + layoutPath);
    }
}

std::string
GenericSSHFrontend::createPIDListFile(MPIRProctable const& procTable, std::string const& stagePath)
{
    auto const pidPath = std::string{stagePath + "/" + SSH_PID_FILE};
    if (auto const pidFile = cti::file::open(pidPath, "wb")) {

        // Write the PID List header.
        cti::file::writeT(pidFile.get(), cti_pidFileheader_t
            { .numPids = (int)procTable.size()
        });

        // Write a PID entry using information from each MPIR ProcTable entry.
        for (auto&& elem : procTable) {
            cti::file::writeT(pidFile.get(), cti_pidFile_t
                { .pid = elem.pid
            });
        }

        return pidPath;
    } else {
        throw std::runtime_error("failed to open PID file path " + pidPath);
    }
}

static std::string
getShimmedLauncherName(std::string const& launcherPath)
{
    if (cti::cstr::basename(launcherPath) == "mpirun") {
        return "mpiexec.hydra";
    }

    return "";
}

FE_daemon::MPIRResult
GenericSSHFrontend::launchApp(const char * const launcher_argv[],
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

        // Use MPIR shim if detected to be necessary
        auto const shimmedLauncherName = getShimmedLauncherName(launcher_path.get());
        if (!shimmedLauncherName.empty()) {
            // Get the shim setup paths from the frontend instance
            auto const shimBinaryPath = Frontend::inst().getBaseDir() + "/libexec/" + CTI_MPIR_SHIM_BINARY;
            auto const temporaryShimBinDir = Frontend::inst().getCfgDir() + "/shim";
            auto const shimmedLauncherPath = cti::take_pointer_ownership(_cti_pathFind(shimmedLauncherName.c_str(), nullptr), std::free);
            if (shimmedLauncherPath == nullptr) {
                throw std::runtime_error("Failed to find launcher in path: " +
                    std::string{shimmedLauncherPath.get()});
            }

            // Launch script with MPIR shim.
            return Daemon().request_LaunchMPIRShim(
                shimBinaryPath.c_str(), temporaryShimBinDir.c_str(), shimmedLauncherPath.get(),
                launcher_path.get(), launcherArgv.get(),
                ::open(inputFile, O_RDONLY), stdoutFd, stderrFd,
                env_list);
        }

        // Launch program under MPIR control.
        return Daemon().request_LaunchMPIR(
            launcher_path.get(), launcherArgv.get(),
            ::open(inputFile, O_RDONLY), stdoutFd, stderrFd,
            env_list);

    } else {
        throw std::runtime_error("Failed to find launcher in path: " + getLauncherName());
    }
}

std::weak_ptr<App>
GenericSSHFrontend::registerRemoteJob(char const* hostname, pid_t launcher_pid)
{
    auto session = SSHSession{hostname, m_username, m_homeDir};
    auto mpirResult = session.attachMPIR(Frontend::inst().getFEDaemonPath(), getLauncherName(), launcher_pid);

    // Register application with local FE daemon and insert into received MPIR response
    auto const mpir_id = Frontend::inst().Daemon().request_RegisterApp(::getpid());
    mpirResult.mpir_id = mpir_id;

    // Create and return new application object using MPIR response
    auto ret = m_apps.emplace(std::make_shared<GenericSSHApp>(*this, std::move(mpirResult)));
    if (!ret.second) {
        throw std::runtime_error("Failed to create new App object.");
    }
    return *ret.first;
}
