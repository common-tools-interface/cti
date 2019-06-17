/******************************************************************************\
 * cti_fe_daemon_iface.hpp - command interface for frontend daemon
 *
 * Copyright 2019 Cray Inc.  All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
 *
 ******************************************************************************/
#pragma once

#include <sys/types.h>
#include <sys/socket.h>

#include <cstring>

#include "frontend/mpir_iface/MPIRProctable.hpp"

#include "useful/cti_execvp.hpp"

/* fd read / write helpers */

// read num_bytes from fd into buf
static inline void readLoop(char* buf, int const fd, int num_bytes)
{
    while (num_bytes > 0) {
        errno = 0;
        int bytes_read = read(fd, buf, num_bytes);
        if (bytes_read < 0) {
            if (errno == EINTR) {
                continue;
            } else {
                throw std::runtime_error("read failed: " + std::string{std::strerror(errno)});
            }
        } else {
            num_bytes -= bytes_read;
        }
    }
}

// read and return an object T from fd (useful for reading message structs from pipes)
template <typename T>
static inline T rawReadLoop(int const fd)
{
    static_assert(std::is_trivially_copyable<T>::value);
    T result;
    readLoop(reinterpret_cast<char*>(&result), fd, sizeof(T));
    return result;
}

// write num_bytes from buf to fd
static void writeLoop(int const fd, char const* buf, int num_bytes)
{
    while (num_bytes > 0) {
        errno = 0;
        int written = write(fd, buf, num_bytes);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            } else {
                throw std::runtime_error("write failed: " + std::string{std::strerror(errno)});
            }
        } else {
            num_bytes -= written;
        }
    }
}

// write an object T to fd (useful for writing message structs to pipes)
template <typename T>
static void rawWriteLoop(int const fd, T const& obj)
{
    static_assert(std::is_trivially_copyable<T>::value);
    writeLoop(fd, reinterpret_cast<char const*>(&obj), sizeof(T));
}

/* protocol helpers for cti_fe_iface
    the frontend implementation in cti_fe_iface.cpp will call these functions, using its internal
    state to provide the file descriptors for the request and response domain sockets
*/
class FE_daemon final {
public: // type definitions
    using DaemonAppId = int;

    // bundle all MPIR data produced by an MPIR launch / attach
    struct MPIRResult
    {
        DaemonAppId mpir_id;
        pid_t launcher_pid;
        uint32_t job_id;
        uint32_t step_id;
        MPIRProctable proctable;
    };

    /* request types */

    // sent before a request to indicate the type of request data that will follow
    enum ReqType : long {
        ForkExecvpApp,
        ForkExecvpUtil,

        LaunchMPIR,
        AttachMPIR,
        ReleaseMPIR,
        TerminateMPIR,

        RegisterApp,
        RegisterUtil,
        DeregisterApp,

        Shutdown
    };

    // sent as part of a utility launch request to indicate whether to wait for utility to exit
    enum RunMode : int {
        Asynchronous, // launch request returns immediately
        Synchronous   // launch request will block until utility exits
    };

    /* communication protocol
        before sending any data, send the request type
    */

    // ForkExecvpApp
    // LaunchMPIR
    /*
        Application launch parameters:
        send socket control message to share FD access rights
        - array of stdin, stdout, stderr FDs
        then send list of null-terminated strings:
        - file path string
        - each argument string
        - EMPTY STRING
        - each environment variable string (format VAR=VAL)
        - EMPTY STRING
    */

    // ForkExecvpUtil
    /*
        send ID of owning application
        send RunMode indicating synchronous or asynchronous run
        send "Application launch parameters" as for ForkExecvpApp
    */

    // AttachMPIR
    // RegisterApp
    // DeregisterApp
    /*
        send PID of target application
    */

    // RegisterUtil
    /*
        send ID of owning application
        send PID of target utility
    */

    // ReleaseMPIR
    /*
        send ID provided by LaunchMPIR request
    */

    // Shutdown
    /* No data */

    // Response types

    enum RespType : long {
        // Shutdown, RegisterApp, RegisterUtil, ReleaseMPIR
        OK,

        // ForkExecvpApp, ForkExecvpUtil
        ID,

        // LaunchMPIR
        MPIR,
    };

    struct OKResp
    {
        RespType type;
        bool success;
    };

    struct IDResp
    {
        RespType type;
        DaemonAppId id;
    };

    struct MPIRResp
    {
        RespType type;
        DaemonAppId mpir_id;
        pid_t launcher_pid;
        uint32_t job_id;
        uint32_t step_id;
        int num_pids;
        // after sending this struct, send `num_pids` elements of:
        // - pid followed by null-terminated hostname
    };

private: // Internal data
    bool                m_init;
    cti::SocketPair     m_req_sock;
    cti::SocketPair     m_resp_sock;

public:
    FE_daemon()
    : m_init{false}
    , m_req_sock{AF_UNIX, SOCK_STREAM, 0}
    , m_resp_sock{AF_UNIX, SOCK_STREAM, 0}
    { }
    ~FE_daemon();

    // This must only be called once. It is to workaround an issue in Frontend
    // construction with initialization ordering. Plus we might want to someday
    // delay starting the fe daemon process until it is actually needed.
    void initialize(std::string const& fe_daemon_bin);

    /*
    ** FE daemon interface
    ** WLM frontend implementations will call these functions to perform app / utility launch and
    ** management operations usage of forkExecvpApp/AsyncUtil/SyncUtil or launchMPIR is preferred
    ** to registerApp/Util, as it prevents a race condition when CTI is killed before registration
    ** can occur.
    ** In this situation, the app or utility that was to be registered can continue running
    ** indefinitely see protocol notes in cti_fe_daemon.hpp for information on indeterminate-length
    ** requests such as the null-terminated argument / environment arrays.
    */

    // fe_daemon will fork and execvp a binary and register it as an app
    // Write an app launch request and parameters to pipe, return launched app id
    DaemonAppId request_ForkExecvpApp(char const* file,
                                      char const* const argv[],
                                      int stdin_fd, int stdout_fd, int stderr_fd,
                                      char const* const env[] );

private:
    // fe_daemon will fork and execvp a binary and register it as a utility belonging to app_id
    // This can either be synchronous or asynchronous depending on runMode. Synchronous means wait
    // for utility to complete before returning from this call.
    // Write a utility launch request and parameters to pipe, return launched util id
    void request_ForkExecvpUtil(DaemonAppId app_id,
                                RunMode runMode,
                                char const* file,
                                char const* const argv[],
                                int stdin_fd, int stdout_fd, int stderr_fd,
                                char const* const env[] );
public:
    // Helper functions for request_ForkExecvpUtil
    void request_ForkExecvpUtil_Async(DaemonAppId app_id,
                                      char const* file,
                                      char const* const argv[],
                                      int stdin_fd, int stdout_fd, int stderr_fd,
                                      char const* const env[] )
    {
        request_ForkExecvpUtil(app_id,
                               RunMode::Asynchronous,
                               file, argv,
                               stdin_fd, stdout_fd, stderr_fd,
                               env);
    }

    void request_ForkExecvpUtil_Sync(DaemonAppId app_id,
                                     char const* file,
                                     char const* const argv[],
                                     int stdin_fd, int stdout_fd, int stderr_fd,
                                     char const* const env[] )
    {
        request_ForkExecvpUtil(app_id,
                               RunMode::Synchronous,
                               file, argv,
                               stdin_fd, stdout_fd, stderr_fd,
                               env );
    }

    // fe_daemon will launch a binary under MPIR control and extract its proctable.
    // Write an mpir launch request and parameters to pipe, return MPIR data including proctable
    MPIRResult request_LaunchMPIR(char const* file,
        char const* const argv[], int stdin_fd, int stdout_fd, int stderr_fd, char const* const env[]);

    // fe_daemon will attach to a binary and extract its proctable.
    // Write an MPIR attach request to pipe, return MPIR data
    MPIRResult request_AttachMPIR(char const* launcher_path, pid_t launcher_pid);

    // fe_daemon will release a binary under mpir control from its breakpoint.
    // Write an mpir release request to pipe, verify response
    void request_ReleaseMPIR(DaemonAppId mpir_id);

    // fe_daemon will terminate a binary under mpir control.
    // Write an mpir release request to pipe, verify response
    void request_TerminateMPIR(DaemonAppId mpir_id);

    // fe_daemon will register an already-forked process as an app. make sure this is paired with a
    // _cti_deregisterApp for timely cleanup.
    // Write an app register request to pipe, verify response, return new app id
    DaemonAppId request_RegisterApp(pid_t app_pid);

    // fe_daemon will register an already-forked process as a utility belonging to app_pid.
    // Write utility register request to pipe, verify response
    void request_RegisterUtil(DaemonAppId app_id, pid_t util_pid);

    // fe_daemon will terminate all utilities belonging to app_pid and deregister app_pid.
    // Write an app deregister request to pipe, verify response
    void request_DeregisterApp(DaemonAppId app_id);
};