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
#include <regex>
#include <iomanip>

#include <sstream>
#include <fstream>

#include "transfer/Manifest.hpp"

#include "PALS/Frontend.hpp"

#include "useful/cti_hostname.hpp"
#include "useful/cti_split.hpp"
#include "frontend/mpir_iface/Inferior.hpp"

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

// Use PBS job ID and qstat to find job execution host
static auto find_job_host_outside_allocation(std::string const& jobId)
{
    Frontend::inst().writeLog("Looking for job host for %s\n", jobId.c_str());

    // Run qstat with machine-parseable format
    char const* qstat_argv[] = {"qstat", "-f", jobId.c_str(), nullptr};
    auto qstatOutput = cti::Execvp{"qstat", (char* const*)qstat_argv, cti::Execvp::stderr::Ignore};

    // Start parsing qstat output
    auto& qstatStream = qstatOutput.stream();
    auto qstatLine = std::string{};

    // Each line is in the format `    Var = Val`
    auto execHost = std::string{};
    while (std::getline(qstatStream, qstatLine)) {

        // Split line on ' = '
        auto const var_end = qstatLine.find(" = ");
        if (var_end == std::string::npos) {
            continue;
        }
        auto const var = cti::split::removeLeadingWhitespace(qstatLine.substr(0, var_end));
        auto const val = qstatLine.substr(var_end + 3);
        if (var == "exec_host") {
            execHost = cti::split::removeLeadingWhitespace(std::move(val));
            break;
        }
    }

    Frontend::inst().writeLog("Full exec_host spec: %s\n", execHost.c_str());

    // Consume rest of stream output
    while (std::getline(qstatStream, qstatLine)) {}

    // Wait for completion and check exit status
    if (auto const qstat_rc = qstatOutput.getExitStatus()) {
        throw std::runtime_error("`qstat -f " + jobId + "` failed with code " + std::to_string(qstat_rc));
    }

    // Reached end of qstat output without finding `exec_host`
    if (execHost.empty()) {
        throw std::runtime_error("invalid job id " + jobId);
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
    jobEnvArg << "PALS_STARTUP_BARRIER=1";
    qsubArgv.add("-v");
    qsubArgv.add(jobEnvArg.str());

    // Add script argument
    qsubArgv.add(scriptPath);

    // Submit batch file to PBS
    auto qsubOutput = cti::Execvp{"qsub", (char* const*)qsubArgv.get(), cti::Execvp::stderr::Ignore};

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
        throw std::runtime_error("failed to submit PBS job using command\n"
            + qsubArgv.string());
    }

    // Wait until PALS application has started
    int retry = 0;
    int max_retry = 3;
    while (retry < max_retry) {
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

    throw std::runtime_error("Timed out waiting for PALS "
        "application to launch");
}

// Determine if apid is a valid PALS application ID
static bool is_pals_apid(std::string const& apid)
{
    // PALS application IDs are UUIDs in form 8-4-4-4-12
    static const auto uuidRegex = std::regex{
        R"(^[[:alnum:]]{8}\b-[[:alnum:]]{4}\b-[[:alnum:]]{4}\b-[[:alnum:]]{4}\b-[[:alnum:]]{12}$)"};

    auto match = std::smatch{};

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

    // Determine if ID is PBS job ID or PALS job ID
    if (is_pals_apid(jobOrApId)) {

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
        int stdoutFd, int stderrFd, const char *inputFile, const char *chdirPath, const char * const env_list[])
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
PALSFrontend::launch(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
    CStr inputFile, CStr chdirPath, CArgArray env_list)
{
    auto fixedEnvVars = setTimeoutEnvironment(getLauncherName(), env_list);

    auto appPtr = std::make_shared<PALSApp>(*this,
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
PALSFrontend::launchBarrier(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
        CStr inputFile, CStr chdirPath, CArgArray env_list)
{
    auto fixedEnvVars = setTimeoutEnvironment(getLauncherName(), env_list);

    auto ret = m_apps.emplace(std::make_shared<PALSApp>(*this,
        launchApp(launcher_argv, stdout_fd, stderr_fd, inputFile, chdirPath, fixedEnvVars.get())));
    if (!ret.second) {
        throw std::runtime_error("Failed to create new App object.");
    }

    return *ret.first;
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
    , m_extraFiles{}

    , m_atBarrier{palsLaunchInfo.atBarrier}
{
    // Get set of hosts for application
    for (auto&& [pid, hostname, executable] : m_procTable) {
        m_hosts.emplace(hostname);
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
}

PALSApp::~PALSApp()
{
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
    // Run palstat to query job with apid
    auto palstatArgv = cti::ManagedArgv{"palstat", "-n", m_execHost, m_apId};
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

    // Create host list argument
    auto allHosts = std::stringstream{};
    for (auto&& host : m_hosts) {
        allHosts << host << ",";
    }

    auto palscpArgv = cti::ManagedArgv{"palscp", "-L", allHosts.str().c_str(),
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
PALSApp::startDaemon(const char* const args[])
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

    // tell frontend daemon to launch palscmd, wait for it to finish
    if (!m_frontend.Daemon().request_ForkExecvpUtil_Sync(
        m_daemonAppId, "palscmd", palscmdArgv.get(),
        FE_daemon::CloseFd, FE_daemon::CloseFd, FE_daemon::CloseFd,
        nullptr)) {
        throw std::runtime_error("failed to launch tool daemon for apid " + m_apId);
    }
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
