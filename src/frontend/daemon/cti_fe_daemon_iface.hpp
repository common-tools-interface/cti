/******************************************************************************\
 * cti_fe_daemon_iface.hpp - command interface for frontend daemon
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
    int offset = 0;
    while (offset < num_bytes) {
        errno = 0;
        int bytes_read = read(fd, buf + offset, num_bytes - offset);
        if (bytes_read < 0) {
            if (errno == EINTR) {
                continue;
            } else {
                throw std::runtime_error("read failed: " + std::string{std::strerror(errno)});
            }
        } else {
            offset += bytes_read;
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
static inline void writeLoop(int const fd, char const* buf, int num_bytes)
{
    int offset = 0;
    while (offset < num_bytes) {
        errno = 0;
        int written = write(fd, buf + offset, num_bytes - offset);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            } else {
                throw std::runtime_error("write failed: " + std::string{std::strerror(errno)});
            }
        } else {
            offset += written;
        }
    }
}

// write an object T to fd (useful for writing message structs to pipes)
template <typename T>
static inline void rawWriteLoop(int const fd, T const& obj)
{
    static_assert(std::is_trivial<T>::value);
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
        MPIRProctable proctable;
        BinaryRankMap binaryRankMap;
    };

    // Read and return an MPIRResult from the provided request pipe
    static MPIRResult readMPIRResp(int const reqFd);

    /* request types */

    // sent before a request to indicate the type of request data that will follow
    enum class ReqType : long {
        ForkExecvpApp,
        ForkExecvpUtil,

        LaunchMPIR,
        LaunchMPIRShim,
        AttachMPIR,
        ReadStringMPIR,
        ReleaseMPIR,
        TerminateMPIR,

        RegisterApp,
        RegisterUtil,
        DeregisterApp,
        CheckApp,

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

    // ReadStringMPIR
    /*
        send ID of target application
        send string name of variable to read from memory
    */

    // ReleaseMPIR
    /*
        send ID provided by LaunchMPIR request
    */

    // LaunchMPIRShim
    /*
        send path to shim binary, temporary shim link directory, launcher path to shim
        send launch parameters as in LaunchMPIR
    */

    // Shutdown
    /* No data */

    // Response types

    enum RespType : long {
        // Shutdown, RegisterApp, RegisterUtil, CheckApp, ReleaseMPIR
        OK,

        // ForkExecvpApp, ForkExecvpUtil
        ID,

        // ReadStringMPIR
        String,

        // LaunchMPIR, LaunchMPIRShim
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

    struct StringResp
    {
        RespType type;
        bool success;
        // after sending this struct, send a null-terminated string value if successful
    };

    struct MPIRResp
    {
        RespType type;
        DaemonAppId mpir_id;
        pid_t launcher_pid;
        int num_pids;
        // after sending this struct, send `num_pids` elements of:
        // - pid, null-terminated hostname, null-terminated executable name
    };
private: // Internal data
    bool      m_init;
    pid_t     m_mainPid; // Main CTI PID that is responsible for daemon cleanup
    cti::FdPair m_req_sock;
    cti::FdPair m_resp_sock;

public:
    FE_daemon()
    : m_init{false}
    , m_mainPid{-1} // Set during daemon fork/exec
    , m_req_sock{}
    , m_resp_sock{}
    {
        // Set up communication through Unix domain sockets
        m_req_sock.socketpair(AF_UNIX, SOCK_STREAM, 0);
        m_resp_sock.socketpair(AF_UNIX, SOCK_STREAM, 0);
    }
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

    // fe_daemon will read the value of a variable from memory under MPIR control.
    // Write an mpir string read request to pipe, return value
    std::string request_ReadStringMPIR(DaemonAppId mpir_id, char const* variable);

    // fe_daemon will terminate a binary under mpir control.
    // Write an mpir release request to pipe, verify response
    void request_TerminateMPIR(DaemonAppId mpir_id);

    // fe_daemon will launch the provided wrapper script, masquerading the MPIR shim utility
    // as the provided launcher name in path. the launch is completed under MPIR control
    // and proctable is extraced. Provide path to mpir_shim binary and the temporary link location.
    // Write an mpir launch request and parameters to pipe, return MPIR data including proctable
    MPIRResult request_LaunchMPIRShim(
        char const* shimBinaryPath, char const* temporaryShimBinDir, char const* shimmedLauncherPath,
        char const* scriptPath, char const* const argv[],
        int stdin_fd, int stdout_fd, int stderr_fd, char const* const env[]);

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

    // Write an app run check request to pipe, return response
    bool request_CheckApp(DaemonAppId app_id);
};
