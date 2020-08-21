/******************************************************************************\
 * Frontend.cpp - SLURM specific frontend library functions.
 *
 * (C) Copyright 2014-2020 Hewlett Packard Enterprise Development LP.
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

#include <algorithm>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

// Pull in manifest to properly define all the forward declarations
#include "transfer/Manifest.hpp"

#include "SLURM/Frontend.hpp"

#include "useful/cti_argv.hpp"
#include "useful/cti_execvp.hpp"
#include "useful/cti_split.hpp"
#include "useful/cti_hostname.hpp"
#include "useful/cti_wrappers.hpp"

/* constructors / destructors */

SLURMApp::SLURMApp(SLURMFrontend& fe, FE_daemon::MPIRResult&& mpirData)
    : App(fe)
    , m_daemonAppId     { mpirData.mpir_id }
    , m_jobId           { (uint32_t)std::stoi(fe.Daemon().request_ReadStringMPIR(m_daemonAppId, "totalview_jobid")) }
    , m_stepId          { (uint32_t)std::stoi(fe.Daemon().request_ReadStringMPIR(m_daemonAppId, "totalview_stepid")) }
    , m_binaryRankMap   { std::move(mpirData.binaryRankMap) }
    , m_stepLayout      { fe.fetchStepLayout(m_jobId, m_stepId) }
    , m_beDaemonSent    { false }

    , m_toolPath    { SLURM_TOOL_DIR }
    , m_attribsPath { cti::cstr::asprintf(SLURM_CRAY_DIR, SLURM_APID(m_jobId, m_stepId)) }
    , m_stagePath   { cti::cstr::mkdtemp(std::string{m_frontend.getCfgDir() + "/" + SLURM_STAGE_DIR}) }
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

    // FIXME: When/if pmi_attribs get fixed for the slurm startup barrier, this
    // call can be removed. Right now the pmi_attribs file is created in the pmi
    // ctor, which is called after the slurm startup barrier, meaning it will not
    // yet be created when launching. So we need to send over a file containing
    // the information to the compute nodes.
    m_extraFiles.push_back(fe.createPIDListFile(mpirData.proctable, m_stagePath));
}

SLURMApp::~SLURMApp()
{
    // Delete the staging directory if it exists.
    if (!m_stagePath.empty()) {
        _cti_removeDirectory(m_stagePath.c_str());
    }

    // Inform the FE daemon that this App is going away
    m_frontend.Daemon().request_DeregisterApp(m_daemonAppId);
}

/* app instance creation */

static FE_daemon::MPIRResult sattachMPIR(SLURMFrontend& fe, uint32_t jobId, uint32_t stepId)
{
    cti::OutgoingArgv<SattachArgv> sattachArgv(SATTACH);
    sattachArgv.add(SattachArgv::Argument("-Q"));
    sattachArgv.add(SattachArgv::Argument(std::to_string(jobId) + "." + std::to_string(stepId)));

    // get path to SATTACH binary for MPIR control
    if (auto const sattachPath = cti::take_pointer_ownership(_cti_pathFind(SATTACH, nullptr), std::free)) {
        try {
            // request an MPIR session to extract proctable
            auto const mpirResult = fe.Daemon().request_LaunchMPIR(
                sattachPath.get(), sattachArgv.get(), -1, -1, -1, nullptr);

            return mpirResult;

        } catch (std::exception const& ex) {
            throw std::runtime_error("Failed to attach to job " +
                std::to_string(jobId) + " " + std::to_string(stepId));
        }
    } else {
        throw std::runtime_error("Failed to find SATTACH in path");
    }
}

/* running app info accessors */

// Note that we should provide this as a jobid.stepid format. It will make turning
// it into a Cray apid easier on the backend since we don't lose any information
// with this format.
std::string
SLURMApp::getJobId() const
{
    return std::string{std::to_string(m_jobId) + "." + std::to_string(m_stepId)};
}

std::string
SLURMApp::getLauncherHostname() const
{
    throw std::runtime_error("not supported for WLM: getLauncherHostname");
}

bool
SLURMApp::isRunning() const
{
    return m_frontend.Daemon().request_CheckApp(m_daemonAppId);
}

std::vector<std::string>
SLURMApp::getHostnameList() const
{
    std::vector<std::string> result;
    // extract hostnames from each NodeLayout
    std::transform(m_stepLayout.nodes.begin(), m_stepLayout.nodes.end(), std::back_inserter(result),
        [](SLURMFrontend::NodeLayout const& node) { return node.hostname; });
    return result;
}

std::vector<CTIHost>
SLURMApp::getHostsPlacement() const
{
    std::vector<CTIHost> result;
    // construct a CTIHost from each NodeLayout
    std::transform(m_stepLayout.nodes.begin(), m_stepLayout.nodes.end(), std::back_inserter(result),
        [](SLURMFrontend::NodeLayout const& node) {
            return CTIHost{node.hostname, node.numPEs};
        });
    return result;
}

std::map<std::string, std::vector<int>>
SLURMApp::getBinaryRankMap() const
{
    return m_binaryRankMap;
}

/* running app interaction interface */

void SLURMApp::releaseBarrier() {
    // release MPIR barrier
    m_frontend.Daemon().request_ReleaseMPIR(m_daemonAppId);
}

void
SLURMApp::redirectOutput(int stdoutFd, int stderrFd)
{
    // create sattach argv
    auto sattachArgv = cti::ManagedArgv {
        SATTACH // first argument should be "sattach"
        , "-Q"    // second argument is quiet
        , getJobId() // third argument is the jobid.stepid
    };

    // redirect stdin / stderr / stdout
    if (stdoutFd < 0) {
        stdoutFd = STDOUT_FILENO;
    }
    if (stderrFd < 0) {
        stderrFd = STDERR_FILENO;
    }

    m_frontend.Daemon().request_ForkExecvpUtil_Async(
        m_daemonAppId, SATTACH, sattachArgv.get(),
        // redirect stdin / stderr / stdout
        open("/dev/null", O_RDONLY), stdoutFd, stderrFd,
        nullptr);
}

void SLURMApp::kill(int signum)
{
    // create the args for scancel
    auto scancelArgv = cti::ManagedArgv {
        SCANCEL // first argument should be "scancel"
        , "-Q"  // second argument is quiet
        , "-s", std::to_string(signum)    // third argument is signal number
        , getJobId() // fourth argument is the jobid.stepid
    };

    // tell frontend daemon to launch scancel, wait for it to finish
    m_frontend.Daemon().request_ForkExecvpUtil_Sync(
        m_daemonAppId, SCANCEL, scancelArgv.get(),
        -1, -1, -1,
        nullptr);
}

void SLURMApp::shipPackage(std::string const& tarPath) const {
    // create the args for sbcast
    auto sbcastArgv = cti::ManagedArgv {
        SBCAST
        , "-C"
        , "-j", std::to_string(m_jobId)
        , tarPath
        , "--force"
    };

    if (auto packageName = cti::take_pointer_ownership(_cti_pathToName(tarPath.c_str()), std::free)) {
        sbcastArgv.add(std::string(SLURM_TOOL_DIR) + "/" + packageName.get());
    } else {
        throw std::runtime_error("_cti_pathToName failed");
    }

    // now ship the tarball to the compute nodes. tell overwatch to launch sbcast, wait to complete
    m_frontend.Daemon().request_ForkExecvpUtil_Sync(
        m_daemonAppId, SBCAST, sbcastArgv.get(),
        -1, -1, -1,
        nullptr);

    // call to request_ForkExecvpUtil_Sync will wait until the sbcast finishes
    // FIXME: There is no way to error check right now because the sbcast command
    // can only send to an entire job, not individual job steps. The /var/spool/alps/<apid>
    // directory will only exist on nodes associated with this particular job step, and the
    // sbcast command will exit with error if the directory doesn't exist even if the transfer
    // worked on the nodes associated with the step. I opened schedmd BUG 1151 for this issue.
}

void SLURMApp::startDaemon(const char* const args[]) {
    // sanity check
    if (args == nullptr) {
        throw std::runtime_error("args array is null!");
    }

    // Send daemon if not already shipped
    if (!m_beDaemonSent) {
        // Get the location of the backend daemon
        if (m_frontend.getBEDaemonPath().empty()) {
            throw std::runtime_error("Unable to locate backend daemon binary. Try setting " + std::string(CTI_BASE_DIR_ENV_VAR) + " environment varaible to the install location of CTI.");
        }

        // Copy the BE binary to its unique storage name
        auto const sourcePath = m_frontend.getBEDaemonPath();
        auto const destinationPath = m_frontend.getCfgDir() + "/" + getBEDaemonName();

        // Create the args for link
        auto linkArgv = cti::ManagedArgv {
            "ln", "-s", sourcePath.c_str(), destinationPath.c_str()
        };

        // Run link command
        m_frontend.Daemon().request_ForkExecvpUtil_Sync(
            m_daemonAppId, "ln", linkArgv.get(),
            -1, -1, -1,
            nullptr);

        // Ship the unique backend daemon
        shipPackage(destinationPath);
        // set transfer to true
        m_beDaemonSent = true;
    }

    // use existing daemon binary on compute node
    std::string const remoteBEDaemonPath{m_toolPath + "/" + getBEDaemonName()};

    // Start adding the args to the launchder argv array
    //
    // This corresponds to:
    //
    // srun --jobid=<job_id> --gres=none --mem-per-cpu=0 --mem_bind=no
    // --cpu_bind=no --share --ntasks-per-node=1 --nodes=<numNodes>
    // --nodelist=<host1,host2,...> --disable-status --quiet --mpi=none
    // --input=none --output=none --error=none <tool daemon> <args>
    //
    auto& slurmFrontend = dynamic_cast<SLURMFrontend&>(m_frontend);
    auto launcherArgv = cti::ManagedArgv {
        slurmFrontend.getLauncherName()
        , "--jobid=" + std::to_string(m_jobId)
        , "--nodes=" + std::to_string(m_stepLayout.nodes.size())
    };
    for (auto&& arg : slurmFrontend.getSrunDaemonArgs()) {
        launcherArgv.add(arg);
    }

    // create the hostlist by contencating all hostnames
    { auto hostlist = std::string{};
        bool firstHost = true;
        for (auto const& node : m_stepLayout.nodes) {
            hostlist += (firstHost ? "" : ",") + node.hostname;
            firstHost = false;
        }
        launcherArgv.add("--nodelist=" + hostlist);
    }

    launcherArgv.add(remoteBEDaemonPath);

    // merge in the args array if there is one
    if (args != nullptr) {
        for (const char* const* arg = args; *arg != nullptr; arg++) {
            launcherArgv.add(*arg);
        }
    }

    // build environment from blacklist
    auto const envVarBlacklist = std::vector<std::string>{
        "SLURM_CHECKPOINT",      "SLURM_CONN_TYPE",         "SLURM_CPUS_PER_TASK",
        "SLURM_DEPENDENCY",      "SLURM_DIST_PLANESIZE",    "SLURM_DISTRIBUTION",
        "SLURM_EPILOG",          "SLURM_GEOMETRY",          "SLURM_NETWORK",
        "SLURM_NPROCS",          "SLURM_NTASKS",            "SLURM_NTASKS_PER_CORE",
        "SLURM_NTASKS_PER_NODE", "SLURM_NTASKS_PER_SOCKET", "SLURM_PARTITION",
        "SLURM_PROLOG",          "SLURM_REMOTE_CWD",        "SLURM_REQ_SWITCH",
        "SLURM_RESV_PORTS",      "SLURM_TASK_EPILOG",       "SLURM_TASK_PROLOG",
        "SLURM_WORKING_DIR"
    };
    cti::ManagedArgv launcherEnv;
    for (auto&& envVar : envVarBlacklist) {
        launcherEnv.add(envVar + "=");
    }

    // tell FE Daemon to launch srun
    m_frontend.Daemon().request_ForkExecvpUtil_Async(
        m_daemonAppId, dynamic_cast<SLURMFrontend&>(m_frontend).getLauncherName().c_str(),
        launcherArgv.get(),
        // redirect stdin / stderr / stdout
        ::open("/dev/null", O_RDONLY), ::open("/dev/null", O_WRONLY), ::open("/dev/null", O_WRONLY),
        launcherEnv.get() );
}

/* SLURM frontend implementation */

static std::string getSlurmVersion()
{
    char const* const srunVersionArgv[] = {"srun", "--version", nullptr};
    auto srunVersionOutput = cti::Execvp{"srun", (char* const*)srunVersionArgv};

    auto slurmVersion = std::string{};
    if (!std::getline(srunVersionOutput.stream(), slurmVersion, '\n')) {
        throw std::runtime_error("failed to get SRUN version number output");
    }
    // slurm major.minor.patch

    slurmVersion = slurmVersion.substr(slurmVersion.find(" ") + 1);
    // major.minor.patch

    slurmVersion = slurmVersion.substr(0, slurmVersion.find("."));
    // major

    return slurmVersion;
}

SLURMFrontend::SLURMFrontend()
    : m_srunAppArgs {}
    , m_srunDaemonArgs
        { "--gres=none"
        , "--mem-per-cpu=0"
        , "--ntasks-per-node=1"
        , "--disable-status"
        , "--quiet"
        , "--mpi=none"
        , "--output=none"
        , "--error=none"
        }
    {

    // Detect SLURM version and set SRUN arguments accordingly
    auto const slurmVersion = std::stoi(getSlurmVersion());
    if (slurmVersion <= 18) {
        m_srunDaemonArgs.insert(m_srunDaemonArgs.end(),
            { "--mem_bind=no"
            , "--cpu_bind=no"
            , "--share"
            }
        );
    } else if (slurmVersion >= 19) {
        m_srunDaemonArgs.insert(m_srunDaemonArgs.end(),
            { "--mem-bind=no"
            , "--cpu-bind=no"
            , "--oversubscribe"
            }
        );
    } else {
        throw std::runtime_error("unknown SLURM version: " + slurmVersion);
    }

    // Add / override SRUN arguments from environment variables
    auto addArgsFromRaw = [](std::vector<std::string>& toVec, char const* fromStr) {
        auto argStr = std::string{fromStr};
        while (true) {
            // Add first argument to vector
            auto const argEnd = argStr.find(" ");
            toVec.emplace_back(argStr.substr(0, argEnd));

            // Continue with next argument, if present
            if (argEnd != std::string::npos) {
                argStr = argStr.substr(argEnd + 1);
            } else {
                break;
            }
        }
    };

    if (auto const rawSrunOverrideArgs = getenv(SRUN_OVERRIDE_ARGS_ENV_VAR)) {
        m_srunAppArgs = {};
        m_srunDaemonArgs = {};
        addArgsFromRaw(m_srunAppArgs,    rawSrunOverrideArgs);
        addArgsFromRaw(m_srunDaemonArgs, rawSrunOverrideArgs);
    }

    if (auto const rawSrunAppendArgs = getenv(SRUN_APPEND_ARGS_ENV_VAR)) {
        addArgsFromRaw(m_srunAppArgs,    rawSrunAppendArgs);
        addArgsFromRaw(m_srunDaemonArgs, rawSrunAppendArgs);
    }
}

bool
SLURMFrontend::isSupported()
{
    // Check that the srun check returns expected code
    { auto srunTestArgv = cti::ManagedArgv{"srun", "--test-only"};
        if (cti::Execvp{"srun", srunTestArgv.get()}.getExitStatus()) {
            return false;
        }
    }

    // Check that srun is a binary and not a script
    { auto binaryTestArgv = cti::ManagedArgv{"bash", "-c",
        "file --mime `which srun` | grep application/x-executable"};
        if (cti::Execvp{"bash", binaryTestArgv.get()}.getExitStatus()) {
            throw std::runtime_error("srun was detected on the system, but it is not a binary file. \
Tool launch requires direct access to the srun binary. \
Ensure that the srun command is not wrapped by a script");
        }
    }

    // Check that the srun binary contains MPIR symbols
    { auto symbolTestArgv = cti::ManagedArgv{"bash", "-c",
        "nm `which srun` | grep MPIR_Breakpoint$"};
        if (cti::Execvp{"bash", symbolTestArgv.get()}.getExitStatus()) {
            throw std::runtime_error("srun was detected on the system, but it does not contain debug symbols. \
Tool launch is coordinated through reading information at these symbols");
        }
    }

    return true;
}

std::weak_ptr<App>
SLURMFrontend::launch(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
    CStr inputFile, CStr chdirPath, CArgArray env_list)
{
    // Slurm calls the launch barrier correctly even when the program is not an MPI application.
    // Delegating to barrier implementation works properly even for serial applications.
    auto appPtr = std::make_shared<SLURMApp>(*this,
        launchApp(launcher_argv, inputFile, stdout_fd, stderr_fd, chdirPath, env_list));

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
SLURMFrontend::launchBarrier(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
    CStr inputFile, CStr chdirPath, CArgArray env_list)
{
    auto ret = m_apps.emplace(std::make_shared<SLURMApp>(*this,
        launchApp(launcher_argv, inputFile, stdout_fd, stderr_fd, chdirPath, env_list)));

    if (!ret.second) {
        throw std::runtime_error("Failed to create new App object.");
    }
    return *ret.first;
}

std::string
SLURMFrontend::getHostname() const
{
    // Delegate to shared implementation supporting both XC and Shasta
    return cti::detectFrontendHostname();
}

/* SLURM static implementations */

std::weak_ptr<App>
SLURMFrontend::registerJob(size_t numIds, ...) {
    if (numIds != 2) {
        throw std::logic_error("expecting job and step ID pair to register app");
    }

    va_list idArgs;
    va_start(idArgs, numIds);

    uint32_t jobId  = va_arg(idArgs, uint32_t);
    uint32_t stepId = va_arg(idArgs, uint32_t);

    va_end(idArgs);

    auto ret = m_apps.emplace(std::make_shared<SLURMApp>(*this, sattachMPIR(*this, jobId, stepId)));
    if (!ret.second) {
        throw std::runtime_error("Failed to create new App object.");
    }
    return *ret.first;
}

std::string
SLURMFrontend::getLauncherName()
{
    auto getenvOrDefault = [](char const* envVar, char const* defaultValue) {
        if (char const* envValue = getenv(envVar)) {
            return envValue;
        }
        return defaultValue;
    };

    // Cache the launcher name result.
    auto static launcherName = std::string{getenvOrDefault(CTI_LAUNCHER_NAME_ENV_VAR, SRUN)};
    return launcherName;
}

SLURMFrontend::StepLayout
SLURMFrontend::fetchStepLayout(uint32_t job_id, uint32_t step_id)
{
    // create sattach instance
    cti::OutgoingArgv<SattachArgv> sattachArgv(SATTACH);
    sattachArgv.add(SattachArgv::DisplayLayout);
    sattachArgv.add(SattachArgv::Argument("-Q"));
    sattachArgv.add(SattachArgv::Argument(std::to_string(job_id) + "." + std::to_string(step_id)));

    // create sattach output capture object
    cti::Execvp sattachOutput(SATTACH, sattachArgv.get());
    auto& sattachStream = sattachOutput.stream();
    std::string sattachLine;

    // start parsing sattach output

    // "Job step layout:"
    if (std::getline(sattachStream, sattachLine)) {
        if (sattachLine.compare("Job step layout:")) {
            throw std::runtime_error(std::string("sattach layout: wrong format: ") + sattachLine);
        }
    } else {
        throw std::runtime_error("sattach layout: wrong format: expected header");
    }

    StepLayout layout;
    auto numNodes = int{0};

    // "  {numPEs} tasks, {numNodes} nodes ({hostname}...)"
    if (std::getline(sattachStream, sattachLine)) {
        // split the summary line
        std::string rawNumPEs, rawNumNodes;
        std::tie(rawNumPEs, std::ignore, rawNumNodes) =
            cti::split::string<3>(cti::split::removeLeadingWhitespace(sattachLine));

        // fill out sattach layout
        layout.numPEs = std::stoul(rawNumPEs);
        numNodes = std::stoi(rawNumNodes);
        layout.nodes.reserve(numNodes);
    } else {
        throw std::runtime_error("sattach layout: wrong format: expected summary");
    }

    // seperator line
    std::getline(sattachStream, sattachLine);

    // "  Node {nodeNum} ({hostname}), {numPEs} task(s): PE_0 {PE_i }..."
    for (auto i = int{0}; std::getline(sattachStream, sattachLine); i++) {
        if (i >= numNodes) {
            throw std::runtime_error("malformed sattach output: too many nodes!");
        }

        // split the summary line
        std::string nodeNum, hostname, numPEs, pe_0;
        std::tie(std::ignore, nodeNum, hostname, numPEs, std::ignore, pe_0) =
            cti::split::string<6>(cti::split::removeLeadingWhitespace(sattachLine));

        // fill out node layout
        layout.nodes.push_back(NodeLayout
            { hostname.substr(1, hostname.length() - 3) // remove parens and comma from hostname
            , std::stoul(numPEs)
            , std::stoul(pe_0)
        });
    }

    // wait for sattach to complete
    auto const sattachCode = sattachOutput.getExitStatus();
    if (sattachCode > 0) {
        throw std::runtime_error("invalid job id " + std::to_string(job_id));
    }

    return layout;
}

std::string
SLURMFrontend::createNodeLayoutFile(StepLayout const& stepLayout, std::string const& stagePath)
{
    // How a SLURM Node Layout File entry is created from a Slurm Node Layout entry:
    auto make_layoutFileEntry = [](NodeLayout const& node) {
        // Ensure we have good hostname information.
        auto const hostname_len = node.hostname.size() + 1;
        if (hostname_len > sizeof(slurmLayoutFile_t::host)) {
            throw std::runtime_error("hostname too large for layout buffer");
        }

        // Extract PE and node information from Node Layout.
        auto layout_entry    = slurmLayoutFile_t{};
        layout_entry.PEsHere = node.numPEs;
        layout_entry.firstPE = node.firstPE;
        memcpy(layout_entry.host, node.hostname.c_str(), hostname_len);

        return layout_entry;
    };

    // Create the file path, write the file using the Step Layout
    auto const layoutPath = std::string{stagePath + "/" + SLURM_LAYOUT_FILE};
    if (auto const layoutFile = cti::file::open(layoutPath, "wb")) {

        // Write the Layout header.
        cti::file::writeT(layoutFile.get(), slurmLayoutFileHeader_t
            { .numNodes = (int)stepLayout.nodes.size()
        });

        // Write a Layout entry using node information from each Slurm Node Layout entry.
        for (auto const& node : stepLayout.nodes) {
            cti::file::writeT(layoutFile.get(), make_layoutFileEntry(node));
        }

        return layoutPath;
    } else {
        throw std::runtime_error("failed to open layout file path " + layoutPath);
    }
}

std::string
SLURMFrontend::createPIDListFile(MPIRProctable const& procTable, std::string const& stagePath)
{
    auto const pidPath = std::string{stagePath + "/" + SLURM_PID_FILE};
    if (auto const pidFile = cti::file::open(pidPath, "wb")) {

        // Write the PID List header.
        cti::file::writeT(pidFile.get(), slurmPidFileHeader_t
            { .numPids = (int)procTable.size()
        });

        // Write a PID entry using information from each MPIR ProcTable entry.
        for (auto&& elem : procTable) {
            cti::file::writeT(pidFile.get(), slurmPidFile_t
                { .pid = elem.pid
            });
        }

        return pidPath;
    } else {
        throw std::runtime_error("failed to open PID file path " + pidPath);
    }
}

FE_daemon::MPIRResult
SLURMFrontend::launchApp(const char * const launcher_argv[],
        const char *inputFile, int stdoutFd, int stderrFd, const char *chdirPath,
        const char * const env_list[])
{
    // Get the launcher path from CTI environment variable / default.
    if (auto const launcher_path = cti::take_pointer_ownership(_cti_pathFind(getLauncherName().c_str(), nullptr), std::free)) {
        // set up arguments and FDs
        if (inputFile == nullptr) { inputFile = "/dev/null"; }
        if (stdoutFd < 0) { stdoutFd = STDOUT_FILENO; }
        if (stderrFd < 0) { stderrFd = STDERR_FILENO; }
        auto const stdoutPath = "/proc/" + std::to_string(::getpid()) + "/fd/" + std::to_string(stdoutFd);
        auto const stderrPath = "/proc/" + std::to_string(::getpid()) + "/fd/" + std::to_string(stderrFd);

        // construct argv array & instance
        cti::ManagedArgv launcherArgv
            { launcher_path.get()
            , "--input="  + std::string{inputFile}
            , "--output=" + stdoutPath
            , "--error="  + stderrPath
        };
        for (auto&& arg : getSrunAppArgs()) {
            launcherArgv.add(arg);
        }
        for (const char* const* arg = launcher_argv; *arg != nullptr; arg++) {
            launcherArgv.add(*arg);
        }

        // Launch program under MPIR control.
        auto const mpirData = Daemon().request_LaunchMPIR(
            launcher_path.get(), launcherArgv.get(),
            // redirect stdin/out/err to /dev/null, use SRUN arguments for in/output instead
            open("/dev/null", O_RDWR), open("/dev/null", O_RDWR), open("/dev/null", O_RDWR),
            env_list);

        return mpirData;

    } else {
        throw std::runtime_error("Failed to find launcher in path: " + getLauncherName());
    }
}

SrunInfo
SLURMFrontend::getSrunInfo(pid_t srunPid) {
    // sanity check
    if (srunPid <= 0) {
        throw std::runtime_error("Invalid srunPid " + std::to_string(srunPid));
    }

    if (auto const launcherPath = cti::take_pointer_ownership(_cti_pathFind(getLauncherName().c_str(), nullptr), std::free)) {
        // tell overwatch to extract information using MPIR attach
        auto const mpirData = Daemon().request_AttachMPIR(launcherPath.get(), srunPid);

        // Get job and step ID via memory read
        auto const jobId  = (uint32_t)std::stoi(Daemon().request_ReadStringMPIR(mpirData.mpir_id, "totalview_jobid"));
        auto const stepId = (uint32_t)std::stoi(Daemon().request_ReadStringMPIR(mpirData.mpir_id, "totalview_stepid"));

        // Release MPIR control
        Daemon().request_ReleaseMPIR(mpirData.mpir_id);

        return SrunInfo { jobId, stepId };
    } else {
        throw std::runtime_error("Failed to find launcher in path: " + getLauncherName());
    }
}

// Apollo SLURM specializations

// Running on an Apollo machine if the `cminfo` cluster info query program is
// installed, and it reports running on login / admin node.
bool ApolloSLURMFrontend::isSupported() {
    try {
        char const* cminfoArgv[] = { "cminfo", "--name", nullptr };

        // Start cminfo
        auto cminfoOutput = cti::Execvp{"cminfo", (char* const*)cminfoArgv};
        if (cminfoOutput.getExitStatus() != 0) {
            return false;
        }

        // Detect if running on admin node
        auto& cminfoStream = cminfoOutput.stream();
        std::string cmName;
        if (std::getline(cminfoStream, cmName)) {
            return !cmName.compare("admin");
        } else {
            return false;
        }

    } catch(...) {
        // cminfo not installed
        return false;
    }
}

// /etc/hosts file for cluster is generated by SGI Tempo.
// Login / admin node is accessible via hostname `admin`.
// See initialization script at /etc/opt/sgi/conf.d/20-hosts
std::string ApolloSLURMFrontend::getHostname() const {
    return "admin";
}
