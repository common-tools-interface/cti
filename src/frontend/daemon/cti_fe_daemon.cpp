/******************************************************************************\
 * cti_fe_daemon.cpp - cti fe_daemon process used to ensure child
 *                     processes will be cleaned up on unexpected exit.
 *
 * Copyright 2019-2020 Hewlett Packard Enterprise Development LP.
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

#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <future>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>

#include "cti_defs.h"
#include "useful/cti_argv.hpp"
#include "useful/cti_execvp.hpp"
#include "useful/cti_wrappers.hpp"
#include "useful/cti_split.hpp"

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/random_generator.hpp>

#include "frontend/mpir_iface/MPIRInstance.hpp"
#include "cti_fe_daemon_iface.hpp"

using DAppId = FE_daemon::DaemonAppId;

using ReqType  = FE_daemon::ReqType;
using RespType = FE_daemon::RespType;
using OKResp   = FE_daemon::OKResp;
using IDResp   = FE_daemon::IDResp;
using StringResp = FE_daemon::StringResp;
using MPIRResp = FE_daemon::MPIRResp;

cti::Logger&
getLogger(void)
{
    static auto _cti_logger = []() {
        // Check if logging is enabled in environment
        if (getenv(CTI_DBG_ENV_VAR)) {
            // Get logging setting / directory from environment
            if (auto const cti_log_dir = getenv(CTI_LOG_DIR_ENV_VAR)) {
                // Check directory permissions
                if (cti::dirHasPerms(cti_log_dir, R_OK | W_OK | X_OK)) {
                    return cti::Logger
                        { true // enabled
                        , std::string{cti_log_dir}
                        , "cti_fe_daemon"
                        , getpid()
                    };
                }
            }
        }
        // Logging disabled
        return cti::Logger{false, "", "", 0};
    }();
    return _cti_logger;
}

static void
tryTerm(pid_t const pid)
{
    if (::kill(pid, SIGTERM)) {
        return;
    }
    ::sleep(3);
    ::kill(pid, SIGKILL);
    cti::waitpid(pid, nullptr, 0);
}

/* types */

struct ProcSet
{
    std::unordered_set<pid_t> m_pids;

    ProcSet() {}

    ProcSet(ProcSet&& moved)
        : m_pids{std::move(moved.m_pids)}
    {
        moved.m_pids.clear();
    }

    void clear()
    {
        // copy and clear member
        auto const pids = m_pids;
        m_pids.clear();

        // create futures
        std::vector<std::future<void>> termFutures;
        termFutures.reserve(m_pids.size());

        // terminate in parallel
        for (auto&& pid : pids) {
            termFutures.emplace_back(std::async(std::launch::async, tryTerm, pid));
        }

        // collect
        for (auto&& future : termFutures) {
            future.wait();
        }
    }

    ~ProcSet()
    {
        if (!m_pids.empty()) {
            clear();
        }
    }

    void insert(pid_t const pid)   { m_pids.insert(pid); }
    void erase(pid_t const pid)    { m_pids.erase(pid);  }
    bool contains(pid_t const pid) { return (m_pids.find(pid) != m_pids.end()); }
};

/* global variables */

static DAppId newId() {
    static auto id = DAppId{0};
    return ++id;
}
auto pidIdMap = std::unordered_map<pid_t, DAppId>{};
auto idPidMap = std::unordered_map<pid_t, DAppId>{};

// running apps / utils
auto appCleanupList = ProcSet{};
auto utilMap = std::unordered_map<DAppId, ProcSet>{};

auto mpirMap     = std::unordered_map<DAppId, std::unique_ptr<MPIRInstance>>{};

// communication
int reqFd  = -1; // incoming request pipe
int respFd = -1; // outgoing response pipe

// threading helpers
std::vector<std::future<void>> runningThreads;
template <typename Func>
static void start_thread(Func&& func) {
    runningThreads.emplace_back(std::async(std::launch::async, func));
}
static void finish_threads() {
    for (auto&& future : runningThreads) {
        future.wait();
    }
}

/* runtime helpers */

static void
usage(char *name)
{
    fprintf(stdout, "Usage: %s [OPTIONS]...\n", name);
    fprintf(stdout, "Create fe_daemon process to ensure children are cleaned up on parent exit\n");
    fprintf(stdout, "This should not be called directly.\n\n");

    fprintf(stdout, "\t-%c, --%s  fd of read control pipe         (required)\n",
        CTIFEDaemonArgv::ReadFD.val, CTIFEDaemonArgv::ReadFD.name);
    fprintf(stdout, "\t-%c, --%s  fd of write control pipe        (required)\n",
        CTIFEDaemonArgv::WriteFD.val, CTIFEDaemonArgv::WriteFD.name);
    fprintf(stdout, "\t-%c, --%s  Display this text and exit\n\n",
        CTIFEDaemonArgv::Help.val, CTIFEDaemonArgv::Help.name);
}

static void
shutdown_and_exit(int const rc)
{
    // block all signals
    sigset_t block_set;
    memset(&block_set, 0, sizeof(block_set));
    if (sigfillset(&block_set)) {
        fprintf(stderr, "sigfillset: %s\n", strerror(errno));
        exit(1);
    }
    if (sigprocmask(SIG_SETMASK, &block_set, nullptr)) {
        fprintf(stderr, "sigprocmask: %s\n", strerror(errno));
        exit(1);
    }

    // terminate all running utilities
    auto utilTermFuture = std::async(std::launch::async, [&](){ utilMap.clear(); });

    // terminate all running apps
    auto appTermFuture = std::async(std::launch::async, [&](){ appCleanupList.clear(); });

    // wait for all threads
    utilTermFuture.wait();
    appTermFuture.wait();
    finish_threads();

    // close pipes
    close(reqFd);
    close(respFd);

    exit(rc);
}

/* signal handlers */

static void
sigchld_handler(pid_t const exitedPid)
{
    // regular app termination
    if (appCleanupList.contains(exitedPid)) {
        // app already terminated
        appCleanupList.erase(exitedPid);
    }

    // find ID associated with exited PID
    auto const pidIdPair = pidIdMap.find(exitedPid);
    if (pidIdPair != pidIdMap.end()) {
        auto const exitedId = pidIdPair->second;

        // terminate all of app's utilities
        if (utilMap.find(exitedId) != utilMap.end()) {
            start_thread([&](){ utilMap.erase(exitedId); });
        }
    }
}

// dispatch to sigchld / term handler
static void
cti_fe_daemon_handler(int sig, siginfo_t *sig_info, void *secret)
{
    if (sig == SIGCHLD) {
        if ((sig_info->si_code == CLD_EXITED) && (sig_info->si_pid > 1)) {
            sigchld_handler(sig_info->si_pid);
        }
    } else if ((sig == SIGTERM) || (sig == SIGHUP)) {
        shutdown_and_exit(0);
    } else {
        // TODO: determine which signals should be relayed to child
    }
}

/* registration helpers */

static DAppId registerAppPID(pid_t const app_pid)
{
    if ((app_pid > 0) && (pidIdMap.find(app_pid) == pidIdMap.end())) {
        // create new app ID for pid
        auto const appId = newId();
        pidIdMap[app_pid] = appId;
        idPidMap[appId] = app_pid;
        return appId;
    } else {
        throw std::runtime_error("invalid app pid: " + std::to_string(app_pid));
    }
}

static void registerUtilPID(DAppId const app_id, pid_t const util_pid)
{
    // verify app pid
    if (idPidMap.find(app_id) == idPidMap.end()) {
        throw std::runtime_error("invalid app id: " + std::to_string(app_id));
    }

    // register utility pid to app
    if (util_pid > 0) {
        utilMap[app_id].insert(util_pid);
    } else {
        throw std::runtime_error("invalid util pid: " + std::to_string(util_pid));
    }
}

static void deregisterAppID(DAppId const app_id)
{
    auto const idPidPair = idPidMap.find(app_id);
    if (idPidPair != idPidMap.end()) {
        auto const app_pid = idPidPair->second;

        // remove from ID list
        idPidMap.erase(idPidPair);
        pidIdMap.erase(app_pid);

        // terminate all of app's utilities
        auto utilTermFuture = std::async(std::launch::async,
            [&](){ utilMap.erase(app_id); });

        // ensure app is terminated
        if (appCleanupList.contains(app_pid)) {
            auto appTermFuture = std::async(std::launch::async, [&](){ tryTerm(app_pid); });
            appCleanupList.erase(app_pid);
            appTermFuture.wait();
        }

        // finish util termination
        utilTermFuture.wait();
    } else {
        throw std::runtime_error("invalid app id: " + std::to_string(app_id));
    }
}

static bool checkAppID(DAppId const app_id)
{
    auto const idPidPair = idPidMap.find(app_id);
    if (idPidPair != idPidMap.end()) {
        auto const app_pid = idPidPair->second;

        // Check if app's PID is still valid
        getLogger().write("check pid %d\n", app_pid);
        if (::kill(app_pid, 0) == 0) {
            // Check if zombie
            auto const statusFilePath = "/proc/" + std::to_string(app_pid) + "/status";
            char const* grepArgv[] = { "grep", "Z (zombie)", statusFilePath.c_str(), nullptr };
            auto grepOutput = cti::Execvp{"grep", (char* const*)grepArgv, cti::Execvp::stderr::Ignore};
            auto const grepExitStatus = grepOutput.getExitStatus();
            auto const pidZombie = (grepExitStatus == 0);

            static int count = 0;
            getLogger().write("%05d %s: %s\n", count++, statusFilePath.c_str(),
                pidZombie ? "zombie" : "no zombie");

            return !pidZombie;
        } else {
            getLogger().write("kill %d sig 0 failed\n", app_pid);

            // PID no longer valid
            return false;
        }
    } else {
        throw std::runtime_error("invalid app id: " + std::to_string(app_id));
    }
}

/* protocol helpers */

struct LaunchData {
    int stdin_fd, stdout_fd, stderr_fd;
    std::string filepath;
    std::vector<std::string> argvList;
    std::vector<std::string> envList;
};

struct ShimData {
    std::string shimBinaryPath;
    std::string temporaryShimBinDir;
    std::string shimmedLauncherPath;
};

// read stdin/out/err fds, filepath, argv, environment map appended to an app / util / mpir launch request
static LaunchData readLaunchData(int const reqFd)
{
    LaunchData result;

    // receive a single null-terminated string from stream
    auto receiveString = [](std::istream& reqStream) {
        std::string result;
        if (!std::getline(reqStream, result, '\0')) {
            throw std::runtime_error("failed to read string");
        }
        return result;
    };

    // read and remap stdin/out/err
    auto const N_FDS = 3;
    struct {
        int fd_data[N_FDS];
        struct cmsghdr cmd_hdr;
    } buffer;

    // create message buffer for one character
    struct iovec empty_iovec;
    char c;
    empty_iovec.iov_base = &c;
    empty_iovec.iov_len  = 1;

    // create empty message header with enough space for fds
    struct msghdr msg_hdr = {};
    msg_hdr.msg_iov     = &empty_iovec;
    msg_hdr.msg_iovlen  = 1;
    msg_hdr.msg_control = &buffer;
    msg_hdr.msg_controllen = CMSG_SPACE(sizeof(int) * N_FDS);

    // fill in the message header type
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg_hdr);
    cmsg->cmsg_len   = CMSG_LEN(sizeof(int) * N_FDS);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type  = SCM_RIGHTS;

    // receive remap FD message
    if (::recvmsg(reqFd, &msg_hdr, 0) < 0) {
        throw std::runtime_error("failed to receive fds: " + std::string{strerror(errno)});
    }
    auto const fds = (int *)CMSG_DATA(cmsg);
    result.stdin_fd  = fds[0];
    result.stdout_fd = fds[1];
    result.stderr_fd = fds[2];

    // set up pipe stream
    cti::FdBuf reqBuf{dup(reqFd)};
    std::istream reqStream{&reqBuf};

    // read filepath
    getLogger().write("recv filename\n");
    result.filepath = receiveString(reqStream);
    getLogger().write("got file: %s\n", result.filepath.c_str());

    // read arguments
    { std::stringstream argvLog;
        while (true) {
            auto const arg = receiveString(reqStream);
            if (arg.empty()) {
                break;
            } else {
                argvLog << arg << " ";
                result.argvList.emplace_back(std::move(arg));
            }
        }
        auto const argvString = argvLog.str();
        getLogger().write("%s\n", argvString.c_str());
    }

    // read env
    while (true) {
        auto const envVarVal = receiveString(reqStream);
        if (envVarVal.empty()) {
            break;
        } else {
            getLogger().write("got envvar: %s\n", envVarVal.c_str());
            result.envList.emplace_back(std::move(envVarVal));
        }
    }

    return result;
}

// if running the function succeeds, write an OK response to pipe
template <typename Func>
static void tryWriteOKResp(int const respFd, Func&& func)
{
    try {
        // run the function
        auto const success = func();

        // send OK response
        fdWriteLoop(respFd, OKResp
            { .type = RespType::OK
            , .success = success
        });

    } catch (std::exception const& ex) {
        getLogger().write("%s\n", ex.what());

        // send failure response
        fdWriteLoop(respFd, OKResp
            { .type = RespType::OK
            , .success = false
        });
    }
}

// if running the pid-producing function succeeds, write a PID response to pipe
template <typename Func>
static void tryWriteIDResp(int const respFd, Func&& func)
{
    try {
        // run the id-producing function
        auto const id = func();

        // send ID response
        fdWriteLoop(respFd, IDResp
            { .type = RespType::ID
            , .id  = id
        });

    } catch (std::exception const& ex) {
        getLogger().write("%s\n", ex.what());

        // send failure response
        fdWriteLoop(respFd, IDResp
            { .type = RespType::ID
            , .id  = DAppId{0}
        });
    }
}

// if running the function succeeds, write a string response to pipe
template <typename Func>
static void tryWriteStringResp(int const respFd, Func&& func)
{
    try {
        // run the string-producing function
        auto const stringData = func();

        // send the string data
        fdWriteLoop(respFd, StringResp
            { .type     = RespType::String
            , .success  = true
        });
        fdWriteLoop(respFd, stringData.c_str(), stringData.length() + 1);

    } catch (std::exception const& ex) {
        getLogger().write("%s\n", ex.what());

        // send failure response
        fdWriteLoop(respFd, StringResp
            { .type     = RespType::String
            , .success  = false
        });
    }
}

// if running the function succeeds, write an MPIR response to pipe
template <typename Func>
static void tryWriteMPIRResp(int const respFd, Func&& func)
{
    try {
        // run the mpir-producing function
        auto const mpirData = func();

        // send the MPIR data
        fdWriteLoop(respFd, MPIRResp
            { .type     = RespType::MPIR
            , .mpir_id  = mpirData.mpir_id
            , .launcher_pid = mpirData.launcher_pid
            , .num_pids = static_cast<int>(mpirData.proctable.size())
            , .error_msg_len = 0
        });
        for (auto&& elem : mpirData.proctable) {
            fdWriteLoop(respFd, elem.pid);
            fdWriteLoop(respFd, elem.hostname.c_str(), elem.hostname.length() + 1);
            fdWriteLoop(respFd, elem.executable.c_str(), elem.executable.length() + 1);
        }

    } catch (std::exception const& ex) {
        getLogger().write("%s\n", ex.what());

        auto const error_msg_len = ::strlen(ex.what()) + 1;

        // send failure response
        fdWriteLoop(respFd, MPIRResp
            { .type     = RespType::MPIR
            , .mpir_id  = DAppId{0}
            , .launcher_pid = {}
            , .num_pids = {}
            , .error_msg_len = error_msg_len
        });

        // Send failure message
        fdWriteLoop(respFd, ex.what(), error_msg_len);
    }
}

/* process helpers */

static pid_t forkExec(LaunchData const& launchData)
{
    // construct argv
    cti::ManagedArgv argv;
    for (auto&& arg : launchData.argvList) {
        argv.add(arg);
    }

    // parse env
    std::unordered_map<std::string, std::string> envMap;
    for (auto&& envVarVal : launchData.envList) {
        auto const equalsAt = envVarVal.find("=");
        if (equalsAt == std::string::npos) {
            throw std::runtime_error("failed to parse env var: " + envVarVal);
        } else {
            getLogger().write("got envvar: %s\n", envVarVal.c_str());
            envMap.emplace(envVarVal.substr(0, equalsAt), envVarVal.substr(equalsAt + 1));
        }
    }

    getLogger().write("remap stdin %d stdout %d stderr %d\n",
        launchData.stdin_fd, launchData.stdout_fd, launchData.stderr_fd);

    // fork exec
    if (auto const forkedPid = fork()) {
        if (forkedPid < 0) {
            throw std::runtime_error("fork error: " + std::string{strerror(errno)});
        }

        // parent case

        return forkedPid;
    } else {
        // child case

        // close communication pipes
        close(reqFd);
        close(respFd);

        // dup2 all stdin/out/err to provided FDs
        dup2(launchData.stdin_fd,  STDIN_FILENO);
        dup2(launchData.stdout_fd, STDOUT_FILENO);
        dup2(launchData.stderr_fd, STDERR_FILENO);

        // set environment variables with overwrite
        for (auto const& envVarVal : envMap) {
            if (!envVarVal.second.empty()) {
                setenv(envVarVal.first.c_str(), envVarVal.second.c_str(), true);
            } else {
                unsetenv(envVarVal.first.c_str());
            }
        }

        // exec srun
        execvp(launchData.filepath.c_str(), argv.get());
        fprintf(stderr, "execvp: %s", strerror(errno));
        _exit(1);
    }
}

static FE_daemon::MPIRResult extractMPIRResult(std::unique_ptr<MPIRInstance>&& mpirInst)
{
    // create new app ID
    auto const launcherPid = mpirInst->getLauncherPid();
    auto const mpirId = registerAppPID(launcherPid);

    // extract proctable
    auto const proctable = mpirInst->getProctable();

    // add to MPIR map for later release
    mpirMap.emplace(std::make_pair(mpirId, std::move(mpirInst)));

    return FE_daemon::MPIRResult
        { mpirId // mpir_id
        , launcherPid // launcher_pid
        , proctable // proctable
    };
}

static FE_daemon::MPIRResult launchMPIR(LaunchData const& launchData)
{

    std::map<int, int> const remapFds
        { { launchData.stdin_fd,  STDIN_FILENO  }
        , { launchData.stdout_fd, STDOUT_FILENO }
        , { launchData.stderr_fd, STDERR_FILENO }
    };

    // Restores environment even in exception case
    struct env_var_restore {
        std::string var, val;
        bool clear;
        env_var_restore(std::string const& var_, std::string const& val_)
            : var{var_}, val{val_}, clear{false} {}
        env_var_restore(std::string const& var_)
            : var{var_}, val{}, clear{true} {}
        ~env_var_restore() {
            if (!var.empty()) {
                if (clear) {
                    ::unsetenv(var.c_str());
                } else {
                    ::setenv(var.c_str(), val.c_str(), true);
                }
            }
        }
        env_var_restore(env_var_restore&& moved) = default;
    };

    // Store environment variables that are going to be overwritten
    auto overwrittenEnv = std::vector<env_var_restore>{};
    for (auto&& envVarVal : launchData.envList) {
        // Get variable name and value to set
        auto const [var, val] = cti::split::string<2>(envVarVal, '=');
        // If variable is set in current environment, set it to restore on scope exit
        if (auto const oldVal = ::getenv(var.c_str())) {
            overwrittenEnv.emplace_back(var, oldVal);
        // If not set in environment, clear it on scope exit
        } else {
            overwrittenEnv.emplace_back(var);
        }
        // Set environment variable to inherit in MPIR instance
        ::setenv(var.c_str(), val.c_str(), true);
    }

    // Start launcher under MPIR control and run to breakpoint
    // If there are any problems with launcher arguments, they will occur at this point.
    // Then, an error message that the user can interpret will be sent back to the
    // main CTI process.
    auto mpirInstance = [](LaunchData const& launchData, std::map<int, int> const& remapFds) {
        try {
            return std::make_unique<MPIRInstance>(launchData.filepath,
                launchData.argvList, std::vector<std::string>{}, remapFds);
        } catch (std::exception const& ex) {
            auto errorMsg = std::stringstream{};

            // Create error message from launcher arguments with possible diagnostic
            errorMsg << "Failed to start launcher with the provided arguments: \n  ";
            for (auto&& arg : launchData.argvList) {
                errorMsg << " " << arg;
            }
            errorMsg << "\nEnsure that the launcher binary exists and \
that all arguments (such as job constraints or project accounts) required \
by your system are provided to the tool's launch command (" << ex.what() << ")";

            throw std::runtime_error{errorMsg.str()};
        }
    }(launchData, remapFds);

    auto mpirResult = extractMPIRResult(std::move(mpirInstance));

    // Terminate launched application on daemon exit
    appCleanupList.insert(mpirResult.launcher_pid);

    return mpirResult;
}

static FE_daemon::MPIRResult attachMPIR(std::string const& launcherPath, pid_t const launcherPid)
{
    // Attach to launcher and attempt to extract MPIR data
    auto mpirInstance = [](std::string const& launcherPath, pid_t const launcherPid) {
        try {
            return std::make_unique<MPIRInstance>(launcherPath, launcherPid);
        } catch (std::exception const& ex) {
            auto errorMsg = std::stringstream{};

            // Create error message from launcher arguments with possible diagnostic
            errorMsg << "Failed to attach to the launcher at '"
                     << launcherPath << "' under PID "
                     << launcherPid << ". Ensure that the launcher file exists at this path \
and that the provided PID is present on your local system (" << ex.what() << ")";

            throw std::runtime_error{errorMsg.str()};
        }
    }(launcherPath, launcherPid);

    return extractMPIRResult(std::move(mpirInstance));
}

static void releaseMPIR(DAppId const mpir_id)
{
    auto const idInstPair = mpirMap.find(mpir_id);
    if (idInstPair != mpirMap.end()) {

        // Release from MPIR breakpoint
        mpirMap.erase(idInstPair);
    } else {
        throw std::runtime_error("release mpir id not found: " + std::to_string(mpir_id));
    }

    getLogger().write("successfully released mpir id %d\n", mpir_id);
}

static std::string readStringMPIR(DAppId const mpir_id, std::string const& variable)
{
    auto const idInstPair = mpirMap.find(mpir_id);
    if (idInstPair != mpirMap.end()) {
        return idInstPair->second->readStringAt(variable);
    } else {
        throw std::runtime_error("read string mpir id not found: " + std::to_string(mpir_id));
    }
}

static std::string readCharArrayMPIR(DAppId const mpir_id, std::string const& variable)
{
    auto const idInstPair = mpirMap.find(mpir_id);
    if (idInstPair != mpirMap.end()) {
        return idInstPair->second->readCharArrayAt(variable);
    } else {
        throw std::runtime_error("read char array mpir id not found: " + std::to_string(mpir_id));
    }
}

static void terminateMPIR(DAppId const mpir_id)
{
    auto const idInstPair = mpirMap.find(mpir_id);
    if (idInstPair != mpirMap.end()) {
        idInstPair->second->terminate();
        mpirMap.erase(idInstPair);
    } else {
        throw std::runtime_error("terminate mpir id not found: " + std::to_string(mpir_id));
    }

    getLogger().write("successfully terminated mpir id %d\n", mpir_id);
}

static FE_daemon::MPIRResult launchMPIRShim(ShimData const& shimData, LaunchData const& launchData)
{
    int shimPipe[2];
    ::pipe(shimPipe);

    auto modifiedLaunchData = launchData;
    
    // Some wrappers make their own calls to srun, and we only want the shim to 
    // activate on our call to srun that launches the app.
    // We insert a token as the last argument to the job launch, which the MPIR
    // shim looks for.
    const auto shimToken = boost::uuids::to_string(boost::uuids::random_generator()());

    auto const shimmedLauncherName = cti::cstr::basename(shimData.shimmedLauncherPath);
    auto const shimBinDir = cti::dir_handle{shimData.temporaryShimBinDir + shimToken};
    auto const shimBinLink = cti::softlink_handle{shimData.shimBinaryPath,
        shimBinDir.m_path + "/" + shimmedLauncherName};

    // Modify PATH in launchData
    auto const rawPath = ::getenv("PATH");
    auto const originalPath = (rawPath != nullptr)
        ? std::string{rawPath}
        : "";
    modifiedLaunchData.envList.emplace_back("PATH=" + shimBinDir.m_path + (originalPath.empty() ? "" : (":" + originalPath)));

    // Communicate output pipe and real launcher path to shim
    modifiedLaunchData.envList.emplace_back("CTI_MPIR_SHIM_INPUT_FD="  + std::to_string(shimPipe[0]));
    modifiedLaunchData.envList.emplace_back("CTI_MPIR_SHIM_OUTPUT_FD=" + std::to_string(shimPipe[1]));
    modifiedLaunchData.envList.emplace_back("CTI_MPIR_LAUNCHER_PATH="  + shimData.shimmedLauncherPath);
    modifiedLaunchData.envList.emplace_back("CTI_MPIR_ORIGINAL_PATH="  + originalPath);
    modifiedLaunchData.envList.emplace_back("CTI_MPIR_STDIN_FD="       + std::to_string(launchData.stdin_fd));
    modifiedLaunchData.envList.emplace_back("CTI_MPIR_STDOUT_FD="      + std::to_string(launchData.stdout_fd));
    modifiedLaunchData.envList.emplace_back("CTI_MPIR_STDERR_FD="      + std::to_string(launchData.stderr_fd));
    modifiedLaunchData.envList.emplace_back("CTI_MPIR_SHIM_TOKEN=" + shimToken);

    modifiedLaunchData.argvList.emplace_back(shimToken);

    forkExec(modifiedLaunchData);
    close(shimPipe[1]);
    getLogger().write("started shim, waiting for pid on pipe %d\n", shimPipe[0]);

    auto const launcherPid = [&](){
        // If the shim fails to start for some reason, the other end of 
        // the pipe will be closed and fdReadLoop will throw std::runtime_error.
        try {
            return fdReadLoop<pid_t>(shimPipe[0]);
        } catch (std::runtime_error &e) {
            // Catch the error only to throw another one with a better message
            getLogger().write("MPIR shim failed to report pid.\n");
            throw std::runtime_error("MPIR shim failed to start. Set the " CTI_DBG_ENV_VAR " environment variable to 1 to show shim/wrapper output.");
        }
    }();

    getLogger().write("got pid: %d, attaching\n", launcherPid);

    // Attach and run to breakpoint
    auto mpirInstance = [](const std::string &launcherName, const pid_t pid) {
        try {
            return std::make_unique<MPIRInstance>(launcherName, pid);
        } catch (std::exception const& ex) {
            getLogger().write("Failed to attach to %s, pid %d\n", launcherName.c_str(), pid);

            auto errorMsg = std::stringstream{};
            // Create error message from launcher arguments with possible diagnostic
            errorMsg << "Failed attach to launcher under MPIR shim (" << ex.what() << ")";
            throw std::runtime_error{errorMsg.str()};
        }
    }(shimData.shimmedLauncherPath, launcherPid);

    auto mpirResult = extractMPIRResult(std::move(mpirInstance));

    // Terminate launched application on daemon exit
    appCleanupList.insert(mpirResult.launcher_pid);

    // MPIR shim stops the launcher with SIGSTOP. The launcher won't start 
    // again, even after ProcControl detaches, unless a SIGCONT is sent at some 
    // point. Sending it here doesn't release the launcher, it's still stopped 
    // under ProcControl, but it enables it to start running again once ProcControl detaches.
    ::kill(launcherPid, SIGCONT);

    return mpirResult;
}

/* handler implementations */

static void handle_ForkExecvpApp(int const reqFd, int const respFd)
{
    tryWriteIDResp(respFd, [&]() {
        auto const launchData = readLaunchData(reqFd);

        auto const appPid = forkExec(launchData);

        auto const appId = registerAppPID(appPid);

        return appId;
    });
}

static void handle_ForkExecvpUtil(int const reqFd, int const respFd)
{
    tryWriteOKResp(respFd, [&]() {
        auto const appId  = fdReadLoop<DAppId>(reqFd);
        auto const runMode = fdReadLoop<FE_daemon::RunMode>(reqFd);
        auto const launchData = readLaunchData(reqFd);

        auto const utilPid = forkExec(launchData);

        registerUtilPID(appId, utilPid);

        // If synchronous, wait for return code
        if (runMode == FE_daemon::Synchronous) {
            int status;
            if (cti::waitpid(utilPid, &status, 0) < 0) {
                return false;
            }

            if (WIFEXITED(status)) {
                getLogger().write("exited with code %d\n", WEXITSTATUS(status));
            }

            return bool{WIFEXITED(status) && (WEXITSTATUS(status) == 0)};

        // Otherwise, report successful
        } else {
            return true;
        }
    });
}

static void handle_LaunchMPIR(int const reqFd, int const respFd)
{
    tryWriteMPIRResp(respFd, [&]() {
        auto const launchData = readLaunchData(reqFd);

        auto const mpirData = launchMPIR(launchData);

        return mpirData;
    });
}

static void handle_AttachMPIR(int const reqFd, int const respFd)
{
    tryWriteMPIRResp(respFd, [&]() {
        // set up pipe stream
        cti::FdBuf reqBuf{dup(reqFd)};
        std::istream reqStream{&reqBuf};

        // read launcher path and pid
        std::string launcherPath;
        if (!std::getline(reqStream, launcherPath, '\0')) {
            throw std::runtime_error("failed to read launcher path");
        }
        auto const launcherPid = fdReadLoop<pid_t>(reqFd);

        auto const mpirData = attachMPIR(launcherPath, launcherPid);

        return mpirData;
    });
}

static void handle_ReleaseMPIR(int const reqFd, int const respFd)
{
    tryWriteOKResp(respFd, [&]() {
        auto const mpirId = fdReadLoop<DAppId>(reqFd);

        releaseMPIR(mpirId);

        return true;
    });
}

static void handle_LaunchMPIRShim(int const reqFd, int const respFd)
{
    tryWriteMPIRResp(respFd, [&]() {
        // set up pipe stream
        cti::FdBuf reqBuf{dup(reqFd)};
        std::istream reqStream{&reqBuf};

        // Read shim setup data
        ShimData shimData;
        if (!std::getline(reqStream, shimData.shimBinaryPath, '\0')) {
            throw std::runtime_error("failed to read shim binary path");
        }
        if (!std::getline(reqStream, shimData.temporaryShimBinDir, '\0')) {
            throw std::runtime_error("failed to read temporary shim directory");
        }
        if (!std::getline(reqStream, shimData.shimmedLauncherPath, '\0')) {
            throw std::runtime_error("failed to read shimmed launcher path");
        }

        // Read MPIR launch data
        auto const launchData = readLaunchData(reqFd);

        auto const mpirData = launchMPIRShim(shimData, launchData);

        return mpirData;
    });
}

static void handle_ReadStringMPIR(int const reqFd, int const respFd)
{
    tryWriteStringResp(respFd, [&]() {
        auto const mpirId = fdReadLoop<DAppId>(reqFd);

        // set up pipe stream
        cti::FdBuf reqBuf{dup(reqFd)};
        std::istream reqStream{&reqBuf};

        std::string variable;
        if (!std::getline(reqStream, variable, '\0')) {
            throw std::runtime_error("failed to read variable name");
        }
        getLogger().write("read string '%s' from mpir id %d\n", variable.c_str(), mpirId);

        return readStringMPIR(mpirId, variable);
    });
}

static void handle_ReadCharArrayMPIR(int const reqFd, int const respFd)
{
    tryWriteStringResp(respFd, [&]() {
        auto const mpirId = fdReadLoop<DAppId>(reqFd);

        // set up pipe stream
        cti::FdBuf reqBuf{dup(reqFd)};
        std::istream reqStream{&reqBuf};

        std::string variable;
        if (!std::getline(reqStream, variable, '\0')) {
            throw std::runtime_error("failed to read variable name");
        }
        getLogger().write("read char array '%s' from mpir id %d\n", variable.c_str(), mpirId);

        return readCharArrayMPIR(mpirId, variable);
    });
}

static void handle_TerminateMPIR(int const reqFd, int const respFd)
{
    tryWriteOKResp(respFd, [&]() {
        auto const mpirId = fdReadLoop<DAppId>(reqFd);

        getLogger().write("terminating mpir id %d\n", mpirId);
        terminateMPIR(mpirId);

        return true;
    });
}

static void handle_RegisterApp(int const reqFd, int const respFd)
{
    tryWriteIDResp(respFd, [&]() {
        auto const appPid = fdReadLoop<pid_t>(reqFd);

        auto const appId = registerAppPID(appPid);

        return appId;
    });
}

static void handle_RegisterUtil(int const reqFd, int const respFd)
{
    tryWriteOKResp(respFd, [&]() {
        auto const appId   = fdReadLoop<DAppId>(reqFd);
        auto const utilPid = fdReadLoop<pid_t>(reqFd);

        registerUtilPID(appId, utilPid);

        return true;
    });
}

static void handle_DeregisterApp(int const reqFd, int const respFd)
{
    tryWriteOKResp(respFd, [&]() {
        auto const appId = fdReadLoop<DAppId>(reqFd);

        deregisterAppID(appId);

        return true;
    });
}

static void handle_CheckApp(int const reqFd, int const respFd)
{
    tryWriteOKResp(respFd, [&]() {
        auto const appId = fdReadLoop<DAppId>(reqFd);

        return checkAppID(appId);
    });
}

static void handle_Shutdown(int const reqFd, int const respFd)
{
    // send OK response
    fdWriteLoop(respFd, OKResp
        { .type = RespType::OK
        , .success = true
    });

    shutdown_and_exit(0);
}

// Return string value of request type for logging
static auto reqTypeString(ReqType const reqType)
{
    switch (reqType) {
        case ReqType::ForkExecvpApp:  return "ForkExecvpApp";
        case ReqType::ForkExecvpUtil: return "ForkExecvpUtil";
        case ReqType::LaunchMPIR:     return "LaunchMPIR";
        case ReqType::AttachMPIR:     return "AttachMPIR";
        case ReqType::ReleaseMPIR:    return "ReleaseMPIR";
        case ReqType::ReadCharArrayMPIR: return "ReadCharArrayMPIR";
        case ReqType::ReadStringMPIR: return "ReadStringMPIR";
        case ReqType::TerminateMPIR:  return "TerminateMPIR";
        case ReqType::LaunchMPIRShim: return "LaunchMPIRShim";
        case ReqType::RegisterApp:    return "RegisterApp";
        case ReqType::RegisterUtil:   return "RegisterUtil";
        case ReqType::DeregisterApp:  return "DeregisterApp";
        case ReqType::CheckApp:       return "CheckApp";
        case ReqType::Shutdown:       return "Shutdown";
        default: return "(unknown)";
    }
}

static void log_terminate()
{
    if (auto eptr = std::current_exception()) {
        try {
            std::rethrow_exception(eptr);
        } catch(const std::exception& e) {
            getLogger().write(e.what());
        }
    }

    std::abort();
}

int
main(int argc, char *argv[])
{
    // Set up logging
    getLogger().hook();
    std::set_terminate(log_terminate);

    // parse incoming argv for request and response FDs
    { auto incomingArgv = cti::IncomingArgv<CTIFEDaemonArgv>{argc, argv};
        int c; std::string optarg;
        while (true) {
            std::tie(c, optarg) = incomingArgv.get_next();
            if (c < 0) {
                break;
            }

            switch (c) {

            case CTIFEDaemonArgv::ReadFD.val:
                reqFd = std::stoi(optarg);
                break;

            case CTIFEDaemonArgv::WriteFD.val:
                respFd = std::stoi(optarg);
                break;

            case CTIFEDaemonArgv::Help.val:
                usage(argv[0]);
                exit(0);

            case '?':
            default:
                usage(argv[0]);
                exit(1);

            }
        }
    }

    // post-process required args to make sure we have everything we need
    if ((reqFd < 0) || (respFd < 0)) {
        usage(argv[0]);
        exit(1);
    }

    // block all signals except noted
    auto const handledSignals = std::vector<int>
        { SIGTERM, SIGCHLD, SIGPIPE, SIGHUP
        , SIGTRAP // used for Dyninst breakpoint events
        , SIGTTIN // used for mpiexec job control

        // mpiexec sends SIGSEGV if a job process segfaults, ignore it
        , SIGSEGV
    };

    sigset_t block_set;
    memset(&block_set, 0, sizeof(block_set));
    if (sigfillset(&block_set)) {
        fprintf(stderr, "sigfillset: %s\n", strerror(errno));
        return 1;
    }

    for (auto&& signum : handledSignals) {
        if (sigdelset(&block_set, signum)) {
            fprintf(stderr, "sigdelset: %s\n", strerror(errno));
            return 1;
        }
    }
    if (sigprocmask(SIG_SETMASK, &block_set, nullptr)) {
        fprintf(stderr, "sigprocmask: %s\n", strerror(errno));
        return 1;
    }

    // set handler for signals
    struct sigaction sig_action;
    memset(&sig_action, 0, sizeof(sig_action));
    sig_action.sa_flags = SA_RESTART | SA_SIGINFO;
    sig_action.sa_sigaction = cti_fe_daemon_handler;
    for (auto&& signum : handledSignals) {
        if (sigaction(signum, &sig_action, nullptr)) {
            fprintf(stderr, "sigaction %d: %s\n", signum, strerror(errno));
            return 1;
        }
    }

    // write our PID to signal to the parent we are all set up
    getLogger().write("%d sending initial ok\n", getpid());
    fdWriteLoop(respFd, getpid());

    // wait for pipe commands
    while (true) {
        auto const reqType = fdReadLoop<ReqType>(reqFd);
        getLogger().write("Received request type %ld: %s\n", reqType, reqTypeString(reqType));

        switch (reqType) {

            case ReqType::ForkExecvpApp:
                handle_ForkExecvpApp(reqFd, respFd);
                break;

            case ReqType::ForkExecvpUtil:
                handle_ForkExecvpUtil(reqFd, respFd);
                break;

            case ReqType::LaunchMPIR:
                handle_LaunchMPIR(reqFd, respFd);
                break;

            case ReqType::AttachMPIR:
                handle_AttachMPIR(reqFd, respFd);
                break;

            case ReqType::ReleaseMPIR:
                handle_ReleaseMPIR(reqFd, respFd);
                break;

            case ReqType::ReadStringMPIR:
                handle_ReadStringMPIR(reqFd, respFd);
                break;

            case ReqType::ReadCharArrayMPIR:
                handle_ReadCharArrayMPIR(reqFd, respFd);
                break;

            case ReqType::TerminateMPIR:
                handle_TerminateMPIR(reqFd, respFd);
                break;

            case ReqType::LaunchMPIRShim:
                handle_LaunchMPIRShim(reqFd, respFd);
                break;

            case ReqType::RegisterApp:
                handle_RegisterApp(reqFd, respFd);
                break;

            case ReqType::RegisterUtil:
                handle_RegisterUtil(reqFd, respFd);
                break;

            case ReqType::DeregisterApp:
                handle_DeregisterApp(reqFd, respFd);
                break;

            case ReqType::CheckApp:
                handle_CheckApp(reqFd, respFd);
                break;

            case ReqType::Shutdown:
                handle_Shutdown(reqFd, respFd);
                break;

            default:
                getLogger().write("unknown req type %ld\n", reqType);
                break;

        }
    }

    // we should not get here
    shutdown_and_exit(1);
}
