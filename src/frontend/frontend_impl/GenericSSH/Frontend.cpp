/******************************************************************************\
 * Frontend.cpp -  Frontend library functions for SSH based workload manager.
 *
 * Copyright 2017-2022 Hewlett Packard Enterprise Development LP.
 * SPDX-License-Identifier: Linux-OpenIB
 ******************************************************************************/

// This pulls in config.h
#include "cti_defs.h"
#include "cti_argv_defs.hpp"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include <sys/types.h>

#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <sys/types.h>

#include <netdb.h>

#include <unordered_map>
#include <thread>
#include <future>

// Pull in manifest to properly define all the forward declarations
#include "transfer/Manifest.hpp"

#include "GenericSSH/Frontend.hpp"

#include "daemon/cti_fe_daemon_iface.hpp"

#include "useful/cti_useful.h"
#include "useful/cti_argv.hpp"
#include "useful/cti_wrappers.hpp"

#include <stdbool.h>
#include <stdlib.h>
#include <libssh2.h>

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

// SSH channel data read / write
namespace remote
{

static inline ssize_t channel_read(LIBSSH2_CHANNEL* channel, char* buf, ssize_t capacity)
{
    while (true) {
        auto const bytes_read = libssh2_channel_read(channel, buf, capacity);
        if (bytes_read < 0) {
            if (bytes_read == LIBSSH2_ERROR_EAGAIN) {
                continue;
            }
            throw std::runtime_error("read failed: " + std::string{std::strerror(bytes_read)});
        }

        return bytes_read;
    }
}

static inline ssize_t channel_write(LIBSSH2_CHANNEL* channel, char const* buf, ssize_t capacity)
{
    while (true) {
        auto const bytes_written = libssh2_channel_write(channel, buf, capacity);
        if (bytes_written < 0) {
            if (bytes_written == LIBSSH2_ERROR_EAGAIN) {
                continue;
            }
            throw std::runtime_error("write failed: " + std::string{std::strerror(bytes_written)});
        }
        return bytes_written;
    }
}

static auto channel_wait(LIBSSH2_SESSION *session, int fd)
{
    // Wait up to 10 seconds
    auto timeout = timeval{ .tv_sec = 10, .tv_usec = 0 };

    // Wait for fd to be ready
    auto fds = fd_set{};
    FD_ZERO(&fds);
    FD_SET(fd, &fds);

    auto block_directions = libssh2_session_block_directions(session);
    auto select_rc = ::select(fd + 1,
        (block_directions & LIBSSH2_SESSION_BLOCK_INBOUND) ? &fds : nullptr,
        (block_directions & LIBSSH2_SESSION_BLOCK_OUTBOUND) ? &fds : nullptr,
        nullptr, &timeout);

    if ((select_rc < 0) && (errno != EAGAIN)) {
        throw std::runtime_error("select on SSH socket failed: "
            + std::string{strerror(errno)});
    }

    return select_rc;
}

} // remote

class SSHSession {
private: // Internal objects
class SSHAgent {
private:
    LIBSSH2_AGENT * m_agent;
    std::string     m_username;

public:
    SSHAgent(LIBSSH2_SESSION *session, std::string username)
    : m_agent{nullptr}, m_username{username}
    {
        assert(session != nullptr);
        // Connect to the ssh-agent
        m_agent = libssh2_agent_init(session);
        if (m_agent == nullptr) {
            throw std::runtime_error("Could not init ssh-agent support.");
        }
    }

    ~SSHAgent()
    {
        // cleanup
        if (m_agent != nullptr) {
            libssh2_agent_disconnect(m_agent);
            libssh2_agent_free(m_agent);
        }
    }

    // Delete copy/move constructors
    SSHAgent(const SSHAgent&) = delete;
    SSHAgent& operator=(const SSHAgent&) = delete;
    SSHAgent(SSHAgent&&) = delete;
    SSHAgent& operator=(SSHAgent&&) = delete;

    bool auth()
    {
        if (libssh2_agent_connect(m_agent)) {
            throw std::runtime_error("Could not connect to ssh-agent.");
        }
        if (libssh2_agent_list_identities(m_agent)) {
            throw std::runtime_error("Could not request identities from ssh-agent.");
        }
        // Try to obtain a valid identity from the agent and authenticate
        struct libssh2_agent_publickey *identity, *prev_identity = nullptr;
        int rc;
        while (1) {
            rc = libssh2_agent_get_identity(m_agent, &identity, prev_identity);
            if ( rc < 0 ) {
                throw std::runtime_error("Could not obtain identity from ssh-agent.");
            }
            if ( rc == 1 ) {
                throw std::runtime_error("ssh-agent reached the end of the public keys without authenticating.");
            }
            // Only valid return codes are 1, 0, or negative value.
            assert( rc == 0 );
            if ( libssh2_agent_userauth(m_agent, m_username.c_str(), identity) == 0 ) {
                // Success
                return true;
            }
            prev_identity = identity;
        }
        // Shouldn't get here
        return false;
    }
};

private: // Data members
    cti::fd_handle m_session_sock;
    std::unique_ptr<LIBSSH2_SESSION,decltype(&delete_ssh2_session)> m_session_ptr;
    const struct passwd &m_pwd;

private: // utility methods
    std::string const getError()
    {
        char *errmsg;
        if (m_session_ptr != nullptr) {
            libssh2_session_last_error(m_session_ptr.get(), &errmsg, nullptr, 0);
            if (errmsg != nullptr) return std::string{errmsg};
        }
        return std::string{"Unknown libssh2 error."};
    }

public: // Constructor/destructor
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
    SSHSession(std::string const& hostname, const struct passwd &pwd)
    : m_session_ptr{nullptr,delete_ssh2_session}
    , m_pwd{pwd}
    {
        int rc;
        struct addrinfo hints = {};
        // Setup the hints structure
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_NUMERICSERV;

        // FIXME: This should be using a service name instead of hardcoded port number.
        //        Adjust ai_flags above for this fix.
        // FIXME: How to handle containers with non-default SSH port numbers?
        struct addrinfo *host;
        if ((rc = getaddrinfo(hostname.c_str(), "22", &hints, &host)) != 0) {
            throw std::runtime_error("getaddrinfo failed: " + std::string{gai_strerror(rc)});
        }
        // Take ownership of the host addrinfo into the unique_ptr.
        // This will enforce cleanup.
        auto host_ptr = cti::take_pointer_ownership(std::move(host),freeaddrinfo);

        // create the ssh socket
        m_session_sock = std::move(cti::fd_handle{ (int)socket(  host_ptr.get()->ai_family,
                                                    host_ptr.get()->ai_socktype,
                                                    host_ptr.get()->ai_protocol)
        });

        // Connect the socket
        if (connect(m_session_sock.fd(), host_ptr.get()->ai_addr, host_ptr.get()->ai_addrlen)) {
            throw std::runtime_error("failed to connect to host " + hostname);
        }

        // Init a new libssh2 session.
        m_session_ptr = cti::take_pointer_ownership(libssh2_session_init(), delete_ssh2_session);
        if (m_session_ptr == nullptr) {
            throw std::runtime_error("libssh2_session_init() failed");
        }

        // Start up the new session.
        // This will trade welcome banners, exchange keys, and setup crypto,
        // compression, and MAC layers.
        if (libssh2_session_handshake(m_session_ptr.get(), m_session_sock.fd()) < 0) {
            throw std::runtime_error("Failure establishing SSH session: " + getError());
        }

        // At this point we havn't authenticated. The first thing to do is check
        // the hostkey's fingerprint against our known hosts.
        auto known_host_ptr = cti::take_pointer_ownership(  libssh2_knownhost_init(m_session_ptr.get()),
                                                            libssh2_knownhost_free);
        if (known_host_ptr == nullptr) {
            throw std::runtime_error("Failure initializing knownhost file");
        }

        // Detect usable SSH directory
        auto sshDir = std::string{m_pwd.pw_dir} + "/.ssh/";

        // Determine if default SSH directory should be overridden (default is ~/.ssh)
        if (auto const ssh_dir = ::getenv(SSH_DIR_ENV_VAR)) {

            if (!cti::dirHasPerms(ssh_dir, R_OK | X_OK)) {
                throw std::runtime_error("Default SSH keyfile directory " + sshDir + " \
was overridden by setting the environment variable " SSH_DIR_ENV_VAR " to " + ssh_dir + " \
, but the directory was not readable / executable. Ensure the directory exists and has \
permission code 700.");
            }

            sshDir = ssh_dir;
        }

        // Verify SSH directory permissions
        if (!cti::dirHasPerms(sshDir.c_str(), R_OK | X_OK)) {
            throw std::runtime_error("The SSH keyfile directory at " + sshDir + " \
is not readable / executable. Ensure the directory exists and has permission code 700. \
If your system is configured to use a non-default SSH directory, it can be overridden \
by setting the environment variable " SSH_DIR_ENV_VAR " to the SSH directory path.");
        }

        // Detect usable knownhosts file
        auto knownHostsPath = sshDir + "/known_hosts";

        // Determine if knownhosts path should be overridden (default is <sshDir>/known_hosts
        if (auto const known_hosts_path = ::getenv(SSH_KNOWNHOSTS_PATH_ENV_VAR)) {

            if (!cti::fileHasPerms(known_hosts_path, R_OK)) {
                throw std::runtime_error("Default SSH known hosts path \
" + knownHostsPath + " was overridden by setting the environment variable \
" SSH_DIR_ENV_VAR " to " + known_hosts_path + ", but the file was not readable. Ensure \
the file exists and has permission code 600.");
            }

            knownHostsPath = known_hosts_path;
        }

        // Verify known_hosts permissions
        if (!cti::fileHasPerms(knownHostsPath.c_str(), R_OK)) {
            throw std::runtime_error("The SSH known hosts file at " + knownHostsPath + " \
is not readable. Ensure the file exists and has permission code 600. If your system is \
configured to use a non-default SSH known_hosts file, it can be overridden by setting \
the environment variable " SSH_KNOWNHOSTS_PATH_ENV_VAR " to the known hosts file path.");
        }

        // Read known_hosts
        rc = libssh2_knownhost_readfile(known_host_ptr.get(), knownHostsPath.c_str(), LIBSSH2_KNOWNHOST_FILE_OPENSSH);
        if (rc < 0) {
            throw std::runtime_error("The SSH known hosts file at " + knownHostsPath + " \
failed to parse correctly. Ensure the file exists and is formatted correctly. If your \
system is configured to use a non-default SSH known_hosts file, it can be overridden \
by setting the environment variable " SSH_KNOWNHOSTS_PATH_ENV_VAR " to the known hosts file \
 path.");
        }

        // obtain the session hostkey fingerprint
        size_t len;
        int type;
        const char *fingerprint = libssh2_session_hostkey(m_session_ptr.get(), &len, &type);
        if (fingerprint == nullptr) {
            throw std::runtime_error("Failed to obtain the remote hostkey");
        }

        // Check the remote hostkey against the knownhosts
        { int keymask = (type == LIBSSH2_HOSTKEY_TYPE_RSA) ? LIBSSH2_KNOWNHOST_KEY_SSHRSA:LIBSSH2_KNOWNHOST_KEY_SSHDSS;
            struct libssh2_knownhost *kh = nullptr;
            int check = libssh2_knownhost_checkp(   known_host_ptr.get(),
                                                    hostname.c_str(), 22,
                                                    fingerprint, len,
                                                    LIBSSH2_KNOWNHOST_TYPE_PLAIN |
                                                    LIBSSH2_KNOWNHOST_KEYENC_BASE64 |
                                                    keymask,
                                                    &kh);
            switch (check) {
                case LIBSSH2_KNOWNHOST_CHECK_MATCH:
                    // Do nothing
                    break;
                case LIBSSH2_KNOWNHOST_CHECK_NOTFOUND:
                    // Don't store empty fingerprint in host file
                    if ((len > 0) && (fingerprint[0] != '\0')) {

                        // Add the host to the host file and continue
                        if (libssh2_knownhost_addc( known_host_ptr.get(),
                                                    hostname.c_str(), nullptr,
                                                    fingerprint, len,
                                                    nullptr, 0,
                                                    LIBSSH2_KNOWNHOST_TYPE_PLAIN |
                                                    LIBSSH2_KNOWNHOST_KEYENC_BASE64 |
                                                    keymask,
                                                    nullptr)) {
                            throw std::runtime_error("Failed to add remote host to knownhosts");
                        }
                    }
                    break;
                case LIBSSH2_KNOWNHOST_CHECK_MISMATCH:
                    throw std::runtime_error("Remote hostkey mismatch with knownhosts file! Remove the host from knownhosts to resolve: " + hostname);
                case LIBSSH2_KNOWNHOST_CHECK_FAILURE:
                default:
                    throw std::runtime_error("Failure with libssh2 knownhost check");
            }
        }

        // FIXME: How to ensure sane pwd?
        assert(m_pwd.pw_name != nullptr);
        std::string const username{m_pwd.pw_name};

        // check what authentication methods are available
        char *userauthlist = libssh2_userauth_list(m_session_ptr.get(), username.c_str(), strlen(username.c_str()));

        // Check to see if we can use the passwordless login method, otherwise ensure
        // we can use interactive login
        if ( strstr(userauthlist, "publickey") != nullptr ) {
            // Start by trying to use the ssh-agent mechanism
            try {
                SSHAgent agent(m_session_ptr.get(), username);
                agent.auth();
                return;
            }
            catch (std::exception const& ex) {
                // ignore exceptions from SSHAgent - we fallback on other mechanisms
            }

            // Detect usable public / private key file
            auto tryAuthKeyfilePair = [](LIBSSH2_SESSION* session, std::string const& username, std::string const& defaultPublickeyPath, std::string const& defaultPrivatekeyPath) {

                auto publickeyPath = defaultPublickeyPath;
                auto privatekeyPath = defaultPrivatekeyPath;

                // Determine if public keyfile path should be overridden
                if (auto const pubkey_path = ::getenv(SSH_PUBKEY_PATH_ENV_VAR)) {

                    if (!cti::fileHasPerms(pubkey_path, R_OK)) {
                        throw std::runtime_error("Default SSH public key path \
" + publickeyPath + " was overridden by setting the environment variable \
" SSH_PUBKEY_PATH_ENV_VAR " to " + pubkey_path + ", but the file was not readable. \
Ensure the file exists and has permission code 644.");
                    }

                    publickeyPath = pubkey_path;
                }

                // Verify public key exists
                if (!cti::pathExists(publickeyPath.c_str())) {
                    return false;
                }

                // Verify public key permissions
                if (!cti::fileHasPerms(publickeyPath.c_str(), R_OK)) {
                    throw std::runtime_error("The SSH public key file at \
" + publickeyPath + " is not readable. Ensure the file exists and has permission code \
644. If your system is configured to use a non-default SSH public key file, it can be \
overridden by setting the environment variable " SSH_PUBKEY_PATH_ENV_VAR " to the public \
key file path.");
                }

                // Determine if private keyfile path should be overridden
                if (auto const prikey_path = ::getenv(SSH_PRIKEY_PATH_ENV_VAR)) {

                    if (!cti::fileHasPerms(prikey_path, R_OK)) {
                        throw std::runtime_error("Default SSH private key path \
" + privatekeyPath + " was overridden by setting the environment variable \
" SSH_PRIKEY_PATH_ENV_VAR " to " + prikey_path + ", but the file was not readable. \
Ensure the file exists and has permission code 600.");
                    }

                    privatekeyPath = prikey_path;
                }

                // Verify private key exists
                if (!cti::pathExists(privatekeyPath.c_str())) {
                    return false;
                }

                // Verify private key permissions
                if (!cti::fileHasPerms(privatekeyPath.c_str(), R_OK)) {
                    throw std::runtime_error("The SSH private key file at \
" + privatekeyPath + " is not readable. Ensure the file exists and has permission code \
600. If your system is configured to use a non-default SSH private key file, it can be \
overridden by setting the environment variable " SSH_PRIKEY_PATH_ENV_VAR " to the private \
key file path.");
                }

                // Read passphrase from environment. If unset, null pointer is interpreted
                // as no passphrase by libssh2_userauth_publickey_fromfile
                auto const ssh_passphrase = ::getenv(SSH_PASSPHRASE_ENV_VAR);

                // Attempt to authenticate using public / private keys
                int rc = LIBSSH2_ERROR_EAGAIN;
                while (rc == LIBSSH2_ERROR_EAGAIN) {
                    rc = libssh2_userauth_publickey_fromfile(session,
                        username.c_str(), publickeyPath.c_str(), privatekeyPath.c_str(),
                        ssh_passphrase);
                }

                // Check return code
                if (rc < 0) {

                    // Get libssh2 error information
                    char *libssh2_error_ptr = nullptr;
                    libssh2_session_last_error(session, &libssh2_error_ptr, nullptr, true);
                    auto const libssh2ErrorStr = std::string{ (libssh2_error_ptr)
                        ? libssh2_error_ptr
                        : "no error information available"
                    };

                    throw std::runtime_error(" \
Failed to authenticate using the username " + username + ", SSH public \
key file at " + publickeyPath + " and private key file at " + privatekeyPath + " . \
If these paths are not correct, they can be overridden by setting the environment \
variables " SSH_PUBKEY_PATH_ENV_VAR " and " SSH_PRIKEY_PATH_ENV_VAR " . If a passhrase \
is required to unlock the keys, it can be provided by setting the environment variable \
" SSH_PASSPHRASE_ENV_VAR " (" + libssh2ErrorStr + ")");
                }

                // Authentication was successful
                return true;
            };

            // Attempt authentication using RSA and DSA keys
            if (tryAuthKeyfilePair(m_session_ptr.get(), username,
                sshDir + "/id_rsa.pub", sshDir + "/id_rsa")) {
                return;
            } else if (tryAuthKeyfilePair(m_session_ptr.get(), username,
                sshDir + "/id_dsa.pub", sshDir + "/id_dsa")) {
                return;
            }

            throw std::runtime_error("Failed to detect SSH key files in \
" + sshDir + " . These paths can be specified by setting the environment \
variables " SSH_PUBKEY_PATH_ENV_VAR " and " SSH_PRIKEY_PATH_ENV_VAR " . If a passhrase \
is required to unlock the keys, it can be provided by setting the environment variable \
" SSH_PASSPHRASE_ENV_VAR " . CTI requires passwordless (public key) SSH authentication \
to compute nodes. If passwordless SSH access to compute nodes is unavailable, \
contact your system adminstrator.");
        }
    }

    ~SSHSession() = default;

    // Delete copy/move constructors
    SSHSession(const SSHSession&) = delete;
    SSHSession& operator=(const SSHSession&) = delete;
    SSHSession(SSHSession&&) = delete;
    SSHSession& operator=(SSHSession&&) = delete;

    /*
     * setRemoveEnvironment - send the current environment to the remote host via 
     * an ssh channel.
     */
    void setRemoteEnvironment(LIBSSH2_CHANNEL *channel)
    {
        extern char** environ;
        for (auto it = environ; *it; ++it) {
            auto var = *it;
            auto len = strlen(var);
            if (auto eq = strchr(var, '=')) {
                auto vlen = eq-var;
                libssh2_channel_setenv_ex(channel, var, vlen, eq+1, len-vlen-1);
            }
        }
    }

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
    void executeRemoteCommand(const char* const args[], bool synchronous)
    {
        // sanity
        assert(args != nullptr);
        assert(args[0] != nullptr);

        // Create a new ssh channel
        auto channel_ptr = cti::take_pointer_ownership( libssh2_channel_open_session(m_session_ptr.get()),
                                                        delete_ssh2_channel);
        if (channel_ptr == nullptr) {
            throw std::runtime_error("Failure opening SSH channel on session");
        }

        setRemoteEnvironment(channel_ptr.get());

        // Create the command string
        std::string argvString {args[0]};
        for (const char* const* arg = &args[1]; *arg != nullptr; arg++) {
            argvString.push_back(' ');
            argvString += std::string(*arg);
        }

        // Continue command in background after SSH channel disconnects
        if (!synchronous) {
            argvString = "nohup " + argvString + " < /dev/null > /dev/null 2>&1 &";
        } else {
            argvString += "< /dev/null > /dev/null 2>&1";
        }

        // Request execution of the command on the remote host
        auto exec_rc = libssh2_channel_exec(channel_ptr.get(), argvString.c_str());
        if ((exec_rc < 0) && (exec_rc != LIBSSH2_ERROR_EAGAIN)) {
            throw std::runtime_error("Execution of ssh command failed: "
                + std::to_string(exec_rc));
        }

        // Wait for synchronous run to complete
        if (synchronous) {
            auto close_rc = libssh2_channel_close(channel_ptr.get());
            while (close_rc == LIBSSH2_ERROR_EAGAIN) {
                remote::channel_wait(m_session_ptr.get(), m_session_sock.fd());
            }
        }
    }

    auto startRemoteCommand(const char* const argv[])
    {
        // sanity
        assert(argv != nullptr);
        assert(argv[0] != nullptr);

        // Create a new ssh channel
        auto channel_ptr = cti::take_pointer_ownership( libssh2_channel_open_session(m_session_ptr.get()),
                                                        delete_ssh2_channel);
        if (channel_ptr == nullptr) {
            throw std::runtime_error("Failure opening SSH channel on session");
        }

        setRemoteEnvironment(channel_ptr.get());

        // Create the command string
        auto const ldLibraryPath = std::string{::getenv("LD_LIBRARY_PATH")};
        auto argvString = std::string{"LD_LIBRARY_PATH="} + ldLibraryPath;
        for (auto arg = argv; *arg != nullptr; arg++) {
            argvString.push_back(' ');
            argvString += std::string(*arg);
        }

        // Request execution of the command on the remote host
        int rc = libssh2_channel_exec(channel_ptr.get(), argvString.c_str());
        if ((rc < 0) && (rc != LIBSSH2_ERROR_EAGAIN)) {
            throw std::runtime_error("Execution of ssh command failed: " + std::to_string(rc));
        }

        return channel_ptr;
    }

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
    void sendRemoteFile(const char* source_path, const char* destination_path, int mode)
    {
        // Sanity
        assert(source_path != nullptr);
        assert(destination_path != nullptr);
        //Get the length of the source file
        struct stat stbuf;
        {   cti::fd_handle source{ open(source_path, O_RDONLY) };
            if ((fstat(source.fd(), &stbuf) != 0) || (!S_ISREG(stbuf.st_mode))) {
              throw std::runtime_error("Could not fstat file to send: " + std::string{source_path});
            }
        }
        // Start a new scp transfer
        auto channel_ptr = cti::take_pointer_ownership( libssh2_scp_send(   m_session_ptr.get(),
                                                                            destination_path,
                                                                            mode & 0777,
                                                                            stbuf.st_size ),
                                                        delete_ssh2_channel);
        if (channel_ptr == nullptr) {
            throw std::runtime_error("Failure to scp send on session: " + getError());
        }
        // Write the contents of the source file to the destination file in blocks
        size_t const BLOCK_SIZE = 1024;
        if (auto source_file = cti::file::open(source_path, "rb")) {
            // read in a block
            char buf[BLOCK_SIZE];
            while (auto bytes_read = cti::file::read(buf, sizeof(char), BLOCK_SIZE, source_file.get())) {
                char *ptr = buf;
                // perform the write
                do {
                    auto rc = libssh2_channel_write(channel_ptr.get(), ptr, bytes_read);
                    if (rc < 0) {
                        throw std::runtime_error("Error writing to remote file: " + getError());
                    }
                    // rc indicates how many bytes were written this time
                    ptr += rc;
                    bytes_read -= rc;
                } while(bytes_read);
            }
        }
    }

    FE_daemon::MPIRResult getMPIRResult(std::string const& launcherName, pid_t launcher_pid)
    {
        // Construct FE remote daemon arguments
        auto daemonArgv = cti::OutgoingArgv<CTIFEDaemonArgv>{Frontend::inst().getFEDaemonPath()};
        daemonArgv.add(CTIFEDaemonArgv::ReadFD,  std::to_string(STDIN_FILENO));
        daemonArgv.add(CTIFEDaemonArgv::WriteFD, std::to_string(STDOUT_FILENO));

        // Launch FE daemon remotely to collect MPIR information
        auto channel = startRemoteCommand(daemonArgv.get());

        // Reader / writer functions will read / write data from and to SSH channel
        auto channel_reader = [&channel](char* buf, ssize_t capacity) {
            return remote::channel_read(channel.get(), buf, capacity);
        };
        auto channel_writer = [&channel](char const* buf, ssize_t capacity) {
            return remote::channel_write(channel.get(), buf, capacity);
        };

        // Read FE daemon initialization message
        [[maybe_unused]] auto const remote_pid = readLoop<pid_t>(channel_reader);

        // Determine path to launcher
        auto const launcherPath = cti::take_pointer_ownership(_cti_pathFind(launcherName.c_str(), nullptr), std::free);
        if (!launcherPath) {
            throw std::runtime_error("failed to find launcher in path: " + launcherName);
        }

        // Write MPIR attach request to channel
        writeLoop(channel_writer, FE_daemon::ReqType::AttachMPIR);
        writeLoop(channel_writer, launcherPath.get(), strlen(launcherPath.get()) + 1);
        writeLoop(channel_writer, launcher_pid);

        // Read MPIR attach request from relay pipe
        auto mpirResult = FE_daemon::readMPIRResp(channel_reader);
        Frontend::inst().writeLog("Received %zu proctable entries from remote daemon\n", mpirResult.proctable.size());

        // Shut down remote daemon
        writeLoop(channel_writer, FE_daemon::ReqType::Shutdown);
        auto const okResp = readLoop<FE_daemon::OKResp>(channel_reader);
        if (okResp.type != FE_daemon::RespType::OK) {
            fprintf(stderr, "warning: daemon shutdown failed\n");
        }

        // Close relay pipe and SSH channel
        channel.reset();

        return mpirResult;
    }
};

GenericSSHApp::GenericSSHApp(GenericSSHFrontend& fe, FE_daemon::MPIRResult&& mpirData)
    : App(fe, mpirData.mpir_id)
    , m_launcherPid { mpirData.launcher_pid }
    , m_binaryRankMap { std::move(mpirData.binaryRankMap) }
    , m_stepLayout  { fe.fetchStepLayout(mpirData.proctable) }
    , m_beDaemonSent { false }
    , m_toolPath    { SSH_TOOL_DIR }
    , m_attribsPath { SSH_TOOL_DIR }
    , m_stagePath   { cti::cstr::mkdtemp(std::string{fe.getCfgDir() + "/" + SSH_STAGE_DIR}) }
    , m_extraFiles  { fe.createNodeLayoutFile(m_stepLayout, m_stagePath) }
{
    // Ensure there are running nodes in the job.
    if (m_stepLayout.nodes.empty()) {
        throw std::runtime_error("Application " + getJobId() + " does not have any nodes.");
    }

    // Ensure application has been registered with daemon
    if (!m_daemonAppId) {
        throw std::runtime_error("tried to create app with invalid daemon id: " + std::to_string(m_daemonAppId));
    }

    // If an active MPIR session was provided, extract the MPIR ProcTable and write the PID List File.
    m_extraFiles.push_back(fe.createPIDListFile(mpirData.proctable, m_stagePath));
}

GenericSSHApp::~GenericSSHApp()
{
    if (!Frontend::isOriginalInstance()) {
        writeLog("~GenericSSHApp: forked PID %d exiting without cleanup\n", getpid());
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
        writeLog("~GenericSSHApp: %s\n", ex.what());
    }
}

/* running app info accessors */

std::string
GenericSSHApp::getJobId() const
{
    return std::to_string(m_launcherPid);
}

std::string
GenericSSHApp::getLauncherHostname() const
{
    throw std::runtime_error("not supported for WLM: getLauncherHostname");
}

bool
GenericSSHApp::isRunning() const
{
    return m_frontend.Daemon().request_CheckApp(m_daemonAppId);
}

std::vector<std::string>
GenericSSHApp::getHostnameList() const
{
    std::vector<std::string> result;
    // extract hostnames from each NodeLayout
    std::transform(m_stepLayout.nodes.begin(), m_stepLayout.nodes.end(), std::back_inserter(result),
        [](GenericSSHFrontend::NodeLayout const& node) { return node.hostname; });
    return result;
}

std::map<std::string, std::vector<int>>
GenericSSHApp::getBinaryRankMap() const
{
    return m_binaryRankMap;
}

std::vector<CTIHost>
GenericSSHApp::getHostsPlacement() const
{
    std::vector<CTIHost> result;
    // construct a CTIHost from each NodeLayout
    std::transform(m_stepLayout.nodes.begin(), m_stepLayout.nodes.end(), std::back_inserter(result),
        [](GenericSSHFrontend::NodeLayout const& node) {
            return CTIHost{node.hostname, node.pids.size()};
        });
    return result;
}

void
GenericSSHApp::releaseBarrier()
{
    // release MPIR barrier
    m_frontend.Daemon().request_ReleaseMPIR(m_daemonAppId);
}

void
GenericSSHApp::kill(int signal)
{
    // Connect through ssh to each node and send a kill command to every pid on that node
    for (auto&& node : m_stepLayout.nodes) {
        // kill -<sig> <pid> ... <pid>
        cti::ManagedArgv killArgv
            { "kill"
            , "-" + std::to_string(signal)
        };
        for (auto&& pid : node.pids) {
            killArgv.add(std::to_string(pid));
        }

        // run remote kill command
        SSHSession(node.hostname, m_frontend.getPwd()).executeRemoteCommand(killArgv.get(),
            /* synchronous */ true);
    }
}

void
GenericSSHApp::shipPackage(std::string const& tarPath) const
{
    auto packageName = cti::cstr::basename(tarPath);
    auto const destination = std::string{SSH_TOOL_DIR} + "/" + packageName;
    writeLog("GenericSSH shipping %s to '%s'\n", tarPath.c_str(), destination.c_str());

    // Send the package to each of the hosts using SCP
    for (auto&& node : m_stepLayout.nodes) {
        SSHSession(node.hostname, m_frontend.getPwd()).sendRemoteFile(tarPath.c_str(), destination.c_str(), S_IRWXU | S_IRWXG | S_IRWXO);
    }
}

void
GenericSSHApp::startDaemon(const char* const args[], bool synchronous)
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

        // Create the args for link
        auto linkArgv = cti::ManagedArgv {
            "ln", "-s", sourcePath.c_str(), destinationPath.c_str()
        };

        // Run link command
        if (!m_frontend.Daemon().request_ForkExecvpUtil_Sync(
            m_daemonAppId, "ln", linkArgv.get(),
            -1, -1, -1,
            nullptr)) {
            throw std::runtime_error("failed to link " + sourcePath + " to " + destinationPath);
        }

        // Ship the unique backend daemon
        shipPackage(destinationPath);
        // set transfer to true
        m_beDaemonSent = true;
    }

    // Use location of existing launcher binary on compute node
    std::string const launcherPath{m_toolPath + "/" + getBEDaemonName()};

    // Prepare the launcher arguments
    cti::ManagedArgv launcherArgv { launcherPath };

    // Copy provided launcher arguments
    launcherArgv.add(args);

    // Execute the launcher on each of the hosts using SSH
    auto executeRemoteCommand = [](std::string const& hostname, const struct passwd& pwd,
        char const* const* argv, bool synchronous) {
        auto session = SSHSession{hostname, pwd};
        session.executeRemoteCommand(argv, synchronous);
    };

    if (synchronous) {

        // Synchronous launches run in parallel as future tasks
        auto launchFutures = std::vector<std::future<void>>{};
        for (auto&& node : m_stepLayout.nodes) {
            launchFutures.push_back(std::async(std::launch::async, executeRemoteCommand,
                node.hostname, m_frontend.getPwd(), launcherArgv.get(),
                /* synchronous */ true));
        }
        for (auto&& future : launchFutures) {
            future.get();
        }

    } else {

        // Asynchronous launches can be started in parallel and continued to run
        for (auto&& node : m_stepLayout.nodes) {
            executeRemoteCommand(node.hostname, m_frontend.getPwd(), launcherArgv.get(),
                /* asynchronous */ false);
        }
    }
}

/* SSH frontend implementation */

GenericSSHFrontend::GenericSSHFrontend()
{
    // Initialize the libssh2 library. Note that this isn't threadsafe.
    // FIXME: Address this.
    if ( libssh2_init(0) ) {
        throw std::runtime_error("Failed to initailize libssh2");
    }
}

GenericSSHFrontend::~GenericSSHFrontend()
{
    // Deinit the libssh2 library.
    libssh2_exit();
}

std::weak_ptr<App>
GenericSSHFrontend::launch(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
    CStr inputFile, CStr chdirPath, CArgArray env_list)
{
    auto appPtr = std::make_shared<GenericSSHApp>(*this,
        launchApp(launcher_argv, stdout_fd, stderr_fd, inputFile, chdirPath, env_list));

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
GenericSSHFrontend::launchBarrier(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
        CStr inputFile, CStr chdirPath, CArgArray env_list)
{
    auto ret = m_apps.emplace(std::make_shared<GenericSSHApp>(*this,
        launchApp(launcher_argv, stdout_fd, stderr_fd, inputFile, chdirPath, env_list)));
    if (!ret.second) {
        throw std::runtime_error("Failed to create new App object.");
    }
    return *ret.first;
}

std::weak_ptr<App>
GenericSSHFrontend::registerJob(size_t numIds, ...)
{
    if (numIds != 1) {
        throw std::logic_error("expecting single pid argument to register app");
    }

    va_list idArgs;
    va_start(idArgs, numIds);

    pid_t launcherPid = va_arg(idArgs, pid_t);

    va_end(idArgs);

    auto ret = m_apps.emplace(std::make_shared<GenericSSHApp>(*this,
        // MPIR attach to launcher
        Daemon().request_AttachMPIR(
            // Get path to launcher binary
            cti::take_pointer_ownership(
                _cti_pathFind(getLauncherName().c_str(), nullptr),
                std::free).get(),
            // Attach to existing launcherPid
            launcherPid)));
    if (!ret.second) {
        throw std::runtime_error("Failed to create new App object.");
    }
    return *ret.first;
}

std::string
GenericSSHFrontend::getHostname() const
{
    return cti::cstr::gethostname();
}

/* SSH frontend implementations */

std::string
GenericSSHFrontend::getLauncherName()
{
    // Cache the launcher name result. Assume mpiexec by default.
    auto static launcherName = std::string{cti::getenvOrDefault(CTI_LAUNCHER_NAME_ENV_VAR, "mpiexec")};
    return launcherName;
}

GenericSSHFrontend::StepLayout
GenericSSHFrontend::fetchStepLayout(MPIRProctable const& procTable)
{
    StepLayout layout;
    layout.numPEs = procTable.size();

    size_t nodeCount = 0;
    size_t peCount   = 0;

    std::unordered_map<std::string, size_t> hostNidMap;

    // For each new host we see, add a host entry to the end of the layout's host list
    // and hash each hostname to its index into the host list
    for (auto&& proc : procTable) {

        // Truncate hostname at first '.' in case the launcher has used FQDNs for hostnames
        auto const base_hostname = proc.hostname.substr(0, proc.hostname.find("."));

        size_t nid;
        auto const hostNidPair = hostNidMap.find(base_hostname);
        if (hostNidPair == hostNidMap.end()) {
            // New host, extend nodes array, and fill in host entry information
            nid = nodeCount++;
            layout.nodes.push_back(NodeLayout
                { .hostname = base_hostname
                , .pids = {}
                , .firstPE = peCount
            });
            hostNidMap[base_hostname] = nid;
        } else {
            nid = hostNidPair->second;
        }

        // add new pe to end of host's list
        layout.nodes[nid].pids.push_back(proc.pid);

        peCount++;
    }

    return layout;
}

std::string
GenericSSHFrontend::createNodeLayoutFile(GenericSSHFrontend::StepLayout const& stepLayout, std::string const& stagePath)
{
    // How a SSH Node Layout File entry is created from a SSH Node Layout entry:
    auto make_layoutFileEntry = [](NodeLayout const& node) {
        // Ensure we have good hostname information.
        auto const hostname_len = node.hostname.size() + 1;
        if (hostname_len > sizeof(cti_layoutFile_t::host)) {
            throw std::runtime_error("hostname too large for layout buffer");
        }

        // Extract PE and node information from Node Layout.
        auto layout_entry    = cti_layoutFile_t{};
        layout_entry.PEsHere = node.pids.size();
        layout_entry.firstPE = node.firstPE;

        memcpy(layout_entry.host, node.hostname.c_str(), hostname_len);

        return layout_entry;
    };

    // Create the file path, write the file using the Step Layout
    auto const layoutPath = std::string{stagePath + "/" + SSH_LAYOUT_FILE};
    if (auto const layoutFile = cti::file::open(layoutPath, "wb")) {

        // Write the Layout header.
        cti::file::writeT(layoutFile.get(), cti_layoutFileHeader_t
            { .numNodes = (int)stepLayout.nodes.size()
        });

        // Write a Layout entry using node information from each SSH Node Layout entry.
        for (auto const& node : stepLayout.nodes) {
            cti::file::writeT(layoutFile.get(), make_layoutFileEntry(node));
        }

        return layoutPath;
    } else {
        throw std::runtime_error("failed to open layout file path " + layoutPath);
    }
}

std::string
GenericSSHFrontend::createPIDListFile(MPIRProctable const& procTable, std::string const& stagePath)
{
    auto const pidPath = std::string{stagePath + "/" + SSH_PID_FILE};
    if (auto const pidFile = cti::file::open(pidPath, "wb")) {

        // Write the PID List header.
        cti::file::writeT(pidFile.get(), cti_pidFileheader_t
            { .numPids = (int)procTable.size()
        });

        // Write a PID entry using information from each MPIR ProcTable entry.
        for (auto&& elem : procTable) {
            cti::file::writeT(pidFile.get(), cti_pidFile_t
                { .pid = elem.pid
            });
        }

        return pidPath;
    } else {
        throw std::runtime_error("failed to open PID file path " + pidPath);
    }
}

static std::string
getShimmedLauncherName(std::string const& launcherPath)
{
    if (cti::cstr::basename(launcherPath) == "mpirun") {
        return "mpiexec.hydra";
    }

    return "";
}

FE_daemon::MPIRResult
GenericSSHFrontend::launchApp(const char * const launcher_argv[],
        int stdoutFd, int stderrFd, const char *inputFile, const char *chdirPath, const char * const env_list[])
{
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

        // Use MPIR shim if detected to be necessary
        auto const shimmedLauncherName = getShimmedLauncherName(launcher_path.get());
        if (!shimmedLauncherName.empty()) {
            // Get the shim setup paths from the frontend instance
            auto const shimBinaryPath = Frontend::inst().getBaseDir() + "/libexec/" + CTI_MPIR_SHIM_BINARY;
            auto const temporaryShimBinDir = Frontend::inst().getCfgDir() + "/shim";
            auto const shimmedLauncherPath = cti::take_pointer_ownership(_cti_pathFind(shimmedLauncherName.c_str(), nullptr), std::free);
            if (shimmedLauncherPath == nullptr) {
                throw std::runtime_error("Failed to find launcher in path: " +
                    std::string{shimmedLauncherPath.get()});
            }

            // Launch script with MPIR shim.
            return Daemon().request_LaunchMPIRShim(
                shimBinaryPath.c_str(), temporaryShimBinDir.c_str(), shimmedLauncherPath.get(),
                launcher_path.get(), launcherArgv.get(),
                ::open(inputFile, O_RDONLY), stdoutFd, stderrFd,
                env_list);
        }

        // Launch program under MPIR control.
        return Daemon().request_LaunchMPIR(
            launcher_path.get(), launcherArgv.get(),
            ::open(inputFile, O_RDONLY), stdoutFd, stderrFd,
            env_list);

    } else {
        throw std::runtime_error("Failed to find launcher in path: " + getLauncherName());
    }
}

std::weak_ptr<App>
GenericSSHFrontend::registerRemoteJob(char const* hostname, pid_t launcher_pid)
{
    auto mpirResult = SSHSession{hostname, Frontend::inst().getPwd()}.getMPIRResult(getLauncherName(), launcher_pid);

    // Register application with local FE daemon and insert into received MPIR response
    auto const mpir_id = Frontend::inst().Daemon().request_RegisterApp(::getpid());
    mpirResult.mpir_id = mpir_id;

    // Create and return new application object using MPIR response
    auto ret = m_apps.emplace(std::make_shared<GenericSSHApp>(*this, std::move(mpirResult)));
    if (!ret.second) {
        throw std::runtime_error("Failed to create new App object.");
    }
    return *ret.first;
}
