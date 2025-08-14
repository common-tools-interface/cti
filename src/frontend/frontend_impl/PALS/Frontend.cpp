/******************************************************************************\
 * Frontend.cpp - PALS specific frontend library functions.
 *
 * Copyright 2020 Hewlett Packard Enterprise Development LP.
 * SPDX-License-Identifier: Linux-OpenIB
 ******************************************************************************/

// This pulls in config.h
#include "cti_defs.h"
#include "cti_argv_defs.hpp"

#include <memory>
#include <thread>
#include <variant>
#include <algorithm>
#include <regex>
#include <iomanip>

#include <sstream>
#include <fstream>

#include "transfer/Manifest.hpp"

#include "PALS/Frontend.hpp"

#include "useful/cti_hostname.hpp"
#include "useful/cti_split.hpp"
#include "frontend/mpir_iface/Inferior.hpp"

// PALS application IDs are UUIDs in form 8-4-4-4-12
static const auto rawUuidRegex = std::string{
    R"([[:alnum:]]{8}-[[:alnum:]]{4}-[[:alnum:]]{4}-[[:alnum:]]{4}-[[:alnum:]]{12})"};

static auto getPalsVersion()
{
    char const* const palstat_argv[] = {"palstat", "--version", nullptr};
    auto palstatOutput = cti::Execvp{"palstat", (char* const*)palstat_argv, cti::Execvp::stderr::Ignore};

    // palstat version major.minor.revision
    auto palstatVersion = std::string{};
    if (!std::getline(palstatOutput.stream(), palstatVersion, '\n')) {
        throw std::runtime_error("Failed: `palstat --version`. Ensure the `cray-pals` module is loaded");
    }

    // major.minor.revision
    std::tie(std::ignore, std::ignore, palstatVersion, std::ignore)
        = cti::split::string<4>(std::move(palstatVersion), ' ');
    auto [major, minor, revision] = cti::split::string<3>(palstatVersion, '.');

    auto stoi_or_zero = [](std::string const& str) {
        if (str.empty()) { return 0; }
        try {
            return std::stoi(str);
        } catch (...) {
            return 0;
        }
    };

    // Parse major and minor version
    auto parsed_major = stoi_or_zero(major);
    auto parsed_minor = stoi_or_zero(minor);
    auto parsed_revision = stoi_or_zero(revision);

    return std::tuple(parsed_major, parsed_minor, parsed_revision);
}

PALSFrontend::PALSFrontend()
    : m_preloadMpirShim{false}
{
    // PALS version 1.6.0 improved attach process, where startup barrier triggers
    // before main instead of during MPI_Init
    // Disable non-MPI barrier unless env. var. enabled
    if ((::getenv(PALS_BARRIER_NON_MPI) == nullptr)
     || (::getenv(PALS_BARRIER_NON_MPI)[0] != '0')) {

        // Check for version below 1.6.0
        auto [major, minor, revision] = getPalsVersion();
        if ((major < 1) || ((major == 1) && (minor < 6))) {
            m_preloadMpirShim = true;
        }
    }
}

std::string
PALSFrontend::getApid(pid_t launcher_pid)
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

// Use PBS_NODEFILE to find job execution host
static auto find_job_host_in_allocation()
{
    // If inside allocation, get first line of PBS_NODEFILE
    if (auto pbs_nodefile = ::getenv("PBS_NODEFILE")) {
        Frontend::inst().writeLog("Trying PBS nodefile %s\n", pbs_nodefile);

        // Open nodefile
        auto nodefileStream = std::ifstream{pbs_nodefile};

        // Read first line
        auto line = std::string{};
        if (std::getline(nodefileStream, line)) {
            Frontend::inst().writeLog("Found host %s\n", line.c_str());
            return line;
        }

        throw std::runtime_error("failed to parse PBS_NODEFILE at "
                + std::string{pbs_nodefile});
    } else {
        throw std::runtime_error("PBS_NODEFILE was not set in allocation environment");
    }
}

// Find job details that CTI uses
static auto get_job_details(std::string const& jobId)
{
    Frontend::inst().writeLog("Getting job details for %s\n", jobId.c_str());

    auto jobState = std::string{};
    auto execHost = std::string{};
    auto comment = std::string{};

    // Run qstat with machine-parseable format
    char const* qstat_argv[] = {"qstat", "-f", jobId.c_str(), nullptr};
    auto qstatOutput = cti::Execvp{"qstat", (char* const*)qstat_argv, cti::Execvp::stderr::Ignore};

    // Start parsing qstat output
    auto& qstatStream = qstatOutput.stream();
    auto qstatLine = std::string{};

    // Each line is in the format `    Var = Val`
    while (std::getline(qstatStream, qstatLine)) {

        // Split line on ' = '
        auto const var_end = qstatLine.find(" = ");
        if (var_end == std::string::npos) {
            continue;
        }
        auto const var = cti::split::removeLeadingWhitespace(qstatLine.substr(0, var_end));
        auto const val = qstatLine.substr(var_end + 3);

        if (var == "job_state") {
            jobState = cti::split::removeLeadingWhitespace(std::move(val));
        } else if (var == "exec_host") {
            execHost = cti::split::removeLeadingWhitespace(std::move(val));
        } else if (var == "comment") {
            comment = cti::split::removeLeadingWhitespace(std::move(val));
        }
    }

    Frontend::inst().writeLog("Full exec_host spec: %s\n", execHost.c_str());

    // Consume rest of stream output
    while (std::getline(qstatStream, qstatLine)) {}

    // Wait for completion and check exit status
    if (auto const qstat_rc = qstatOutput.getExitStatus()) {
        throw std::runtime_error("`qstat -f " + jobId + "` failed, is the job running?");
    }

    return std::make_tuple(std::move(jobState), std::move(execHost), std::move(comment));
}

// Use PBS job ID and qstat to find job execution host
static auto find_job_host_outside_allocation(std::string const& jobId)
{
    Frontend::inst().writeLog("Looking for job host for %s\n", jobId.c_str());

    // Get exec_host
    auto&& [jobState, execHost, comment] = get_job_details(jobId);

    // Reached end of qstat output without finding `exec_host`
    if (execHost.empty()) {
        if (comment.empty()) {
            throw std::runtime_error("Failed to find exec_host for " + jobId + ", is the job running?");
        } else {
            throw std::runtime_error("Failed to find exec_host for " + jobId + ". PBS reported: \n"
                + comment + "\nThe job may still be queued.");
        }
    }

    // Extract main hostname from exec_host
    /* qstat manpage:
        The exec_host string has the format:
           <host1>/<T1>*<P1>[+<host2>/<T2>*<P2>+...]
    */
    auto const hostname_end = execHost.find("/");
    if (hostname_end != std::string::npos) {
        auto firstHost = execHost.substr(0, hostname_end);
        Frontend::inst().writeLog("First host from qstat: %s\n", firstHost.c_str());
        return firstHost;
    }

    throw std::runtime_error("failed to parse qstat exec_host: " + execHost);
}

static auto find_apid_from_jobid(std::string const& execHost, std::string const& pbsJobId)
{
    Frontend::inst().writeLog("Looking for PBS job %s on exec host %s\n",
        pbsJobId.c_str(), execHost.c_str());

    // Run palstat to query jobs on node
    char const* palstat_argv[] = {"palstat", "--node", execHost.c_str(), nullptr};
    auto palstatOutput = cti::Execvp{"palstat", (char* const*)palstat_argv, cti::Execvp::stderr::Ignore};

    // Start parsing palstat output
    auto& palstatStream = palstatOutput.stream();
    auto palstatLine = std::string{};

    // APID / JobID lines are in the format `Var: Val`
    // APID appears before JobID
    auto foundApid = false;
    auto parsedApid = std::string{};
    while (std::getline(palstatStream, palstatLine)) {

        // Split line on ': '
        auto const var_end = palstatLine.find(": ");
        if (var_end == std::string::npos) {
            continue;
        }
        auto const var = cti::split::removeLeadingWhitespace(palstatLine.substr(0, var_end));
        auto const val = palstatLine.substr(var_end + 2);
        if (var == "APID") {
            parsedApid = cti::split::removeLeadingWhitespace(std::move(val));
        } else if (var == "JobID") {

            // Check against provided job ID (may be prefix of PBS's version of job ID)
            auto fullJobId = cti::split::removeLeadingWhitespace(std::move(val));
            if (fullJobId.rfind(pbsJobId, 0) == 0) {
                foundApid = true;
                break;
            }
        }
    }

    Frontend::inst().writeLog("Found app ID %s\n", parsedApid.c_str());

    // Consume rest of stream output
    palstatStream.ignore(std::numeric_limits<std::streamsize>::max());

    // Wait for completion and check exit status
    if (auto const palstat_rc = palstatOutput.getExitStatus()) {
        throw std::runtime_error("`palstat --node " + execHost + "` failed with code " + std::to_string(palstat_rc));
    }

    // Reached end of palstat output without finding the right apid
    if (!foundApid || (parsedApid.empty())) {
        throw std::runtime_error("invalid PBS job id " + pbsJobId + " for host " + execHost
            + ". Check for this job by running `palstat --node " + execHost + "`");
    }

    return parsedApid;
}

static bool is_apid_running(std::string const& execHost, std::string const& apId)
{
    // Run palstat to query job with apid
    auto palstatArgv = cti::ManagedArgv{"palstat", "-n", execHost, apId};
    auto palstatOutput = cti::Execvp{"palstat", (char* const*)palstatArgv.get(),
        cti::Execvp::stderr::Ignore};

    // Start parsing palstat output
    auto& palstatStream = palstatOutput.stream();
    auto palstatLine = std::string{};

    // APID / JobID lines are in the format `Var: Val`
    // APID appears before JobID
    auto running = false;
    while (std::getline(palstatStream, palstatLine)) {

        // Split line on ': '
        auto const var_end = palstatLine.find(": ");
        if (var_end == std::string::npos) {
            continue;
        }
        auto const var = cti::split::removeLeadingWhitespace(palstatLine.substr(0, var_end));
        auto const val = palstatLine.substr(var_end + 2);
        if (var == "State") {
            auto appState = cti::split::removeLeadingWhitespace(std::move(val));

            // Running / launched states
            if ((appState == "pending") || (appState == "startup")
             || (appState == "running")) {
                running = true;
                break;
            }
        }
    }

    // Consume rest of stream output
    palstatStream.ignore(std::numeric_limits<std::streamsize>::max());

    // Wait for completion and check exit status
    if (auto const palstat_rc = palstatOutput.getExitStatus()) {
        running = false;
    }

    return running;
}

template <typename Func>
static bool palstat_search(std::string const& execHost, std::string const& appId, Func&& callback)
{
    // Run palstat to query job with apid
    auto palstatArgv = cti::ManagedArgv{"palstat", "-n", execHost, appId};
    auto palstatOutput = cti::Execvp{"palstat", (char* const*)palstatArgv.get(),
        cti::Execvp::stderr::Ignore};

    // Start parsing palstat output
    auto& palstatStream = palstatOutput.stream();
    auto palstatLine = std::string{};

    while (std::getline(palstatStream, palstatLine)) {

        // Split line on ': '
        auto const var_end = palstatLine.find(": ");
        if (var_end == std::string::npos) {
            continue;
        }
        auto const var = cti::split::removeLeadingWhitespace(palstatLine.substr(0, var_end));
        auto const val = palstatLine.substr(var_end + 2);
        if (callback(var, cti::split::removeLeadingWhitespace(std::move(val)))) {
            return true;
        }
    }

    // Consume rest of stream output
    palstatStream.ignore(std::numeric_limits<std::streamsize>::max());

    // Ignore palstat exit status
    (void)palstatOutput.getExitStatus();

    return false;
}

std::string
PALSFrontend::submitJobScript(std::string const& scriptPath, char const* const* launcher_args,
    char const* const* env_list)
{
    Frontend::inst().writeLog("Submitting PBS job script %s\n", scriptPath.c_str());

    // Build qsub arguments
    auto qsubArgv = cti::ManagedArgv{"qsub"};
    if (launcher_args != nullptr) {
        for (auto arg = launcher_args; *arg != nullptr; arg++) {
            qsubArgv.add(*arg);
        }
    }

    // Add custom environment arguments
    auto jobEnvArg = std::stringstream{};
    if (env_list != nullptr) {
        for (auto env_setting = env_list; *env_setting != nullptr; env_setting++) {
            // Escape commas in setting
            jobEnvArg << std::quoted(*env_setting, ',') << ',';
        }
    }

    // Add startup barrier environment setting
    jobEnvArg << "PALS_LOCAL_LAUNCH=0,PALS_STARTUP_BARRIER=1";
    qsubArgv.add("-v");
    qsubArgv.add(jobEnvArg.str());

    // Add script argument
    qsubArgv.add(scriptPath);

    // Submit batch file to PBS
    auto qsubOutput = cti::Execvp{"qsub", (char* const*)qsubArgv.get(), cti::Execvp::stderr::Pipe};

    // Read qsub output
    auto& qsubStream = qsubOutput.stream();
    auto pbsJobId = std::string{};
    auto getline_failed = false;
    if (!std::getline(qsubStream, pbsJobId)) {
        getline_failed = true;
    }
    qsubStream.ignore(std::numeric_limits<std::streamsize>::max());

    // Wait for completion and check exit status
    if ((qsubOutput.getExitStatus() != 0) || getline_failed) {
        if (pbsJobId.empty()) {
            throw std::runtime_error("failed to submit PBS job using command\n    "
                + qsubArgv.string());
        } else {
            // pbsJobId contains the error output for qsub
            throw std::runtime_error("failed to submit PBS job using command\n    "
                + qsubArgv.string() + "\n" + pbsJobId);
        }
    }

    // Wait until PALS application has started
    // When launching a job, CTI will submit the job to PBS, then wait for PALS
    // to start the job on the execution host.
    int retry = 0;
    int max_retry = 10;
    auto disable_timeout = (::getenv(PALS_DISABLE_TIMEOUT)
        && (::getenv(PALS_DISABLE_TIMEOUT)[0] != '0'));
    while ((retry < max_retry) || disable_timeout) {
        Frontend::inst().writeLog("PBS job %s submitted, waiting for PALS application "
            "to launch (attempt %d/%d)\n", pbsJobId.c_str(), retry + 1,
            max_retry);
        ::sleep(3);

        // Get exec host for PBS job
        auto execHost = find_job_host_outside_allocation(pbsJobId);

        try {

            // Check for PALS application ID
            auto palsApid = find_apid_from_jobid(execHost, pbsJobId);
            Frontend::inst().writeLog("Successfully launched PALS application %s\n",
                palsApid.c_str());

            return pbsJobId;

        } catch (...) {

            // PALS application not started yet
            retry++;
        }
    }

    throw std::runtime_error("Timed out waiting for PALS application to launch");
}

// Determine if apid is a valid PALS application ID
static bool is_pals_apid(std::string const& apid)
{
    auto match = std::smatch{};
    static auto uuidRegex = std::regex{rawUuidRegex};
    return std::regex_match(apid, match, uuidRegex);
}

// Determine if running inside PBS allocation
static bool in_pbs_allocation()
{
    // Check for PBS environment variables that PALS utilities require
    if ((::getenv("PBS_NODEFILE") == nullptr)
     || (::getenv("PBS_JOBID") == nullptr)) {
        return false;
    }

    return true;
}

// Determine if running inside PBS allocation that contains the provided
// PALS application
static bool in_matching_pbs_allocation(std::string const& apid)
{
    Frontend::inst().writeLog("Looking for PBS job %s in current allocation\n",
        apid.c_str());

    // Run palstat to query jobs on node
    char const* palstat_argv[] = {"palstat", apid.c_str(), nullptr};
    auto palstatOutput = cti::Execvp{"palstat", (char* const*)palstat_argv, cti::Execvp::stderr::Ignore};

    // Consume palstat output
    auto& palstatStream = palstatOutput.stream();
    palstatStream.ignore(std::numeric_limits<std::streamsize>::max());

    // Wait for completion and check exit status
    return palstatOutput.getExitStatus() == 0;
}

PALSFrontend::PalsLaunchInfo
PALSFrontend::attachApp(std::string const& jobOrApId)
{
    // Execution host will be read from environment
    // if ID is a PALS application ID and running in PBS allocation
    // PALS ID and execution host will be determined from PBS
    // utilities and does not need to be running in PBS allocation
    auto execHost = std::string{};
    auto apId = std::string{};

    // Can supply both exec host and PALS app ID separated by :
    auto colon_at = jobOrApId.rfind(":");
    if (colon_at != std::string::npos) {

        // Split on colon
        auto jobId = jobOrApId.substr(0, colon_at);
        apId = jobOrApId.substr(colon_at + 1);

        // Find execution host from job ID
        execHost = find_job_host_outside_allocation(jobId);

    // Determine if ID is PBS job ID or PALS job ID
    } else if (is_pals_apid(jobOrApId)) {

        // If execution host was manually supplied, can attach outside
        // of job's allocation
        if (auto pals_exec_host = ::getenv(PALS_EXEC_HOST)) {
            execHost = pals_exec_host;

        // No execution host supplied, detect from allocation
        } else {

            // Ensure running inside allocation
            if (!in_pbs_allocation()) {
                throw std::runtime_error("Provided PALS application ID "
                    + jobOrApId + ", but this tool was not launched inside "
                    "the application's PBS allocation");
            }

            // Ensure running inside matching allocation
            if (!in_matching_pbs_allocation(jobOrApId)) {
                throw std::runtime_error("This tool was launched in a PBS "
                    "allocation, but could not find the provided PALS "
                    "application " + jobOrApId + ". Ensure that the tool "
                    "was started inside the same PBS allocation as the "
                    "PALS application");
            }

            execHost = find_job_host_in_allocation();
        }

        apId = jobOrApId;

    // Provided PBS job ID
    } else {

        // Find exec host from PBS ID
        execHost = find_job_host_outside_allocation(jobOrApId);

        // Find PALS app ID from PBS job ID and exec host
        apId = find_apid_from_jobid(execHost, jobOrApId);
    }

    // Ensure application still running
    if (!is_apid_running(execHost, apId)) {

        // Running in allocation
        if (::getenv(PALS_EXEC_HOST)) {
            throw std::runtime_error("Attempted to attach to PALS application "
                + apId + ", but PALS reported that it was not running");

        // Execution host manually supplied
        } else {
            throw std::runtime_error("Attempted to attach to PALS application "
                + apId + " running on host " + execHost + ", but PALS reported "
                "that it was not running");
        }
    }

    // Create daemon ID for new application
    auto result = PalsLaunchInfo
        { .daemonAppId = Daemon().request_RegisterApp()
        , .execHost = std::move(execHost)
        , .apId = std::move(apId)
        , .procTable = {}
        , .binaryRankMap = {}
        , .atBarrier = false
    };

    // Launch palstat MPIR query
    char const* palstat_argv[] = { "palstat", "-n", result.execHost.c_str(),
        "-p", result.apId.c_str(), nullptr };
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

    // Check proctable result
    if (result.procTable.empty()) {
        throw std::runtime_error("Failed to get job layout for job ID "
            + jobOrApId + ". Check with `palstat -n " + result.execHost
            + " --proctable " + result.apId + "`");
    }

    // Build binary-rank map
    auto rank = size_t{0};
    for (auto&& [pid, host, executable] : result.procTable) {
        result.binaryRankMap[executable].push_back(rank++);
    }

    return result;
}

PALSFrontend::PalsLaunchInfo
PALSFrontend::launchApp(const char * const launcher_argv[],
        int stdoutFd, int stderrFd, const char *inputFile, const char *chdirPath,
		const char * const env_list[])
{
    // Inside allocation, get first line of PBS_NODEFILE
    auto execHost = std::string{};
    if (auto pbs_nodefile = ::getenv("PBS_NODEFILE")) {

        // Open nodefile
        auto nodefileStream = std::ifstream{pbs_nodefile};

        // Read first line
        if (!std::getline(nodefileStream, execHost)) {
            throw std::runtime_error("Failed to parse PBS_NODEFILE at "
                + std::string{pbs_nodefile});
        }
    }

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
        auto mpirData = [&]() {
            try {
                return Daemon().request_LaunchMPIR(
                    launcher_path.get(), launcherArgv.get(),
                    ::open(inputFile, O_RDONLY), stdoutFd, stderrFd,
                    env_list);

            } catch (std::exception const& ex) {
                // PALS checks the following locations for host lists
                // Defined in hpc-rm-pals:src/util/palsutil.c
                if (!::getenv("PALS_HOSTLIST") && !::getenv("PALS_HOSTFILE")
                 && !::getenv("PBS_NODEFILE")) {
                    throw std::runtime_error("Launcher failed to start application. "
                        "PALS_HOSTLIST, PALS_HOSTFILE, and PBS_NODEFILE were not set. "
                        "Ensure you are launching inside an active PBS allocation");
                }
                throw;
            }
        }();

        // Get application ID from launcher
        auto apid = Daemon().request_ReadStringMPIR(mpirData.mpir_id,
            "totalview_jobid");

        // Construct launch info struct
        return PalsLaunchInfo
            { .daemonAppId = mpirData.mpir_id
            , .execHost = std::move(execHost)
            , .apId = std::move(apid)
            , .procTable = std::move(mpirData.proctable)
            , .binaryRankMap = std::move(mpirData.binaryRankMap)
            , .atBarrier = true
        };

    } else {
        throw std::runtime_error("Failed to find launcher in path: " + getLauncherName());
    }
}

// Add necessary launch environment arguments to the provided list
static inline auto fixLaunchEnvironment(std::string const& launcherName, CArgArray env_list)
{
    // Determine the timeout environment variable for PALS `mpiexec` or PALS `aprun` command
    // Set timeout to five minutes
    // https://connect.us.cray.com/jira/browse/PE-34329
    auto const timeout_env = (launcherName == "aprun")
        ? "APRUN_RPC_TIMEOUT=300"
        : "PALS_RPC_TIMEOUT=300";

    // Always send launch events to the PALS service
    // https://jira-pro.it.hpe.com:8443/browse/ALT-764
    auto const local_launch_env = (launcherName == "aprun")
        ? "APRUN_LOCAL_LAUNCH=0"
        : "PALS_LOCAL_LAUNCH=0";

    // Add the new variables to a new environment list
    auto fixedEnvVars = cti::ManagedArgv{};

    // Copy provided environment list
    if (env_list != nullptr) {
        fixedEnvVars.add(env_list);
    }

    // Append new environment variable
    fixedEnvVars.add(timeout_env);
    fixedEnvVars.add(local_launch_env);

    return fixedEnvVars;
}

// Add necessary launch arguments to the provided list
static inline auto fixLaunchArguments(std::string const& launcherName, CArgArray launcher_argv,
	bool preload_pals)
{
    // Add the new variables to a new environment list
    auto fixedArgs = cti::ManagedArgv{};

	if (launcher_argv == nullptr) {
		return fixedArgs;
	}

	// If PALS preload library is to be added, save previous LD_PRELOAD
	auto preloadPalsPath = std::string{};
	if (preload_pals) {

		// Get path to PALS preload library
		try {
			preloadPalsPath = cti::accessiblePath(Frontend::inst().getBaseDir()
				+ "/lib/libctipreloadpals.so");
		} catch (std::exception const& ex) {
			Frontend::inst().writeLog("failed to get PALS preload library path: %s\n", ex.what());
		}
	}

	if (!preloadPalsPath.empty()) {

		// Find existing LD_PRELOAD argument
		auto found_ld_preload = false;
		for (size_t i = 0; launcher_argv[i] != nullptr; i++) {

			// Multiple --env=LD_PRELOAD arguments will add multiple fixed LD_PRELOAD values,
			// which follow regular behavior of using the last --env=LD_PRELOAD setting

			// --env=LD_PRELOAD=<value>
			if (::strncmp(launcher_argv[i], "--env=LD_PRELOAD=", 17) == 0) {
				auto ldPreload = std::get<2>(cti::split::string<3>(launcher_argv[i], '='));
				fixedArgs.add("--env=LD_PRELOAD=" + preloadPalsPath + ":" + ldPreload);
				fixedArgs.add("--env=" SAVE_LD_PRELOAD "=" + ldPreload);
				found_ld_preload = true;

			// --env LD_PRELOAD=<value>
			} else if ((::strncmp(launcher_argv[i], "--env", 4) == 0)
			        && (launcher_argv[i + 1] != nullptr)
                    && (::strncmp(launcher_argv[i + 1], "LD_PRELOAD=", 11) == 0)) {
				auto ldPreload = std::get<1>(cti::split::string<2>(launcher_argv[i + 1], '='));
				fixedArgs.add("--env=LD_PRELOAD=" + preloadPalsPath + ":" + ldPreload);
				fixedArgs.add("--env=" SAVE_LD_PRELOAD "=" + ldPreload);
				found_ld_preload = true;

				// Skip --env
				i++;

			// Copy argument
			} else {
				fixedArgs.add(launcher_argv[i]);
			}
		}

		// Add LD_PRELOAD value if it wasn't already found
		if (!found_ld_preload) {
			fixedArgs.add_front("--env=LD_PRELOAD=" + preloadPalsPath);
		}

	// Copy arguments
	} else {
		fixedArgs.add(launcher_argv);
	}

    return fixedArgs;
}

std::weak_ptr<App>
PALSFrontend::launch(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
    CStr inputFile, CStr chdirPath, CArgArray env_list)
{
	auto fixedLaunchArgs = fixLaunchArguments(getLauncherName(), launcher_argv,
		false /* no MPI library preload */);
    auto fixedEnvVars = fixLaunchEnvironment(getLauncherName(), env_list);

    auto appPtr = std::make_shared<PALSApp>(*this,
        launchApp(fixedLaunchArgs.get(), stdout_fd, stderr_fd, inputFile, chdirPath, fixedEnvVars.get()));

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
PALSFrontend::launchBarrier(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
        CStr inputFile, CStr chdirPath, CArgArray env_list)
{
	auto fixedLaunchArgs = fixLaunchArguments(getLauncherName(), launcher_argv, m_preloadMpirShim);
    auto fixedEnvVars = fixLaunchEnvironment(getLauncherName(), env_list);

    auto ret = m_apps.emplace(std::make_shared<PALSApp>(*this,
        launchApp(fixedLaunchArgs.get(), stdout_fd, stderr_fd, inputFile, chdirPath, fixedEnvVars.get())));
    if (!ret.second) {
        throw std::runtime_error("Failed to create new App object.");
    }

    return *ret.first;
}

std::weak_ptr<App>
PALSFrontend::launchBarrierNonMpi(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
        CStr inputFile, CStr chdirPath, CArgArray env_list)
{
	auto fixedLaunchArgs = fixLaunchArguments(getLauncherName(), launcher_argv, m_preloadMpirShim);
    auto fixedEnvVars = fixLaunchEnvironment(getLauncherName(), env_list);

    auto ret = m_apps.emplace(std::make_shared<PALSApp>(*this,
        launchApp(fixedLaunchArgs.get(), stdout_fd, stderr_fd, inputFile, chdirPath, fixedEnvVars.get())));
    if (!ret.second) {
        throw std::runtime_error("Failed to create new App object.");
    }

    return *ret.first;
}

std::string
PALSFrontend::getPMIxUtilPath()
{
    // Path provided in environment
    if (auto pals_pmix = ::getenv(PALS_PMIX)) {
        if (!cti::fileHasPerms(pals_pmix, R_OK | X_OK)) {
            throw std::runtime_error("PMIx utility set in " PALS_PMIX " was not accesible: "
                + std::string{pals_pmix});
        }

        return pals_pmix;
    }

    auto result = Frontend::inst().getCfgDir() + "/cti_pmix_util";

    // Utility already built
    if (cti::fileHasPerms(result.c_str(), R_OK | X_OK)) {
        return result;
    }

    // Check utility source
    auto pmixSource = Frontend::inst().getBaseDir() + "/pals/" PALS_PMIX_SRC;
    if (!cti::fileHasPerms(pmixSource.c_str(), R_OK)) {
        throw std::runtime_error("Could not find PMIx utility source at " + pmixSource);
    }

    // Construct build arguments
    auto bashArgv = cti::ManagedArgv{"bash", "-c"};
    auto ccCmd = std::stringstream{};
    ccCmd << "cc ";
    if (auto pals_pmix_cflags = ::getenv(PALS_PMIX_CFLAGS)) {
        ccCmd << pals_pmix_cflags << " ";
    } else {
        ccCmd << "-g -lpmix ";
    }
    ccCmd << pmixSource << " -o " << result;
    auto ccCmdStr = ccCmd.str();
    bashArgv.add(ccCmdStr);

    // Run build
    writeLog("Running PMIx utility build with\n%s\n", ccCmdStr.c_str());
    auto bashOutput = cti::Execvp{"bash", bashArgv.get(), cti::Execvp::stderr::Pipe};
    auto stderrStream = std::stringstream{};
    stderrStream << bashOutput.stream().rdbuf();
    if (bashOutput.getExitStatus()) {
        throw std::runtime_error("Failed to build PMIx helper utility: \n"
            + ccCmdStr + "\nBuild the source file at " + pmixSource + " and set "
            PALS_PMIX " to the path to the built binary. Build output: \n" + stderrStream.str());
    }

    return result;
}

std::weak_ptr<App>
PALSFrontend::registerJob(size_t numIds, ...)
{
    if (numIds != 1) {
        throw std::logic_error("expecting single apid argument to register app");
    }

    va_list idArgs;
    va_start(idArgs, numIds);

    char const* apid = va_arg(idArgs, char const*);

    va_end(idArgs);

    auto ret = m_apps.emplace(std::make_shared<PALSApp>(*this,
        attachApp(apid)));
    if (!ret.second) {
        throw std::runtime_error("Failed to create new App object.");
    }

    return *ret.first;
}

// Current address can now be obtained using the `cminfo` tool.
std::string
PALSFrontend::getHostname() const
{
    static auto const nodeAddress = cti::detectHpcmAddress();

    return nodeAddress;
}

std::string
PALSFrontend::getLauncherName()
{
    // Cache the launcher name result. Assume mpiexec by default.
    auto static launcherName = std::string{cti::getenvOrDefault(CTI_LAUNCHER_NAME_ENV_VAR, "mpiexec")};
    return launcherName;
}

static std::string
createNodeLayoutFile(MPIRProctable const& mpirProctable, std::string const& stagePath)
{
    auto hostLayouts = std::map<std::string, std::vector<cti_rankPidPair_t>>{};

    for (size_t rank = 0; rank < mpirProctable.size(); rank++) {

        auto rankPidPair = cti_rankPidPair_t { .pid = mpirProctable[rank].pid, .rank = (int)rank };

        // Try to insert new host / first rank pair
        auto [hostLayoutPair, added] = hostLayouts.insert({mpirProctable[rank].hostname, {rankPidPair}});

        // Already exists, add instead
        if (!added) {
            hostLayoutPair->second.push_back(rankPidPair);
        }
    }

    auto make_layoutFileEntry = [](std::string const& hostname, std::vector<cti_rankPidPair_t> const& rankPidPairs) {
        // Ensure we have good hostname information.
        auto const hostname_len = hostname.size() + 1;
        if (hostname_len > sizeof(palsLayoutEntry_t::host)) {
            throw std::runtime_error("hostname too large for layout buffer");
        }

        // Extract PE and node information from Node Layout.
        auto layout_entry = palsLayoutEntry_t{};
        layout_entry.numRanks = rankPidPairs.size();
        memcpy(layout_entry.host, hostname.c_str(), hostname_len);

        return layout_entry;
    };

    // Create the file path, write the file using the Step Layout
    auto const layoutPath = std::string{stagePath + "/" + SLURM_LAYOUT_FILE};
    if (auto const layoutFile = cti::file::open(layoutPath, "wb")) {

        // Write the Layout header.
        cti::file::writeT(layoutFile.get(), palsLayoutFileHeader_t
            { .numNodes = (int)hostLayouts.size()
        });

        // Write a Layout entry using node information from each Slurm Node Layout entry.
        for (auto const& [hostname, rankPidPairs] : hostLayouts) {
            cti::file::writeT(layoutFile.get(), make_layoutFileEntry(hostname, rankPidPairs));
            for (auto&& rankPidPair : rankPidPairs) {
                cti::file::writeT(layoutFile.get(), rankPidPair);
            }
        }

        return layoutPath;
    } else {
        throw std::runtime_error("failed to open layout file path " + layoutPath);
    }
}

PALSApp::PALSApp(PALSFrontend& fe, PALSFrontend::PalsLaunchInfo&& palsLaunchInfo)
    : App{fe, palsLaunchInfo.daemonAppId}
    , m_execHost{std::move(palsLaunchInfo.execHost)}
    , m_apId{std::move(palsLaunchInfo.apId)}

    , m_beDaemonSent{false}
    , m_procTable{std::move(palsLaunchInfo.procTable)}
    , m_binaryRankMap{std::move(palsLaunchInfo.binaryRankMap)}

    , m_apinfoPath{"/var/run/palsd/" + m_apId + "/apinfo"}
    , m_toolPath{"/tmp/cti-" + m_apId}
    , m_attribsPath{"/var/run/palsd/" + m_apId} // BE daemon looks for <m_attribsPath>/pmi_attribs
    , m_stagePath{cti::cstr::mkdtemp(std::string{m_frontend.getCfgDir() + "/palsXXXXXX"})}
    , m_extraFiles{createNodeLayoutFile(m_procTable, m_stagePath)}

    , m_atBarrier{palsLaunchInfo.atBarrier}
    , m_pmix{false}
{
    // Get set of hosts for application
    for (auto&& [pid, hostname, executable] : m_procTable) {
        m_hosts.insert(hostname);
    }

    // Create remote toolpath directory
    { auto palscmdArgv = cti::ManagedArgv { "palscmd", "-n", m_execHost, m_apId,
            "mkdir", "-p", m_toolPath };

        if (!m_frontend.Daemon().request_ForkExecvpUtil_Sync(
            m_daemonAppId, "palscmd", palscmdArgv.get(),
            FE_daemon::CloseFd, FE_daemon::CloseFd, FE_daemon::CloseFd,
            nullptr)) {
            throw std::runtime_error("failed to create remote toolpath directory for apid " + m_apId);
        }
    }

    // Add PMIx utility if running with PMIx
    if (palstat_search(m_execHost, m_apId, [&](std::string const& key, std::string const& val) {
        return (key == "PMI") && (val == "pmix");
    })) {
        m_pmix = true;
        m_extraFiles.push_back(fe.getPMIxUtilPath());

        // PMIx tool support is projected to release in PALS 1.7.0 (USS-3013)
        auto [major, minor, revision] = getPalsVersion();
        if ((major < 1) || ((major == 1) && (minor < 7))) {
            fprintf(stderr, "warning: detected PALS version %d.%d.%d and targeting a PMIx application.\n"
                "PMIx tool support is projected to release in PALS 1.7.0. Launch will continue, but "
                "you may encounter a tool failure\n",
                major, minor, revision);
        }
    }
}

PALSApp::~PALSApp()
{
    if (!Frontend::isOriginalInstance()) {
        writeLog("~PALSApp: forked PID %d exiting without cleanup\n", getpid());
        return;
    }

    // Ignore failures in destructor
    try {

        // Remove remote toolpath directory
        auto palscmdArgv = cti::ManagedArgv{"palscmd", "-n", m_execHost, m_apId,
            "rm", "-rf", m_toolPath};

        m_frontend.Daemon().request_ForkExecvpUtil_Sync(
            m_daemonAppId, "palscmd", palscmdArgv.get(),
            FE_daemon::CloseFd, FE_daemon::CloseFd, FE_daemon::CloseFd,
            nullptr);
    } catch (std::exception const& ex) {
        writeLog("~PALSApp: %s\n", ex.what());
    }
}

std::string
PALSApp::getLauncherHostname() const
{
    throw std::runtime_error{"not supported for PALS: " + std::string{__func__}};
}

bool
PALSApp::isRunning() const
{
    return is_apid_running(m_execHost, m_apId);
}

size_t
PALSApp::getNumPEs() const
{
    return m_procTable.size();
}

size_t
PALSApp::getNumHosts() const
{
    return m_hosts.size();
}

std::vector<std::string>
PALSApp::getHostnameList() const
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
PALSApp::getHostsPlacement() const
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
PALSApp::getBinaryRankMap() const
{
    return m_binaryRankMap;
}

void
PALSApp::releaseBarrier()
{
    if (!m_atBarrier) {
        throw std::runtime_error("application is not at startup barrier");
    }

    // XXX: Workaround for PE-43365. In PALS 1.2.3, there is a race condition
    // bug in mpiexec/pals where the launched app can get a SIGCONT before it
    // actually reaches the startup barrier. This results in the app getting
    // stuck in the barrier forever.
    if (auto* releaseDelay = ::getenv(PALS_BARRIER_RELEASE_DELAY)) {
        if (auto seconds = strtol(releaseDelay, nullptr, 10); seconds > 0) {
            Frontend::inst().writeLog(
                PALS_BARRIER_RELEASE_DELAY
                " detected, waiting %d seconds before release.\n",
                seconds);
            sleep(seconds);
        }
    }

    m_frontend.Daemon().request_ReleaseMPIR(m_daemonAppId);
    m_atBarrier = false;
}

void
PALSApp::shipPackage(std::string const& tarPath) const
{
    auto const destinationName = cti::cstr::basename(tarPath);

    // Create host list file
    auto hostFileHandle = cti::temp_file_handle{m_stagePath + "/hostsXXXXXX"};
    auto hostFile = std::ofstream{hostFileHandle.get()};

    // PALS bug PE-49724, `palscp` will silently skip the execution host
    // unless it is placed first in the host list

    // Determine the hostname for the execution host
    auto firstHost = [](std::set<std::string> const& hosts, std::string const& execHost) {

        // Match on first part of exec host domain
        auto first_dot = execHost.find(".");
        auto full_match = (first_dot == std::string::npos);
        auto execHostPrefix = (full_match)
            ? execHost
            : execHost.substr(0, first_dot + 1);

        // Find hostname whose prefix matches that of the execution host
        auto host = std::find_if(hosts.begin(), hosts.end(),
            [&execHostPrefix](auto&& host) { return host.rfind(execHostPrefix, 0) == 0; });
        if (host != hosts.end()) {
            Frontend::inst().writeLog("PE-49724: Found hostname for exec host: %s\n", host->c_str());
            return std::string{*host};
        }

        // Fall back to original host list ordering
        Frontend::inst().writeLog("PE-49724: Failed to find hostname for exec host: %s\n", execHost.c_str());
        return std::string{};
    }(m_hosts, m_execHost);

    // Write execution hostname as first entry
    if (!firstHost.empty()) {
        hostFile << firstHost << "\n";
    }

    // Write the rest of the hostnames
    for (auto&& host : m_hosts) {
        if (host != firstHost) {
            hostFile << host;
        }
    }

    // Host file generation completed
    hostFile.close();

    auto palscpArgv = cti::ManagedArgv{"palscp", "-l", hostFileHandle.get(),
        "-f", tarPath, "-d", destinationName, m_apId};

    if (!m_frontend.Daemon().request_ForkExecvpUtil_Sync(
        m_daemonAppId, "palscp", palscpArgv.get(),
        FE_daemon::CloseFd, FE_daemon::CloseFd, FE_daemon::CloseFd,
        nullptr)) {
        throw std::runtime_error("failed to ship " + tarPath + " using palscp");
    }

    // Move shipped file from noexec filesystem to toolpath directory
    auto const palscpDestination = "/var/run/palsd/" + m_apId + "/files/" + destinationName;
    auto palscmdArgv = cti::ManagedArgv { "palscmd", "-n", m_execHost, m_apId,
            "mv", palscpDestination, m_toolPath };

    if (!m_frontend.Daemon().request_ForkExecvpUtil_Sync(
        m_daemonAppId, "palscmd", palscmdArgv.get(),
        FE_daemon::CloseFd, FE_daemon::CloseFd, FE_daemon::CloseFd,
        nullptr)) {
        throw std::runtime_error("failed to move shipped package for apid " + m_apId);
    }
}

void
PALSApp::startDaemon(const char* const args[], bool synchronous)
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
            FE_daemon::CloseFd, FE_daemon::CloseFd, FE_daemon::CloseFd,
            nullptr)) {
            throw std::runtime_error("failed to copy " + sourcePath + " to " + destinationPath);
        }

        // Ship the unique backend daemon
        shipPackage(destinationPath);
        // set transfer to true
        m_beDaemonSent = true;
    }

    // Create the arguments for palscmd
    auto palscmdArgv = cti::ManagedArgv{"palscmd", "-n", m_execHost, m_apId};

    // Use location of existing launcher binary on compute node
    auto const launcherPath = m_toolPath + "/" + getBEDaemonName();
    palscmdArgv.add(launcherPath);

    // Copy provided launcher arguments
    palscmdArgv.add(args);

    // If PMIx mode, add PMIx environment setting
    auto daemonEnv = cti::ManagedArgv{};
    if (m_pmix) {
        auto& palsFrontend = dynamic_cast<PALSFrontend&>(m_frontend);
        auto backendPmixPath = m_frontend.getCfgDir() + "/" + cti::cstr::basename(palsFrontend.getPMIxUtilPath());
        daemonEnv.add(std::string{PALS_PMIX_BE_PATH} + "=" + backendPmixPath);
    }

    auto stderrPipe = cti::Pipe{};

    // tell frontend daemon to launch palscmd
    m_frontend.Daemon().request_ForkExecvpUtil_Async(
        m_daemonAppId, "palscmd", palscmdArgv.get(),
        FE_daemon::CloseFd, FE_daemon::CloseFd, stderrPipe.getWriteFd(),
        (daemonEnv.size() == 0) ? nullptr : daemonEnv.get());

    // Parse palstat output to get tool helper ID
    auto toolHelperId = std::string{};
    { stderrPipe.closeWrite();
        auto stderrBuf = cti::FdBuf{stderrPipe.getReadFd()};
        auto stderrStream = std::istream{&stderrBuf};

        auto line = std::string{};
        static auto toolHelperIdRegex = std::regex{"Launched tool helper (" + rawUuidRegex + ")"};
        if (std::getline(stderrStream, line)) {
            auto matches = std::smatch{};
            if (std::regex_search(line, matches, toolHelperIdRegex)) {
                toolHelperId = matches[1];
                Frontend::inst().writeLog("Found tool helper ID %s\n", toolHelperId.c_str());
            }
        }

        // Consume rest of output
        stderrStream.ignore(std::numeric_limits<std::streamsize>::max());
        stderrPipe.closeRead();
    }

    // Ignore parse failure
    if (toolHelperId.empty()) {
        Frontend::inst().writeLog("warning: failed to find tool helper ID in palscmd output\n");
    }

    if (synchronous) {

        // Ignore parse failure, wait a bit and return
        if (toolHelperId.empty()) {
            ::sleep(1);
            return;

        // Query palstat for tool helper status until completed
        } else {

            for (int retry = 0; retry < 10; retry++) {
                if (!palstat_search(m_execHost, m_apId, [&](std::string const& key, std::string const& val) {
                    return (key == "Tool ID") && (val == toolHelperId);
                })) {
                    Frontend::inst().writeLog("Tool helper %s completed\n",
                        toolHelperId.c_str());
                    return;
                }

                ::sleep(1);
                Frontend::inst().writeLog("Tool helper %s probably still running, retry (%d/10)\n",
                    toolHelperId.c_str(), retry + 1);
            }

            Frontend::inst().writeLog("Gave up waiting for palstat to complete tool helper %s\n",
                toolHelperId.c_str());
        }
    }
}

std::set<std::string>
PALSApp::checkFilesExist(std::set<std::string> const& paths)
{
    auto result = std::set<std::string>{};

    // Create arguments for file check launch
    auto num_nodes = m_hosts.size();
    writeLog("Checking for duplicate files on %zu nodes\n", num_nodes);

    auto mpiexecArgv = cti::ManagedArgv{
        "mpiexec", "--envnone", "--mem-bind=none", "--ppn=1", "-n", std::to_string(num_nodes),

        // This is a new PALS application running outside the context of the original,
        // so it doesn't have access to daemons that were already shipped. However, mpiexec
        // will ship it automatically during launch.
        m_frontend.getBEDaemonPath()
    };
    for (auto&& path : paths) {
        mpiexecArgv.add("--file=" + path);
    }

    auto stdoutPipe = cti::Pipe{};

    // Tell FE Daemon to launch mpiexec
    char const *local_launch_env[] = {"PALS_LOCAL_LAUNCH=0", nullptr};
    m_frontend.Daemon().request_ForkExecvpUtil_Async(
        m_daemonAppId, "mpiexec",
        mpiexecArgv.get(),
        // redirect stdin / stderr / stdout
        ::open("/dev/null", O_RDONLY), stdoutPipe.getWriteFd(), ::open("/dev/null", O_WRONLY),
        local_launch_env);

    stdoutPipe.closeWrite();
    auto stdoutBuf = cti::FdBuf{stdoutPipe.getReadFd()};
    auto stdoutStream = std::istream{&stdoutBuf};

    // Track number of present files
    auto pathCountMap = std::map<std::string, size_t>{};

    // Read out all paths from daemon
    auto exit_count = num_nodes;
    auto line = std::string{};
    while ((exit_count > 0) && std::getline(stdoutStream, line)) {

        // Daemons will print an empty line when output is completed
        if (line.empty()) {
            writeLog("Nodes left to check: %zu\n", exit_count);
            exit_count--;

        // Received path from daemon
        } else {

            // Increment count for path and add to duplicate list if all nodes have file
            if (++pathCountMap[line] == num_nodes) {
                result.emplace(std::move(line));
            }
        }
    }

    writeLog("Finished checking all nodes\n");

    return result;
}

void PALSApp::kill(int signum)
{
    // create the args for palsig
    auto palsigArgv = cti::ManagedArgv{"palsig", "-n", m_execHost,
        "-s", std::to_string(signum), m_apId};

    // tell frontend daemon to launch palsig, wait for it to finish
    if (!m_frontend.Daemon().request_ForkExecvpUtil_Sync(
        m_daemonAppId, "palsig", palsigArgv.get(),
        FE_daemon::CloseFd, FE_daemon::CloseFd, FE_daemon::CloseFd,
        nullptr)) {
        throw std::runtime_error("failed to send signal to apid " + m_apId);
    }
}
