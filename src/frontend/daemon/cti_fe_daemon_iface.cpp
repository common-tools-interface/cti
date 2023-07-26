/******************************************************************************\
 * cti_fe_daemon_iface.cpp - command interface for frontend daemon
 *
 * Copyright 2019-2020 Hewlett Packard Enterprise Development LP.
 * SPDX-License-Identifier: Linux-OpenIB
 ******************************************************************************/

// This pulls in config.h
#include "cti_defs.h"
#include "cti_argv_defs.hpp"

#include <sys/types.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/time.h>

#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <stdexcept>

#include "useful/cti_execvp.hpp"

#include "cti_fe_daemon_iface.hpp"
using DaemonAppId = FE_daemon::DaemonAppId;

/* protocol helpers */

// write FD remap control message, binary path, arguments, environment to domain socket
static void writeLaunchData(int const reqFd, char const* file, char const* const argv[],
    int stdin_fd, int stdout_fd, int stderr_fd, char const* const env[])
{
    // verify that reqFd is a domain socket
    { struct sockaddr sa;
        auto len = socklen_t{sizeof(sa)};
        if (::getsockname(reqFd, &sa, &len) < 0) {
            throw std::runtime_error("getsockname failed: " + std::string{strerror(errno)});
        }
        if (sa.sa_family != AF_UNIX) {
            throw std::runtime_error("daemon request file descriptor must be a domain socket");
        }
    }

    // Share standard in / out / err if set to StdFd, closed if CloseFd, or provided FD
    auto const N_FDS = 3;
    int fds_source[N_FDS] =

        { (stdin_fd == FE_daemon::StdFd)
            ? STDIN_FILENO
            : (stdin_fd == FE_daemon::CloseFd)
                ? ::open("/dev/null", O_RDONLY)
                : stdin_fd

        , (stdout_fd == FE_daemon::StdFd)
            ? STDOUT_FILENO
            : (stdout_fd == FE_daemon::CloseFd)
                ? ::open("/dev/null", O_WRONLY)
                : stdout_fd

        , (stderr_fd == FE_daemon::StdFd)
            ? STDERR_FILENO
            : (stderr_fd == FE_daemon::CloseFd)
                ? ::open("/dev/null", O_WRONLY)
                : stderr_fd
    };

    struct {
        int fd_data[N_FDS];
        struct cmsghdr cmd_hdr;
    } buffer;
    ::memset(&buffer, 0, sizeof(buffer));

    // create message buffer for one character
    struct iovec empty_iovec;
    char c = ' ';
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
    for(int i = 0; i < N_FDS; i++) {
        ((int *)CMSG_DATA(cmsg))[i] = fds_source[i];
    }

    // send remap FD message
    if (::sendmsg(reqFd, &msg_hdr, 0) < 0) {
        throw std::runtime_error("failed to receive fds: " + std::string{strerror(errno)});
    }

    // write filepath string
    fdWriteLoop(reqFd, file, strlen(file) + 1);

    // write argument array length and contents
    auto argc = size_t{0};
    for (auto arg = argv; *arg != nullptr; arg++) {argc++;};

    auto argc_str = std::to_string(argc);
    fdWriteLoop(reqFd, argc_str.c_str(), argc_str.size() + 1);

    for (size_t i = 0; i < argc; i++) {
        fdWriteLoop(reqFd, argv[i], strlen(argv[i]) + 1);
    }

    // write environment array length and contents
    auto envc = size_t{0};
    if (env)
        for (auto var = env; *var != nullptr; var++) {envc++;};

    auto envc_str = std::to_string(envc);
    fdWriteLoop(reqFd, envc_str.c_str(), envc_str.size() + 1);

    if (env) {
        for (size_t i = 0; i < envc; i++) {
            fdWriteLoop(reqFd, env[i], strlen(env[i]) + 1);
        }
    }
}

// return boolean response from pipe
static bool readOKResp(int const reqFd)
{
    auto const okResp = fdReadLoop<FE_daemon::OKResp>(reqFd);
    if (okResp.type != FE_daemon::RespType::OK) {
        throw std::runtime_error("daemon did not send expected OK response type");
    }
    return okResp.success;
}

// throw if boolean response is not true, indicating failure
static void verifyOKResp(int const reqFd)
{
    if (readOKResp(reqFd) == false) {
        throw std::runtime_error("daemon response indicated failure");
    }
}

// return ID response content, throw if id < 0, indicating failure
static DaemonAppId readIDResp(int const reqFd)
{
    auto const idResp = fdReadLoop<FE_daemon::IDResp>(reqFd);
    if ((idResp.type != FE_daemon::RespType::ID) || (idResp.id < 0)) {
        throw std::runtime_error("failed to read DaemonAppID response");
    }
    return idResp.id;
}

// return string data, throw if failure indicated
static std::string readStringResp(int const reqFd)
{
    // read basic response information
    auto const stringResp = fdReadLoop<FE_daemon::StringResp>(reqFd);
    if (stringResp.type != FE_daemon::RespType::String) {
        throw std::runtime_error("daemon did not send expected String response type");
    } else if (stringResp.success == false) {
        throw std::runtime_error("daemon failed to read string from memory");
    }

    // set up pipe stream
    cti::FdBuf respBuf{dup(reqFd)};
    std::istream respStream{&respBuf};

    // read value
    std::string result;
    if (!std::getline(respStream, result, '\0')) {
        throw std::runtime_error("failed to read string");
    }

    return result;
}

// return MPIR launch / attach data, throw if MPIR ID < 0, indicating failure
FE_daemon::MPIRResult FE_daemon::readMPIRResp(int const reqFd)
{
    // Delegate to general MPIR-reading function
    return readMPIRResp([reqFd](char* buf, ssize_t capacity) {
        errno = 0;
        while (true) {
            int bytes_read = ::read(reqFd, buf, capacity);
            if (bytes_read < 0) {
                if (errno == EINTR) {
                    continue;
                } else {
                    throw std::runtime_error("read failed: " + std::string{std::strerror(errno)});
                }
            } else if (bytes_read == 0) {
                throw std::runtime_error("read failed: zero bytes read");
            }

            return bytes_read;
        }
    });
}

FE_daemon::MPIRResult FE_daemon::readMPIRResp(std::function<ssize_t(char*, size_t)> reader)
{
    // read basic table information
    auto const mpirResp = readLoop<FE_daemon::MPIRResp>(reader);
    if (mpirResp.type != FE_daemon::RespType::MPIR) {
        throw std::runtime_error("daemon did not send expected MPIR response type");

    // Error handling
    } else if (mpirResp.mpir_id == 0) {

        if (mpirResp.error_msg_len > 0) {

            char buf[mpirResp.error_msg_len];
            auto error_msg_provided = false;

            // Read null-terminated error message
            try {
                readLoop(buf, mpirResp.error_msg_len, reader);
                // (Should already be null-terminated)
                buf[mpirResp.error_msg_len - 1] = '\0';

                error_msg_provided = true;
            } catch (...) {
                // Fall through to generic failure message
            }

            // Throw full error message if provided
            if (error_msg_provided) {
                throw std::runtime_error(buf);
            }
        }

        throw std::runtime_error("failed to perform MPIR launch");
    }

    // fill in MPIR data excluding proctable
    FE_daemon::MPIRResult result
        { .mpir_id = mpirResp.mpir_id
        , .launcher_pid = mpirResp.launcher_pid
        , .proctable = {}
        , .binaryRankMap = {}
    };
    result.proctable.reserve(mpirResp.num_pids);

    // fill in pid and hostname of proctable elements, generate executable path to rank ID map
    for (int i = 0; i < mpirResp.num_pids; i++) {
        MPIRProctableElem elem;
        // read pid
        elem.pid = readLoop<pid_t>(reader);

        // Read null-terminated hostname and executable name
        while (auto const c = readLoop<char>(reader)) {
            elem.hostname.push_back(c);
        }
        while (auto const c = readLoop<char>(reader)) {
            elem.executable.push_back(c);
        }

        result.proctable.emplace_back(std::move(elem));
    }

    // fill in binary rank map
    result.binaryRankMap = generateBinaryRankMap(result.proctable);

    return result;
}

/* interface implementation */

FE_daemon::~FE_daemon()
{
    // Send shutdown request if we have initialized the daemon
    if (m_init) {
        m_init = false;

        // Send daemon a shutdown request if we are the "main" PID
        if (getpid() == m_mainPid) {

            // This should be the only way to call ReqType::Shutdown
            try {
                fdWriteLoop(m_req_sock.getWriteFd(), ReqType::Shutdown);
                verifyOKResp(m_resp_sock.getReadFd());
            } catch (std::exception const& ex) {
                fprintf(stderr, "warning: %s\n", ex.what());
            }
        }
    }
    // FIXME: Shouldn't this do a waitpid???
}

void
FE_daemon::initialize(std::string const& fe_daemon_bin)
{
    // Only fork once!
    if (m_init) {
        return;
    }

    // Start the frontend daemon
    if (auto const forkedPid = fork()) {
        // parent case

        // Set this PID as the one responsible for cleaning up the daemon
        m_mainPid = getpid();

        // set child in own process gorup
        if (setpgid(forkedPid, forkedPid) < 0) {
            perror("setpgid");

            // All exit calls indicating fatal CTI initalization error should be _exit
            // (exit will run global destructors, but initalization hasn't completed yet)
            _exit(1);
        }

        // set up fe_daemon req / resp pipe
        m_req_sock.closeRead();
        m_resp_sock.closeWrite();

        // wait until fe_daemon set up
        if (fdReadLoop<pid_t>(m_resp_sock.getReadFd()) != forkedPid) {
            throw std::runtime_error("fe_daemon launch failed");
        }
    }
    else {
        // child case

        // set in own process gorup
        if (setpgid(0, 0) < 0) {
            perror("setpgid");
            _exit(1);
        }

        // set up death signal
        prctl(PR_SET_PDEATHSIG, SIGHUP);

        // set up fe_daemon req /resp pipe
        m_req_sock.closeWrite();
        m_resp_sock.closeRead();

        // remap standard FDs
        dup2(open("/dev/null", O_RDONLY), STDIN_FILENO);

        if (::getenv("CTI_DEBUG") == nullptr) {
            dup2(open("/dev/null", O_WRONLY), STDOUT_FILENO);
            dup2(open("/dev/null", O_WRONLY), STDERR_FILENO);
        }

        // close FDs above pipe FDs
        auto max_fd = size_t{};
        { struct rlimit rl;
            if (getrlimit(RLIMIT_NOFILE, &rl) < 0) {
                throw std::runtime_error("getrlimit failed.");
            } else {
                max_fd = (rl.rlim_max == RLIM_INFINITY) ? 1024 : rl.rlim_max;
            }
        }
        int const min_fd = std::max(m_req_sock.getReadFd(), m_resp_sock.getWriteFd()) + 1;
        for (size_t i = min_fd; i < max_fd; ++i) {
            close(i);
        }

        // setup args
        using FEDA = CTIFEDaemonArgv;
        cti::OutgoingArgv<FEDA> fe_daemonArgv{fe_daemon_bin};
        fe_daemonArgv.add(FEDA::ReadFD,  std::to_string(m_req_sock.getReadFd()));
        fe_daemonArgv.add(FEDA::WriteFD, std::to_string(m_resp_sock.getWriteFd()));

        // exec
        execvp(fe_daemon_bin.c_str(), fe_daemonArgv.get());
        _exit(-1);
    }
    // Setup in parent was sucessful
    m_init = true;
}

DaemonAppId
FE_daemon::request_ForkExecvpApp(char const* file,
    char const* const argv[], int stdin_fd, int stdout_fd, int stderr_fd, char const* const env[])
{
    fdWriteLoop(m_req_sock.getWriteFd(), ReqType::ForkExecvpApp);
    writeLaunchData(m_req_sock.getWriteFd(), file, argv, stdin_fd, stdout_fd, stderr_fd, env);
    return readIDResp(m_resp_sock.getReadFd());
}

bool
FE_daemon::request_ForkExecvpUtil(DaemonAppId app_id, RunMode runMode,
    char const* file, char const* const argv[], int stdin_fd, int stdout_fd, int stderr_fd,
    char const* const env[])
{
    fdWriteLoop(m_req_sock.getWriteFd(), ReqType::ForkExecvpUtil);
    fdWriteLoop(m_req_sock.getWriteFd(), app_id);
    fdWriteLoop(m_req_sock.getWriteFd(), runMode);
    writeLaunchData(m_req_sock.getWriteFd(), file, argv, stdin_fd, stdout_fd, stderr_fd, env);

    // Expect successful async launch
    if (runMode == RunMode::Asynchronous) {
        verifyOKResp(m_resp_sock.getReadFd());
        return true;

    // Return whether launching the synchronous application was successful
    } else {
        return readOKResp(m_resp_sock.getReadFd());
    }
}

FE_daemon::MPIRResult
FE_daemon::request_LaunchMPIR(char const* file,
    char const* const argv[], int stdin_fd, int stdout_fd, int stderr_fd, char const* const env[])
{
    fdWriteLoop(m_req_sock.getWriteFd(), ReqType::LaunchMPIR);
    writeLaunchData(m_req_sock.getWriteFd(), file, argv, stdin_fd, stdout_fd, stderr_fd, env);
    return readMPIRResp(m_resp_sock.getReadFd());
}

FE_daemon::MPIRResult
FE_daemon::request_AttachMPIR(char const* launcher_path, pid_t launcher_pid)
{
    fdWriteLoop(m_req_sock.getWriteFd(), ReqType::AttachMPIR);
    fdWriteLoop(m_req_sock.getWriteFd(), launcher_path, strlen(launcher_path) + 1);
    fdWriteLoop(m_req_sock.getWriteFd(), launcher_pid);
    return readMPIRResp(m_resp_sock.getReadFd());
}

void
FE_daemon::request_ReleaseMPIR(DaemonAppId mpir_id)
{
    fdWriteLoop(m_req_sock.getWriteFd(), ReqType::ReleaseMPIR);
    fdWriteLoop(m_req_sock.getWriteFd(), mpir_id);
    verifyOKResp(m_resp_sock.getReadFd());
}

std::string
FE_daemon::request_ReadStringMPIR(DaemonAppId mpir_id, char const* variable)
{
    fdWriteLoop(m_req_sock.getWriteFd(), ReqType::ReadStringMPIR);
    fdWriteLoop(m_req_sock.getWriteFd(), mpir_id);
    fdWriteLoop(m_req_sock.getWriteFd(), variable, strlen(variable) + 1);
    return readStringResp(m_resp_sock.getReadFd());
}

void
FE_daemon::request_TerminateMPIR(DaemonAppId mpir_id)
{
    fdWriteLoop(m_req_sock.getWriteFd(), ReqType::TerminateMPIR);
    fdWriteLoop(m_req_sock.getWriteFd(), mpir_id);
    verifyOKResp(m_resp_sock.getReadFd());
}

FE_daemon::MPIRResult
FE_daemon::request_LaunchMPIRShim(
    char const* shimBinaryPath, char const* temporaryShimBinDir, char const* shimmedLauncherPath,
    char const* scriptPath, char const* const argv[],
    int stdin_fd, int stdout_fd, int stderr_fd, char const* const env[])
{
    fdWriteLoop(m_req_sock.getWriteFd(), ReqType::LaunchMPIRShim);
    fdWriteLoop(m_req_sock.getWriteFd(), shimBinaryPath,      strlen(shimBinaryPath)      + 1);
    fdWriteLoop(m_req_sock.getWriteFd(), temporaryShimBinDir, strlen(temporaryShimBinDir) + 1);
    fdWriteLoop(m_req_sock.getWriteFd(), shimmedLauncherPath, strlen(shimmedLauncherPath) + 1);
    writeLaunchData(m_req_sock.getWriteFd(), scriptPath, argv, stdin_fd, stdout_fd, stderr_fd, env);
    return readMPIRResp(m_resp_sock.getReadFd());
}

DaemonAppId
FE_daemon::request_RegisterApp()
{
    fdWriteLoop(m_req_sock.getWriteFd(), ReqType::RegisterApp);
    fdWriteLoop(m_req_sock.getWriteFd(), 0);
    return readIDResp(m_resp_sock.getReadFd());
}

DaemonAppId
FE_daemon::request_RegisterApp(pid_t app_pid)
{
    fdWriteLoop(m_req_sock.getWriteFd(), ReqType::RegisterApp);
    fdWriteLoop(m_req_sock.getWriteFd(), app_pid);
    return readIDResp(m_resp_sock.getReadFd());
}

void
FE_daemon::request_RegisterUtil(DaemonAppId app_id, pid_t util_pid)
{
    fdWriteLoop(m_req_sock.getWriteFd(), ReqType::RegisterUtil);
    fdWriteLoop(m_req_sock.getWriteFd(), app_id);
    fdWriteLoop(m_req_sock.getWriteFd(), util_pid);
    verifyOKResp(m_resp_sock.getReadFd());
}

void
FE_daemon::request_DeregisterApp(DaemonAppId app_id)
{
    fdWriteLoop(m_req_sock.getWriteFd(), ReqType::DeregisterApp);
    fdWriteLoop(m_req_sock.getWriteFd(), app_id);
    verifyOKResp(m_resp_sock.getReadFd());
}

void
FE_daemon::request_ReleaseApp(DaemonAppId app_id)
{
    fdWriteLoop(m_req_sock.getWriteFd(), ReqType::ReleaseApp);
    fdWriteLoop(m_req_sock.getWriteFd(), app_id);
    verifyOKResp(m_resp_sock.getReadFd());
}

bool
FE_daemon::request_CheckApp(DaemonAppId app_id)
{
    fdWriteLoop(m_req_sock.getWriteFd(), ReqType::CheckApp);
    fdWriteLoop(m_req_sock.getWriteFd(), app_id);
    return readOKResp(m_resp_sock.getReadFd());
}
