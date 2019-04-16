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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

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

#include <unordered_map>

#include "cti_defs.h"
#include "cti_fe_iface.h"

#include "GenericSSH/Frontend.hpp"

#include "useful/cti_useful.h"
#include "useful/cti_dlopen.hpp"
#include "useful/cti_argv.hpp"
#include "useful/cti_wrappers.hpp"

#include <stdbool.h>
#include <stdlib.h>
#include <libssh2.h>

class SSHSession {
private: // Custom deleters
	void delete_ssh2_session(LIBSSH2_SESSION *pSession)
	{
		libssh2_session_disconnect(pSession, "Error occured");
		libssh2_session_free(pSession);
	}

private: // Data members
	std::unique_ptr<LIBSSH2_SESSION,decltype(&delete_ssh2_session)> m_session_ptr;
	cti::fd_handle m_session_sock;

private: // utility methods
	std::string const getError()
	{
		char *errmsg;
		libssh2_session_last_error(m_session_ptr.get(), &errmsg, nullptr, 0);
		if (errmsg != nullptr) return std::string{errmsg};
		return std::string{"Unknown libssh2 error."};
	}

public: // Constructor/destructor
	/*
	 * SSHSession constructor - start and authenticate an ssh session with a remote host
	 *
	 * detail
	 *		starts an ssh session with hostname, verifies the identity of the remote host,
	 *		and authenticates the user using the public key method. this is the only supported
	 *		ssh authentication method.
	 *
	 * arguments
	 *		hostname - hostname of remote host to which to connect
	 *
	 */
	SSHSession(std::string const& hostname)
	: m_session_ptr{nullptr,&delete_ssh2_session}
	{
		int rc;
		struct addrinfo hints = {};
		// Setup the hints structure
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_flags = AI_NUMERICSERV;

		// FIXME: This should be using a service name instead of hardcoded port number.
		// 		  Adjust ai_flags above for this fix.
		// FIXME: How to handle containers with non-default SSH port numbers?
		struct addrinfo *host;
		if ((rc = getaddrinfo(hostname.c_str(), "22", &hints, &host)) != 0) {
			throw std::runtime_error("getaddrinfo failed: " + std::string{gai_strerror(rc)});
		}
		// Take ownership of the host addrinfo into the unique_ptr.
		// This will enforce cleanup.
		std::unique_ptr<struct addrinfo,decltype(&freeaddrinfo)> host_ptr{host,&freeaddrinfo};
		host = nullptr;

		// create the ssh socket
		m_session_sock = cti::fd_handle{ socket(	host_ptr.get()->ai_family,
													host_ptr.get()->ai_socktype,
													host_ptr.get()->ai_protocol)
		};

		// Connect the socket
		if (connect(m_session_sock.fd(), host_ptr.get()->ai_addr, host_ptr.get()->ai_addrlen)) {
			throw std::runtime_error("failed to connect to host " + hostname);
		}

		// Init a new libssh2 session.
		if (m_session_ptr = move_pointer_ownership(libssh2_session_init(),&delete_ssh2_session)) {
			throw std::runtime_error("libssh2_session_init() failed");
		}

		// Start up the new session.
		// This will trade welcome banners, exchange keys, and setup crypto,
		// compression, and MAC layers.
		if (libssh2_session_handshake(m_session_ptr.get(), m_session_sock.fd())) {
			throw std::runtime_error("Failure establishing SSH session: " + getError());
		}

		// At this point we havn't authenticated. The first thing to do is check
		// the hostkey's fingerprint against our known hosts.
		std::unique_ptr<LIBSSH2_KNOWNHOSTS,decltype(&libssh2_knownhost_free)> known_host_ptr;
		if (known_host_ptr = move_pointer_ownership(libssh2_knownhost_init(m_session_ptr.get()),
													&libssh2_knownhost_free)) {
			throw std::runtime_error("Failure initializing knownhost file");
		}

		// read all hosts from here
		rc = libssh2_knownhost_readfile(known_host_ptr.get(), "known_hosts", LIBSSH2_KNOWNHOST_FILE_OPENSSH);
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
		int keymask = (type == LIBSSH@_HOSTKEY_TYPE_RSA) ? LIBSSH2_KNOWNHOST_KEY_SSHRSA:LIBSSH2_KNOWNHOST_KEY_SSHDSS;
		struct libssh2_knownhost *kh;
		int check = libssh2_knownhost_checkp(	known_host_ptr.get(),
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
				if (libssh2_knownhost_addc(	known_host_ptr.get(),
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

		// check what authentication methods are available
		char *userauthlist = libssh2_userauth_list(m_session_ptr.get(), username, strlen(username));

		} catch (...) {
			// Cleanup and re-throw
			if (nh != nullptr) libssh2_knownhost_free(nh);
			if (m_session_ptr != nullptr) {
				libssh2_session_disconnect(m_session_ptr, "Error occured");
				libssh2_session_free(m_session_ptr);
			}
			if (m_session_sock != -1) close(m_session_sock);
			throw;
		}


		_cti_getLibSSH().ssh_options_set(session.get(), SSH_OPTIONS_HOST, hostname.c_str());

		// connect to remote host
		int rc = _cti_getLibSSH().ssh_connect(session.get());
		if (rc != SSH_OK) {
			throw std::runtime_error("ssh connection error: " + getError());
		}

		// verify the identity of the remote host
		if (!_cti_ssh_server_valid(session.get())) {
			_cti_getLibSSH().ssh_disconnect(session.get());
			throw std::runtime_error("could not verify backend node identity: " + getError());
		}

		// authenticate user with the remote host using public key authentication
		rc = _cti_getLibSSH().ssh_userauth_publickey_auto(session.get(), nullptr, nullptr);
		switch (rc) {
			case SSH_AUTH_PARTIAL:
			case SSH_AUTH_DENIED:
			case SSH_AUTH_ERROR:
				_cti_getLibSSH().ssh_disconnect(session.get());
				throw std::runtime_error("Authentication failed: " + getError() + ". CTI requires paswordless (public key) SSH authentication to the backends. Contact your system administrator about setting this up.");
		}
	}

	~SSHSession() {
		if ( session_ptr != nullptr ) {

		}
	}

	/*
	 * _cti_ssh_execute_remote_command - Execute a command on a remote host through an open ssh session
	 *
	 * Detail
	 *		Executes a command with the specified arguments and environment on the remote host
	 *		connected by the specified session.
	 *
	 * Arguments
	 *		args - 			null-terminated cstring array which holds the arguments array for the command to be executed
	 *		environment - 	A list of environment variables to forward to the backend while executing
	 *						the command or NULL to forward no environment variables
	 */
	void executeRemoteCommand(const char* const args[], const char* const environment[]) {
		// Start a new ssh channel
		UniquePtrDestr<std::remove_pointer<ssh_channel>::type> channel(_cti_getLibSSH().ssh_channel_new(session.get()), _cti_getLibSSH().ssh_channel_free);
		if (channel == nullptr) {
			throw std::runtime_error("Error allocating ssh channel: " + getError());
		}

		// open session on channel
		if (_cti_getLibSSH().ssh_channel_open_session(channel.get()) != SSH_OK) {
			throw std::runtime_error("Error opening session on ssh channel: " + getError());
		}

		// Forward environment variables before execution. May not be supported on
		// all systems if user environments are disabled by the ssh server
		if (environment != nullptr) {
			for (const char* const* var = environment; *var != nullptr; var++) {
				if (const char* val = getenv(*var)) {
					_cti_getLibSSH().ssh_channel_request_env(channel.get(), *var, val);
				}
			}
		}

		// Request execution of the command on the remote host
		std::string argvString;
		for (const char* const* arg = args; *arg != nullptr; arg++) {
			argvString.push_back(' ');
			argvString += std::string(*arg);
		}
		if (_cti_getLibSSH().ssh_channel_request_exec(channel.get(), argvString.c_str()) != SSH_OK) {
			throw std::runtime_error("Execution of ssh command failed: " + getError());
			_cti_getLibSSH().ssh_channel_close(channel.get());
		}

		// End the channel
		_cti_getLibSSH().ssh_channel_send_eof(channel.get());
		_cti_getLibSSH().ssh_channel_close(channel.get());
	}

	/*
	 * _cti_ssh_copy_file_to_remote - Send a file to a remote host on an open ssh session
	 *
	 * Detail
	 *		Sends the file specified by source_path to the remote host connected on session
	 *		at the location destination_path on the remote host with permissions specified by
	 *		mode.
	 *
	 * Arguments
	 *		source_path - A C-string specifying the path to the file to ship
	 *		destination_path- A C-string specifying the path of the destination on the remote host
	 *		mode- POSIX mode for specifying permissions of new file on remote host
	 */
	void sendRemoteFile(const char* source_path, const char* destination_path, int mode) {
		// Start a new scp session
		UniquePtrDestr<std::remove_pointer<ssh_scp>::type> scp(
			_cti_getLibSSH().ssh_scp_new(session.get(), SSH_SCP_WRITE, _cti_pathToDir(destination_path)),
				_cti_getLibSSH().ssh_scp_free);

		if (scp == nullptr) {
			throw std::runtime_error("Error allocating scp session: " + getError());
		}


		// initialize scp session
		if (_cti_getLibSSH().ssh_scp_init(scp.get()) != SSH_OK) {
			throw std::runtime_error("Error initializing scp session: " + getError());
		}

		//Get the length of the source file
		struct stat stbuf;
		{ int fd = open(source_path, O_RDONLY);
			if (fd < 0) {
				_cti_getLibSSH().ssh_scp_close(scp.get());
				throw std::runtime_error("Could not open source file for shipping to the backends");
			}

			if ((fstat(fd, &stbuf) != 0) || (!S_ISREG(stbuf.st_mode))) {
				close(fd);
				_cti_getLibSSH().ssh_scp_close(scp.get());
				throw std::runtime_error("Could not fstat source file for shipping to the backends");
			}

			close(fd);
		}

		size_t file_size = stbuf.st_size;
		std::string const relative_destination("/" + std::string(_cti_pathToName(destination_path)));

		// Create an empty file with the correct length on the remote host
		if (_cti_getLibSSH().ssh_scp_push_file(scp.get(), relative_destination.c_str(), file_size, mode) != SSH_OK) {
			_cti_getLibSSH().ssh_scp_close(scp.get());
			throw std::runtime_error("Can't open remote file: " + getError());
		}

		// Write the contents of the source file to the destination file in blocks
		size_t const BLOCK_SIZE = 1024;
		if (auto source_file = UniquePtrDestr<FILE>(fopen(source_path, "rb"), ::fclose)) {

			// read in a block
			char buf[BLOCK_SIZE];
			while (int bytes_read = fread(buf, sizeof(char), BLOCK_SIZE, source_file.get())) {

				// check for file read error
				if(ferror(source_file.get())) {
					_cti_getLibSSH().ssh_scp_close(scp.get());
					throw std::runtime_error("Error in reading from file " + std::string(source_path));
				}

				// perform the write
				if (_cti_getLibSSH().ssh_scp_write(scp.get(), buf, bytes_read) != SSH_OK) {
					_cti_getLibSSH().ssh_scp_close(scp.get());
					throw std::runtime_error("Error writing to remote file: " + getError());
				}
			}
		} else {
			_cti_getLibSSH().ssh_scp_close(scp.get());
			throw std::runtime_error("Could not open source file for shipping to the backends");
		}

		_cti_getLibSSH().ssh_scp_close(scp.get());
	}
};

GenericSSHApp::GenericSSHApp(pid_t launcherPid, std::unique_ptr<MPIRInstance>&& launcherInstance)
	: m_launcherPid { launcherPid }
	, m_stepLayout  { GenericSSHFrontend::fetchStepLayout(launcherInstance->getProcTable()) }
	, m_dlaunchSent { false }

	, m_launcherInstance { std::move(launcherInstance) }

	, m_toolPath    { SSH_TOOL_DIR }
	, m_attribsPath { SSH_TOOL_DIR }
	, m_stagePath   { cti::cstr::mkdtemp(std::string{_cti_getCfgDir() + "/" + SSH_STAGE_DIR}) }
	, m_extraFiles  { GenericSSHFrontend::createNodeLayoutFile(m_stepLayout, m_stagePath) }

{
	// Ensure there are running nodes in the job.
	if (m_stepLayout.nodes.empty()) {
		throw std::runtime_error("Application " + getJobId() + " does not have any nodes.");
	}

	// If an active MPIR session was provided, extract the MPIR ProcTable and write the PID List File.
	if (m_launcherInstance) {
		m_extraFiles.push_back(GenericSSHFrontend::createPIDListFile(m_launcherInstance->getProcTable(), m_stagePath));
	}
}

GenericSSHApp::GenericSSHApp(GenericSSHApp&& moved)
	: m_launcherPid { moved.m_launcherPid }
	, m_stepLayout  { moved.m_stepLayout }
	, m_dlaunchSent { moved.m_dlaunchSent }

	, m_launcherInstance { std::move(moved.m_launcherInstance) }

	, m_toolPath    { moved.m_toolPath }
	, m_attribsPath { moved.m_attribsPath }
	, m_stagePath   { moved.m_stagePath }
	, m_extraFiles  { moved.m_extraFiles }
{
	// We have taken ownership of the staging path, so don't let moved delete the directory.
	moved.m_stagePath.erase();
}

GenericSSHApp::~GenericSSHApp()
{
	// Delete the staging directory if it exists.
	if (!m_stagePath.empty()) {
		_cti_removeDirectory(m_stagePath.c_str());
	}
}

/* app instance creation */

GenericSSHApp::GenericSSHApp(pid_t launcherPid)
	: GenericSSHApp{launcherPid, nullptr}
{}

GenericSSHApp::GenericSSHApp(std::unique_ptr<MPIRInstance>&& launcherInstance)
	: GenericSSHApp
		{ launcherInstance->getLauncherPid()
		, std::move(launcherInstance)
	}
{}

GenericSSHApp::GenericSSHApp(const char * const launcher_argv[], int stdout_fd, int stderr_fd,
	const char *inputFile, const char *chdirPath, const char * const env_list[])
	: GenericSSHApp{ GenericSSHFrontend::launchApp(launcher_argv, stdout_fd, stderr_fd, inputFile, chdirPath, env_list) }
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
		SSHSession(node.hostname).executeRemoteCommand(killArgv.get(), nullptr);
	}
}

void
GenericSSHApp::shipPackage(std::string const& tarPath) const
{
	if (auto packageName = cti::move_pointer_ownership(_cti_pathToName(tarPath.c_str()), std::free)) {
		auto const destination = std::string{std::string{SSH_TOOL_DIR} + "/" + packageName.get()};

		// Send the package to each of the hosts using SCP
		for (auto&& node : m_stepLayout.nodes) {
			SSHSession(node.hostname).sendRemoteFile(tarPath.c_str(), destination.c_str(), S_IRWXU | S_IRWXG | S_IRWXO);
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

	// Transfer the dlaunch binary to the backends if it has not yet been transferred
	if (!m_dlaunchSent) {
		// Get the location of the daemon launcher
		if (_cti_getDlaunchPath().empty()) {
			throw std::runtime_error("Required environment variable not set:" + std::string(BASE_DIR_ENV_VAR));
		}

		shipPackage(_cti_getDlaunchPath());

		// set transfer to true
		m_dlaunchSent = true;
	}

	// Use location of existing launcher binary on compute node
	std::string const launcherPath{m_toolPath + "/" + CTI_DLAUNCH_BINARY};

	// Prepare the launcher arguments
	cti::ManagedArgv launcherArgv { launcherPath };
	for (const char* const* arg = args; *arg != nullptr; arg++) {
		launcherArgv.add(*arg);
	}

	// Execute the launcher on each of the hosts using SSH
	auto const forwardedEnvVars = std::vector<char const*>{
		DBG_LOG_ENV_VAR,
		DBG_ENV_VAR,
		nullptr
	};
	for (auto&& node : m_stepLayout.nodes) {
		SSHSession(node.hostname).executeRemoteCommand(launcherArgv.get(), forwardedEnvVars.data());
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

	// Get the password file entry
	// Start by grabbing the recommended buffer size
	size_t buf_len = 4096;
	long rl = sysconf(_SC_GETPW_R_SIZE_MAX);
	if ( rl != -1 ) buf_len = static_cast<size_t>(rl);
	// resize the vector
	m_pwd_buf.resize(buf_len);
	// Get the password file
	struct passwd *result = nullptr;
	if ( getpwuid_r(	geteuid(),
						&m_pwd,
						m_pwd_buf.data(),
						m_pwd_buf.size(),
						&result)) {
		throw std::runtime_error("getpwuid_r failed: " + std::string{strerror(errno)});
	}
	// Ensure we obtained the result
	if (result == nullptr) {
		throw std::runtime_error("password file entry not found for euid " + std::to_string(geteuid()));
	}
}

GenericSSHFrontend::~GenericSSHFrontend()
{
	// Deinit the libssh2 library.
	libssh2_exit();
}

std::unique_ptr<App>
GenericSSHFrontend::launchBarrier(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
		CStr inputFile, CStr chdirPath, CArgArray env_list)
{
	return std::make_unique<GenericSSHApp>(launcher_argv, stdout_fd, stderr_fd, inputFile,
		chdirPath, env_list);
}

std::unique_ptr<App>
GenericSSHFrontend::registerJob(size_t numIds, ...)
{
	if (numIds != 1) {
		throw std::logic_error("expecting single pid argument to register app");
	}

	va_list idArgs;
	va_start(idArgs, numIds);

	pid_t launcherPid = va_arg(idArgs, pid_t);

	va_end(idArgs);

	return std::make_unique<GenericSSHApp>(launcherPid);
}

std::string
GenericSSHFrontend::getHostname() const
{
	return cti::cstr::gethostname();
}

/* SSH frontend static implementations */

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
GenericSSHFrontend::fetchStepLayout(MPIRInstance::ProcTable const& procTable)
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
GenericSSHFrontend::createPIDListFile(MPIRInstance::ProcTable const& procTable, std::string const& stagePath)
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
