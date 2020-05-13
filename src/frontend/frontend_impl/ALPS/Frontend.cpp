/******************************************************************************\
 * Frontend.cpp - ALPS specific frontend library functions.
 *
 * Copyright 2014-2019 Cray Inc. All Rights Reserved.
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

#include <fstream>
#include <algorithm>
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

#include <sys/socket.h>
#include <netdb.h>

#include "alps/apInfo.h"

// Pull in manifest to properly define all the forward declarations
#include "transfer/Manifest.hpp"

#include "ALPS/Frontend.hpp"

#include "useful/cti_argv.hpp"
#include "useful/cti_execvp.hpp"
#include "useful/cti_split.hpp"
#include "useful/cti_wrappers.hpp"

/* helper functions */

static auto
getSvcNid()
{
    static auto const svcNid = []() {
        auto result = int{};

        // Open NID file
        auto alpsXTNidFile = std::fstream{};
        alpsXTNidFile.open(ALPS_XT_NID, std::ios_base::in);

        // Read NID from file
        alpsXTNidFile >> result;

        return result;
    }();

    return svcNid;
}

/* ALPSFrontend implementation */

bool
ALPSFrontend::isSupported()
{
    try {
        return !cti::findPath(APRUN).empty();
    } catch (...) {
        return false;
    }
}

std::weak_ptr<App>
ALPSFrontend::launch(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
    CStr inputFile, CStr chdirPath, CArgArray env_list)
{
    auto ret = m_apps.emplace(std::make_shared<ALPSApp>(*this,
        launchApp(launcher_argv, stdout_fd, stderr_fd, inputFile, chdirPath, env_list)));
    if (!ret.second) {
        throw std::runtime_error("Failed to create new App object.");
    }
    return *ret.first;
}

std::weak_ptr<App>
ALPSFrontend::launchBarrier(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
    CStr inputFile, CStr chdirPath, CArgArray env_list)
{
    auto ret = m_apps.emplace(std::make_shared<ALPSApp>(*this,
        launchAppBarrier(launcher_argv, stdout_fd, stderr_fd, inputFile, chdirPath, env_list)));
    if (!ret.second) {
        throw std::runtime_error("Failed to create new App object.");
    }
    return *ret.first;
}

std::weak_ptr<App>
ALPSFrontend::registerJob(size_t numIds, ...)
{
    if (numIds != 1) {
        throw std::logic_error("expecting single aprun ID argument to register app");
    }

    va_list idArgs;
    va_start(idArgs, numIds);

    uint64_t aprunId = va_arg(idArgs, uint64_t);

    va_end(idArgs);

    auto ret = m_apps.emplace(std::make_shared<ALPSApp>(*this, getAprunLaunchInfo(aprunId)));
    if (!ret.second) {
        throw std::runtime_error("Failed to create new App object.");
    }
    return *ret.first;
}

std::string
ALPSFrontend::getHostname() const
{
    // Format NID into XC hostname
    return cti::cstr::asprintf(ALPS_XT_HOSTNAME_FMT, getSvcNid());
}

std::string
ALPSFrontend::getLauncherName() const
{
    auto getenvOrDefault = [](char const* envVar, char const* defaultValue) {
        if (char const* envValue = getenv(envVar)) {
            return envValue;
        }
        return defaultValue;
    };

    // Cache the launcher name result.
    auto static launcherName = std::string{getenvOrDefault(CTI_LAUNCHER_NAME_ENV_VAR, APRUN)};
    return launcherName;
}

ALPSFrontend::AprunLaunchInfo
ALPSFrontend::getAprunLaunchInfo(uint64_t aprunId)
{
    // Produce managed objects from libALPS allocating function
    auto const getAppInfoVer2Err = [&](uint64_t aprunId) {
        // Allocate and fill ALPS data structures from libALPS
        auto alpsAppInfo = std::make_unique<appInfo_t>();
        cmdDetail_t *alpsCmdDetail;
        placeNodeList_t *alpsPlaceNodeList;
        char *libAlpsError = nullptr;

        alpsAppInfo->apid = aprunId;

        // Run and check result
        if (m_libAlps.alps_get_appinfo_ver2_err(aprunId,
            alpsAppInfo.get(), &alpsCmdDetail, &alpsPlaceNodeList,
            &libAlpsError, (int*)nullptr) != 1) {

            throw std::runtime_error((libAlpsError != nullptr)
                ? libAlpsError
                : "alps_get_appinfo_ver2_err");
        }

        return std::make_tuple(
            std::move(alpsAppInfo),
            cti::take_pointer_ownership(std::move(alpsCmdDetail),     ::free),
            cti::take_pointer_ownership(std::move(alpsPlaceNodeList), ::free)
        );
    };

    // Run libALPS query
    auto [alpsAppInfo, alpsCmdDetail, alpsPlaceNodeList] = getAppInfoVer2Err(aprunId);

    // Register APRUN as application with CTI Daemon
    auto const aprunPid = alpsAppInfo->aprunPid;
    auto const daemonAppId = Daemon().request_RegisterApp(aprunPid);

    // Fill result
    auto result = AprunLaunchInfo
        { .daemonAppId = daemonAppId
        , .alpsAppInfo = std::move(alpsAppInfo)
        , .pe0Node = alpsPlaceNodeList.get()[0].nid
        , .hostsPlacement = {}
        , .barrierReleaseFd = -1
        , .barrierReleaseSync = -1
    };

    result.hostsPlacement.reserve(result.alpsAppInfo->numPlaces);
    for (int i = 0; i < result.alpsAppInfo->numPlaces; i++) {
        result.hostsPlacement.emplace_back( CTIHost
            { cti::cstr::asprintf(ALPS_XT_HOSTNAME_FMT, alpsPlaceNodeList.get()[i].nid)
            , (size_t)alpsPlaceNodeList.get()[i].numPEs
        } );
    }

    return result;
}

uint64_t
ALPSFrontend::getApid(pid_t aprunPid) const
{
    // Look up apid using NID and aprun PID
    return m_libAlps.alps_get_apid(getSvcNid(), aprunPid);
}

ALPSFrontend::ALPSFrontend()
    : m_libAlpsPath{cti::accessiblePath("/opt/cray/alps/default/lib64/" + std::string{ALPS_FE_LIB_NAME})}
    , m_libAlps{m_libAlpsPath}
{}

/* ALPSApp implementation */

std::string
ALPSApp::getJobId() const
{
    return std::to_string(m_alpsAppInfo->apid);
}

std::string
ALPSApp::getLauncherHostname() const
{
    return cti::cstr::asprintf(ALPS_XT_HOSTNAME_FMT, m_alpsAppInfo->aprunNid);
}

bool
ALPSApp::isRunning() const
{
    return m_frontend.Daemon().request_CheckApp(m_daemonAppId);
}

size_t
ALPSApp::getNumPEs() const
{
    return std::accumulate(m_hostsPlacement.begin(), m_hostsPlacement.end(), 0,
        [](int total, CTIHost const& host) { return total + host.numPEs; });
}

size_t
ALPSApp::getNumHosts() const
{
    return m_hostsPlacement.size();
}

std::vector<std::string>
ALPSApp::getHostnameList() const
{
    auto result = std::vector<std::string>{};

    std::transform(m_hostsPlacement.begin(), m_hostsPlacement.end(), std::back_inserter(result),
        [](CTIHost const& host) { return host.hostname; });

    return result;
}

void
ALPSApp::releaseBarrier()
{
    if ((m_barrierReleaseFd < 0) || (m_barrierReleaseSync < 0)) {
        throw std::runtime_error("application is not at startup barrier");
    }

    // Conduct a pipe write for alps to release app from the startup barrier.
    // Just write back what we read earlier.
    if (::write(m_barrierReleaseFd, &m_barrierReleaseSync, sizeof(m_barrierReleaseSync)) <= 0) {
        throw std::runtime_error("Aprun barrier release operation failed.");
    }
    ::close(m_barrierReleaseFd);
    m_barrierReleaseFd = -1;
    m_barrierReleaseSync = -1;
}

// Suppress stdout / stderr for libALPS functions that write messages
struct OutputSupressor {
    int stdout_fd, stderr_fd;

    OutputSupressor()
        : stdout_fd{::dup(STDOUT_FILENO)}
        , stderr_fd{::dup(STDERR_FILENO)}
    {
        ::dup2(::open("/dev/null", O_WRONLY), STDOUT_FILENO);
        ::dup2(::open("/dev/null", O_WRONLY), STDERR_FILENO);
    }
    ~OutputSupressor() {
        ::dup2(stdout_fd, STDOUT_FILENO);
        ::dup2(stderr_fd, STDERR_FILENO);
    }
};

void
ALPSApp::kill(int signal)
{
    // create the args for apkill
    auto apkillArgv = cti::ManagedArgv {
        APKILL // first argument should be "apkill"
        , "-" + std::to_string(signal) // second argument is -signal
        , std::to_string(m_alpsAppInfo->apid) // third argument is apid
    };

    // tell frontend daemon to launch scancel, wait for it to finish
    m_frontend.Daemon().request_ForkExecvpUtil_Sync(
        m_daemonAppId, APKILL, apkillArgv.get(),
        -1, -1, -1,
        nullptr);
}

static constexpr auto LAUNCH_TOOL_RETRY = 5;

void
ALPSApp::shipPackage(std::string const& tarPath) const
{
    auto rawTarPath = cti::take_pointer_ownership(::strdup(tarPath.c_str()), ::free);

    auto libAlpsError = (char const*){nullptr};
    { OutputSupressor outputSupressor;
        for (int i = 0; i < LAUNCH_TOOL_RETRY; i++) {
            auto rawTarPathPtr = rawTarPath.get();
            libAlpsError = m_libAlpsRef.alps_launch_tool_helper(m_alpsAppInfo->apid, m_pe0Node, 1, 0, 1, &rawTarPathPtr);

            if (libAlpsError == nullptr) {
                return;
            }

            usleep(500000);
        }
    }

    auto const error_msg = (libAlpsError != nullptr)
        ? "alps_launch_tool_helper: " + std::string{libAlpsError}
        : "alps_launch_tool_helper";
    throw std::runtime_error(error_msg);
}

void
ALPSApp::startDaemon(const char* const args[])
{
    // sanity check
    if (args == nullptr) {
        throw std::runtime_error("args array is null!");
    }

    auto const transferDaemon = m_beDaemonSent ? 0 : 1;
    auto const beDaemonPath = m_beDaemonSent
        ? m_toolPath + "/" + CTI_BE_DAEMON_BINARY // Reuse daemon already on backend
        : m_frontend.getBEDaemonPath();

    // Build command string
    auto commandStream = std::stringstream{};
    commandStream << beDaemonPath;

    if (args != nullptr) {
        for (const char* const* arg = args; *arg != nullptr; arg++) {
            commandStream << " " << *arg;
        }
    }
    auto const command = commandStream.str();

    auto rawCommand = cti::take_pointer_ownership(::strdup(command.c_str()), ::free);
    auto rawCommandPtr = rawCommand.get();

    if (auto const libAlpsError = m_libAlpsRef.alps_launch_tool_helper(m_alpsAppInfo->apid, m_pe0Node, transferDaemon, 1, 1, &rawCommandPtr)) {
        throw std::runtime_error("alps_launch_tool_helper: " + std::string{libAlpsError});
    }

    if (transferDaemon) {
        m_beDaemonSent = true;
    }
}

uint64_t
ALPSApp::getApid() const
{
    return m_alpsAppInfo->apid;
}

cti_aprunProc_t
ALPSApp::get_cti_aprunProc_t() const
{
    return cti_aprunProc_t
        { m_alpsAppInfo->apid
        , m_alpsAppInfo->aprunPid
    };
}

int
ALPSApp::getAlpsOverlapOrdinal() const
{
    char *libAlpsError = nullptr;

    auto const result = m_libAlpsRef.alps_get_overlap_ordinal(m_alpsAppInfo->apid, &libAlpsError, nullptr);

    if (result < 0) {
        throw std::runtime_error((libAlpsError != nullptr)
            ? libAlpsError
            : "alps_get_overlap_ordinal");
    }

    return result;
}

ALPSApp::ALPSApp(ALPSFrontend& fe, ALPSFrontend::AprunLaunchInfo&& aprunInfo)
    : App{fe}

    , m_beDaemonSent{false}

    , m_libAlpsRef{fe.m_libAlps}

    , m_daemonAppId{aprunInfo.daemonAppId}
    , m_alpsAppInfo{std::move(aprunInfo.alpsAppInfo)}
    , m_pe0Node{aprunInfo.pe0Node}
    , m_hostsPlacement{std::move(aprunInfo.hostsPlacement)}

    , m_barrierReleaseFd{aprunInfo.barrierReleaseFd}
    , m_barrierReleaseSync{aprunInfo.barrierReleaseSync}

    , m_toolPath{}
    , m_attribsPath{}
    , m_stagePath{}
    , m_extraFiles{}
{

    // Check to see if this system is using the new OBS system for the alps
    // dependencies. This will affect the way we set the toolPath for the backend
    struct stat statbuf;
    auto const apid = m_alpsAppInfo->apid;
    if (::stat(ALPS_OBS_LOC, &statbuf) < 0) {
        // Could not stat ALPS_OBS_LOC, assume it's using the old format.
        m_toolPath = cti::cstr::asprintf(OLD_TOOLHELPER_DIR, apid, apid);
        m_attribsPath = cti::cstr::asprintf(OLD_ATTRIBS_DIR, apid);
    } else {
        // Assume it's using the OBS format
        m_toolPath = cti::cstr::asprintf(OBS_TOOLHELPER_DIR, apid, apid);
        m_attribsPath = cti::cstr::asprintf(OBS_ATTRIBS_DIR, apid);
    }

}

// The following code was added to detect if a site is using a wrapper script
// around aprun. Some sites use these as prologue/epilogue. I know this
// functionality has been added to alps, but sites are still using the
// wrapper. If this is no longer true in the future, rip this stuff out.

// If the executable under launchedPid does not have the basename of launcherName,
// then CTI will sleep and retry. This is due to a race condition where CTI has
// forked, but hasn't yet execed the launcher process.

// FIXME: This doesn't handle multiple layers of depth.
static pid_t
findRealAprunPid(std::string const& launcherName, pid_t launchedPid)
{
    auto const _cti_alps_checkPathForWrappedAprun = [](char const* aprun_path) {
        char *          usr_aprun_path;
        char *          default_obs_realpath = NULL;
        struct stat     buf;
        
        // The following is used when a user sets the CRAY_APRUN_PATH environment
        // variable to the absolute location of aprun. It overrides the default
        // behavior.
        if ((usr_aprun_path = getenv(USER_DEF_APRUN_LOC_ENV_VAR)) != NULL)
        {
            // There is a path to aprun set, lets try to stat it to make sure it
            // exists
            if (stat(usr_aprun_path, &buf) == 0)
            {
                // We were able to stat it! Lets check aprun_path against it
                if (strncmp(aprun_path, usr_aprun_path, strlen(usr_aprun_path)))
                {
                    // This is a wrapper. Return 1.
                    return 1;
                }
                
                // This is a real aprun. Return 0.
                return 0;
            } else
            {
                // We were unable to stat the file pointed to by usr_aprun_path, lets
                // print a warning and fall back to using the default method.
                throw std::runtime_error(std::string{USER_DEF_APRUN_LOC_ENV_VAR} + " is set but cannot stat its value.");
            }
        }
        
        // check to see if the path points at the old aprun location
        if (strncmp(aprun_path, OLD_APRUN_LOCATION, strlen(OLD_APRUN_LOCATION)))
        {
            // it doesn't point to the old aprun location, so check the new OBS
            // location. Note that we need to resolve this location with a call to 
            // realpath.
            if ((default_obs_realpath = realpath(OBS_APRUN_LOCATION, NULL)) == NULL)
            {
                // Fix for BUG 810204 - Ensure that the OLD_APRUN_LOCATION exists before giving up.
                if ((default_obs_realpath = realpath(OLD_APRUN_LOCATION, NULL)) == NULL)
                {
                    fprintf(stderr, "Could not resolve realpath of aprun.");
                    // FIXME: Assume this is the real aprun...
                    return 0;
                }
                // This is a wrapper. Return 1.
                free(default_obs_realpath);
                return 1;
            }
            // Check the string
            if (strncmp(aprun_path, default_obs_realpath, strlen(default_obs_realpath)))
            {
                // This is a wrapper. Return 1.
                free(default_obs_realpath);
                return 1;
            }
            // cleanup
            free(default_obs_realpath);
        }

        // This is a real aprun, return 0
        return 0;
    };

    // first read the link of the exe in /proc for the aprun pid.

    // create the path to the /proc/<pid>/exe location
    auto const procExePath = "/proc/" + std::to_string(launchedPid) + "/exe";
    auto realExePath = cti::cstr::readlink(procExePath);

    // Sleep and retry if CTI hasn't execed launcher yet
    for (int retry = 0; retry < 5; retry++) {
        realExePath = cti::cstr::readlink(procExePath);
        if (cti::cstr::basename(realExePath) == launcherName) {
            break;
        }
        sleep(1);
    }

    // check the link path to see if its the real aprun binary
    if (_cti_alps_checkPathForWrappedAprun(realExePath.c_str())) {

        // aprun is wrapped, we need to start harvesting stuff out from /proc.
        if (auto procDirPtr = cti::take_pointer_ownership(::opendir("/proc"), closedir)) {
            while (auto ent = ::readdir(procDirPtr.get())) {
                if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) {
                    continue;
                }

                pid_t potentialAprunPid;
                try {
                    potentialAprunPid = std::stoi(ent->d_name);
                } catch (...) {
                    continue;
                }

                // create the path to the /proc/<pid>/stat for this entry
                auto const statFilePath = cti::cstr::asprintf("/proc/%d/stat", potentialAprunPid);

                // open the stat file for reading
                if (auto const statFile = cti::file::open(statFilePath, "r")) {

                    // parse the stat file for the ppid
                    pid_t proc_ppid;
                    if (::fscanf(statFile.get(), "%*d %*s %*c %d", &proc_ppid) != 1) {
                        // could not get the ppid?? continue to the next entry
                        continue;
                    }

                    // check to see if the ppid matches the pid of our child
                    if (proc_ppid == launchedPid) {

                        // it matches, check to see if this is the real aprun
                        auto const nestedProcExePath = "/proc/" + std::to_string(potentialAprunPid) + "/exe";

                        // check the link path to see if its the real aprun binary
                        if (_cti_alps_checkPathForWrappedAprun(nestedProcExePath.c_str())) {

                            // success! This is the real aprun
                            return potentialAprunPid;
                        }
                    }

                } else {
                    // ignore this entry and go onto the next
                    continue;
                }
            }

            throw std::runtime_error("Could not find child aprun process of wrapped aprun command.");
        } else {
            throw std::runtime_error("Could not enumerate /proc for real aprun process.");
        }
    } else {
        // aprun not nested
        return launchedPid;
    }
}

ALPSFrontend::AprunLaunchInfo
ALPSFrontend::launchApp(const char * const launcher_argv[], int stdout_fd,
    int stderr_fd, const char *inputFile, const char *chdirPath, const char * const env_list[])
{
    // Get the launcher path from CTI environment variable / default.
    if (auto const launcher_path = cti::take_pointer_ownership(_cti_pathFind(getLauncherName().c_str(), nullptr), std::free)) {

        if (auto launcherPid = fork()) {
            if (launcherPid < 0) {
                throw std::runtime_error("fork failed");
            }

            struct PidGuard {
                pid_t pid;
                PidGuard(pid_t&& pid_) : pid{pid_} {}
                ~PidGuard() { if (pid > 0) { ::kill(pid, SIGKILL); } }
                pid_t get() const { return pid; }
                pid_t eject() { auto const result = pid; pid = -1; return result; }
            };

            auto launcherPidGuard = PidGuard{std::move(launcherPid)};

            // Find wrapped APRUN pid, if detected as wrapped
            auto const aprunPid = findRealAprunPid(getLauncherName(), launcherPidGuard.get());

            // Get ALPS info from real APRUN PID
            auto aprunInfo = getAprunLaunchInfo(getApid(aprunPid));

            // if APRUN is wrapped, register the wrapper as a utility
            if (aprunPid != launcherPidGuard.get()) {
                Daemon().request_RegisterUtil(aprunInfo.daemonAppId, launcherPidGuard.eject());
            } else {
                launcherPidGuard.eject();
            }

            return aprunInfo;

        } else {
            // set up arguments and FDs
            if (inputFile == nullptr) { inputFile = "/dev/null"; }
            if (::dup2(::open(inputFile, O_RDONLY), STDIN_FILENO) < 0) {
                perror("dup2");
                _exit(-1);
            }
            if ((stdout_fd >= 0) && (::dup2(stdout_fd, STDOUT_FILENO) < 0)) {
                perror("dup2");
                _exit(-1);
            }
            if ((stderr_fd >= 0) && (::dup2(stderr_fd, STDERR_FILENO) < 0)) {
                perror("dup2");
                _exit(-1);
            }

            // construct argv array & instance
            cti::ManagedArgv launcherArgv{ launcher_path.get() };
            for (const char* const* arg = launcher_argv; *arg != nullptr; arg++) {
                launcherArgv.add(*arg);
            }

            // chdir if directed
            if ((chdirPath != nullptr) && (::chdir(chdirPath) < 0)) {
                perror("chdir");
                _exit(-1);
            }

            // if env_list is not null, call putenv for each entry in the list
            if (env_list != nullptr) {
                for (auto env_var = env_list; *env_var != nullptr; env_var++) {
                    if ((*env_var != nullptr) && (::putenv(::strdup(*env_var)) < 0)) {
                        perror("putenv");
                        _exit(-1);
                    }
                }
            }

            // exec aprun
            ::execvp(getLauncherName().c_str(), launcherArgv.get());

            // exec shouldn't return
            fprintf(stderr, "CTI error: Return from exec.\n");
            perror("execvp");
            _exit(-1);
        }

    } else {
        throw std::runtime_error("Failed to find launcher in path: " + getLauncherName());
    }
}

ALPSFrontend::AprunLaunchInfo
ALPSFrontend::launchAppBarrier(const char * const launcher_argv[], int stdout_fd,
    int stderr_fd, const char *inputFile, const char *chdirPath, const char * const env_list[])
{
    // Get the launcher path from CTI environment variable / default.
    if (auto const launcher_path = cti::take_pointer_ownership(_cti_pathFind(getLauncherName().c_str(), nullptr), std::free)) {

        auto const ReadEnd  = 0;
        auto const WriteEnd = 1;
        int ctiToAprunPipe[2] = {-1, -1};
        int aprunToCtiPipe[2] = {-1, -1};

        // Set up barrier pipe and arguments
        if (::pipe(ctiToAprunPipe) || ::pipe(aprunToCtiPipe)) {
            throw std::runtime_error("pipe failed");
        }

        if (auto launcherPid = fork()) {
            if (launcherPid < 0) {
                throw std::runtime_error("fork failed");
            }

            struct PidGuard {
                pid_t pid;
                PidGuard(pid_t&& pid_) : pid{pid_} {}
                ~PidGuard() { if (pid > 0) { ::kill(pid, SIGKILL); } }
                pid_t get() const { return pid; }
                pid_t eject() { auto const result = pid; pid = -1; return result; }
            };

            auto launcherPidGuard = PidGuard{std::move(launcherPid)};

            // Close unused ends of pipes
            ::close(ctiToAprunPipe[ReadEnd]);
            ::close(aprunToCtiPipe[WriteEnd]);

            // Wait on pipe read for app to start and get to barrier - once this happens
            // we know the real aprun is up and running
            int syncInt;
            if (::read(aprunToCtiPipe[ReadEnd], &syncInt, sizeof(syncInt)) <= 0) {
                throw std::runtime_error("sync pipe read failed");
            }
            ::close(aprunToCtiPipe[ReadEnd]);

            // Find wrapped APRUN pid, if detected as wrapped
            auto const aprunPid = findRealAprunPid(getLauncherName(), launcherPidGuard.get());

            // Get ALPS info from real APRUN PID
            auto aprunInfo = getAprunLaunchInfo(getApid(aprunPid));

            // Save barrier release FD
            aprunInfo.barrierReleaseFd = std::move(ctiToAprunPipe[WriteEnd]);
            aprunInfo.barrierReleaseSync = std::move(syncInt);

            // if APRUN is wrapped, register the wrapper as a utility
            if (aprunPid != launcherPidGuard.get()) {
                Daemon().request_RegisterUtil(aprunInfo.daemonAppId, launcherPidGuard.eject());
            } else {
                launcherPidGuard.eject();
            }

            return aprunInfo;

        } else {
            // close unused ends of pipe
            ::close(ctiToAprunPipe[WriteEnd]);
            ::close(aprunToCtiPipe[ReadEnd]);

            // set up arguments and FDs
            if (inputFile == nullptr) { inputFile = "/dev/null"; }
            if (::dup2(::open(inputFile, O_RDONLY), STDIN_FILENO) < 0) {
                perror("dup2");
                _exit(-1);
            }
            if ((stdout_fd >= 0) && (::dup2(stdout_fd, STDOUT_FILENO) < 0)) {
                perror("dup2");
                _exit(-1);
            }
            if ((stderr_fd >= 0) && (::dup2(stderr_fd, STDERR_FILENO) < 0)) {
                perror("dup2");
                _exit(-1);
            }

            // construct argv array & instance
            cti::ManagedArgv launcherArgv
                { launcher_path.get()
                , "-P", std::to_string(aprunToCtiPipe[WriteEnd]) + "," +
                    std::to_string(ctiToAprunPipe[ReadEnd])
            };
            for (const char* const* arg = launcher_argv; *arg != nullptr; arg++) {
                launcherArgv.add(*arg);
            }

            // chdir if directed
            if ((chdirPath != nullptr) && (::chdir(chdirPath) < 0)) {
                perror("chdir");
                _exit(-1);
            }

            // if env_list is not null, call putenv for each entry in the list
            if (env_list != nullptr) {
                for (auto env_var = env_list; *env_var != nullptr; env_var++) {
                    if ((*env_var != nullptr) && (::putenv(::strdup(*env_var)) < 0)) {
                        perror("putenv");
                        _exit(-1);
                    }
                }
            }

            // exec aprun
            ::execvp(getLauncherName().c_str(), launcherArgv.get());

            // exec shouldn't return
            fprintf(stderr, "CTI error: Return from exec.\n");
            perror("execvp");
            _exit(-1);
        }

    } else {
        throw std::runtime_error("Failed to find launcher in path: " + getLauncherName());
    }
}

ALPSApp::~ALPSApp()
{}