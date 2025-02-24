/******************************************************************************\
 * SSHSession.cpp - Utility library functions for SSH based workload manager.
 *
 * Copyright 2017-2023 Hewlett Packard Enterprise Development LP.
 * SPDX-License-Identifier: Linux-OpenIB
 ******************************************************************************/

// This pulls in config.h
#include "cti_defs.h"
#include "cti_argv_defs.hpp"

#include <assert.h>
#include <netdb.h>

#include "SSHSession.hpp"

#include "useful/cti_argv.hpp"
#include "useful/cti_split.hpp"
#include "useful/cti_log.h"

#include <libssh2.h>

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

class SSHAgent {
private:
    LIBSSH2_AGENT * m_agent;
    std::string     m_username;

public:
    SSHAgent(LIBSSH2_SESSION *session, std::string username)
        : m_agent{nullptr}, m_username{username}
    {
        if (session == nullptr) {
            throw std::logic_error("SSHAgent: session was null");
        }

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

    void auth()
    {
        if (libssh2_agent_connect(m_agent)) {
            throw std::runtime_error("Could not connect to ssh-agent.");
        }
        if (libssh2_agent_list_identities(m_agent)) {
            throw std::runtime_error("Could not request identities from ssh-agent.");
        }
        // Try to obtain a valid identity from the agent and authenticate
        struct libssh2_agent_publickey *identity, *prev_identity = nullptr;
        while (1) {
            auto rc = libssh2_agent_get_identity(m_agent, &identity, prev_identity);

            if (rc < 0) {
                throw std::runtime_error("Could not obtain identity from ssh-agent.");

            } else if (rc == 1) {
                throw std::runtime_error("ssh-agent reached the end of the public keys without authenticating.");
            }

            // Only valid return codes are 1, 0, or negative value.
            if (libssh2_agent_userauth(m_agent, m_username.c_str(), identity) == 0) {
                return;
            }

            prev_identity = identity;
        }
    }
};

// SSHSession implementations

// Get libssh2 error information
static auto get_libssh2_error(LIBSSH2_SESSION* session)
{
    char *libssh2_error_ptr = nullptr;
    libssh2_session_last_error(session, &libssh2_error_ptr, nullptr, false);
    return std::string{ (libssh2_error_ptr)
        ? libssh2_error_ptr
        : "no error information available"
    };
}

// Retry if hit timeout
template <typename Func>
static auto libssh2_retry(Func&& func)
{
    auto rc = LIBSSH2_ERROR_TIMEOUT;
    for (auto i = 0; i < 10; i++) {
        rc = func();

        if (rc != LIBSSH2_ERROR_TIMEOUT)  {
            break;
        }

        ::sleep(1);
    }

    return rc;
}

SSHSession::SSHSession(std::string const& hostname, std::string const& username, std::string const& homeDir)
    : m_session_ptr{nullptr, delete_ssh2_session}
{
    int rc;
    struct addrinfo hints = {};
    // Setup the hints structure
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICSERV;

    // FIXME: This should be using a service name instead of hardcoded port number.
    //        Adjust ai_flags above for this fix.
    struct addrinfo *host;
    auto ssh_port = (getenv(SSH_PORT_ENV_VAR)) ? getenv(SSH_PORT_ENV_VAR) : "22";
    if ((rc = getaddrinfo(hostname.c_str(), ssh_port, &hints, &host)) != 0) {
        throw std::runtime_error("getaddrinfo failed: " + std::string{gai_strerror(rc)});
    }
    // Take ownership of the host addrinfo into the unique_ptr.
    // This will enforce cleanup.
    auto host_ptr = cti::take_pointer_ownership(std::move(host), freeaddrinfo);

    // create the ssh socket
    m_session_sock = std::move(cti::fd_handle{ (int)socket(  host_ptr->ai_family,
                                                host_ptr->ai_socktype,
                                                host_ptr->ai_protocol)
    });

    // Connect the socket
    if (connect(m_session_sock.fd(), host_ptr->ai_addr, host_ptr->ai_addrlen)) {
        throw std::runtime_error("failed to connect to host " + hostname);
    }

    // Init a new libssh2 session.
    m_session_ptr = cti::take_pointer_ownership(libssh2_session_init(), delete_ssh2_session);
    if (m_session_ptr == nullptr) {
        throw std::runtime_error("libssh2_session_init() failed");
    }

    // Set to blocking mode
    libssh2_session_set_blocking(m_session_ptr.get(), 1);
    if (::getenv("CTI_DEBUG") != nullptr) {
        libssh2_trace(m_session_ptr.get(), LIBSSH2_TRACE_KEX | LIBSSH2_TRACE_AUTH | LIBSSH2_TRACE_ERROR);
    }

    // Start up the new session.
    // This will trade welcome banners, exchange keys, and setup crypto,
    // compression, and MAC layers.
    auto handshake_rc = libssh2_retry([this]() {
        return libssh2_session_handshake(m_session_ptr.get(), m_session_sock.fd());
    });

    if (handshake_rc < 0) {
        throw std::runtime_error("Failure establishing SSH session: "
            + get_libssh2_error(m_session_ptr.get()));
    }

    // At this point we havn't authenticated. The first thing to do is check
    // the hostkey's fingerprint against our known hosts.
    auto known_host_ptr = cti::take_pointer_ownership(  libssh2_knownhost_init(m_session_ptr.get()),
                                                        libssh2_knownhost_free);
    if (known_host_ptr == nullptr) {
        throw std::runtime_error("Failure initializing knownhost file");
    }

    // Detect usable SSH directory
    auto sshDir = homeDir + "/.ssh/";

    // Determine if default SSH directory should be overridden (default is ~/.ssh)
    if (auto const ssh_dir = ::getenv(SSH_DIR_ENV_VAR)) {

        if (!cti::dirHasPerms(ssh_dir, R_OK | X_OK)) {
            throw std::runtime_error("Default SSH keyfile directory " + sshDir +
                " was overridden by setting the environment variable " SSH_DIR_ENV_VAR " to " + ssh_dir +
                ", but the directory was not readable / executable. Ensure the directory exists and has "
                "permission code 500.");
        }

        sshDir = ssh_dir;
    }

    // Verify SSH directory permissions
    if (!cti::dirHasPerms(sshDir.c_str(), R_OK | X_OK)) {
        throw std::runtime_error("The SSH keyfile directory at " + sshDir +
            " is not readable / executable. Ensure the directory exists and has permission code 700. "
            "If your system is configured to use a non-default SSH directory, it can be overridden "
            "by setting the environment variable " SSH_DIR_ENV_VAR " to the SSH directory path.");
    }

    // Detect usable knownhosts file
    auto knownHostsPath = sshDir + "/known_hosts";

    // Determine if knownhosts path should be overridden (default is <sshDir>/known_hosts
    if (auto const known_hosts_path = ::getenv(SSH_KNOWNHOSTS_PATH_ENV_VAR)) {

        if (!cti::fileHasPerms(known_hosts_path, R_OK)) {
            throw std::runtime_error("Default SSH known hosts path " + knownHostsPath +
            " was overridden by setting the environment variable "
            SSH_DIR_ENV_VAR " to " + known_hosts_path + ", but the file was not readable. "
            "Ensure the file exists and has permission code 600.");
        }

        knownHostsPath = known_hosts_path;
    }

    // Verify known_hosts permissions
    if (!cti::fileHasPerms(knownHostsPath.c_str(), R_OK)) {
        throw std::runtime_error("The SSH known hosts file at " + knownHostsPath +
            " is not readable. Ensure the file exists and has permission code 600. If your system is "
            "configured to use a non-default SSH known_hosts file, it can be overridden by setting "
            "the environment variable " SSH_KNOWNHOSTS_PATH_ENV_VAR " to the known hosts file path.");
    }

    // Read known_hosts
    rc = libssh2_knownhost_readfile(known_host_ptr.get(), knownHostsPath.c_str(), LIBSSH2_KNOWNHOST_FILE_OPENSSH);
    if (rc < 0) {
        throw std::runtime_error("The SSH known hosts file at " + knownHostsPath +
            " failed to parse correctly. Ensure the file exists and is formatted correctly. If your "
            "system is configured to use a non-default SSH known_hosts file, it can be overridden "
            "by setting the environment variable " SSH_KNOWNHOSTS_PATH_ENV_VAR " to the known hosts file "
            "path.");
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

    // check what authentication methods are available
    auto userauthlist = libssh2_userauth_list(m_session_ptr.get(), username.c_str(), strlen(username.c_str()));

    // Check to see if we can use the passwordless login method, otherwise ensure
    // we can use interactive login
    if ((userauthlist != nullptr) && (::strstr(userauthlist, "publickey") != nullptr)) {
        // Start by trying to use the ssh-agent mechanism
        try {
            SSHAgent agent(m_session_ptr.get(), username);
            agent.auth();
            return;

        } catch (std::exception const& ex) {
            // ignore exceptions from SSHAgent - we fallback on other mechanisms
        }

        // Detect usable public / private key file
        auto tryAuthKeyfilePair = [](LIBSSH2_SESSION* session, std::string const& username, std::string const& defaultPublickeyPath, std::string const& defaultPrivatekeyPath) {

            auto publickeyPath = defaultPublickeyPath;
            auto privatekeyPath = defaultPrivatekeyPath;

            // Determine if public keyfile path should be overridden
            if (auto const pubkey_path = ::getenv(SSH_PUBKEY_PATH_ENV_VAR)) {

                if (!cti::fileHasPerms(pubkey_path, R_OK)) {
                    throw std::runtime_error("Default SSH public key path " + publickeyPath +
                        " was overridden by setting the environment variable "
                        SSH_PUBKEY_PATH_ENV_VAR " to " + pubkey_path +
                        ", but the file was not readable. Ensure the file exists "
                        "and has permission code 644.");
                }

                publickeyPath = pubkey_path;
            }

            // Verify public key exists
            if (!cti::pathExists(publickeyPath.c_str())) {
                return false;
            }

            // Verify public key permissions
            if (!cti::fileHasPerms(publickeyPath.c_str(), R_OK)) {
                throw std::runtime_error("The SSH public key file at " + publickeyPath +
                    " is not readable. Ensure the file exists and has permission code "
                    "644. If your system is configured to use a non-default SSH public "
                    "key file, it can be overridden by setting the environment variable "
                    SSH_PUBKEY_PATH_ENV_VAR " to the public key file path.");
            }

            // Determine if private keyfile path should be overridden
            if (auto const prikey_path = ::getenv(SSH_PRIKEY_PATH_ENV_VAR)) {

                if (!cti::fileHasPerms(prikey_path, R_OK)) {
                    throw std::runtime_error("Default SSH private key path " + privatekeyPath +
                        " was overridden by setting the environment variable "
                        SSH_PRIKEY_PATH_ENV_VAR " to " + prikey_path + ", but the file was not "
                        "readable. Ensure the file exists and has permission code 600.");
                }

                privatekeyPath = prikey_path;
            }

            // Verify private key exists
            if (!cti::pathExists(privatekeyPath.c_str())) {
                return false;
            }

            // Verify private key permissions
            if (!cti::fileHasPerms(privatekeyPath.c_str(), R_OK)) {
                throw std::runtime_error("The SSH private key file at " + privatekeyPath +
                    " is not readable. Ensure the file exists and has permission code "
                    "600. If your system is configured to use a non-default SSH private "
                    "key file, it can be overridden by setting the environment variable "
                    SSH_PRIKEY_PATH_ENV_VAR " to the private key file path.");
            }

            // Read passphrase from environment. If unset, null pointer is interpreted
            // as no passphrase by libssh2_userauth_publickey_fromfile
            auto const ssh_passphrase = ::getenv(SSH_PASSPHRASE_ENV_VAR);

            // Attempt to authenticate using public / private keys
            // Authentication call suffers from spurious timeout
            auto userauth_rc = libssh2_retry([&]() {
                return libssh2_userauth_publickey_fromfile(session,
                    username.c_str(), publickeyPath.c_str(), privatekeyPath.c_str(),
                    ssh_passphrase);
            });

            // Check return code
            if (userauth_rc < 0) {

                throw std::runtime_error("Failed to authenticate using the "
                    "username " + username + ", SSH public key file at " + publickeyPath +
                    " and private key file at " + privatekeyPath + " . If these paths are "
                    "not correct, they can be overridden by setting the environment "
                    "variables " SSH_PUBKEY_PATH_ENV_VAR " and " SSH_PRIKEY_PATH_ENV_VAR
                    " . If a passhrase is required to unlock the keys, it can be provided "
                    "by setting the environment variable " SSH_PASSPHRASE_ENV_VAR " (" +
                    get_libssh2_error(session) + ", " + std::to_string(userauth_rc) + ")");
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

        throw std::runtime_error("Failed to detect SSH key files in " + sshDir +
            " . These paths can be specified by setting the environment variables "
            SSH_PUBKEY_PATH_ENV_VAR " and " SSH_PRIKEY_PATH_ENV_VAR " . If a passhrase "
            "is required to unlock the keys, it can be provided by setting the environment "
            "variable " SSH_PASSPHRASE_ENV_VAR " . CTI requires passwordless (public key) "
            "SSH authentication to compute nodes. If passwordless SSH access to compute nodes "
            "is unavailable, contact your system adminstrator.");
        }
}

SSHSession::SSHSession(SSHSession&& expiring)
    : m_session_sock{std::move(expiring.m_session_sock)}
    , m_session_ptr{std::move(expiring.m_session_ptr)}
    , m_username{expiring.m_username}
    , m_homeDir{expiring.m_homeDir}
{
    expiring.m_homeDir = {};
    expiring.m_username = {};
}

SSHSession& SSHSession::operator=(SSHSession&& expiring)
{
    m_session_sock = std::move(expiring.m_session_sock);
    m_session_ptr = std::move(expiring.m_session_ptr);
    m_username = std::move(expiring.m_username);
    m_homeDir = std::move(expiring.m_homeDir);

    expiring.m_homeDir = {};
    expiring.m_username = {};

    return *this;
}

void SSHSession::executeRemoteCommand(char const* const* args, char const* const* env, bool synchronous)
{
    // sanity
    assert(args != nullptr);
    assert(args[0] != nullptr);

    // Create a new ssh channel
    auto channel_ptr = std::unique_ptr<LIBSSH2_CHANNEL, decltype(&delete_ssh2_channel)>
        {nullptr, delete_ssh2_channel};
    auto open_session_rc = libssh2_retry([&]() {
        channel_ptr.reset(libssh2_channel_open_session(m_session_ptr.get()));
        return (channel_ptr == nullptr) ? LIBSSH2_ERROR_TIMEOUT : 0;
    });
    if (open_session_rc < 0) {
        throw std::runtime_error("Failure opening SSH channel on session: "
            + get_libssh2_error(m_session_ptr.get()));
    }

    // Create the command string
    std::string argvString {args[0]};
    for (const char* const* arg = &args[1]; *arg != nullptr; arg++) {
        argvString.push_back(' ');
        argvString += std::string(*arg);
    }

    // Set remote environment variables
    if (env) {
        for (auto setting = env; *setting != nullptr; setting++) {
            auto&& [var, val] = cti::split::string<2>(*setting, '=');
            if (val.empty()) {
                continue;
            }
            libssh2_channel_setenv_ex(channel_ptr.get(), var.c_str(), var.length(),
                val.c_str(), val.length());
        }
    }

    // Continue command in background after SSH channel disconnects
    if (!synchronous) {
        argvString = "nohup " + argvString + " < /dev/null > /dev/null 2>&1 &";
    } else {
        argvString += "< /dev/null > /dev/null 2>&1";
    }

    // Request execution of the command on the remote host
    auto exec_rc = libssh2_retry([&]() {
        return libssh2_channel_exec(channel_ptr.get(), argvString.c_str());
    });
    if (exec_rc < 0) {
        throw std::runtime_error("Executing remote command failed: "
            + get_libssh2_error(m_session_ptr.get()));
    }

    // Wait for synchronous run to complete
    if (synchronous) {
        waitCloseChannel(std::move(channel_ptr));
    }
}

SSHSession::UniqueChannel SSHSession::startRemoteCommand(const char* const argv[])
{
    // sanity
    assert(argv != nullptr);
    assert(argv[0] != nullptr);

    // Create a new ssh channel
    auto channel_ptr = std::unique_ptr<LIBSSH2_CHANNEL, decltype(&delete_ssh2_channel)>
        {nullptr, delete_ssh2_channel};
    auto open_session_rc = libssh2_retry([&]() {
        channel_ptr.reset(libssh2_channel_open_session(m_session_ptr.get()));
        return (channel_ptr == nullptr) ? LIBSSH2_ERROR_TIMEOUT : 0;
    });
    if (open_session_rc < 0) {
        throw std::runtime_error("Failure opening SSH channel on session: "
            + get_libssh2_error(m_session_ptr.get()));
    }

    auto argvStream = std::stringstream{};

    // Add environment settings used by daemon
    for (auto&& arg : {"CTI_DEBUG", "CTI_LOG_DIR", "PATH", "LD_LIBRARY_PATH"}) {
        if (auto val = ::getenv(arg)) {
            argvStream << arg << "=" << val << " ";
        }
    }

    // Create the command string
    for (auto arg = argv; *arg != nullptr; arg++) {
        argvStream << *arg << " ";
    }
    auto argvString = argvStream.str();

    // Request execution of the command on the remote host
    auto exec_rc = libssh2_retry([&]() {
        return libssh2_channel_exec(channel_ptr.get(), argvString.c_str());
    });
    if (exec_rc < 0) {
        throw std::runtime_error("Executing remote command failed: "
            + get_libssh2_error(m_session_ptr.get()));
    }

    return channel_ptr;
}

void SSHSession::waitCloseChannel(UniqueChannel&& channel)
{
    auto close_rc = libssh2_channel_close(channel.get());
    while (close_rc == LIBSSH2_ERROR_EAGAIN) {
        remote::channel_wait(m_session_ptr.get(), m_session_sock.fd());
    }
}

void SSHSession::sendRemoteFile(const char* source_path, const char* destination_path, int mode)
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
        throw std::runtime_error("Failure to scp send on session: "
            + get_libssh2_error(m_session_ptr.get()));
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
                    throw std::runtime_error("Error writing to remote file: "
                        + get_libssh2_error(m_session_ptr.get()));
                }
                // rc indicates how many bytes were written this time
                ptr += rc;
                bytes_read -= rc;
            } while(bytes_read);
        }
    }
}

// Remote Daemon implementation

// Reader / writer functions will read / write data from and to SSH channel
static auto channel_reader(LIBSSH2_CHANNEL* channel)
{
    return [channel](char* buf, ssize_t capacity) {
        auto rc = remote::channel_read(channel, buf, capacity);
        return rc;
    };
}
static auto channel_writer(LIBSSH2_CHANNEL* channel)
{
    return [channel](char const* buf, ssize_t capacity) {
        return remote::channel_write(channel, buf, capacity);
    };
}

FE_daemon::MPIRResult SSHSession::attachMPIR(std::string const& daemonPath, std::string const& launcherName,
    pid_t launcher_pid)
{
    // Construct FE remote daemon arguments
    auto daemonArgv = cti::OutgoingArgv<CTIFEDaemonArgv>{daemonPath};
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

    // Write MPIR attach request to channel
    writeLoop(channel_writer, FE_daemon::ReqType::AttachMPIR);
    writeLoop(channel_writer, launcherName.c_str(), launcherName.length() + 1);
    writeLoop(channel_writer, launcher_pid);

    // Read MPIR attach request from relay pipe
    auto mpirResult = FE_daemon::readMPIRResp(channel_reader);

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

std::tuple<SSHSession::UniqueChannel, pid_t> SSHSession::startRemoteDaemon(std::string const& daemonPath)
{
    // Construct FE remote daemon arguments
    auto daemonArgv = cti::OutgoingArgv<CTIFEDaemonArgv>{daemonPath};
    daemonArgv.add(CTIFEDaemonArgv::ReadFD,  std::to_string(STDIN_FILENO));
    daemonArgv.add(CTIFEDaemonArgv::WriteFD, std::to_string(STDOUT_FILENO));

    // Launch FE daemon remotely to collect MPIR information
    auto channel = startRemoteCommand(daemonArgv.get());

    // Read FE daemon initialization message
    auto daemon_pid = readLoop<pid_t>(channel_reader(channel.get()));

    return std::make_tuple(std::move(channel), daemon_pid);
}

FE_daemon::MPIRResult SSHSession::launchMPIR(LIBSSH2_CHANNEL* channel, char const* const* launcher_argv, char const* const* env)
{
    // Write MPIR launch request to channel
    writeLoop(channel_writer(channel), FE_daemon::ReqType::LaunchMPIR);

    // Launcher binary or name
    writeLoop(channel_writer(channel), launcher_argv[0], strlen(launcher_argv[0]) + 1);

    // Launcher argc
    size_t launcher_argc = 0;
    { for (auto arg = launcher_argv; *arg != nullptr; arg++) { launcher_argc++; };
        auto launcherArgcStr = std::to_string(launcher_argc);
        writeLoop(channel_writer(channel), launcherArgcStr.c_str(), launcherArgcStr.length() + 1);
    }

    // Launcher argv
    for (size_t i = 0; i < launcher_argc; i++) {
        writeLoop(channel_writer(channel), launcher_argv[i], ::strlen(launcher_argv[i]) + 1);
    }

    // Environment settings
    if (env) {

        // Environment count
        size_t envc = 0;
        for (auto var = env; *var != nullptr; var++) { envc++; }
        auto envcStr = std::to_string(envc);
        writeLoop(channel_writer(channel), envcStr.c_str(), envcStr.length() + 1);

        // Environment
        for (size_t i = 0; i < envc; i++) {
            writeLoop(channel_writer(channel), env[i], strlen(env[i]) + 1);
        }

    } else {
        writeLoop(channel_writer(channel), "0", 2);
    }

    // Read MPIR attach request from relay pipe
    auto mpirResult = FE_daemon::readMPIRResp(channel_reader(channel));

    return mpirResult;
}

std::string SSHSession::readStringMPIR(LIBSSH2_CHANNEL* channel, FE_daemon::DaemonAppId mpir_id, std::string const& var)
{
    writeLoop(channel_writer(channel), FE_daemon::ReqType::ReadStringMPIR);
    writeLoop(channel_writer(channel), mpir_id);
    writeLoop(channel_writer(channel), var.c_str(), var.length() + 1);

     // read basic response information
    auto stringResp = readLoop<FE_daemon::StringResp>(channel_reader(channel));
    if (stringResp.type != FE_daemon::RespType::String) {
        throw std::runtime_error("daemon did not send expected String response type");
    } else if (stringResp.success == false) {
        throw std::runtime_error("daemon failed to read string from memory");
    }

    // Read string response
    auto result = std::string{};
    while (auto c = readLoop<char>(channel_reader(channel))) {
        result.push_back(c);
    }

    return result;
}

void SSHSession::releaseMPIR(LIBSSH2_CHANNEL* channel, FE_daemon::DaemonAppId mpir_id)
{
    writeLoop(channel_writer(channel), FE_daemon::ReqType::ReleaseMPIR);
    writeLoop(channel_writer(channel), mpir_id);
    auto okResp = readLoop<FE_daemon::OKResp>(channel_reader(channel));
    if (okResp.type != FE_daemon::RespType::OK) {
        throw std::runtime_error("warning: remote daemon failed to release from barrier");
    }
}

bool SSHSession::waitMPIR(LIBSSH2_CHANNEL* channel, FE_daemon::DaemonAppId mpir_id)
{
    writeLoop(channel_writer(channel), FE_daemon::ReqType::WaitMPIR);
    writeLoop(channel_writer(channel), mpir_id);
    auto okResp = readLoop<FE_daemon::OKResp>(channel_reader(channel));
    return (okResp.type == FE_daemon::RespType::OK);
}

bool SSHSession::checkApp(LIBSSH2_CHANNEL* channel, FE_daemon::DaemonAppId mpir_id)
{
    writeLoop(channel_writer(channel), FE_daemon::ReqType::CheckApp);
    writeLoop(channel_writer(channel), mpir_id);
    auto okResp = readLoop<FE_daemon::OKResp>(channel_reader(channel));
    return (okResp.type == FE_daemon::RespType::OK);
}

void SSHSession::deregisterApp(LIBSSH2_CHANNEL* channel, FE_daemon::DaemonAppId mpir_id)
{
    writeLoop(channel_writer(channel), FE_daemon::ReqType::DeregisterApp);
    writeLoop(channel_writer(channel), mpir_id);
    auto okResp = readLoop<FE_daemon::OKResp>(channel_reader(channel));
    if (okResp.type != FE_daemon::RespType::OK) {
        throw std::runtime_error("warning: remote daemon failed to deregister application");
    }
}

void SSHSession::stopRemoteDaemon(SSHSession::UniqueChannel&& channel, pid_t daemon_pid)
{
    // Shut down remote daemon
    writeLoop(channel_writer(channel.get()), FE_daemon::ReqType::Shutdown);
    auto okResp = readLoop<FE_daemon::OKResp>(channel_reader(channel.get()));
    if (okResp.type != FE_daemon::RespType::OK) {
        throw std::runtime_error("remote daemon shutdown failed (has PID "
            + std::to_string(daemon_pid) + ")");
    }

    waitCloseChannel(std::move(channel));
}

RemoteDaemon::RemoteDaemon(SSHSession&& session, std::string const& daemonPath)
    : m_session{std::move(session)}
    , m_channel{nullptr, delete_ssh2_channel}
    , m_daemonPid{-1}
{
    std::tie(m_channel, m_daemonPid) = m_session.startRemoteDaemon(daemonPath);
}

RemoteDaemon::RemoteDaemon(RemoteDaemon&& expiring)
    : m_session{std::move(expiring.m_session)}
    , m_channel{std::move(expiring.m_channel)}
    , m_daemonPid{expiring.m_daemonPid}
{
    expiring.m_daemonPid = -1;
}

RemoteDaemon::~RemoteDaemon()
{
    if (m_daemonPid > 0) {
        try {
            m_session.stopRemoteDaemon(std::move(m_channel), m_daemonPid);
        } catch (std::exception const& ex) {
            fprintf(stderr, "warning: %s\n", ex.what());
        }
    }

    // Close relay pipe and SSH channel
    m_channel.reset();
}

FE_daemon::MPIRResult RemoteDaemon::launchMPIR(char const* const* launcher_argv, char const* const* env)
{
    return m_session.launchMPIR(m_channel.get(), launcher_argv, env);
}

std::string RemoteDaemon::readStringMPIR(FE_daemon::DaemonAppId mpir_id, std::string const& var)
{
    return m_session.readStringMPIR(m_channel.get(), mpir_id, var);
}

void RemoteDaemon::releaseMPIR(FE_daemon::DaemonAppId mpir_id)
{
    m_session.releaseMPIR(m_channel.get(), mpir_id);
}

bool RemoteDaemon::checkApp(FE_daemon::DaemonAppId mpir_id)
{
    return m_session.checkApp(m_channel.get(), mpir_id);
}

void RemoteDaemon::deregisterApp(FE_daemon::DaemonAppId mpir_id)
{
    m_session.deregisterApp(m_channel.get(), mpir_id);
}
