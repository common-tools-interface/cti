/******************************************************************************\
 * Frontend.cpp -  Frontend library functions for SSH based workload manager.
 *
 * Copyright 2017-2019 Cray Inc.  All Rights Reserved.
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

#include <netdb.h>

#include <unordered_map>

// Pull in manifest to properly define all the forward declarations
#include "cti_transfer/Manifest.hpp"

#include "GenericSSH/Frontend.hpp"

#include "useful/cti_useful.h"
#include "useful/cti_dlopen.hpp"
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
    libssh2_channel_close(pChannel);
    libssh2_channel_free(pChannel);
}

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
        auto host_ptr = cti::move_pointer_ownership(std::move(host),freeaddrinfo);

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
        m_session_ptr = cti::move_pointer_ownership(libssh2_session_init(), delete_ssh2_session);
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
        auto known_host_ptr = cti::move_pointer_ownership(  libssh2_knownhost_init(m_session_ptr.get()),
                                                            libssh2_knownhost_free);
        if (known_host_ptr == nullptr) {
            throw std::runtime_error("Failure initializing knownhost file");
        }

        // read all hosts from here
        std::string const known_hosts_path{std::string{m_pwd.pw_dir} + "/.ssh/known_hosts"};
        rc = libssh2_knownhost_readfile(known_host_ptr.get(), known_hosts_path.c_str(), LIBSSH2_KNOWNHOST_FILE_OPENSSH);
        if (rc < 0) {
            throw std::runtime_error("Failure reading knownhost file");
        }

        // obtain the session hostkey fingerprint
        size_t len;
        int type;
        const char *fingerprint = libssh2_session_hostkey(m_session_ptr.get(), &len, &type);
        if (fingerprint == nullptr) {
            throw std::runtime_error("Failed to obtain the remote hostkey");
        }

        // Check the remote hostkey against the knownhosts
        int keymask = (type == LIBSSH2_HOSTKEY_TYPE_RSA) ? LIBSSH2_KNOWNHOST_KEY_SSHRSA:LIBSSH2_KNOWNHOST_KEY_SSHDSS;
        struct libssh2_knownhost *kh;
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
                break;
            case LIBSSH2_KNOWNHOST_CHECK_MISMATCH:
                throw std::runtime_error("Remote hostkey mismatch with knownhosts file! Remote the host from knownhosts to resolve: " + hostname);
            case LIBSSH2_KNOWNHOST_CHECK_FAILURE:
            default:
                throw std::runtime_error("Failure with libssh2 knownhost check");
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
            auto keyFileAuth = [&](std::string const& pubKeyPath, std::string const& priKeyPath) {
                if (cti::pathExists(pubKeyPath.c_str()) && cti::pathExists(priKeyPath.c_str())) {
                    // Try authenticating with an empty passphrase
                    int rc = LIBSSH2_ERROR_EAGAIN;
                    while (rc == LIBSSH2_ERROR_EAGAIN) {
                        rc = libssh2_userauth_publickey_fromfile(m_session_ptr.get(),
                                                                 username.c_str(),
                                                                 pubKeyPath.c_str(),
                                                                 priKeyPath.c_str(),
                                                                 nullptr);
                    }
                    return rc;
                } else {
                    return -1;
                }
            };
            int rc;
            rc = keyFileAuth(std::string{m_pwd.pw_dir} + "/.ssh/id_rsa.pub",
                std::string{m_pwd.pw_dir} + "/.ssh/id_rsa");
            if (!rc) {
                return;
            }
            rc = keyFileAuth(std::string{m_pwd.pw_dir} + "/.ssh/id_dsa.pub",
                std::string{m_pwd.pw_dir} + "/.ssh/id_dsa");
            if (!rc) {
                return;
            }
        }
        // TODO: Any other valid authentication mechanisms? We don't want to use keyboard interactive
        //       from a library.
        throw std::runtime_error("No supported ssh authentication mechanism found. CTI requires passwordless (public key) SSH authentication to the compute nodes. Contact your system adminstrator.");
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
        auto channel_ptr = cti::move_pointer_ownership( libssh2_channel_open_session(m_session_ptr.get()),
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
        auto channel_ptr = cti::move_pointer_ownership( libssh2_scp_send(   m_session_ptr.get(),
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
};

GenericSSHApp::GenericSSHApp(GenericSSHFrontend& fe, pid_t launcherPid, std::unique_ptr<MPIRInstance>&& launcherInstance)
    : App(fe)
    , m_launcherPid { fe.Daemon().request_RegisterApp(launcherPid) }
    , m_launcherInstance { std::move(launcherInstance) }
    , m_stepLayout  { fe.fetchStepLayout(m_launcherInstance->getProctable()) }
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

    // If an active MPIR session was provided, extract the MPIR ProcTable and write the PID List File.
    if (m_launcherInstance) {
        m_extraFiles.push_back(fe.createPIDListFile(m_launcherInstance->getProctable(), m_stagePath));
    }
}

GenericSSHApp::~GenericSSHApp()
{
    // Delete the staging directory if it exists.
    if (!m_stagePath.empty()) {
        _cti_removeDirectory(m_stagePath.c_str());
    }

    // Inform the FE daemon that this App is going away
    m_frontend.Daemon().request_DeregisterApp(m_launcherPid);
}

/* app instance creation */

GenericSSHApp::GenericSSHApp(GenericSSHFrontend& fe, pid_t launcherPid)
    : GenericSSHApp
        { fe
        , launcherPid

        // MPIR attach to launcher
        , std::make_unique<MPIRInstance>(

            // Get path to launcher binary
            cti::move_pointer_ownership(
                _cti_pathFind(GenericSSHFrontend::getLauncherName().c_str(), nullptr),
                std::free).get(),

            // Attach to existing launcherPid
            launcherPid)
        }
{}

GenericSSHApp::GenericSSHApp(GenericSSHFrontend& fe, std::unique_ptr<MPIRInstance>&& launcherInstance)
    : GenericSSHApp
        { fe
        , launcherInstance->getLauncherPid()
        , std::move(launcherInstance)
        }
{}

GenericSSHApp::GenericSSHApp(GenericSSHFrontend& fe, const char * const launcher_argv[], int stdout_fd, int stderr_fd,
    const char *inputFile, const char *chdirPath, const char * const env_list[])
    : GenericSSHApp
        { fe
        , fe.launchApp(launcher_argv, stdout_fd, stderr_fd, inputFile, chdirPath, env_list)
        }
{}

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

std::vector<std::string>
GenericSSHApp::getHostnameList() const
{
    std::vector<std::string> result;
    // extract hostnames from each NodeLayout
    std::transform(m_stepLayout.nodes.begin(), m_stepLayout.nodes.end(), std::back_inserter(result),
        [](GenericSSHFrontend::NodeLayout const& node) { return node.hostname; });
    return result;
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
    // release MPIR barrier if applicable
    if (!m_launcherInstance) {
        throw std::runtime_error("app not under MPIR control");
    }
    m_launcherInstance.reset();
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
        SSHSession(node.hostname, m_frontend.getPwd()).executeRemoteCommand(killArgv.get());
    }
}

void
GenericSSHApp::shipPackage(std::string const& tarPath) const
{
    if (auto packageName = cti::move_pointer_ownership(_cti_pathToName(tarPath.c_str()), std::free)) {
        auto const destination = std::string{std::string{SSH_TOOL_DIR} + "/" + packageName.get()};
        writeLog("GenericSSH shipping %s to '%s'\n", tarPath.c_str(), destination.c_str());

        // Send the package to each of the hosts using SCP
        for (auto&& node : m_stepLayout.nodes) {
            SSHSession(node.hostname, m_frontend.getPwd()).sendRemoteFile(tarPath.c_str(), destination.c_str(), S_IRWXU | S_IRWXG | S_IRWXO);
        }
    } else {
        throw std::runtime_error("_cti_pathToName failed");
    }
}

void
GenericSSHApp::startDaemon(const char* const args[])
{
    // sanity check
    if (args == nullptr) {
        throw std::runtime_error("args array is empty!");
    }

    // Transfer the backend daemon to the backends if it has not yet been transferred
    if (!m_beDaemonSent) {
        // Get the location of the backend daemon
        if (m_frontend.getBEDaemonPath().empty()) {
            throw std::runtime_error("Required environment variable not set:" + std::string(BASE_DIR_ENV_VAR));
        }
        shipPackage(m_frontend.getBEDaemonPath());
        // set transfer to true
        m_beDaemonSent = true;
    }

    // Use location of existing launcher binary on compute node
    std::string const launcherPath{m_toolPath + "/" + CTI_BE_DAEMON_BINARY};

    // Prepare the launcher arguments
    cti::ManagedArgv launcherArgv { launcherPath };
    for (const char* const* arg = args; *arg != nullptr; arg++) {
        launcherArgv.add(*arg);
    }

    // TODO for PE-26002: use values in CRAY_DBG_LOG_DIR, CRAY_CTI_DBG to add daemon debug arguments

    // Execute the launcher on each of the hosts using SSH
    for (auto&& node : m_stepLayout.nodes) {
        SSHSession(node.hostname, m_frontend.getPwd()).executeRemoteCommand(launcherArgv.get());
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
GenericSSHFrontend::launchBarrier(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
        CStr inputFile, CStr chdirPath, CArgArray env_list)
{
    auto ret = m_apps.emplace(std::make_shared<GenericSSHApp>(  *this,
                                                                launcher_argv,
                                                                stdout_fd,
                                                                stderr_fd,
                                                                inputFile,
                                                                chdirPath,
                                                                env_list));
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

    auto ret = m_apps.emplace(std::make_shared<GenericSSHApp>(*this, launcherPid));
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
    auto getenvOrDefault = [](char const* envVar, char const* defaultValue) {
        if (char const* envValue = getenv(envVar)) {
            return envValue;
        }
        return defaultValue;
    };

    // Cache the launcher name result.
    auto static launcherName = std::string{getenvOrDefault(CTI_LAUNCHER_NAME, SRUN)};
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
        size_t nid;
        auto const hostNidPair = hostNidMap.find(proc.hostname);
        if (hostNidPair == hostNidMap.end()) {
            // New host, extend nodes array, and fill in host entry information
            nid = nodeCount++;
            layout.nodes.push_back(NodeLayout
                { .hostname = proc.hostname
                , .pids = {}
                , .firstPE = peCount
            });
            hostNidMap[proc.hostname] = nid;
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
    auto const pidPath = std::string{stagePath + "/" + SLURM_PID_FILE};
    if (auto const pidFile = cti::file::open(pidPath, "wb")) {

        // Write the PID List header.
        cti::file::writeT(pidFile.get(), slurmPidFileHeader_t
            { .numPids = (int)procTable.size()
        });

        // Write a PID entry using information from each MPIR ProcTable entry.
        for (auto&& elem : procTable) {
            cti::file::writeT(pidFile.get(), slurmPidFile_t
                { .pid = elem.pid
            });
        }

        return pidPath;
    } else {
        throw std::runtime_error("failed to open PID file path " + pidPath);
    }
}

bool
GenericSSHFrontend::isSupported()
{
    // Could attempt to SSH to compute node here

    return true;
}

std::unique_ptr<MPIRInstance>
GenericSSHFrontend::launchApp(const char * const launcher_argv[],
        int stdout_fd, int stderr_fd, const char *inputFile, const char *chdirPath, const char * const env_list[])
{
    // Open input file (or /dev/null to avoid stdin contention).
    auto openFileOrDevNull = [&](char const* inputFile) {
        int input_fd = -1;
        if (inputFile == nullptr) {
            inputFile = "/dev/null";
        }
        errno = 0;
        input_fd = open(inputFile, O_RDONLY);
        if (input_fd < 0) {
            throw std::runtime_error("Failed to open input file " + std::string(inputFile) +": " + std::string(strerror(errno)));
        }

        return input_fd;
    };

    // Get the launcher path from CTI environment variable / default.
    if (auto const launcher_path = cti::move_pointer_ownership(_cti_pathFind(GenericSSHFrontend::getLauncherName().c_str(), nullptr), std::free)) {

        /* construct argv array & instance*/
        std::vector<std::string> launcherArgv{launcher_path.get()};
        for (const char* const* arg = launcher_argv; *arg != nullptr; arg++) {
            launcherArgv.emplace_back(*arg);
        }

        /* env_list null-terminated strings in format <var>=<val>*/
        std::vector<std::string> envVars;
        if (env_list != nullptr) {
            for (const char* const* arg = env_list; *arg != nullptr; arg++) {
                envVars.emplace_back(*arg);
            }
        }

        // redirect stdout / stderr to /dev/null; use sattach to redirect the output instead
        // note: when using SRUN as launcher, this output redirection doesn't work.
        // see CraySLURM's implementation (need to use SATTACH after launch)
        std::map<int, int> remapFds {
            { openFileOrDevNull(inputFile), STDIN_FILENO }
        };
        if (stdout_fd >= 0) { remapFds[stdout_fd] = STDOUT_FILENO; }
        if (stderr_fd >= 0) { remapFds[stderr_fd] = STDERR_FILENO; }

        // Launch program under MPIR control.
        return std::make_unique<MPIRInstance>(launcher_path.get(), launcherArgv, envVars, remapFds);
    } else {
        throw std::runtime_error("Failed to find launcher in path: " + GenericSSHFrontend::getLauncherName());
    }
}
