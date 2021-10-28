/******************************************************************************\
 * Copyright 2021 Hewlett Packard Enterprise Development LP.
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

#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <libssh2.h>

#include <vector>

// Custom deleters
static void delete_ssh2_session(LIBSSH2_SESSION *pSession)
{
    libssh2_session_disconnect(pSession, "Error occured");
    libssh2_session_free(pSession);
}

static void delete_ssh2_channel(LIBSSH2_CHANNEL *pChannel)
{
    libssh2_channel_close(pChannel);
    libssh2_channel_free(pChannel);
}

// SSH channel data read / write
namespace remote
{

// Read data of a specified length into the provided buffer
static inline void readLoop(LIBSSH2_CHANNEL* channel, char* buf, int capacity)
{
    int offset = 0;
    while (offset < capacity) {
        if (auto const bytes_read = libssh2_channel_read(channel, buf + offset, capacity - offset)) {
            if (bytes_read < 0) {
                if (bytes_read == LIBSSH2_ERROR_EAGAIN) {
                    continue;
                }
                throw std::runtime_error("read failed: " + std::string{std::strerror(bytes_read)});
            }
            offset += bytes_read;
        }
    }
}

// Read and return a known data type
template <typename T>
static inline T rawReadLoop(LIBSSH2_CHANNEL* channel)
{
    static_assert(std::is_trivial<T>::value);
    T result;
    readLoop(channel, reinterpret_cast<char*>(&result), sizeof(T));
    return result;
}

// Write data of a specified length from the provided buffer
static inline void writeLoop(LIBSSH2_CHANNEL* channel, char const* buf, int capacity)
{
    int offset = 0;
    while (offset < capacity) {
        if (auto const bytes_written = libssh2_channel_write(channel, buf + offset, capacity - offset)) {
            if (bytes_written < 0) {
                if (bytes_written == LIBSSH2_ERROR_EAGAIN) {
                    continue;
                }
                throw std::runtime_error("write failed: " + std::string{std::strerror(bytes_written)});
            }
            offset += bytes_written;
        }
    }
}

// Write a trivially-copyable object
template <typename T>
static inline void rawWriteLoop(LIBSSH2_CHANNEL* channel, T const& obj)
{
    static_assert(std::is_trivially_copyable<T>::value);
    writeLoop(channel, reinterpret_cast<char const*>(&obj), sizeof(T));
}

// Relay data received over SSH to a provided file descriptor
static void relay_task(LIBSSH2_CHANNEL* channel, int fd)
{
    char buf[4096];
    auto const capacity = sizeof(buf);
    while (auto const bytes_read = libssh2_channel_read(channel, buf, capacity)) {
        if (bytes_read < 0) {
            if (bytes_read == LIBSSH2_ERROR_EAGAIN) {
                continue;
            } else {
                break;
            }
        }
        if (::write(fd, buf, bytes_read) < 0) {
            break;
        }
    }

    ::close(fd);
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
     * executeRemoteCommand - Execute a command on a remote host through an ssh session
     *
     * Detail
     *      Executes a command with the specified arguments and environment on the remote host
     *      connected by the specified session.
     *
     * Arguments
     *      args -          null-terminated cstring array which holds the arguments array for the command to be executed
     */
    void executeRemoteCommand(const char* const args[])
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

        // Create the command string
        std::string argvString {args[0]};
        for (const char* const* arg = &args[1]; *arg != nullptr; arg++) {
            argvString.push_back(' ');
            argvString += std::string(*arg);
        }

        // Continue command in background after SSH channel disconnects
        argvString = "nohup " + argvString + " < /dev/null > /dev/null 2>&1 &";

        // Request execution of the command on the remote host
        int rc = libssh2_channel_exec(channel_ptr.get(), argvString.c_str());
        if ((rc < 0) && (rc != LIBSSH2_ERROR_EAGAIN)) {
            throw std::runtime_error("Execution of ssh command failed: " + std::to_string(rc));
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

        return std::move(channel_ptr);
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
            throw std::runtime_error("Failure to scp send to " + std::string{destination_path} + " on session: " + getError());
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
};
