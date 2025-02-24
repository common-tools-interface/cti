/******************************************************************************\
 * SSHSession.hpp - A header file for the SSH helper functions
 *
 * Copyright 2017-2023 Hewlett Packard Enterprise Development LP.
 * SPDX-License-Identifier: Linux-OpenIB
 ******************************************************************************/

#pragma once

#include <memory>

#include <libssh2.h>

#include "frontend/daemon/cti_fe_daemon_iface.hpp"

#include "useful/cti_wrappers.hpp"

// Custom deleters
static void delete_ssh2_session(LIBSSH2_SESSION *pSession)
{
    libssh2_session_disconnect(pSession, "Error occured");
    libssh2_session_free(pSession);
}

static void delete_ssh2_channel(LIBSSH2_CHANNEL *pChannel)
{
    // SSH standard does not mandate sending EOF before closing connection,
    // but some SSH servers will not respond properly to shutdown requests
    // unless an EOF message is received
    libssh2_channel_send_eof(pChannel);
    libssh2_channel_wait_eof(pChannel);

    libssh2_channel_close(pChannel);
    libssh2_channel_wait_closed(pChannel);
    libssh2_channel_free(pChannel);
}

class SSHSession
{
public: // types
    using UniqueSession = std::unique_ptr<LIBSSH2_SESSION, decltype(&delete_ssh2_session)>;
    using UniqueChannel = std::unique_ptr<LIBSSH2_CHANNEL, decltype(&delete_ssh2_channel)>;

private: // members
    cti::fd_handle m_session_sock;
    UniqueSession m_session_ptr;
    std::string m_username;
    std::string m_homeDir;

public: // interface
    /*
     * SSHSession constructor - start and authenticate an ssh session with a remote host
     *
     * detail
     *      starts an ssh session with hostname, verifies the identity of the remote host,
     *      and authenticates the user using the public key method. this is the only supported
     *      ssh authentication method.
     *
     * arguments
     *      hostname - hostname of remote host to which to connect
     *
     */
    SSHSession(std::string const& hostname, std::string const& username, std::string const& homeDir);

    ~SSHSession() = default;

    SSHSession(SSHSession&& expiring);
    SSHSession& operator=(SSHSession&& expiring);

    // Delete copy constructors
    SSHSession(const SSHSession&) = delete;
    SSHSession& operator=(const SSHSession&) = delete;

    /*
     * executeRemoteCommand - Execute a command on a remote host through an ssh session
     *
     * Detail
     *      Executes a command with the specified arguments and environment on the remote host
     *      connected by the specified session.
     *
     * Arguments
     *      args -          null-terminated cstring array which holds the arguments array for the command to be executed
     */
    void executeRemoteCommand(char const* const* args, char const* const* env, bool synchronous);
    UniqueChannel startRemoteCommand(const char* const argv[]);
    void waitCloseChannel(UniqueChannel&& channel);

    /*
     * sendRemoteFile - Send a file to a remote host on an open ssh session
     *
     * Detail
     *      Sends the file specified by source_path to the remote host connected on session
     *      at the location destination_path on the remote host with permissions specified by
     *      mode.
     *
     * Arguments
     *      source_path - A C-string specifying the path to the file to ship
     *      destination_path- A C-string specifying the path of the destination on the remote host
     *      mode- POSIX mode for specifying permissions of new file on remote host
     */
    void sendRemoteFile(const char* source_path, const char* destination_path, int mode);

    FE_daemon::MPIRResult attachMPIR(std::string const& daemonPath, std::string const& launcherName,
        pid_t launcher_pid);

    std::tuple<UniqueChannel, pid_t> startRemoteDaemon(std::string const& daemonPath);
    FE_daemon::MPIRResult launchMPIR(LIBSSH2_CHANNEL* channel,
        char const* const* launcher_argv, char const* const* env);
    std::string readStringMPIR(LIBSSH2_CHANNEL* channel, FE_daemon::DaemonAppId mpir_id, std::string const& var);
    void releaseMPIR(LIBSSH2_CHANNEL* channel, FE_daemon::DaemonAppId mpir_id);
    bool waitMPIR(LIBSSH2_CHANNEL* channel, FE_daemon::DaemonAppId mpir_id);
    bool checkApp(LIBSSH2_CHANNEL* channel, FE_daemon::DaemonAppId mpir_id);
    void deregisterApp(LIBSSH2_CHANNEL* channel, FE_daemon::DaemonAppId mpir_id);
    void stopRemoteDaemon(UniqueChannel&& channel, pid_t daemon_pid);
};

struct RemoteDaemon
{
    SSHSession m_session;
    SSHSession::UniqueChannel m_channel;
    pid_t m_daemonPid;

    RemoteDaemon(SSHSession&& session, std::string const& daemonPath);
    RemoteDaemon(RemoteDaemon&& expiring);
    ~RemoteDaemon();

    FE_daemon::MPIRResult launchMPIR(char const* const* launcher_argv, char const* const* env);
    std::string readStringMPIR(FE_daemon::DaemonAppId mpir_id, std::string const& var);
    void releaseMPIR(FE_daemon::DaemonAppId mpir_id);
    bool checkApp(FE_daemon::DaemonAppId mpir_id);
    void deregisterApp(FE_daemon::DaemonAppId mpir_id);
};
