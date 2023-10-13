/******************************************************************************\
 * Frontend.cpp - SLURM specific frontend library functions.
 *
 * Copyright 2014-2020 Hewlett Packard Enterprise Development LP.
 * SPDX-License-Identifier: Linux-OpenIB
 ******************************************************************************/

// This pulls in config.h
#include "cti_defs.h"
#include "cti_argv_defs.hpp"

#include <algorithm>
#include <iomanip>
#include <filesystem>
#include <regex>
#include <functional>
#include <numeric>

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

// Use squeue to check if job is registered with slurmd
static bool job_registered(std::string const& jobId)
{
    auto squeueArgv = cti::ManagedArgv{"squeue", "--job", jobId};
    return cti::Execvp::runExitStatus("squeue", (char* const*)squeueArgv.get()) == 0;
}

// Use squeue to check if job has a step 0 in started state. We assume that the
// job is already registered and treat an invalid jobId as an error instead of
// waiting for it to appear.
static bool job_step_zero_started(std::string const& jobId)
{
    auto squeueArgv = cti::ManagedArgv{"squeue"};

    // Print step info
    squeueArgv.add("--steps");

    // The step state truncated to 7 characters, followed by '|', followed by the step id.
    // This option is case sensitive and the capital F is required for the long form format.
    squeueArgv.add("--Format=StepState:7|,StepID");

    // Add job ID
    squeueArgv.add(std::string{"--jobs="} + jobId);

    // Don't print header
    squeueArgv.add("--noheader");

    // Run squeue
    auto squeueOutput = cti::Execvp{"squeue", (char* const*)squeueArgv.get(),
        cti::Execvp::stderr::Ignore};

    // Read squeue output
    auto& squeueStream = squeueOutput.stream();
    auto squeueLine = std::string{};
    auto gotOutput = false;

    // Search for "RUNNING|<jobid>.0"
    auto needle = std::string{"RUNNING|"} + jobId + ".0";

    while (std::getline(squeueStream, squeueLine)) {
        gotOutput = true;
        // squeue will add extra whitespace.
        if (squeueLine.rfind(needle, 0) == 0) break;
    }

    // Consume rest of squeue output
    squeueStream.ignore(std::numeric_limits<std::streamsize>::max());

    // Check for fatal errors

    if (const auto exitStatus = squeueOutput.getExitStatus(); exitStatus != 0) {
        throw std::runtime_error("checking status of job step " + jobId + ".0 failed using command:\n"
            + squeueArgv.string() + "\n" + "(bad exit status: " + std::to_string(exitStatus) +")\n");
    }

    // Passing --steps to squeue changes its API. It doesn't check if the job id
    // passed with --jobs is valid. With an invalid job id, it will just produce
    // empty output and a successful exit code. This means that we can't assume
    // the exit status check above will catch an invalid job id.
    //
    // On the other hand, it's possible for a job id to be valid but not have
    // any steps yet (waiting in queue), which also produces empty output. So we
    // have to do another squeue call (job_registered) to distinguish the "job
    // id is invalid" empty output case from the "job id is valid but waiting"
    // empty output case.
    if (!gotOutput && !job_registered(jobId)) {
        throw std::runtime_error("checking status of job step " + jobId + ".0 failed using command:\n"
            + squeueArgv.string() + "\n" + "(job id is invalid)\n");
    }

    return squeueLine.rfind(needle, 0) == 0;
}

// Wait forever for `func`to return true. If `func` ever throws an exception,
// stop waiting and rethrow it.
template <typename Func>
static void wait_forever_for_application_state(Func&& func, std::string const& jobId)
{
    // Wait forever for application state
    while (true) {
        Frontend::inst().writeLog("Slurm job %s submitted, waiting forever for Slurm application "
            "to launch...\n", jobId.c_str());

        try {
            // Check if Slurm job has entered desired state
            if (func(jobId)) {
                Frontend::inst().writeLog("Successfully launched Slurm application %s\n",
                    jobId.c_str());
                return;
            }
        } catch (std::exception const& ex) {
            Frontend::inst().writeLog("State check failed: %s, bailing out.\n", ex.what());
            throw;
        }

        ::sleep(1);
    }
}

// If the application state isn't arrived at within a few seconds, this will throw a
// std::runtime_error.
template <typename Func>
static void wait_briefly_for_application_state(Func&& func, std::string const& jobId)
{
    // Wait until Slurm application has started
    int retry = 0;
    int wait_seconds = 1;
    int max_retry = 3;
    while (retry < max_retry) {
        Frontend::inst().writeLog("Slurm job %s submitted, waiting for Slurm application "
            "to launch (attempt %d/%d)\n", jobId.c_str(), retry + 1, max_retry);

        try {

            // Check if Slurm job has entered desired state
            if (func(jobId)) {
                Frontend::inst().writeLog("Successfully launched Slurm application %s\n",
                    jobId.c_str());

                return;
            }

        } catch (std::exception const& ex) {
            Frontend::inst().writeLog("State check failed: %s, %s\n",
                ex.what(), (retry + 1 < max_retry) ? "retrying" : "giving up");
        }

        // Application not in state yet
        ::sleep(wait_seconds);
        retry++;
        wait_seconds *= 2;
    }

    throw std::runtime_error("Timed out waiting for Slurm application to launch. "
        "Application may still be waiting for job resources (check using `squeue -j "
        + jobId + "`). Once launched, job can be attached using its job ID");
}

// Wait for Slurm job to register with central daemon as a valid job. Throws
// std::runtime_error after a few seconds of waiting. Note that `jobId` might be provided
// as <jobId> or <jobId>.<stepId>.
static void wait_briefly_for_application_registered(std::string const& jobId)
{
    wait_briefly_for_application_state(job_registered, jobId);
}

static void wait_forever_for_application_started(std::string const& jobId)
{
    wait_forever_for_application_state(job_step_zero_started, jobId);
}

// Heterogeneous jobs have multiple job IDs that include a hetjob offset
// Get all real job IDs associated with the provided ID
static std::vector<SLURMFrontend::HetJobId>
get_all_job_ids(uint32_t job_id, uint32_t step_id)
{
    auto result = std::vector<SLURMFrontend::HetJobId>{};

    // Query all job / step IDs associated with this job ID
    auto jobId = std::to_string(job_id) + "." + std::to_string(step_id);
    auto squeueArgv = cti::ManagedArgv{SQUEUE, "-h", "-o", "%i", "--job", jobId.c_str()};

    // Create squeue output stream
    auto squeueOutput = cti::Execvp(SQUEUE, squeueArgv.get(), cti::Execvp::stderr::Ignore);
    auto& squeueStream = squeueOutput.stream();

    // Read all IDs output by squeue
    auto squeueLine = std::string{};
    while (std::getline(squeueStream, squeueLine)) {

        // Convert possible heterogeneous job ID to regular job ID
        auto [baseId, hetjobOffset] = cti::split::string<2>(squeueLine, '+');

        try {

            // Job ID contained hetjob offset
            if (!hetjobOffset.empty()) {

                // Parse ID and offset
                auto base_id = (uint32_t)std::stoul(baseId);
                auto offset = (uint32_t)std::stoul(hetjobOffset);

                // Add offset job ID to result list
                result.push_back( SLURMFrontend::HetJobId
                    { .job_id = base_id
                    , .step_id = step_id
                    , .het_offset = offset
                });

            // Nonheterogeneous job ID
            } else {

                // Parse ID and offset
                auto base_id = (uint32_t)std::stoul(squeueLine);

                // Add job ID to result list
                result.push_back( SLURMFrontend::HetJobId
                    { .job_id = base_id
                    , .step_id = step_id
                });
            }

        } catch (std::exception const&) {
            throw std::runtime_error("Failed to parse job ID " + squeueLine + " from squeue " + jobId);
        }
    }

    // wait for squeue to complete
    if (squeueOutput.getExitStatus()) {
        throw std::runtime_error("squeue " + jobId + " failed");
    }

    return result;
}

/* constructors / destructors */

SLURMApp::SLURMApp(SLURMFrontend& fe, FE_daemon::MPIRResult&& mpirData)
    : App{fe, mpirData.mpir_id}
    , m_jobId           { (uint32_t)std::stoi(fe.Daemon().request_ReadStringMPIR(m_daemonAppId, "totalview_jobid")) }
    , m_stepId          { (uint32_t)std::stoi(fe.Daemon().request_ReadStringMPIR(m_daemonAppId, "totalview_stepid")) }
    , m_binaryRankMap   { std::move(mpirData.binaryRankMap) }
    , m_allJobIds       { get_all_job_ids(m_jobId, m_stepId) }
    , m_stepLayout      { fe.fetchStepLayout(m_allJobIds) }
    , m_beDaemonSent    { false }
    , m_gresSetting {}

    , m_toolPath    { SLURM_TOOL_DIR }
    , m_attribsPath { cti::cstr::asprintf(SLURM_CRAY_DIR, SLURM_APID(m_jobId, m_stepId)) }
    , m_stagePath   { cti::cstr::mkdtemp(std::string{m_frontend.getCfgDir() + "/" + SLURM_STAGE_DIR}) }
    , m_extraFiles  { fe.createNodeLayoutFile(m_stepLayout, m_stagePath) }

{
    // Ensure there are running nodes in the job.
    for (auto&& [jobId, layout] : m_stepLayout) {
        if (layout.nodes.empty()) {
            throw std::runtime_error("Application " + jobId + " does not have any nodes.");
        }
    }

    // Ensure application has been registered with daemon
    if (!m_daemonAppId) {
        throw std::runtime_error("tried to create app with invalid daemon id: " + std::to_string(m_daemonAppId));
    }

    // Remap proctable if backend wrapper binary was specified in the environment
    if (auto const wrapper_binary = ::getenv(CTI_BACKEND_WRAPPER_ENV_VAR)) {
        mpirData.proctable = reparentProctable(mpirData.proctable, wrapper_binary);

        writeLog("Reparented proctable:\n");
        for (size_t i = 0; i < mpirData.proctable.size(); i++) {
            auto&& [pid, hostname, executable] = mpirData.proctable[i];
            writeLog("[%zu] %d %s %s\n", i, pid, hostname.c_str(), executable.c_str());
        }

        m_binaryRankMap = generateBinaryRankMap(mpirData.proctable);
    }

    // If an active MPIR session was provided, extract the MPIR ProcTable and write the PID List File.

    // FIXME: When/if pmi_attribs get fixed for the slurm startup barrier, this
    // call can be removed. Right now the pmi_attribs file is created in the pmi
    // ctor, which is called after the slurm startup barrier, meaning it will not
    // yet be created when launching. So we need to send over a file containing
    // the information to the compute nodes.
    m_extraFiles.push_back(fe.createPIDListFile(mpirData.proctable, m_stagePath));

    // Wait for application to be registered with Slurmd
    // This may be initially false while srun has just launched the application during
    // multiple concurrent launches
    wait_briefly_for_application_registered(getJobId());
}

SLURMApp::~SLURMApp()
{
    if (!Frontend::isOriginalInstance()) {
        writeLog("~SLURMApp: forked PID %d exiting without cleanup\n", getpid());
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
        writeLog("~SLURMApp: %s\n", ex.what());
    }
}

/* app instance creation */

static FE_daemon::MPIRResult sattachMPIR(SLURMFrontend& fe, uint32_t job_id, uint32_t step_id)
{
    auto result = FE_daemon::MPIRResult
        { .mpir_id = -1
        , .launcher_pid = -1
    };

    // Get all real job and step IDs
    auto jobIds = get_all_job_ids(job_id, step_id);

    for (auto&& hetJobId : jobIds) {

        auto sattachArgv = cti::OutgoingArgv<SattachArgv>(SATTACH);
        sattachArgv.add(SattachArgv::Argument("-Q"));
        sattachArgv.add(SattachArgv::Argument(hetJobId.get_sattach_id()));

        // get path to SATTACH binary for MPIR control
        if (auto const sattachPath = cti::take_pointer_ownership(_cti_pathFind(SATTACH, nullptr), std::free)) {
            try {

                // request an MPIR session to extract proctable
                auto mpirResult = fe.Daemon().request_LaunchMPIR(
                    sattachPath.get(), sattachArgv.get(), -1, -1, -1, nullptr);

                // Add to result table
                if (result.mpir_id < 0) {
                    result.mpir_id = mpirResult.mpir_id;
                } else {
                    fe.Daemon().request_ReleaseMPIR(mpirResult.mpir_id);
                }
                if (result.launcher_pid < 0) {
                    result.launcher_pid = mpirResult.launcher_pid;
                }
                result.proctable.reserve(result.proctable.size() + mpirResult.proctable.size());
                std::move(mpirResult.proctable.begin(), mpirResult.proctable.end(),
                    std::back_inserter(result.proctable));
                for (auto&& [binary, srcRanks] : mpirResult.binaryRankMap) {
                    auto dstRanks = result.binaryRankMap[binary];
                    dstRanks.reserve(dstRanks.size() + srcRanks.size());
                    std::move(srcRanks.begin(), srcRanks.end(), std::back_inserter(dstRanks));
                }

            } catch (std::exception const& ex) {
                throw std::runtime_error("Failed to attach to job using SATTACH. Try running `"
                    SATTACH " -Q " + std::to_string(job_id) + "." + std::to_string(step_id) + "`");
            }
        } else {
            throw std::runtime_error("Failed to find SATTACH in path");
        }
    }

    return result;
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

size_t
SLURMApp::getNumPEs() const
{
    return std::accumulate(m_stepLayout.begin(), m_stepLayout.end(), 0,
        [](size_t sum, auto const& idLayoutPair) { return sum + idLayoutPair.second.numPEs; });
}

size_t
SLURMApp::getNumHosts() const
{
    return std::accumulate(m_stepLayout.begin(), m_stepLayout.end(), 0,
        [](size_t sum, auto const& idLayoutPair) { return sum + idLayoutPair.second.nodes.size(); });
}

std::vector<std::string>
SLURMApp::getHostnameList() const
{
    std::vector<std::string> result;
    // extract hostnames from each NodeLayout
    for (auto&& [id, layout] : m_stepLayout) {
        std::transform(layout.nodes.begin(), layout.nodes.end(), std::back_inserter(result),
            [](SLURMFrontend::NodeLayout const& node) { return node.hostname; });
    }
    return result;
}

std::vector<CTIHost>
SLURMApp::getHostsPlacement() const
{
    std::vector<CTIHost> result;
    // construct a CTIHost from each NodeLayout
    for (auto&& [id, layout] : m_stepLayout) {
        std::transform(layout.nodes.begin(), layout.nodes.end(), std::back_inserter(result),
            [](SLURMFrontend::NodeLayout const& node) {
                return CTIHost{node.hostname, node.numPEs};
            });
    }
    return result;
}

std::map<std::string, std::vector<int>>
SLURMApp::getBinaryRankMap() const
{
    return m_binaryRankMap;
}

/* running app interaction interface */

void SLURMApp::releaseBarrier()
{
    // release MPIR barrier
    m_frontend.Daemon().request_ReleaseMPIR(m_daemonAppId);
}

void
SLURMApp::shipDaemon()
{
    // Get the location of the backend daemon
    if (m_frontend.getBEDaemonPath().empty()) {
        throw std::runtime_error("Unable to locate backend daemon binary. Load the \
system default CTI module with `module load cray-cti`, or set the \
environment variable " CTI_BASE_DIR_ENV_VAR " to the CTI install location.");
    }

    // Copy the BE binary to its unique storage name
    auto const sourcePath = m_frontend.getBEDaemonPath();
    auto const destinationPath = m_frontend.getCfgDir() + "/" + getBEDaemonName();
    std::filesystem::copy_file(sourcePath, destinationPath,
        std::filesystem::copy_options::overwrite_existing);

    // Ship the unique backend daemon
    shipPackage(destinationPath);
    // set transfer to true
    m_beDaemonSent = true;
}

cti::ManagedArgv SLURMApp::generateDaemonLauncherArgv(char const* const* launcher_args)
{
    auto result = cti::ManagedArgv{};

    auto& slurmFrontend = dynamic_cast<SLURMFrontend&>(m_frontend);

    // Start adding the args to the launcher argv array
    result.add(slurmFrontend.getLauncherName());

    // For each job ID, add 
    // --jobid=<job_id> --mem-per-cpu=0 --mem_bind=no
    // --cpu_bind=no --share --ntasks-per-node=1 --nodes=<numNodes>
    // --nodelist=<host1,host2,...> --disable-status --quiet --mpi=none
    // --input=none --output=none --error=none <tool daemon> <args>

    auto first = true;
    for (auto&& [id, layout] : m_stepLayout) {

        // Hetjob launch separator
        if (first) {
            first = false;
        } else {
            result.add(":");
        }

        result.add("--jobid=" + id);
        result.add("--nodes=" + std::to_string(layout.nodes.size()));
        if (!m_gresSetting.empty()) {
            result.add("--gres=" + m_gresSetting);
        }

        for (auto&& arg : slurmFrontend.getSrunDaemonArgs()) {
            result.add(arg);
        }

        // Add any extra from environment
        if (auto slurm_daemon_args = getenv(SLURM_DAEMON_ARGS_ENV_VAR)) {
            SLURMFrontend::detail::add_quoted_args(result, slurm_daemon_args);
        }

        // create the hostlist by concatenating all hostnames
        auto hostlist = std::string{};
        bool firstHost = true;
        for (auto const& node : layout.nodes) {
            hostlist += (firstHost ? "" : ",") + node.hostname;
            firstHost = false;
        }
        result.add("--nodelist=" + hostlist);

        // Add the supplied launcher arguments
        for (auto arg = launcher_args; *arg != nullptr; arg++) {
            result.add(*arg);
        }
    }

    return result;
}

void SLURMApp::kill(int signum)
{
    if (!dynamic_cast<SLURMFrontend&>(m_frontend).getCheckScancelOutput()) {

        // create the args for scancel
        auto scancelArgv = cti::ManagedArgv
            { SCANCEL
            , "-Q" // Quiet
            , "-s", std::to_string(signum) // Signal number
            , getJobId()
        };

        if (!m_frontend.Daemon().request_ForkExecvpUtil_Sync(
            m_daemonAppId, SCANCEL, scancelArgv.get(),
            -1, -1, -1,
            nullptr)) {
            throw std::runtime_error("failed to send signal to job ID " + getJobId());
        }

    // Check verbose output from scancel to work around PE-45572
    } else {

        // create the args for scancel
        auto scancelArgv = cti::ManagedArgv
            { SCANCEL
            , "-v" // Verbose output to check that signal was sent
            , "-s", std::to_string(signum) // Signal number
            , getJobId()
        };

        // Set up pipe to read scancel output
        auto stderrPipe = cti::Pipe{};
        auto stderrPipeBuf = cti::FdBuf{stderrPipe.getReadFd()};
        auto stderrStream = std::istream{&stderrPipeBuf};

        // Request daemon launch scancel
        m_frontend.Daemon().request_ForkExecvpUtil_Async(
            m_daemonAppId, SCANCEL, scancelArgv.get(),
            -1, -1, stderrPipe.getWriteFd(),
            nullptr);

        // Match line "Signal <sig> to step <jobid>"
        auto scancel_succeeded = false;
        static const auto signalSentRegex = std::regex{
            R"((Signal [[:digit:]]+ to step)|(Terminating step))"};

        // Parse scancel output
        stderrPipe.closeWrite();
        auto line = std::string{};
        while (std::getline(stderrStream, line)) {
            if (std::regex_search(line, signalSentRegex)) {
                scancel_succeeded = true;
                break;
            }
        }

        // Consume rest of squeue output
        stderrStream.ignore(std::numeric_limits<std::streamsize>::max());
        stderrPipe.closeRead();

        if (!scancel_succeeded) {
            throw std::runtime_error("failed to send signal to job ID " + getJobId());
        }
    }
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

    auto packageName = cti::cstr::basename(tarPath);
    sbcastArgv.add(std::string(SLURM_TOOL_DIR) + "/" + packageName);

    // Add environment setting to disable library detection
    // Sbcast starting in Slurm 22.05 will fail to ship non-executable if site enables
    // send-libs in global configuration (SchedMD bug 15132)
    char const* sbcast_env[] = {"SBCAST_SEND_LIBS=no", nullptr};

    // now ship the tarball to the compute nodes. tell overwatch to launch sbcast, wait to complete
    writeLog("starting sbcast invocation\n");

    auto success = m_frontend.Daemon().request_ForkExecvpUtil_Sync(
        m_daemonAppId, SBCAST, sbcastArgv.get(),
        -1, -1, -1,
        sbcast_env);

    // sbcast can randomly fail under high load, so try again a few times if it does.
    for (auto tries_left = 2; !success && tries_left > 0; tries_left--) {
        writeLog("sbcast failed, trying again (%d)\n", tries_left);

        ::sleep(1);

        success = m_frontend.Daemon().request_ForkExecvpUtil_Sync(
            m_daemonAppId, SBCAST, sbcastArgv.get(),
            -1, -1, -1,
            sbcast_env);
    }

    if (!success) {
        throw std::runtime_error("sbcast failure: Failed to ship " + tarPath + " package to compute node");
    }

    writeLog("sbcast invocation completed\n");
}

MPIRProctable SLURMApp::reparentProctable(MPIRProctable const& procTable,
    std::string const& wrapperBinary)
{
    // Run first child utility on each supplied PID on remote host
    auto getFirstChildInformation = [this](std::string const& reparentUtilityPath,
        std::string const& hostname, std::set<pid_t> const& pids) {

        // Start adding the args to the launcher argv array
        auto& slurmFrontend = dynamic_cast<SLURMFrontend&>(m_frontend);

        auto launcherArgv = cti::ManagedArgv{slurmFrontend.getLauncherName()};

        auto first = true;
        for (auto&& [id, layout] : m_stepLayout) {

            // Hetjob launch separator
            if (first) {
                first = false;
            } else {
                launcherArgv.add(":");
            }

            launcherArgv.add("--jobid=" + id);
            launcherArgv.add("--nodes=" + std::to_string(layout.nodes.size()));
            launcherArgv.add("--nodelist=" + hostname);

            // Add daemon launch arguments, except for output redirection
            for (auto&& arg : slurmFrontend.getSrunDaemonArgs()) {
                if (arg != "--output=none") {
                    launcherArgv.add(arg);
                }
            }

            // Add utility command and each PID
            launcherArgv.add(reparentUtilityPath);
            for (auto&& pid : pids) {
                launcherArgv.add(std::to_string(pid));
            }
        }

        // Build environment from blacklist
        cti::ManagedArgv launcherEnv;
        for (auto&& envVar : slurmFrontend.getSrunEnvBlacklist()) {
            launcherEnv.add(envVar + "=");
        }

        // Capture lines of output from srun
        auto outputPipe = cti::Pipe{};
        auto outputPipeBuf = cti::FdBuf{outputPipe.getReadFd()};
        auto outputStream = std::istream{&outputPipeBuf};

        // Tell FE Daemon to launch srun
        m_frontend.Daemon().request_ForkExecvpUtil_Async(
            m_daemonAppId, dynamic_cast<SLURMFrontend&>(m_frontend).getLauncherName().c_str(),
            launcherArgv.get(),
            ::open("/dev/null", O_RDONLY), outputPipe.getWriteFd(), ::open("/dev/null", O_WRONLY),
            launcherEnv.get() );
        outputPipe.closeWrite();

        // Read and store output from remote tool launch
        auto result = std::vector<std::tuple<pid_t, pid_t, std::string>>{};
        auto line = std::string{};
        while (true) {

            // Read PID and executable lines
            try {

                // An empty PID line will terminate the loop
                if (!std::getline(outputStream, line) || (line.empty())) { break; }
                auto pid = std::stoi(line);

                // Child and executable PIDs can be blank if they were not able to be detected
                if (!std::getline(outputStream, line)) { break; }
                auto child_pid = std::stoi(line);
                if (!std::getline(outputStream, line)) { break; }
                result.emplace_back(pid, child_pid, std::move(line));

            } catch (std::exception const& ex) {
                // Continue with reading output if there was a parse failure
                writeLog("failed to parse reparenting utility output: %s\n", line.c_str());
                continue;
            }
        }
        outputPipe.closeRead();

        return result;
    };

    // Copy proctable, will be modifying entries containing the wrapped executable
    auto result = MPIRProctable{procTable};

    // Ship reparenting utility
    auto const sourcePath = m_frontend.getBaseDir() + "/libexec/" CTI_FIRST_SUBPROCESS_BINARY;
    auto const destinationPath = std::string(SLURM_TOOL_DIR) + "/" CTI_FIRST_SUBPROCESS_BINARY;
    std::filesystem::copy_file(sourcePath, destinationPath,
        std::filesystem::copy_options::overwrite_existing);
    shipPackage(destinationPath);

    // Map hostname to wrapped PIDs on that host
    auto hostSingularityMap = std::map<std::string, std::set<pid_t>>{};
    for (auto&& [pid, hostname, executable] : procTable) {
        if (executable == wrapperBinary) {
            hostSingularityMap[hostname].insert(pid);
        }
    }
    for (auto&& [hostname, pids] : hostSingularityMap) {
        writeLog("%s has %lu wrapped pids\n", hostname.c_str(), pids.size());
    }

    // Map wrapper executable instance to child PID / executable information
    // Wrapper entries in the proctable will be replaced by its first child
    using HostnamePidPair = std::pair<std::string, pid_t>;
    using PidExecutablePair = std::pair<pid_t, std::string>;
    auto singularityChildMap = std::map<HostnamePidPair, PidExecutablePair>{};

    // Query wrappers' child information on each host
    for (auto&& [hostname, pids] : hostSingularityMap) {
        writeLog("Querying %lu PIDs on %s\n", pids.size(), hostname.c_str());

        auto pidExeMappings = getFirstChildInformation(destinationPath, hostname, pids);
        for (auto&& [pid, child_pid, executable] : pidExeMappings) {
            singularityChildMap[{hostname, pid}] = {child_pid, std::move(executable)};
        }
    }

    // Replace proctable entries of wrapped binaries
    for (auto&& [pid, hostname, executable] : result) {
        writeLog("Processing line %d %s %s\n", pid, hostname.c_str(), executable.c_str());

        // If child PID was found, replace wrapper with child
        auto pidExeIter = singularityChildMap.find({hostname, pid});
        if (pidExeIter != singularityChildMap.end()) {
            std::tie(pid, executable) = std::move(pidExeIter->second);
        }
    }

    return result;
}

void SLURMApp::startDaemon(const char* const args[], bool synchronous)
{
    // sanity check
    if (args == nullptr) {
        throw std::runtime_error("args array is null!");
    }

    // Send daemon if not already shipped
    if (!m_beDaemonSent) {
        shipDaemon();
    }

    // Build daemon launcher arguments
    auto launcherArgs = cti::ManagedArgv{};
    launcherArgs.add("--output=none"); // Suppress tool output

    // Use container instance if provided
    if (auto container_instance = ::getenv(CTI_CONTAINER_INSTANCE_ENV_VAR)) {
        launcherArgs.add("singularity");
        launcherArgs.add("exec");
        launcherArgs.add(container_instance);
    }

    launcherArgs.add(m_toolPath + "/" + getBEDaemonName());

    // merge in the args array if there is one
    if (args != nullptr) {
        for (const char* const* arg = args; *arg != nullptr; arg++) {
            launcherArgs.add(*arg);
        }
    }

    // Generate the final launcher argv array
    auto launcherArgv = generateDaemonLauncherArgv(launcherArgs.get());

    // build environment from blacklist
    auto& slurmFrontend = dynamic_cast<SLURMFrontend&>(m_frontend);
    auto launcherEnv = cti::ManagedArgv{};
    for (auto&& envVar : slurmFrontend.getSrunEnvBlacklist()) {
        launcherEnv.add(envVar + "=");
    }

    // tell FE Daemon to launch srun
    auto fork_execvp_args = std::make_tuple(&m_frontend.Daemon(),
        m_daemonAppId, slurmFrontend.getLauncherName().c_str(),
        launcherArgv.get(),
        // redirect stdin / stderr / stdout
        ::open("/dev/null", O_RDONLY), ::open("/dev/null", O_WRONLY), ::open("/dev/null", O_WRONLY),
        launcherEnv.get());

    if (synchronous) {
        std::apply(std::mem_fn(&FE_daemon::request_ForkExecvpUtil_Sync),
            fork_execvp_args);
    } else {
        std::apply(std::mem_fn(&FE_daemon::request_ForkExecvpUtil_Async),
            fork_execvp_args);
    }
}

std::set<std::string>
SLURMApp::checkFilesExist(std::set<std::string> const& paths)
{
    auto result = std::set<std::string>{};

    // Send daemon if not already shipped
    if (!m_beDaemonSent) {
        shipDaemon();
    }

    // Build daemon launcher arguments
    auto launcherArgs = cti::ManagedArgv{};
    launcherArgs.add(m_toolPath + "/" + getBEDaemonName());
    for (auto&& path : paths) {
        launcherArgs.add("--file=" + path);
    }

    // Generate the final launcher argv array
    auto launcherArgv = generateDaemonLauncherArgv(launcherArgs.get());

    auto stdoutPipe = cti::Pipe{};

    // Tell FE Daemon to launch srun
    m_frontend.Daemon().request_ForkExecvpUtil_Async(
        m_daemonAppId, dynamic_cast<SLURMFrontend&>(m_frontend).getLauncherName().c_str(),
        launcherArgv.get(),
        // redirect stdin / stderr / stdout
        ::open("/dev/null", O_RDONLY), stdoutPipe.getWriteFd(), ::open("/dev/null", O_WRONLY),
        {});

    stdoutPipe.closeWrite();
    auto stdoutBuf = cti::FdBuf{stdoutPipe.getReadFd()};
    auto stdoutStream = std::istream{&stdoutBuf};

    // Track number of present files
    auto num_nodes = getNumHosts();
    auto pathCountMap = std::map<std::string, size_t>{};

    // Read out all paths from daemon
    auto exit_count = num_nodes;
    auto line = std::string{};
    while ((exit_count > 0) && std::getline(stdoutStream, line)) {

        // Daemons will print an empty line when output is completed
        if (line.empty()) {
            exit_count--;

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

/* SLURM frontend implementation */

static auto getSlurmVersion()
{
    char const* const srunVersionArgv[] = {"srun", "--version", nullptr};
    auto srunVersionOutput = cti::Execvp{"srun", (char* const*)srunVersionArgv, cti::Execvp::stderr::Ignore};

    // slurm major.minor.patch
    auto slurmVersion = std::string{};
    if (!std::getline(srunVersionOutput.stream(), slurmVersion, '\n')) {
        throw std::runtime_error("Failed to get SRUN version number output. Try running \
`srun --version`");
    }

    // major.minor.patch
    slurmVersion = slurmVersion.substr(slurmVersion.find(" ") + 1);
    auto const [major, minor, patch] = cti::split::string<3>(slurmVersion, '.');

    auto stoi_or_zero = [](std::string const& str) {
        if (str.empty()) { return 0; }
        try {
            return std::stoi(str);
        } catch (...) {
            return 0;
        }
    };

    // Fail if at least major version could not be determined
    if (auto parsed_major = stoi_or_zero(major)) {
        return std::make_tuple(parsed_major, stoi_or_zero(minor), stoi_or_zero(patch));

    } else {
        throw std::runtime_error("Failed to parse SRUN version '"
            + slurmVersion + "'. Try running `srun --version`");
    }
}

std::string SLURMFrontend::detail::get_gres_setting(char const* const* launcher_argv)
{
    // Slurm bug https://bugs.schedmd.com/show_bug.cgi?id=12642 breaks gres=none setting
    // Allow user to specify or clear this argument via environment variable
    if (auto const slurm_gres = ::getenv(SLURM_DAEMON_GRES_ENV_VAR)) {
        return slurm_gres;
    }

    // Inherit GPU GRES setting if provided
    for (auto&& arg = launcher_argv; *arg != nullptr; arg++) {
        if (::strncmp(*arg, "--gres", 6) == 0) {

            // Get the inherited GRES setting
            auto gresSetting = [](auto&& arg_ptr) {

                // --gres setting
                auto separate_arg = ((*arg_ptr)[6] == '\0');
                auto has_next_arg = (*(arg_ptr + 1) != nullptr);
                if (separate_arg && has_next_arg) {
                    return std::string{*(arg_ptr + 1)};
                }

                // --gres=setting
                auto equals_arg = ((*arg_ptr)[6] == '=');
                if (equals_arg) {
                    return std::string{(*arg_ptr) + 7};
                }

                return std::string{};
            }(arg);

            // Inherit GPU setting
            while (!gresSetting.empty()) {

                // Search GRES for GPU
                auto&& [head, tail] = cti::split::string<2>(std::move(gresSetting), ',');

                // Search for GPU GRES setting
                if (head.rfind("gpu:", 0) == 0) {
                    return head;
                }

                // Try next part of GRES
                gresSetting = std::move(tail);
            }
        }
    }

    // If GRES argument is not specified, use gres=none
    return "none";
}

SLURMFrontend::SLURMFrontend()
    : m_srunAppArgs {}
    , m_srunDaemonArgs
        { "--mem-per-cpu=0"
        , "--ntasks-per-node=1"
        , "--disable-status"
        , "--quiet"
        , "--mpi=none"
        , "--error=none"
        }
    , m_checkScancelOutput{false}
    {

    // Detect SLURM version and set SRUN arguments accordingly
    { auto const [major, minor, patch] = getSlurmVersion();

        if (major <= 18) {
            m_srunDaemonArgs.insert(m_srunDaemonArgs.end(),
                { "--mem_bind=no"
                , "--cpu_bind=no"
                , "--share"
            });
        } else if (major >= 19) {
            m_srunDaemonArgs.insert(m_srunDaemonArgs.end(),
                { "--mem-bind=no"
                , "--cpu-bind=no"
                , "--oversubscribe"
            });
        }

        // Starting in 20.11, --exclusive is default and must be
        // reversed with --overlap
        if (((major == 20) && (minor >= 11)) || (major > 20)) {
            m_srunDaemonArgs.insert(m_srunDaemonArgs.end(),
                { "--overlap"
            });
        }

        // Up to (and possibly beyond) Slurm 22.05.8, scancel might report that
        // it failed to send a signal even if it was successful. This can be
        // correctly determined via parsing verbose output rather than using
        // return code (PE-45772, see SLURMApp::kill implementation). This bug
        // most commonly happens when a CTI app is run while inside an
        // interactive allocation (salloc). See https://bugs.schedmd.com/show_bug.cgi?id=16551
        //
        // We currently don't know if/when this bug is going to be fixed, so we are
        // leaving the workaround on for all versions, for now. Set
        // SLURM_NEVER_PARSE_SCANCEL to force the workaround off and rely on scancel's
        // return code.
        m_checkScancelOutput = ::getenv(SLURM_NEVER_PARSE_SCANCEL) == nullptr;
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

std::weak_ptr<App>
SLURMFrontend::launch(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
    CStr inputFile, CStr chdirPath, CArgArray env_list)
{
    // Slurm calls the launch barrier correctly even when the program is not an MPI application.
    // Delegating to barrier implementation works properly even for serial applications.
    auto appPtr = std::make_shared<SLURMApp>(*this,
        launchApp(launcher_argv, inputFile, stdout_fd, stderr_fd, chdirPath, env_list));

    // Set tool daemon GRES based on launcher argument
    appPtr->setGres(detail::get_gres_setting(launcher_argv));

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
    auto appPtr = std::make_shared<SLURMApp>(*this,
        launchApp(launcher_argv, inputFile, stdout_fd, stderr_fd, chdirPath, env_list));

    // Set tool daemon GRES based on launcher argument
    appPtr->setGres(detail::get_gres_setting(launcher_argv));

    // Register with frontend application set
    auto resultInsertedPair = m_apps.emplace(std::move(appPtr));
    if (!resultInsertedPair.second) {
        throw std::runtime_error("Failed to insert new App object.");
    }

    return *resultInsertedPair.first;
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
        throw std::runtime_error("Failed to insert new SLURMApp.");
    }
    return *ret.first;
}

std::string
SLURMFrontend::getLauncherName()
{
    // Cache the launcher name result.
    auto static launcherName = std::string{cti::getenvOrDefault(CTI_LAUNCHER_NAME_ENV_VAR, SRUN)};
    return launcherName;
}

// For heterogeneous jobs, this is invoked multiple times. Each invocation will start
// the PE number at 0. Track the proper PE number and pass it as `pe_offset`
static auto
getStepLayout(std::string const& jobId, size_t pe_offset)
{
    auto result = SLURMFrontend::StepLayout{};

    // create sattach instance
    cti::OutgoingArgv<SattachArgv> sattachArgv(SATTACH);
    sattachArgv.add(SattachArgv::DisplayLayout);
    sattachArgv.add(SattachArgv::Argument("-Q"));
    sattachArgv.add(SattachArgv::Argument(jobId));

    // create sattach output capture object
    cti::Execvp sattachOutput(SATTACH, sattachArgv.get(), cti::Execvp::stderr::Pipe);
    auto& sattachStream = sattachOutput.stream();
    std::string sattachLine;

    // start parsing sattach output

    // "Job step layout:"
    if (std::getline(sattachStream, sattachLine)) {
        if (sattachLine.compare("Job step layout:")) {
            throw std::runtime_error(
                "Unexpected layout output from SATTACH: '" + sattachLine + "'. "
                "Try running `" SATTACH " --layout " + jobId + "`");
        }
    } else {
        throw std::runtime_error(
            "End of layout output from SATTACH (expected header). "
            "Try running `" SATTACH " --layout " + jobId + "`");
    }

    auto num_nodes = int{0};

    // "  {numPEs} tasks, {num_nodes} nodes ({hostname}...)"
    if (std::getline(sattachStream, sattachLine)) {
        // split the summary line
        std::string rawNumPEs, rawNumNodes;
        std::tie(rawNumPEs, std::ignore, rawNumNodes) =
            cti::split::string<3>(cti::split::removeLeadingWhitespace(sattachLine));

        // fill out sattach layout
        result.numPEs += std::stoul(rawNumPEs);
        num_nodes = std::stoi(rawNumNodes);
        result.nodes.reserve(result.nodes.size() + num_nodes);

    } else {
        throw std::runtime_error(
            "End of layout output from SATTACH (expected summary). "
            "Try running `" SATTACH " --layout " + jobId + "`");
    }

    // seperator line
    std::getline(sattachStream, sattachLine);

    // "  Node {nodeNum} ({hostname}), {numPEs} task(s): PE_0 {PE_i }..."
    for (auto i = int{0}; std::getline(sattachStream, sattachLine); i++) {
        if (i >= num_nodes) {
            throw std::runtime_error(
                "Target job has " + std::to_string(num_nodes) + " nodes, but received "
                "extra layout information from SATTACH. "
                "Try running `" SATTACH " --layout " + jobId + "`");
        }

        // split the summary line
        std::string nodeNum, hostname, numPEs, pe_0;
        std::tie(std::ignore, nodeNum, hostname, numPEs, std::ignore, pe_0) =
            cti::split::string<6>(cti::split::removeLeadingWhitespace(sattachLine));

        // fill out node layout
        result.nodes.push_back(SLURMFrontend::NodeLayout
            { hostname.substr(1, hostname.length() - 3) // remove parens and comma from hostname
            , std::stoul(numPEs)
            , std::stoul(pe_0) + pe_offset
        });
    }

    // wait for sattach to complete
    auto const sattachCode = sattachOutput.getExitStatus();
    if (sattachCode > 0) {
        throw std::runtime_error("SATTACH failed. Try running `" SATTACH " --layout " + jobId + "`");
    }

    return result;
}

std::map<std::string, SLURMFrontend::StepLayout>
SLURMFrontend::fetchStepLayout(std::vector<HetJobId> const& jobIds)
{
    auto result = std::map<std::string, SLURMFrontend::StepLayout>{};

    auto next_pe_start = size_t{0};
    for (auto&& hetJobId : jobIds) {

        // Query sattach to get layout for job
        auto layout = getStepLayout(hetJobId.get_sattach_id(), next_pe_start);
        next_pe_start += layout.numPEs;
        result[hetJobId.get_srun_id()] = std::move(layout);
    }

    return result;
}

std::string
SLURMFrontend::createNodeLayoutFile(std::map<std::string, StepLayout> const& idLayouts,
    std::string const& stagePath)
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

    // Get total number of nodes across all jobs
    auto total_nodes = std::accumulate(idLayouts.begin(), idLayouts.end(), 0, 
        [](size_t sum, auto const& idLayoutPair) { return sum + idLayoutPair.second.nodes.size(); });

    // Create the file path, write the file using the Step Layout
    auto const layoutPath = std::string{stagePath + "/" + SLURM_LAYOUT_FILE};
    if (auto const layoutFile = cti::file::open(layoutPath, "wb")) {

        // Write the Layout header.
        cti::file::writeT(layoutFile.get(), slurmLayoutFileHeader_t
            { .numNodes = (int)total_nodes
        });

        // Write a Layout entry using node information from each Slurm Node Layout entry.
        for (auto&& [id, layout] : idLayouts) {
            for (auto const& node : layout.nodes) {
                cti::file::writeT(layoutFile.get(), make_layoutFileEntry(node));
            }
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

// Read string from file descriptor, break if timeout is hit during read wait
static auto read_timeout(int fd, int64_t usec)
{
    auto result = std::string{};

    // File descriptor select set
    auto select_set = fd_set{};
    FD_ZERO(&select_set);
    FD_SET(fd, &select_set);

    // Set up timeout
    auto timeout = timeval
        { .tv_sec = 0
        , .tv_usec = usec
    };

    // Select loop
    while (auto select_rc = ::select(fd + 1, &select_set, nullptr, nullptr, &timeout)) {
        if (select_rc < 0) {
            break;
        }

        // Read string into buffer
        char buf[1024];
        if (auto read_rc = ::read(fd, buf, sizeof(buf) - 1)) {

            // Retry if interrupted
            if ((read_rc < 0) && (errno == EINTR)) {
                continue;

            // Bytes read, add to result
            } else if (read_rc > 0) {
                buf[read_rc] = '\0';
                result += buf;

            // Otherwise exit loop
            } else {
                break;
            }
        }
    }

    return result;
}

void SLURMFrontend::detail::add_quoted_args(cti::ManagedArgv& args, std::string const& quotedArgs)
{
    const auto view = std::string_view{quotedArgs};

    // The only escaping/special character handling we do is double
    // quotes. We want to get the arguments to the wrapper just like
    // bash would, so we don't do any fancy escaping of \n etc. here.
    bool inQuote = false;
    std::string pending;
    for (size_t i = 0; i < view.size(); i++) {
        if (std::isspace(view[i]) && !inQuote && !pending.empty()) {
            args.add(pending);
            pending.clear();
        } else if (view[i] == '\\' && i < view.size() - 1) {
            if (view[++i] == '"') {
                pending += '"';
            } else {
                pending += '\\';
                pending += view[i];
            }
        } else if (view[i] == '"') {
            inQuote = !inQuote;
        } else if (inQuote || !std::isspace(view[i])) {
            pending += view[i];
        }
    }

    if (inQuote) {
        throw std::runtime_error("Unclosed quote in " + quotedArgs);
    }

    if (!pending.empty()) args.add(pending);
}

FE_daemon::MPIRResult
SLURMFrontend::launchApp(const char * const launcher_argv[],
        const char *inputFile, int stdoutFd, int stderrFd, const char *chdirPath,
        const char * const env_list[])
{
    // Find the path to the launcher
    auto const launcherPath = cti::take_pointer_ownership(_cti_pathFind(getLauncherName().c_str(), nullptr), std::free);
    if (launcherPath == nullptr) {
        throw std::runtime_error("Failed to find launcher in path: " + getLauncherName());
    }

    // set up arguments and FDs
    if (inputFile == nullptr) { inputFile = "/dev/null"; }
    if (stdoutFd < 0) { stdoutFd = STDOUT_FILENO; }
    if (stderrFd < 0) { stderrFd = STDERR_FILENO; }
    auto const stdoutPath = "/proc/" + std::to_string(::getpid()) + "/fd/" + std::to_string(stdoutFd);
    auto const stderrPath = "/proc/" + std::to_string(::getpid()) + "/fd/" + std::to_string(stderrFd);

    // construct argv array & instance
    auto use_shim = false;
    auto launcherArgv = cti::ManagedArgv{};

    // Check for launcher wrapper
    if (auto launcherWrapper = getenv(CTI_LAUNCHER_WRAPPER_ENV_VAR)) {
        detail::add_quoted_args(launcherArgv, launcherWrapper);
        use_shim = true;
    }

    // Check for launcher script
    if (auto launcherScript = getenv(CTI_LAUNCHER_SCRIPT_ENV_VAR)) {
        // Use provided launcher script
        launcherArgv.add(launcherScript);
        use_shim = true;

    // Use normal launcher from PATH
    } else {
        launcherArgv.add(launcherPath.get());
    }

    // Construct rest of argv array
    launcherArgv.add("--input="  + std::string{inputFile});
    launcherArgv.add("--output=" + stdoutPath);
    launcherArgv.add("--error="  + stderrPath);
    for (auto&& arg : getSrunAppArgs()) {
        launcherArgv.add(arg);
    }
    for (const char* const* arg = launcher_argv; *arg != nullptr; arg++) {
        launcherArgv.add(*arg);
    }

    if (!use_shim) {

        auto result = FE_daemon::MPIRResult{};

        // Capture srun error output
        auto srunPipe = cti::Pipe{};

        try {

            // Launch program under MPIR control.
            result = Daemon().request_LaunchMPIR(
                launcherPath.get(), launcherArgv.get(),
                // Redirect stdin/out to /dev/null, use SRUN arguments for in/output instead
                // Capture stderr output in case launch fails
                ::open("/dev/null", O_RDWR), ::open("/dev/null", O_RDWR), srunPipe.getWriteFd(),
                env_list);

        } catch (std::exception const& ex) {

            // Get stderr output from srun and add to error message
            auto stderrOutput = read_timeout(srunPipe.getReadFd(), 10000);
            throw std::runtime_error(std::string{ex.what()} + "\n" + stderrOutput);
        }

        // Re-ignore srun stderr output after successful launch to avoid blockages
        if (::dup2(::open("/dev/null", O_RDWR), srunPipe.getWriteFd()) < 0) {
            fprintf(stderr, "warning: failed to ignore Slurm stderr output\n");
        }

        return result;

    // Use MPIR shim to launch program
    } else {

        // launcherPath is path of wrapper script, use sattach to find path of
        // real srun binary.
        // An alternative would be to search PATH for the first srun binary with MPIR
        // symbols.
        auto sattachPath = cti::take_pointer_ownership(_cti_pathFind("sattach", nullptr), std::free);
        if (sattachPath == nullptr) {
            throw std::runtime_error("Failed to find sattach in path: ");
        }
        auto slurmDirectory = std::filesystem::path{sattachPath.get()}.parent_path();
        auto srunPath = std::string{slurmDirectory / "srun"};

        auto const shimBinaryPath = Frontend::inst().getBaseDir() + "/libexec/" + CTI_MPIR_SHIM_BINARY;
        auto const temporaryShimBinDir = Frontend::inst().getCfgDir() + "/shim";

        // If CTI_DEBUG is enabled, show wrapper output
        auto outputFd = ::getenv(CTI_DBG_ENV_VAR) ? ::open(stderrPath.c_str(), O_RDWR) : ::open("/dev/null", O_RDWR);

        return Daemon().request_LaunchMPIRShim(
            shimBinaryPath.c_str(), temporaryShimBinDir.c_str(),
            srunPath.c_str(), launcherPath.get(), launcherArgv.get(), ::open("/dev/null", O_RDWR), outputFd, outputFd, env_list
        );
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

SrunInfo
SLURMFrontend::submitBatchScript(std::string const& scriptPath,
    char const* const* sbatch_args, char const* const* env_list)
{
    // Check for existing Slurm task prolog
    if (auto slurm_task_prolog = ::getenv("SLURM_TASK_PROLOG")) {
        throw std::runtime_error("CTI uses a task prolog to hold the launched job at "
            "startup. Slurm user task prologs are not supported with sbatch submission "
            "(SLURM_TASK_PROLOG was set to " + std::string{slurm_task_prolog} + " in "
            "the launch environment)");
    }

    Frontend::inst().writeLog("Submitting Slurm job script %s\n", scriptPath.c_str());

    // Build sbatch arguments
    auto sbatchArgv = cti::ManagedArgv{"sbatch"};
    if (sbatch_args != nullptr) {
        for (auto arg = sbatch_args; *arg != nullptr; arg++) {
            sbatchArgv.add(*arg);
        }
    }

    // Sbatch will output <jobid>; <cluster name>
    sbatchArgv.add("--parsable");

    // Add custom environment arguments
    auto jobEnvArg = std::stringstream{};

    // Add startup barrier environment setting
    auto ctiSlurmStopBinary = Frontend::inst().getBaseDir() + "/libexec/"
        + CTI_SLURM_STOP_BINARY;

    // Inherit current environment and ensure CTI_INSTALL_DIR is avaliable to stop job
    jobEnvArg << "ALL,CTI_INSTALL_DIR=" << Frontend::inst().getBaseDir() << ","
        << "SLURM_TASK_PROLOG=" << ctiSlurmStopBinary << ",";
    if (env_list != nullptr) {
        for (auto env_setting = env_list; *env_setting != nullptr; env_setting++) {
            // Escape commas in setting
            jobEnvArg << std::quoted(*env_setting, ',') << ',';
        }
    }
    sbatchArgv.add("--export");
    sbatchArgv.add(jobEnvArg.str());

    // Add script argument
    sbatchArgv.add(scriptPath);

    // Submit batch file to Slurm
    auto sbatchOutput = cti::Execvp{"sbatch", (char* const*)sbatchArgv.get(),
        cti::Execvp::stderr::Ignore};

    // Read sbatch output
    auto& sbatchStream = sbatchOutput.stream();
    auto sbatchLine = std::string{};
    auto getline_failed = false;
    if (!std::getline(sbatchStream, sbatchLine)) {
        getline_failed = true;
    }
    sbatchStream.ignore(std::numeric_limits<std::streamsize>::max());

    // Wait for completion and check exit status
    if ((sbatchOutput.getExitStatus() != 0) || getline_failed) {
        throw std::runtime_error("failed to submit Slurm job using command\n"
            + sbatchArgv.string());
    }

    // Split job ID from sbatch output
    auto [jobId, clusterName] = cti::split::string<2>(sbatchLine, ';');
    if (jobId.empty()) {
        throw std::runtime_error("Failed to extract job ID from sbatch output: "
            + sbatchLine);
    }

    // Parse job ID
    auto result = SrunInfo{ 1, 0 };
    try {
        result = SrunInfo { static_cast<uint32_t>(std::stoul(jobId)), 0 };
    } catch (std::exception const&) {
        throw std::runtime_error("Failed to parse job ID from sbatch output: "
            + jobId);
    }

    // Wait a short time for slurm to register our requested job. Job
    // registration should be more or less instant.
    wait_briefly_for_application_registered(jobId);

    // Now wait again for slurm to actually run the binary. The above wait check
    // will pass as soon as the job is queued, but we can't return at that
    // point.
    //
    // Since this function launches the job without registering it with CTI, we
    // expect the user to call registerJob to finish the registration, then
    // finally call releaseAppBarrier to start running the binary. registerJob
    // uses sattach to get MPIR info, and sattach requires a valid stepId. A
    // valid stepId won't exist until the application actually starts.
    wait_forever_for_application_started(jobId);

    return result;
}

// HPCM SLURM specializations


// Current address can now be obtained using the `cminfo` tool.
std::string
HPCMSLURMFrontend::getHostname() const
{
    static auto const nodeAddress = cti::detectHpcmAddress();

    return nodeAddress;
}
