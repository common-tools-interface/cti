/******************************************************************************\
 * ssh_fe.c -  Frontend library functions for fallback (SSH based) workload manager.
 *
 * Copyright 2017 Cray Inc.  All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
 *
 * $HeadURL$
 * $Date$
 * $Rev$
 * $Author$
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

#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <unordered_map>

#include "cti_fe.h"
#include "cti_defs.h"
#include "cti_error.h"

#include "useful/cti_useful.h"
#include "useful/cti_path.h"
#include "useful/cti_stringList.h"
#include "useful/make_unique.hpp"
#include "useful/cti_argv.hpp"
#include "useful/Dlopen.hpp"

#include "mpir_iface/mpir_iface.h"

#include "ssh_fe.hpp"

#include <stdbool.h>
#include <stdlib.h>
#include <libssh/libssh.h>
#include <dlfcn.h>

/* Types used here */

template <typename... Args>
static std::string
string_asprintf(const char* const formatCStr, Args... args) {
	char *rawResult = nullptr;
	if (asprintf(&rawResult, formatCStr, args...) < 0) {
		throw std::runtime_error("asprintf failed.");
	}
	UniquePtrDestr<char> result(rawResult, ::free);
	return std::string(result.get());
}

struct sshHostEntry_t {
	std::string		host;				// hostname of this node
	size_t			firstPE;			// First PE number on this node
	std::vector<pid_t>	pids; // Pids of the PEs running on this node 
};

struct sshLayout_t {
	int						numPEs;
	std::vector<sshHostEntry_t>	hosts;			// Array of hosts

	/*
	 * _cti_ssh_createLayout - Transforms the cti_mpir_procTable_t harvested from the launcher
	 *						   into the internal sshLayout_t data structure
	 *
	 * Arguments
	 *      proctable - The cti_mpir_procTable_t to transform
	 *
	 * Returns
	 *      A sshLayout_t* that contains the layout of the application
	 * 
	 */
	sshLayout_t(cti_mpir_procTable_t* proctable) {
		numPEs = proctable->num_pids;

		size_t nodeCount = 0;
		size_t peCount   = 0;

		std::unordered_map<std::string, pid_t> hostNidMap;

		// For each new host we see, add a host entry to the end of the layout's host list
		// and hash each hostname to its index into the host list 
		for(size_t i = 0; i < proctable->num_pids; i++){
			std::string const procHost(proctable->hostnames[i]);
			pid_t procPid = proctable->pids[i];

			auto hostNidPair = hostNidMap.find(procHost);
			if (hostNidPair == hostNidMap.end()) {
				// New host, extend hosts array, and fill in host entry information
				hostNidPair->second = nodeCount++;

				auto const newHost = sshHostEntry_t{procHost, peCount, {}};
				hosts.emplace_back(newHost);
			}

			// add new pe to end of host's list
			hosts[hostNidPair->second].pids.emplace_back(procPid);

			peCount++;
		}
	}
};

static std::vector<std::string> const _cti_ssh_extraFiles(sshLayout_t const& layout, std::string const& stagePath);

struct sshInfo_t {
	cti_app_id_t		appId;			// CTI appid associated with this alpsInfo_t obj
	pid_t 				launcher_pid;	// PID of the launcher
	mpir_id_t			mpir_id;			// MPIR instance handle
	sshLayout_t			layout;			// Layout of job step
	std::string			toolPath;		// Backend staging directory
	std::string			attribsPath;    // PMI_ATTRIBS location on the backend
	bool				dlaunch_sent;	// True if we have already transfered the dlaunch utility
	std::string			stagePath;		// directory to stage this instance files in for transfer to BE
	std::vector<std::string> extraFiles;		// extra files to transfer to BE associated with this app

	/*
	 * _cti_ssh_registerJob - Registers an already running application for
	 *                                  use with the Cray tool interface.
	 * 
	 * Detail
	 *      This function is used for registering a valid application that was
	 *      previously launched through external means for use with the tool
	 *      interface. It is recommended to use the built-in functions to launch
	 *      applications, however sometimes this is impossible (such is the case for
	 *      a debug attach scenario). In order to use any of the functions defined
	 *      in this interface, the pid of the launcher must be supplied.
	 *
	 * Arguments
	 *      launcher_pid - The pid of the running launcher to which to attach if the layout is needed.
	 *      layout - pointer to existing layout information (or fetch if NULL)
	 *
	 * Returns
	 *      A cti_app_id_t that contains the id registered in this interface. This
	 *      app_id should be used in subsequent calls. 0 is returned on error.
	 * 
	 */
	sshInfo_t(pid_t launcher_pid_, mpir_id_t mpir_id_, cti_app_id_t newAppId)
		: appId{newAppId}
		, launcher_pid{launcher_pid_}
		, mpir_id{mpir_id_}
		, layout{sshLayout_t{_cti_mpir_newProcTable(launcher_pid)}}
		, toolPath{string_asprintf(SSH_TOOL_DIR)}
		, attribsPath{string_asprintf(SSH_TOOL_DIR)}
		, dlaunch_sent{false}
		, stagePath{_cti_getCfgDir() + "/" + SSH_STAGE_DIR}
		, extraFiles{_cti_ssh_extraFiles(layout, stagePath)} {}

	/*
	 * _cti_ssh_consumeSshInfo - Destroy an sshInfo_t object
	 *
	 * Arguments
	 *      this - A pointer to the sshInv_t to destroy
	 *
	 */
	~sshInfo_t() {
		// release mpir instance
		if (mpir_id > 0) {
			_cti_mpir_releaseInstance(mpir_id);
		}
	}
};

const char * _cti_ssh_forwarded_env_vars[] = {
	DBG_LOG_ENV_VAR,
	DBG_ENV_VAR,
	LIBALPS_ENABLE_DSL_ENV_VAR,
	CTI_LIBALPS_ENABLE_DSL_ENV_VAR,
	NULL
};

static void _cti_ssh_release(sshInfo_t& my_app);

class LibSSH {
private: // types
	struct FnTypes {
		using ssh_channel_close = int(ssh_channel channel);
		using ssh_channel_free = void(ssh_channel channel);
		using ssh_channel_new = ssh_channel(ssh_session session);
		using ssh_channel_open_session = int(ssh_channel channel);
		using ssh_channel_request_env = int(ssh_channel channel, const char *name, const char *value);
		using ssh_channel_request_exec = int(ssh_channel channel, const char *cmd);
		using ssh_channel_send_eof = int(ssh_channel channel);
		using ssh_connect = int(ssh_session session);
		using ssh_disconnect = void(ssh_session session);
		using ssh_free = void(ssh_session session);
		using ssh_get_error = const char *(void *error);
		using ssh_is_server_known = int(ssh_session session);
		using ssh_new = ssh_session(void);
		using ssh_options_set = int(ssh_session session, enum ssh_options_e type, const void *value);
		using ssh_scp_close = int(ssh_scp scp);
		using ssh_scp_free = void(ssh_scp scp);
		using ssh_scp_init = int(ssh_scp scp);
		using ssh_scp_new = ssh_scp(ssh_session session, int mode, const char *location);
		using ssh_scp_push_file = int(ssh_scp scp, const char *filename, size_t size, int mode);
		using ssh_scp_write = int(ssh_scp scp, const void *buffer, size_t len);
		using ssh_userauth_publickey_auto = int(ssh_session session, const char *username, const char *passphrase);
		using ssh_write_knownhost = int(ssh_session session);
	};

public: // variables
	Dlopen::Handle libSSHHandle;

	std::function<FnTypes::ssh_channel_close> ssh_channel_close;
	std::function<FnTypes::ssh_channel_free> ssh_channel_free;
	std::function<FnTypes::ssh_channel_new> ssh_channel_new;
	std::function<FnTypes::ssh_channel_open_session> ssh_channel_open_session;
	std::function<FnTypes::ssh_channel_request_env> ssh_channel_request_env;
	std::function<FnTypes::ssh_channel_request_exec> ssh_channel_request_exec;
	std::function<FnTypes::ssh_channel_send_eof> ssh_channel_send_eof;
	std::function<FnTypes::ssh_connect> ssh_connect;
	std::function<FnTypes::ssh_disconnect> ssh_disconnect;
	std::function<FnTypes::ssh_free> ssh_free;
	std::function<FnTypes::ssh_get_error> ssh_get_error;
	std::function<FnTypes::ssh_is_server_known> ssh_is_server_known;
	std::function<FnTypes::ssh_new> ssh_new;
	std::function<FnTypes::ssh_options_set> ssh_options_set;
	std::function<FnTypes::ssh_scp_close> ssh_scp_close;
	std::function<FnTypes::ssh_scp_free> ssh_scp_free;
	std::function<FnTypes::ssh_scp_init> ssh_scp_init;
	std::function<FnTypes::ssh_scp_new> ssh_scp_new;
	std::function<FnTypes::ssh_scp_push_file> ssh_scp_push_file;
	std::function<FnTypes::ssh_scp_write> ssh_scp_write;
	std::function<FnTypes::ssh_userauth_publickey_auto> ssh_userauth_publickey_auto;
	std::function<FnTypes::ssh_write_knownhost> ssh_write_knownhost;

public: // interface
	LibSSH()
		: libSSHHandle("libssh.so.4")
		, ssh_channel_close(libSSHHandle.load<FnTypes::ssh_channel_close>("ssh_channel_close"))
		, ssh_channel_free(libSSHHandle.load<FnTypes::ssh_channel_free>("ssh_channel_free"))
		, ssh_channel_new(libSSHHandle.load<FnTypes::ssh_channel_new>("ssh_channel_new"))
		, ssh_channel_open_session(libSSHHandle.load<FnTypes::ssh_channel_open_session>("ssh_channel_open_session"))
		, ssh_channel_request_env(libSSHHandle.load<FnTypes::ssh_channel_request_env>("ssh_channel_request_env"))
		, ssh_channel_request_exec(libSSHHandle.load<FnTypes::ssh_channel_request_exec>("ssh_channel_request_exec"))
		, ssh_channel_send_eof(libSSHHandle.load<FnTypes::ssh_channel_send_eof>("ssh_channel_send_eof"))
		, ssh_connect(libSSHHandle.load<FnTypes::ssh_connect>("ssh_connect"))
		, ssh_disconnect(libSSHHandle.load<FnTypes::ssh_disconnect>("ssh_disconnect"))
		, ssh_free(libSSHHandle.load<FnTypes::ssh_free>("ssh_free"))
		, ssh_get_error(libSSHHandle.load<FnTypes::ssh_get_error>("ssh_get_error"))
		, ssh_is_server_known(libSSHHandle.load<FnTypes::ssh_is_server_known>("ssh_is_server_known"))
		, ssh_new(libSSHHandle.load<FnTypes::ssh_new>("ssh_new"))
		, ssh_options_set(libSSHHandle.load<FnTypes::ssh_options_set>("ssh_options_set"))
		, ssh_scp_close(libSSHHandle.load<FnTypes::ssh_scp_close>("ssh_scp_close"))
		, ssh_scp_free(libSSHHandle.load<FnTypes::ssh_scp_free>("ssh_scp_free"))
		, ssh_scp_init(libSSHHandle.load<FnTypes::ssh_scp_init>("ssh_scp_init"))
		, ssh_scp_new(libSSHHandle.load<FnTypes::ssh_scp_new>("ssh_scp_new"))
		, ssh_scp_push_file(libSSHHandle.load<FnTypes::ssh_scp_push_file>("ssh_scp_push_file"))
		, ssh_scp_write(libSSHHandle.load<FnTypes::ssh_scp_write>("ssh_scp_write"))
		, ssh_userauth_publickey_auto(libSSHHandle.load<FnTypes::ssh_userauth_publickey_auto>("ssh_userauth_publickey_auto"))
		, ssh_write_knownhost(libSSHHandle.load<FnTypes::ssh_write_knownhost>("ssh_write_knownhost")) {}
};
static const LibSSH libSSH;


struct SSHSession {
	UniquePtrDestr<std::remove_pointer<ssh_session>::type> session;
	std::string const getError() { return std::string(libSSH.ssh_get_error(session.get())); }

	/*
	 * _cti_ssh_verify_server - Verify server's identity on an ssh session
	 * 
	 * Arguments
	 *      ssh_session - The session to be validated
	 *
	 * Returns
	 *      1 on error, 0 on success
	 * 
	 */
	static bool _cti_ssh_server_valid(ssh_session session) {
		switch (libSSH.ssh_is_server_known(session)) {
		case SSH_SERVER_KNOWN_OK:
			return true;
		case SSH_SERVER_KNOWN_CHANGED:
			fprintf(stderr, "Host key for server changed: it is now:\n");
			fprintf(stderr, "For security reasons, connection will be stopped\n");
			return false;
		case SSH_SERVER_FOUND_OTHER:
			fprintf(stderr, "The host key for this server was not found but an other"
				"type of key exists.\n");
			fprintf(stderr, "An attacker might change the default server key to"
				"confuse your client into thinking the key does not exist\n");
			fprintf(stderr, "For security reasons, connection will be stopped\n");
			return false;
		case SSH_SERVER_FILE_NOT_FOUND:
			/* fallback to SSH_SERVER_NOT_KNOWN behavior */
		case SSH_SERVER_NOT_KNOWN:
			fprintf(stderr,"Warning: backend node not in known_hosts. Updating known_hosts.\n");
			if (libSSH.ssh_write_knownhost(session) < 0) {
				throw std::runtime_error("Error writing known host: " + std::string(strerror(errno)));
			}
			return true;
		default:
		case SSH_SERVER_ERROR:
			throw std::runtime_error("Error validating server: " + std::string(libSSH.ssh_get_error(session)));
		}
	}

	/*
	 * _cti_ssh_start_session - start and authenticate an ssh session with a remote host
	 *
	 * detail
	 *		starts an ssh session with hostname, verifies the identity of the remote host,
	 *		and authenticates the user using the public key method. this is the only supported
	 *		ssh authentication method.
	 *
	 * arguments
	 *		hostname - hostname of remote host to which to connect
	 *
	 * returns
	 *      an ssh_session which is connected to the remote host and authenticated, or null on error
	 * 
	 */
	SSHSession(std::string const& hostname) : session(libSSH.ssh_new(), libSSH.ssh_free) {
		// open session and set hostname to which to connect
		if (session == nullptr){
			throw std::runtime_error("error allocating new ssh session: " + getError());
		}
		libSSH.ssh_options_set(session.get(), SSH_OPTIONS_HOST, hostname.c_str());

		// connect to remote host
		int rc = libSSH.ssh_connect(session.get());
		if (rc != SSH_OK) {
			throw std::runtime_error("ssh connection error: " + getError());
		}
		
		// verify the identity of the remote host
		if (!_cti_ssh_server_valid(session.get())) {
			libSSH.ssh_disconnect(session.get());
			throw std::runtime_error("could not verify backend node identity: " + getError());
		}

		// authenticate user with the remote host using public key authentication
		rc = libSSH.ssh_userauth_publickey_auto(session.get(), nullptr, nullptr);
		switch (rc) {
			case SSH_AUTH_PARTIAL:
			case SSH_AUTH_DENIED:
			case SSH_AUTH_ERROR:
				libSSH.ssh_disconnect(session.get());
				throw std::runtime_error("Authentication failed: " + getError() + ". CTI requires paswordless (public key) SSH authentication to the backends. Contact your system administrator about setting this up.");
		}
	}

	~SSHSession() {
		libSSH.ssh_disconnect(session.get());
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
		UniquePtrDestr<std::remove_pointer<ssh_channel>::type> channel(libSSH.ssh_channel_new(session.get()), libSSH.ssh_channel_free);
		if (channel == nullptr) {
			throw std::runtime_error("Error allocating ssh channel: " + getError());
		}

		// open session on channel
		if (libSSH.ssh_channel_open_session(channel.get()) != SSH_OK) {
			throw std::runtime_error("Error opening session on ssh channel: " + getError());
		}

		// Forward environment variables before execution. May not be supported on 
		// all systems if user environments are disabled by the ssh server
		if (environment != nullptr) {
			for (const char* const* var = environment; *var != nullptr; var++) {
				if (const char* val = getenv(*var)) {
					libSSH.ssh_channel_request_env(channel.get(), *var, val);
				}
			}
		}

		// Request execution of the command on the remote host
		std::string argvString;
		for (const char* const* arg = args; *arg != nullptr; arg++) {
			argvString.push_back(' ');
			argvString += std::string(*arg);
		}
		if (libSSH.ssh_channel_request_exec(channel.get(), argvString.c_str()) != SSH_OK) {
			throw std::runtime_error("Execution of ssh command failed: " + getError());
			libSSH.ssh_channel_close(channel.get());
		}

		// End the channel
		libSSH.ssh_channel_send_eof(channel.get());
		libSSH.ssh_channel_close(channel.get());
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
			libSSH.ssh_scp_new(session.get(), SSH_SCP_WRITE, _cti_pathToDir(destination_path)),
				libSSH.ssh_scp_free);

		if (scp == nullptr) {
			throw std::runtime_error("Error allocating scp session: " + getError());
		}


		// initialize scp session
		if (libSSH.ssh_scp_init(scp.get()) != SSH_OK) {
			throw std::runtime_error("Error initializing scp session: " + getError());
		}

		//Get the length of the source file
		struct stat stbuf;
		{ int fd = open(source_path, O_RDONLY);
			if (fd < 0) {
				libSSH.ssh_scp_close(scp.get());
				throw std::runtime_error("Could not open source file for shipping to the backends");
			}

			if ((fstat(fd, &stbuf) != 0) || (!S_ISREG(stbuf.st_mode))) {
				close(fd);
				libSSH.ssh_scp_close(scp.get());
				throw std::runtime_error("Could not fstat source file for shipping to the backends");
			}

			close(fd);
		}

		size_t file_size = stbuf.st_size;
		std::string const relative_destination("/" + std::string(_cti_pathToName(destination_path)));

		// Create an empty file with the correct length on the remote host
		if (libSSH.ssh_scp_push_file(scp.get(), relative_destination.c_str(), file_size, mode) != SSH_OK) {
			libSSH.ssh_scp_close(scp.get());
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
					libSSH.ssh_scp_close(scp.get());
					throw std::runtime_error("Error in reading from file " + std::string(source_path));
				}

				// perform the write
				if (libSSH.ssh_scp_write(scp.get(), buf, bytes_read) != SSH_OK) {
					libSSH.ssh_scp_close(scp.get());
					throw std::runtime_error("Error writing to remote file: " + getError());
				}
			}
		} else {
			libSSH.ssh_scp_close(scp.get());
			throw std::runtime_error("Could not open source file for shipping to the backends");
		}

		libSSH.ssh_scp_close(scp.get());
	}
};

/* Constructor/Destructor functions */

/*
 * cti_ssh_fini - Deinitialize a ssh based cti session 
 *
 */
static void
_cti_ssh_fini(void)
{
	// force cleanup to happen on any pending srun launches
	_cti_mpir_releaseAllInstances();

	// done
	return;
}

/*
 * _cti_ssh_getJobId - Get the string of the job identifier
 * 
 *
 * Arguments
 *      my_app - A cti_wlm_obj that represents the info struct for the application
 *
 * Returns
 *      A C-string representing the job identifier
 * 
 */
static char *
_cti_ssh_getJobId(sshInfo_t& my_app) {
	char *				rtn = NULL;

	asprintf(&rtn, "%d", my_app.launcher_pid);

	return rtn;
}

/*
 * _cti_ssh_launch_common - Launch an application and optionally hold it in a startup barrier
 * 
 * Arguments
 *      launcher_argv -  A null terminated list of arguments to pass directly to
 *                       the launcher. This differs from a traditional argv in
 *                       the sense that launcher_argv[0] is the start of the
 *                       actual arguments passed to the launcher and not the
 *                       name of launcher itself.
 *      stdout_fd -      The file descriptor opened for writing to redirect
 *                       stdout to or -1 if no redirection should take place.
 *      stderr_fd -      The file descriptor opened for writing to redirect
 *                       stderr to or -1 if no redirection should take place.
 *      inputFile -      The pathname of a file to open and redirect stdin or
 *                       NULL if no redirection should take place. If NULL,
 *                       /dev/null will be used for stdin.
 *      chdirPath -      The path to change the current working directory to or 
 *                       NULL if no cd should take place.
 *      env_list -       A null terminated list of strings of the form 
 *                       "name=value". The name in the environment will be set
 *                       to value.
 *		doBarrier - 	 If set to 1, the application will be held in a startup barrier.
 *						 Otherwise, it will not.
 *
 * Returns
 *      A cti_app_id_t that contains the id registered in this interface. This
 *      app_id should be used in subsequent calls. 0 is returned on error.
 * 
 */
static UniquePtrDestr<sshInfo_t>
_cti_ssh_launch_common(	const char * const launcher_argv[], int stdout_fd, int stderr_fd,
								const char *inputFile, const char *chdirPath,
								const char * const env_list[], int doBarrier, cti_app_id_t newAppId)
{
	UniquePtrDestr<sshInfo_t> sinfo;
	mpir_id_t			mpir_id;
	const char*			launcher_path;

	// get the launcher path
	launcher_path = _cti_pathFind(SRUN, nullptr);
	if (launcher_path == nullptr)
	{
		throw std::runtime_error("Required environment variable not set: " + std::string(BASE_DIR_ENV_VAR));
	}

	// optionally open input file
	int input_fd = -1;
	if (inputFile == nullptr) {
		inputFile = "/dev/null";
	}
	errno = 0;
	input_fd = open(inputFile, O_RDONLY);
	if (input_fd < 0) {
		throw std::runtime_error("Failed to open input file " + std::string(inputFile) +": " + std::string(strerror(errno)));
	}
	
	// Create a new MPIR instance. We want to interact with it.
	if ((mpir_id = _cti_mpir_newLaunchInstance(launcher_path, launcher_argv, env_list, input_fd, stdout_fd, stderr_fd)) < 0)
	{
		std::string argvString;
		for (const char* const* arg = launcher_argv; *arg != nullptr; arg++) {
			argvString.push_back(' ');
			argvString += *arg;
		}
		throw std::runtime_error("Failed to launch: " + argvString);
	}

	// Register this app with the application interface
	auto const launcher_pid = _cti_mpir_getLauncherPid(mpir_id);
	try {
		sinfo = std::make_unique<sshInfo_t>(launcher_pid, mpir_id, newAppId);
	} catch (std::exception const& ex) {
		_cti_mpir_releaseInstance(mpir_id);
		throw ex;
	}

	// Release the application from the startup barrier according to the doBarrier flag
	if (!doBarrier) {
		_cti_ssh_release(*sinfo);
	}

	return sinfo;
	
}

/*
 * _cti_ssh_launch - Launch an application
 * 
 * Arguments
 *      launcher_argv -  A null terminated list of arguments to pass directly to
 *                       the launcher. This differs from a traditional argv in
 *                       the sense that launcher_argv[0] is the start of the
 *                       actual arguments passed to the launcher and not the
 *                       name of launcher itself.
 *      stdout_fd -      The file descriptor opened for writing to redirect
 *                       stdout to or -1 if no redirection should take place.
 *      stderr_fd -      The file descriptor opened for writing to redirect
 *                       stderr to or -1 if no redirection should take place.
 *      inputFile -      The pathname of a file to open and redirect stdin or
 *                       NULL if no redirection should take place. If NULL,
 *                       /dev/null will be used for stdin.
 *      chdirPath -      The path to change the current working directory to or 
 *                       NULL if no cd should take place.
 *      env_list -       A null terminated list of strings of the form 
 *                       "name=value". The name in the environment will be set
 *                       to value.
 *
 * Returns
 *      A cti_app_id_t that contains the id registered in this interface. This
 *      app_id should be used in subsequent calls. 0 is returned on error.
 * 
 */
static UniquePtrDestr<sshInfo_t>
_cti_ssh_launch(	const char * const launcher_argv[], int stdout_fd, int stderr_fd,
					const char *inputFile, const char *chdirPath,
					const char * const env_list[], cti_app_id_t newAppId)
{
	return _cti_ssh_launch_common(launcher_argv, stdout_fd, stderr_fd, inputFile, 
								  chdirPath, env_list, 0, newAppId);
}

/*
 * _cti_ssh_launchBarrier - Launch an application and hold it in a startup barrier
 * 
 * Arguments
 *      launcher_argv -  A null terminated list of arguments to pass directly to
 *                       the launcher. This differs from a traditional argv in
 *                       the sense that launcher_argv[0] is the start of the
 *                       actual arguments passed to the launcher and not the
 *                       name of launcher itself.
 *      stdout_fd -      The file descriptor opened for writing to redirect
 *                       stdout to or -1 if no redirection should take place.
 *      stderr_fd -      The file descriptor opened for writing to redirect
 *                       stderr to or -1 if no redirection should take place.
 *      inputFile -      The pathname of a file to open and redirect stdin or
 *                       NULL if no redirection should take place. If NULL,
 *                       /dev/null will be used for stdin.
 *      chdirPath -      The path to change the current working directory to or 
 *                       NULL if no cd should take place.
 *      env_list -       A null terminated list of strings of the form 
 *                       "name=value". The name in the environment will be set
 *                       to value.
 *
 * Returns
 *      A cti_app_id_t that contains the id registered in this interface. This
 *      app_id should be used in subsequent calls. 0 is returned on error.
 * 
 */
static UniquePtrDestr<sshInfo_t>
_cti_ssh_launchBarrier(	const char * const launcher_argv[], int stdout_fd, int stderr_fd,
						const char *inputFile, const char *chdirPath,
						const char * const env_list[], cti_app_id_t newAppId)
{
	return _cti_ssh_launch_common(launcher_argv, stdout_fd, stderr_fd, inputFile, 
								  chdirPath, env_list, 1, newAppId);
}

/*
 * _cti_ssh_release - Release an application from its startup barrier
 * 
 *
 * Arguments
 *      my_app - A cti_wlm_obj that represents the info struct for the application
 *
 * Returns
 *      1 on error, 0 on success
 * 
 */
static void
_cti_ssh_release(sshInfo_t& my_app)
{
	// call the release function
	if (_cti_mpir_releaseInstance(my_app.mpir_id))
	{
		throw std::runtime_error("srun barrier release operation failed.");
	}
	my_app.mpir_id = -1;
}

/*
 * _cti_ssh_killApp - Send a signal to each application process
 * 
 * Detail
 *		Delivers a signal to each process of the application by delivering
 *		the kill command through SSH to each running application process
 *		whose pids are provided by the MPIR_PROCTABLE
 *
 * Arguments
 *      my_app - A cti_wlm_obj that represents the info struct for the application
 *		signum - An int representing the type of signal to send to the application
 *
 * Returns
 *      1 on error, 0 on success
 * 
 */
static void
_cti_ssh_killApp(sshInfo_t& my_app, int signum) {
	//Connect through ssh to each node and send a kill command to every pid on that node
	for (size_t i = 0; i < my_app.layout.hosts.size(); ++i) {
		ManagedArgv killArgv;
		killArgv.add("kill");
		killArgv.add("-" + std::to_string(signum));
		for(size_t j = 0; j < my_app.layout.hosts[i].pids.size(); j++) {
			killArgv.add(std::to_string(my_app.layout.hosts[i].pids[j]));
		}

		SSHSession(my_app.layout.hosts[i].host).executeRemoteCommand(killArgv.get(), nullptr);
	}
}

/*
 * _cti_ssh_extraBinaries - Specifies locations of extra workload manager specific binaries
 *						   to be shipped to the backend nodes
 * 
 * Detail
 *		This ssh based fallback implementation does not require extra binaries, 
 *		so this function always returns NULL.
 *
 * Arguments
 *      my_app - A cti_wlm_obj that represents the info struct for the application
 *
 * Returns
 *      NULL to signify no extra binaries are needed
 * 
 */
static const char * const *
_cti_ssh_extraBinaries(sshInfo_t& my_app)
{
	return NULL;
}

/*
 * _cti_ssh_extraLibraries - Specifies locations of extra workload manager specific libraries
 *						   to be shipped to the backend nodes
 * 
 * Detail
 *		This ssh based fallback implementation does not require extra libraries, 
 *		so this function always returns NULL.
 *
 * Arguments
 *      my_app - A cti_wlm_obj that represents the info struct for the application
 *
 * Returns
 *      NULL to signify no extra libraries are needed
 * 
 */
static const char * const *
_cti_ssh_extraLibraries(sshInfo_t& my_app)
{
	return NULL;
}

/*
 * _cti_ssh_extraLibDirs - Specifies locations of extra workload manager specific library 
 *						   directories to be shipped to the backend nodes
 * 
 * Detail
 *		This ssh based fallback implementation does not require extra library
 *		directories, so this function always returns NULL.
 *
 * Arguments
 *      my_app - A cti_wlm_obj that represents the info struct for the application
 *
 * Returns
 *      NULL to signify no extra library directories are needed
 * 
 */
static const char * const *
_cti_ssh_extraLibDirs(sshInfo_t& my_app)
{
	return NULL;
}

/*
 * _cti_ssh_extraFiles - Specifies locations of extra workload manager specific 
 *						 files to be shipped to the backend nodes
 * 
 * Detail
 *		Creates two files: the layout file and the pid file for shipping to the backends.
 *		The layout file specifies each host along with the number of PEs and first PE
 *		at each host. The pid file specifies the pids of each of the running PEs.
 *		Returns an array of paths to the two files created.
 *
 * Arguments
 *      my_app - A cti_wlm_obj that represents the info struct for the application
 *
 * Returns
 *      An array of paths to the two files created containing the path to the layout file
 *		and the path to the pid file
 * 
 */
static std::vector<std::string> const
_cti_ssh_extraFiles(sshLayout_t const& layout, std::string const& stagePath) {
	std::vector<std::string> result;

	// Create the temporary directory for the manifest package
	auto rawStagePath = UniquePtrDestr<char>(strdup(stagePath.c_str()), ::free);
	if (mkdtemp(rawStagePath.get()) == nullptr) {
		throw std::runtime_error("mkdtemp failed.");
	}

	// Create layout file path in staging directory for writing
	std::string const layoutPath(std::string{rawStagePath.get()} + "/" + SSH_LAYOUT_FILE);

	// open the layout file in staging directory
	if (auto layoutFile = UniquePtrDestr<FILE>(fopen(layoutPath.c_str(), "wb"), ::fclose)) {

		// init the layout header
		cti_layoutFileHeader_t	layout_hdr;
		memset(&layout_hdr, 0, sizeof(layout_hdr));
		layout_hdr.numNodes = layout.hosts.size();

		// write the header
		if (fwrite(&layout_hdr, sizeof(cti_layoutFileHeader_t), 1, layoutFile.get()) != 1) {
			// cannot continue, so return NULL. BE API might fail.
			// TODO: How to handle this error?
			throw std::runtime_error("failed to write the layout header");
		}

		// write each of the entries
		for (size_t i = 0; i < layout.hosts.size(); ++i) {
			// set this entry
			cti_layoutFile_t		layout_entry;
			memcpy(&layout_entry.host[0], layout.hosts[i].host.c_str(), layout.hosts[i].host.length() + 1);
			layout_entry.PEsHere = layout.hosts[i].pids.size();
			layout_entry.firstPE = layout.hosts[i].firstPE;

			// write to file
			if (fwrite(&layout_entry, sizeof(cti_layoutFile_t), 1, layoutFile.get()) != 1) {
				throw std::runtime_error("failed to write a host entry to layout file");
			}
		}
	} else {
		throw std::runtime_error("Failed to open layout file " + layoutPath);
	}

	// add layout file as extra file to ship
	result.push_back(layoutPath);

	// Create pid file in staging directory for writing
	std::string const pidPath(std::string{rawStagePath.get()} + "/" + SSH_PID_FILE);

	fprintf(stderr, "PID FILE: %s\n", pidPath.c_str());

	// Open the pid file
	if (auto pidFile = UniquePtrDestr<FILE>(fopen(pidPath.c_str(), "wb"), ::fclose)) {

		// init the pid header
		cti_pidFileheader_t pid_hdr;
		memset(&pid_hdr, 0, sizeof(pid_hdr));
		pid_hdr.numPids = layout.hosts.size();

		// write the header
		if (fwrite(&pid_hdr, sizeof(cti_pidFileheader_t), 1, pidFile.get()) != 1) {
			throw std::runtime_error("failed to write pidfile header");
		}

		// write each of the entries
		slurmPidFile_t pid_entry;
		memset(&pid_entry, 0, sizeof(pid_entry));
		for (size_t i = 0; i < layout.hosts.size(); ++i) {
			for(size_t j = 0; j < layout.hosts[i].pids.size(); j++) {
				// set this entry
				pid_entry.pid = layout.hosts[i].pids[j];

				// write this entry
				if (fwrite(&pid_entry, sizeof(cti_pidFile_t), 1, pidFile.get()) != 1) {
					throw std::runtime_error("failed to write pidfile entry");
				}
			}
		}
	} else {
		throw std::runtime_error("failed to open pidfile path " + pidPath);
	}

	// add pid file as extra file to ship
	result.push_back(pidPath);

	return result;
}

/*
 * _cti_ssh_ship_package - Ship the cti manifest package tarball to the backends.
 *
 * Detail
 *		Ships the cti manifest package specified by package to each backend node 
 *		in the application using SSH.
 *
 * Arguments
 *      my_app - A cti_wlm_obj that represents the info struct for the application
 *		package - A C-string specifying the path to the package to ship
 *
 * Returns
 *      1 on error, 0 on success
 * 
 */
static void
_cti_ssh_ship_package(sshInfo_t& my_app, std::string const& package)
{
	// Sanity check the arguments
	if (my_app.layout.hosts.empty()) {
		throw std::runtime_error("No nodes in application");
	}

	// Prepare the destination path for the package on the remote host
	if (auto packageName = UniquePtrDestr<char>(_cti_pathToName(package.c_str()), ::free)) {
		auto const destination = std::string{std::string{SSH_TOOL_DIR} + "/" + packageName.get()};

		// Send the package to each of the hosts using SCP
		for (size_t i = 0; i < my_app.layout.hosts.size(); ++i) {
			SSHSession(my_app.layout.hosts[i].host).sendRemoteFile(package.c_str(), destination.c_str(), S_IRWXU | S_IRWXG | S_IRWXO);
		}
	} else {
		throw std::runtime_error("_cti_pathToName failed");
	}
}

/*
 * _cti_ssh_start_daemon - Launch and execute the cti daemon on each of the 
 * 						   backend nodes of the application.
 * 
 * Detail
 *		Launches the daemon using the arguments specified in args
 *		to each node in the application using SSH.
 *
 * Arguments
 *      my_app - A cti_wlm_obj that represents the info struct for the application
 *		args - A cti_args_t object holding the arguments to pass to the daemon
 *
 * Returns
 *      1 on error, 0 on success
 * 
 */
static void
_cti_ssh_start_daemon(sshInfo_t& my_app, const char* const args[]) {
	// sanity check
	if (args == nullptr) {
		throw std::runtime_error("args array is empty!");
	}
	if (my_app.layout.hosts.empty()) {
		throw std::runtime_error("No nodes in application");
	}

	// Transfer the dlaunch binary to the backends if it has not yet been transferred
	if (!my_app.dlaunch_sent) {
		
		// Get the location of the daemon launcher
		if (_cti_getDlaunchPath().empty()) {
			throw std::runtime_error("Required environment variable not set:" + std::string(BASE_DIR_ENV_VAR));
		}

		_cti_ssh_ship_package(my_app, _cti_getDlaunchPath().c_str());

		// set transfer to true
		my_app.dlaunch_sent = 1;
	}

	// Use location of existing launcher binary on compute node
	std::string const launcherPath(my_app.toolPath + "/" + CTI_LAUNCHER);

	// Prepare the launcher arguments
	ManagedArgv launcherArgv;
	launcherArgv.add(launcherPath);
	for (const char* const* arg = args; *arg != nullptr; arg++) {
		launcherArgv.add(*arg);
	}

	// Execute the launcher on each of the hosts using SSH
	for (size_t i = 0; i < my_app.layout.hosts.size(); ++i) {
		SSHSession(my_app.layout.hosts[i].host).executeRemoteCommand(launcherArgv.get(), _cti_ssh_forwarded_env_vars);
	}
}

/*
 * _cti_ssh_getNumAppPEs - Gets the number of PEs on which the application is running.
 *
 * Arguments
 *      my_app - A cti_wlm_obj that represents the info struct for the application
 *
 * Returns
 *      An int representing the number of PEs on which the application is running
 * 
 */
static size_t
_cti_ssh_getNumAppPEs(sshInfo_t& my_app) {
	return my_app.layout.numPEs;
}

/*
 * _cti_ssh_getNumAppNodes - Gets the number of nodes on which the application is running.
 *
 * Arguments
 *      my_app - A cti_wlm_obj that represents the info struct for the application
 *
 * Returns
 *      An int representing the number of nodes on which the application is running
 * 
 */
static size_t
_cti_ssh_getNumAppNodes(sshInfo_t& my_app) {
	return my_app.layout.hosts.size();
}

/*
 * _cti_ssh_getAppHostsList - Gets a list of hostnames on which the application is running.
 *
 * Arguments
 *      my_app - A cti_wlm_obj that represents the info struct for the application
 *
 * Returns
 *      A NULL terminated array of C-strings representing the list of hostnames
 * 
 */
static std::vector<std::string>
_cti_ssh_getAppHostsList(sshInfo_t& my_app) {
	std::vector<std::string> result;

	// ensure numNodes is non-zero
	if (my_app.layout.hosts.empty()) {
		throw std::runtime_error("Application does not have any nodes.");
	}

	// allocate space for the hosts list, add an extra entry for the null terminator
	result.reserve(my_app.layout.hosts.size());

	// iterate through the hosts list
	for (size_t i = 0; i < my_app.layout.hosts.size(); ++i) {
		result.emplace_back(my_app.layout.hosts[i].host);
	}

	return result;
}

/*
 * _cti_ssh_getAppHostsPlacement - Gets the hostname to PE placement information 
 *								   for the application.
 * 
 * Detail
 *		Gets a list which contains all of the hostnames of the application and 
		the number of PEs at each host.
 *
 * Arguments
 *      my_app - A cti_wlm_obj that represents the info struct for the application
 *
 * Returns
 *      A pointer to a cti_hostsList_t containing the placement information
 * 
 */
static std::vector<Frontend::CTIHost>
_cti_ssh_getAppHostsPlacement(sshInfo_t& my_app) {
	std::vector<Frontend::CTIHost> result;

	// ensure hosts.size() is non-zero
	if (my_app.layout.hosts.size() <= 0) {
		throw std::runtime_error("Application does not have any nodes.");
	}

	// allocate space for the cti_hostsList_t struct
	result.reserve(my_app.layout.hosts.size());

	// iterate through the hosts list
	for (size_t i = 0; i < my_app.layout.hosts.size(); ++i) {
		auto const newPlacement = Frontend::CTIHost{
			my_app.layout.hosts[i].host,
			(size_t)my_app.layout.hosts[i].pids.size()
		};
		result.emplace_back(newPlacement);
	}

	return result;
}

/*
 * _cti_ssh_getHostName - Gets the hostname of the current node.
 *
 * Returns
 *      A C-string representing the hostname of the current node.
 * 
 */
static char *
_cti_ssh_getHostName(void)
{

	char host[HOST_NAME_MAX+1];

	if (gethostname(host, HOST_NAME_MAX+1))
	{
		_cti_set_error("gethostname failed.");
		return NULL;
	}

	return strdup(host);
}

#include <vector>
#include <string>
#include <unordered_map>

#include <memory>

#include <stdexcept>

/* wlm interface implementation */

using AppId   = Frontend::AppId;
using CTIHost = Frontend::CTIHost;

/* active app management */

static std::unordered_map<AppId, UniquePtrDestr<sshInfo_t>> appList;
static const AppId APP_ERROR = 0;
static AppId newAppId() noexcept {
	static AppId nextId = 1;
	return nextId++;
}

static sshInfo_t&
getAppInfo(AppId appId) {
	auto infoPtr = appList.find(appId);
	if (infoPtr != appList.end()) {
		return *(infoPtr->second);
	}

	throw std::runtime_error("invalid appId: " + std::to_string(appId));
}

bool
SSHFrontend::appIsValid(AppId appId) const {
	return appList.find(appId) != appList.end();
}

void
SSHFrontend::deregisterApp(AppId appId) const {
	appList.erase(appId);
}

cti_wlm_type
SSHFrontend::getWLMType() const {
	return CTI_WLM_CRAY_SLURM;
}

std::string const
SSHFrontend::getJobId(AppId appId) const {
	return _cti_ssh_getJobId(getAppInfo(appId));
}

AppId
SSHFrontend::launch(CArgArray launcher_argv, int stdout_fd, int stderr,
					 CStr inputFile, CStr chdirPath, CArgArray env_list) {
	auto const appId = newAppId();
	appList[appId] = _cti_ssh_launch_common(launcher_argv, stdout_fd, stderr, inputFile, chdirPath, env_list, 0, appId);
	return appId;
}

AppId
SSHFrontend::launchBarrier(CArgArray launcher_argv, int stdout_fd, int stderr,
							CStr inputFile, CStr chdirPath, CArgArray env_list) {
	auto const appId = newAppId();
	appList[appId] = _cti_ssh_launch_common(launcher_argv, stdout_fd, stderr, inputFile, chdirPath, env_list, 1, appId);
	return appId;
}

void
SSHFrontend::releaseBarrier(AppId appId) {
	_cti_mpir_releaseInstance(getAppInfo(appId).mpir_id);
}

void
SSHFrontend::killApp(AppId appId, int signal) {
	_cti_ssh_killApp(getAppInfo(appId), signal);
}

std::vector<std::string> const
SSHFrontend::getExtraFiles(AppId appId) const {
	return getAppInfo(appId).extraFiles;
}


void
SSHFrontend::shipPackage(AppId appId, std::string const& tarPath) const {
	_cti_ssh_ship_package(getAppInfo(appId), tarPath.c_str());
}

void
SSHFrontend::startDaemon(AppId appId, CArgArray argv) const {
	_cti_ssh_start_daemon(getAppInfo(appId), argv);
}

size_t
SSHFrontend::getNumAppPEs(AppId appId) const {
	return _cti_ssh_getNumAppPEs(getAppInfo(appId));
}

size_t
SSHFrontend::getNumAppNodes(AppId appId) const {
	return _cti_ssh_getNumAppNodes(getAppInfo(appId));
}

std::vector<std::string> const
SSHFrontend::getAppHostsList(AppId appId) const {
	return _cti_ssh_getAppHostsList(getAppInfo(appId));
}

std::vector<CTIHost> const
SSHFrontend::getAppHostsPlacement(AppId appId) const {
	return _cti_ssh_getAppHostsPlacement(getAppInfo(appId));
}

std::string const
SSHFrontend::getHostName(void) const {
	return _cti_ssh_getHostName();
}

std::string const
SSHFrontend::getLauncherHostName(AppId appId) const {
	throw std::runtime_error("getLauncherHostName not supported for SSH frontend (app ID " + std::to_string(appId));
}

std::string const
SSHFrontend::getToolPath(AppId appId) const {
	return getAppInfo(appId).toolPath;
}

std::string const
SSHFrontend::getAttribsPath(AppId appId) const {
	return getAppInfo(appId).attribsPath;
}

/* extended frontend implementation */

SSHFrontend::~SSHFrontend() {
	_cti_ssh_fini();
}

AppId
SSHFrontend::registerJob(pid_t launcher_pid) {
	auto const appId = newAppId();
	if (auto const launcher_path = _cti_pathFind(SRUN, nullptr)) {
		if (auto const mpir_id = _cti_mpir_newAttachInstance(launcher_path, launcher_pid)) {
			appList[appId] = std::make_unique<sshInfo_t>(launcher_pid, mpir_id, appId);
		} else {
			throw std::runtime_error("failed to attach to launcher pid " + std::to_string(launcher_pid)); 
		}
	} else {
		throw std::runtime_error("failed to get launcher path from CTI");
	}
	return appId;
}