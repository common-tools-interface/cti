/******************************************************************************\
 * cray_slurm_fe.c - Cray SLURM specific frontend library functions.
 *
 * Copyright 2014-2019 Cray Inc.  All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
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
#include "cti_transfer/Manifest.hpp"

#include "CraySLURM/Frontend.hpp"

#include "useful/cti_argv.hpp"
#include "useful/cti_execvp.hpp"
#include "useful/cti_split.hpp"
#include "useful/cti_wrappers.hpp"

/* constructors / destructors */

CraySLURMApp::CraySLURMApp(CraySLURMFrontend& fe, SrunInstance&& srunInstance)
    : App(fe)
    , m_daemonAppId     { srunInstance.mpirData.mpir_id }
    , m_jobId           { srunInstance.mpirData.job_id }
    , m_stepId          { srunInstance.mpirData.step_id }
    , m_stepLayout      { fe.fetchStepLayout(m_jobId, m_stepId) }
    , m_beDaemonSent    { false }

    , m_outputPath    { std::move(srunInstance.outputPath) }
    , m_errorPath     { std::move(srunInstance.errorPath) }

    , m_toolPath    { CRAY_SLURM_TOOL_DIR }
    , m_attribsPath { cti::cstr::asprintf(CRAY_SLURM_CRAY_DIR, CRAY_SLURM_APID(m_jobId, m_stepId)) }
    , m_stagePath   { cti::cstr::mkdtemp(std::string{m_frontend.getCfgDir() + "/" + SLURM_STAGE_DIR}) }
    , m_extraFiles  { fe.createNodeLayoutFile(m_stepLayout, m_stagePath) }

{
    // Ensure there are running nodes in the job.
    if (m_stepLayout.nodes.empty()) {
        throw std::runtime_error("Application " + getJobId() + " does not have any nodes.");
    }

    // If an active MPIR session was provided, extract the MPIR ProcTable and write the PID List File.
    if (m_daemonAppId) {
        // FIXME: When/if pmi_attribs get fixed for the slurm startup barrier, this
        // call can be removed. Right now the pmi_attribs file is created in the pmi
        // ctor, which is called after the slurm startup barrier, meaning it will not
        // yet be created when launching. So we need to send over a file containing
        // the information to the compute nodes.
        m_extraFiles.push_back(fe.createPIDListFile(srunInstance.mpirData.proctable, m_stagePath));
    } else {
        throw std::runtime_error("tried to create app with invalid daemon id: " + std::to_string(m_daemonAppId));
    }
}

CraySLURMApp::~CraySLURMApp()
{
    // Delete the staging directory if it exists.
    if (!m_stagePath.empty()) {
        _cti_removeDirectory(m_stagePath.c_str());
    }

    if (m_daemonAppId > 0) {
        // Inform the FE daemon that this App is going away
        m_frontend.Daemon().request_DeregisterApp(m_daemonAppId);
    }
}

/* app instance creation */

static FE_daemon::MPIRResult sattachMPIR(CraySLURMFrontend& fe, uint32_t jobId, uint32_t stepId)
{
    cti::OutgoingArgv<SattachArgv> sattachArgv(SATTACH);
    sattachArgv.add(SattachArgv::Argument("-Q"));
    sattachArgv.add(SattachArgv::Argument(std::to_string(jobId) + "." + std::to_string(stepId)));

    // get path to SATTACH binary for MPIR control
    if (auto const sattachPath = cti::move_pointer_ownership(_cti_pathFind(SATTACH, nullptr), std::free)) {
        try {
            // request an MPIR session to extract proctable
            auto const mpirResult = fe.Daemon().request_LaunchMPIR(
                sattachPath.get(), sattachArgv.get(), -1, -1, -1, nullptr);
            // have the proctable, terminate SATTACh
            fe.Daemon().request_TerminateMPIR(mpirResult.mpir_id);

            return mpirResult;

        } catch (std::exception const& ex) {
            throw std::runtime_error("Failed to attach to job " +
                std::to_string(jobId) + " " + std::to_string(stepId));
        }
    } else {
        throw std::runtime_error("Failed to find SATTACH in path");
    }
}

CraySLURMApp::CraySLURMApp(CraySLURMFrontend& fe, uint32_t jobId, uint32_t stepId)
    : CraySLURMApp
        { fe
        , SrunInstance
            { .mpirData = sattachMPIR(fe, jobId, stepId)
            , .outputPath = cti::temp_file_handle{fe.getCfgDir() + "/cti-output-fifo-XXXXXX"}
            , .errorPath  = cti::temp_file_handle{fe.getCfgDir() + "/cti-error-fifo-XXXXXX"}
            }
        }
{ }

CraySLURMApp::CraySLURMApp(CraySLURMFrontend& fe, const char * const launcher_argv[], int stdout_fd, int stderr_fd,
    const char *inputFile, const char *chdirPath, const char * const env_list[])
    : CraySLURMApp{ fe, fe.launchApp(launcher_argv, inputFile, stdout_fd, stderr_fd, chdirPath, env_list) }
{ }

/* running app info accessors */

// Note that we should provide this as a jobid.stepid format. It will make turning
// it into a Cray apid easier on the backend since we don't lose any information
// with this format.
std::string
CraySLURMApp::getJobId() const
{
    return std::string{std::to_string(m_jobId) + "." + std::to_string(m_stepId)};
}

std::string
CraySLURMApp::getLauncherHostname() const
{
    throw std::runtime_error("not supported for WLM: getLauncherHostname");
}

std::vector<std::string>
CraySLURMApp::getHostnameList() const
{
    std::vector<std::string> result;
    // extract hostnames from each NodeLayout
    std::transform(m_stepLayout.nodes.begin(), m_stepLayout.nodes.end(), std::back_inserter(result),
        [](CraySLURMFrontend::NodeLayout const& node) { return node.hostname; });
    return result;
}

std::vector<CTIHost>
CraySLURMApp::getHostsPlacement() const
{
    std::vector<CTIHost> result;
    // construct a CTIHost from each NodeLayout
    std::transform(m_stepLayout.nodes.begin(), m_stepLayout.nodes.end(), std::back_inserter(result),
        [](CraySLURMFrontend::NodeLayout const& node) {
            return CTIHost{node.hostname, node.numPEs};
        });
    return result;
}

/* running app interaction interface */

void CraySLURMApp::releaseBarrier() {
    // check MPIR barrier
    if (!m_daemonAppId) {
        throw std::runtime_error("app not under MPIR control");
    }

    // release MPIR barrier
    m_frontend.Daemon().request_ReleaseMPIR(m_daemonAppId);
}

void
CraySLURMApp::redirectOutput(int stdoutFd, int stderrFd)
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

void CraySLURMApp::kill(int signum)
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

void CraySLURMApp::shipPackage(std::string const& tarPath) const {
    // create the args for sbcast
    auto sbcastArgv = cti::ManagedArgv {
        SBCAST
        , "-C"
        , "-j", std::to_string(m_jobId)
        , tarPath
        , "--force"
    };

    if (auto packageName = cti::move_pointer_ownership(_cti_pathToName(tarPath.c_str()), std::free)) {
        sbcastArgv.add(std::string(CRAY_SLURM_TOOL_DIR) + "/" + packageName.get());
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

void CraySLURMApp::startDaemon(const char* const args[]) {
    // sanity check
    if (args == nullptr) {
        throw std::runtime_error("args array is null!");
    }

    // use existing daemon binary on compute node
    std::string const remoteBEDaemonPath{m_toolPath + "/" + CTI_BE_DAEMON_BINARY};

    // Start adding the args to the launchder argv array
    //
    // This corresponds to:
    //
    // srun --jobid=<job_id> --gres=none --mem-per-cpu=0 --mem_bind=no
    // --cpu_bind=no --share --ntasks-per-node=1 --nodes=<numNodes>
    // --nodelist=<host1,host2,...> --disable-status --quiet --mpi=none
    // --input=none --output=none --error=none <tool daemon> <args>
    //
    auto launcherArgv = cti::ManagedArgv {
        dynamic_cast<CraySLURMFrontend&>(m_frontend).getLauncherName()
        , "--jobid=" + std::to_string(m_jobId)
        , "--gres=none"
        , "--mem-per-cpu=0"
        , "--mem_bind=no"
        , "--cpu_bind=no"
        , "--share"
        , "--ntasks-per-node=1"
        , "--nodes=" + std::to_string(m_stepLayout.nodes.size())
        , "--disable-status"
        , "--quiet"
        , "--mpi=none"
        , "--output=none"
        , "--error=none"
    };

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
        m_daemonAppId, dynamic_cast<CraySLURMFrontend&>(m_frontend).getLauncherName().c_str(),
        launcherArgv.get(),
        // redirect stdin / stderr / stdout
        ::open("/dev/null", O_RDONLY), ::open("/dev/null", O_WRONLY), ::open("/dev/null", O_WRONLY),
        launcherEnv.get() );
}

/* cray slurm frontend implementation */

std::weak_ptr<App>
CraySLURMFrontend::launchBarrier(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
    CStr inputFile, CStr chdirPath, CArgArray env_list)
{
    auto ret = m_apps.emplace(std::make_shared<CraySLURMApp>(   *this,
                                                                launcher_argv,
                                                                stdout_fd,
                                                                stderr_fd,
                                                                inputFile,
                                                                chdirPath,
                                                                env_list));
    if (!ret.second) {
        throw std::runtime_error("Failed to create new App object.");
    }
    return *ret.first;
}

std::string
CraySLURMFrontend::getHostname() const
{
    auto tryParseHostnameFile = [](char const* filePath) {
        if (auto nidFile = cti::file::try_open(filePath, "r")) {
            int nid;
            { // We expect this file to have a numeric value giving our current Node ID.
                char buf[BUFSIZ];
                if (fgets(buf, BUFSIZ, nidFile.get()) == nullptr) {
                    throw std::runtime_error("_cti_cray_slurm_getHostname fgets failed.");
                }
                nid = std::stoi(std::string{buf});
            }

            // Use the NID to create the standard hostname format.
            return cti::cstr::asprintf(CRAY_HOSTNAME_FMT, nid);

        } else {
            return cti::cstr::gethostname();
        }
    };

    // Cache the hostname result.
    static auto hostname = tryParseHostnameFile(CRAY_NID_FILE);
    return hostname;
}

/* Cray-SLURM static implementations */

std::weak_ptr<App>
CraySLURMFrontend::registerJob(size_t numIds, ...) {
    if (numIds != 2) {
        throw std::logic_error("expecting job and step ID pair to register app");
    }

    va_list idArgs;
    va_start(idArgs, numIds);

    uint32_t jobId  = va_arg(idArgs, uint32_t);
    uint32_t stepId = va_arg(idArgs, uint32_t);

    va_end(idArgs);

    auto ret = m_apps.emplace(std::make_shared<CraySLURMApp>(*this, jobId, stepId));
    if (!ret.second) {
        throw std::runtime_error("Failed to create new App object.");
    }
    return *ret.first;
}

std::string
CraySLURMFrontend::getLauncherName()
{
    auto getenvOrDefault = [](char const* envVar, char const* defaultValue) {
        if (char const* envValue = getenv(envVar)) {
            return envValue;
        }
        return defaultValue;
    };

    // Cache the launcher name result.
    auto static launcherName = std::string{getenvOrDefault(CTI_LAUNCHER_NAME, SRUN)};
    return launcherName;
}

CraySLURMFrontend::StepLayout
CraySLURMFrontend::fetchStepLayout(uint32_t job_id, uint32_t step_id)
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

    // wait for sattach to complete
    auto const sattachCode = sattachOutput.getExitStatus();
    if (sattachCode > 0) {
        throw std::runtime_error("invalid job id " + std::to_string(job_id));
    }

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

    return layout;
}

std::string
CraySLURMFrontend::createNodeLayoutFile(StepLayout const& stepLayout, std::string const& stagePath)
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
CraySLURMFrontend::createPIDListFile(MPIRProctable const& procTable, std::string const& stagePath)
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

CraySLURMFrontend::SrunInstance
CraySLURMFrontend::launchApp(const char * const launcher_argv[],
        const char *inputFile, int stdoutFd, int stderrFd, const char *chdirPath,
        const char * const env_list[])
{
    auto srunInstance = SrunInstance {
        .mpirData = FE_daemon::MPIRResult
            { FE_daemon::DaemonAppId{0} // mpir_id
            , pid_t{0} // launcher_pid
            , uint32_t{0} // job_id
            , uint32_t{0} // step_id
            , MPIRProctable{} // proctable
        }
        , .outputPath = cti::temp_file_handle{getCfgDir() + "/cti-output-fifo-XXXXXX"} // outputPath
        , .errorPath  = cti::temp_file_handle{getCfgDir() + "/cti-error-fifo-XXXXXX"} // errorPath
    };

    // Open input file (or /dev/null to avoid stdin contention).
    auto openFileOrDevNull = [&](char const* inputFile) {
        int input_fd = -1;
        if (inputFile == nullptr) {
            inputFile = "/dev/null";
        }
        errno = 0;
        input_fd = open(inputFile, O_RDONLY);
        if (input_fd < 0) {
            throw std::runtime_error("Failed to open input file " + std::string(inputFile) +": " + std::string(strerror(errno)));
        }

        return input_fd;
    };

    // attach output / error fifo to user-provided FDs if applicable
    if (mkfifo(srunInstance.outputPath.get(), 0600) < 0) {
        throw std::runtime_error("mkfifo failed on " + std::string{srunInstance.outputPath.get()} +": " + std::string{strerror(errno)});
    }
    if (mkfifo(srunInstance.errorPath.get(), 0600) < 0) {
        throw std::runtime_error("mkfifo failed on " + std::string{srunInstance.errorPath.get()} +": " + std::string{strerror(errno)});
    }

    // Get the launcher path from CTI environment variable / default.
    if (auto const launcher_path = cti::move_pointer_ownership(_cti_pathFind(getLauncherName().c_str(), nullptr), std::free)) {
        // set up arguments and FDs
        std::string const redirectPath = getBaseDir() + "/libexec/" + OUTPUT_REDIRECT_BINARY;
        char const* redirectArgv[] = {
            OUTPUT_REDIRECT_BINARY, srunInstance.outputPath.get(), srunInstance.errorPath.get(), nullptr
        };
        if (stdoutFd < 0) { stdoutFd = STDOUT_FILENO; }
        if (stderrFd < 0) { stderrFd = STDERR_FILENO; }

        // run output redirection binary as app (register as util later)
        auto const redirectPid = Daemon().request_ForkExecvpApp(
            redirectPath.c_str(), redirectArgv,
            -1, stdoutFd, stderrFd,
            nullptr);

        // construct argv array & instance
        cti::ManagedArgv launcherArgv
            { launcher_path.get()
            , "--output=" + std::string{srunInstance.outputPath.get()}
            , "--error="  + std::string{srunInstance.errorPath.get()}
        };
        for (const char* const* arg = launcher_argv; *arg != nullptr; arg++) {
            launcherArgv.add(*arg);
        }

        // Launch program under MPIR control.
        srunInstance.mpirData = Daemon().request_LaunchMPIR(
            launcher_path.get(), launcherArgv.get(),
            // redirect stdout / stderr to /dev/null; use sattach to redirect the output instead
            openFileOrDevNull(inputFile), open("/dev/null", O_RDWR), open("/dev/null", O_RDWR),
            env_list);

        // overwatch redirect utility
        Daemon().request_RegisterUtil(srunInstance.mpirData.mpir_id, redirectPid);

        return srunInstance;
    } else {
        throw std::runtime_error("Failed to find launcher in path: " + getLauncherName());
    }
}

SrunInfo
CraySLURMFrontend::getSrunInfo(pid_t srunPid) {
    // sanity check
    if (srunPid <= 0) {
        throw std::runtime_error("Invalid srunPid " + std::to_string(srunPid));
    }

    if (auto const launcherPath = cti::move_pointer_ownership(_cti_pathFind(getLauncherName().c_str(), nullptr), std::free)) {
        // tell overwatch to extract information using MPIR attach
        auto const mpirData = Daemon().request_AttachMPIR(launcherPath.get(), srunPid);
        Daemon().request_ReleaseMPIR(mpirData.mpir_id);
        return SrunInfo { mpirData.job_id, mpirData.step_id };
    } else {
        throw std::runtime_error("Failed to find launcher in path: " + getLauncherName());
    }
}
