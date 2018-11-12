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

#include "cti_fe.h"
#include "cti_defs.h"
#include "cti_error.h"

#include "useful/cti_useful.h"
#include "useful/cti_path.h"
#include "useful/cti_stringList.h"
#include "useful/make_unique.hpp"
#include "useful/strong_argv.hpp"
#include "useful/Dlopen.hpp"

#include "mpir_iface/mpir_iface.h"

#include "ssh_fe.hpp"

#include <stdbool.h>
#include <stdlib.h>
#include <libssh/libssh.h>
#include <dlfcn.h>

/* Types used here */

typedef struct
{
	char *			host;				// hostname of this node
	int				PEsHere;			// Number of PEs running on this node
	int				firstPE;			// First PE number on this node
	pid_t* 			pids;               // Pids of the PEs running on this node 
} sshHostEntry_t;

typedef struct
{
	int						numPEs;			
	int						numNodes;
	sshHostEntry_t *		hosts;			// Array of hosts of length numNodes
} sshLayout_t;

typedef struct
{
	cti_app_id_t		appId;			// CTI appid associated with this alpsInfo_t obj
	pid_t 				launcher_pid;	// PID of the launcher
	sshLayout_t *		layout;			// Layout of job step
	mpir_id_t			mpir_id;			// MPIR instance handle
	
	char *				toolPath;		// Backend staging directory
	char *				attribsPath;    // PMI_ATTRIBS location on the backend
	bool				dlaunch_sent;	// True if we have already transfered the dlaunch utility
	char *				stagePath;		// directory to stage this instance files in for transfer to BE
	char **				extraFiles;		// extra files to transfer to BE associated with this app
} sshInfo_t;

const char * _cti_ssh_forwarded_env_vars[] = {
	DBG_LOG_ENV_VAR,
	DBG_ENV_VAR,
	LIBALPS_ENABLE_DSL_ENV_VAR,
	CTI_LIBALPS_ENABLE_DSL_ENV_VAR,
	NULL
};

static void _cti_ssh_consumeSshLayout(sshLayout_t* layout);

static void _cti_ssh_end_session(ssh_session session);
static int _cti_ssh_release(sshInfo_t& my_app);
static int _cti_ssh_execute_remote_command(ssh_session session, const char* const args[], const char* const environment[]);

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
 * cti_ssh_destroy - Used to destroy the cti_wlm_obj defined by this impelementation
 *
 * Arguments
 *      my_app - A cti_wlm_obj that represents the info struct for the application
 *
 */
static void 
_cti_ssh_destroy(sshInfo_t* sinfo)
{
	// sanity
	if (sinfo == NULL)
		return;

	_cti_ssh_consumeSshLayout(sinfo->layout);
	_cti_mpir_releaseInstance(sinfo->mpir_id);
	
	if (sinfo->toolPath != NULL)
		free(sinfo->toolPath);
		
	// cleanup staging directory if it exists
	if (sinfo->stagePath != NULL)
	{
		_cti_removeDirectory(sinfo->stagePath);
		free(sinfo->stagePath);
	}
	
	// cleanup the extra files array
	if (sinfo->extraFiles != NULL)
	{
		char **	ptr = sinfo->extraFiles;
		
		while (*ptr != NULL)
		{
			free(*ptr++);
		}
		
		free(sinfo->extraFiles);
	}
	
	free(sinfo);
}

/*
 * _cti_ssh_consumeSshLayout - Destroy an sshLayout_t object
 *
 * Arguments
 *      layout - A pointer to the sshLayout_t to destroy
 *
 */
static void
_cti_ssh_consumeSshLayout(sshLayout_t *layout)
{
	int i;

	// sanity
	if (layout == NULL)
		return;
		
	for (i=0; i < layout->numNodes; ++i)
	{
		if (layout->hosts[i].host != NULL)
		{
			free(layout->hosts[i].host);
			free(layout->hosts[i].pids);
		}
	}
	
	free(layout->hosts);
	free(layout);
}

/*
 * _cti_ssh_newSshInfo - Creates a new sshInfo_t object
 *
 * Returns
 *      The newly created sshInfo_t object
 *
 */
static sshInfo_t *
_cti_ssh_newSshInfo(void)
{
	sshInfo_t *	sinfo;

	if ((sinfo = (decltype(sinfo))malloc(sizeof(sshInfo_t))) == NULL)
	{
		// Malloc failed
		_cti_set_error("malloc failed.");
		
		return NULL;
	}

	sinfo->layout = NULL;
	sinfo->mpir_id = -1;
	sinfo->toolPath = NULL;
	sinfo->attribsPath = NULL;
	sinfo->stagePath = NULL;
	sinfo->extraFiles = NULL;
	sinfo->dlaunch_sent = false;
	
	return sinfo;
}

/*
 * _cti_ssh_consumeSshInfo - Destroy an sshInfo_t object
 *
 * Arguments
 *      this - A pointer to the sshInv_t to destroy
 *
 */
static void
_cti_ssh_consumeSshInfo(sshInfo_t *sinfo)
{
	if(sinfo == NULL){
		return;
	}

	if(sinfo->layout != NULL){
		_cti_ssh_consumeSshLayout(sinfo->layout);
	}

	// release mpir instance
	_cti_mpir_releaseInstance(sinfo->mpir_id);

	free(sinfo->toolPath);
	sinfo->toolPath = NULL;

	free(sinfo->attribsPath);
	sinfo->attribsPath = NULL;

	free(sinfo->stagePath);
	sinfo->stagePath = NULL;

	if(sinfo->extraFiles != NULL){
		int i=0;
		while(sinfo->extraFiles[i] != NULL){
			free(sinfo->extraFiles[i]);
			i++;
		}

		free(sinfo->extraFiles);
	}

	free(sinfo);
}

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
static sshLayout_t* 
_cti_ssh_createLayout(cti_mpir_procTable_t* proctable)
{
	sshLayout_t * layout = (decltype(layout))malloc(sizeof(sshLayout_t));
	layout->numPEs = proctable->num_pids;
	layout->hosts = NULL;

	int num_nodes = 0;
	int current_pe = 0;

	stringList_t* host_map = _cti_newStringList();

	// For each new host we see, add a host entry to the end of the layout's host list
	// and hash each hostname to its index into the host list 
	for(size_t i = 0; i<proctable->num_pids; i++){
		char* current_node = strdup(proctable->hostnames[i]);
		pid_t current_pid = proctable->pids[i];

		int* index = (int*)_cti_lookupValue(host_map, current_node);
		int host_index;

		// New host, extend hosts array, and fill in host entry information
		if(index == NULL){
			int value = num_nodes; //num_nodes before incrementing gives the index
			_cti_addString(host_map, strdup(current_node), &value);
			host_index = num_nodes;

			num_nodes++;
			layout->hosts = (decltype(layout->hosts))realloc(layout->hosts, sizeof(sshHostEntry_t)*num_nodes);
			layout->hosts[host_index].host = current_node;
			layout->hosts[host_index].PEsHere = 1;
			layout->hosts[host_index].firstPE = current_pe;
			layout->hosts[host_index].pids = (decltype(layout->hosts[host_index].pids))malloc(sizeof(pid_t));
			layout->hosts[host_index].pids[0] = current_pid;
		}
		// Host exists, update it to accomodate the new PE
		else{
			host_index = *index;
			layout->hosts[host_index].PEsHere++;
			layout->hosts[host_index].pids = (decltype(layout->hosts[host_index].pids))realloc(layout->hosts[host_index].pids, sizeof(pid_t)*layout->hosts[host_index].PEsHere);
			layout->hosts[host_index].pids[layout->hosts[host_index].PEsHere-1] = current_pid;
		}

		current_pe++;
	}

	layout->numNodes = num_nodes;

	return layout;
}

/*
 * _cti_ssh_getLayout - Gets the layout of an application by attaching to the launcher
 *						and harvesting the MPIR_Proctable
 * 
 * Detail
 *		Attaches to the launcher with pid launcher_pid and returns the sshLayout_t which
 *		holds the layout harvested from the MPIR_Proctable in the launcher
 *
 * Arguments
 *      launcher_pid - The pid of the running launcher to which to attach
 *
 * Returns
 *      A sshLayout_t* that contains the layout of the application
 * 
 */
static sshLayout_t* 
_cti_ssh_getLayout(pid_t launcher_pid)
{
	cti_mpir_procTable_t*	proctable; // return object
	
	// sanity check
	if (launcher_pid <= 0)
	{
		_cti_set_error("Invalid launcher pid %d.", (int)launcher_pid);
		return NULL;
	}

	_cti_set_error("_cti_ssh_getLayout on pid %d not implemented.", (int)launcher_pid);
	return NULL;

	return _cti_ssh_createLayout(proctable);
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
static UniquePtrDestr<sshInfo_t>
_cti_ssh_registerJob(pid_t launcher_pid, cti_app_id_t newAppId, sshLayout_t* layout = nullptr)
{
	sshInfo_t	*	sinfo;
	
	// Sanity check arguments
	if (cti_current_wlm() != CTI_WLM_SSH)
	{
		_cti_set_error("Invalid call. SSH WLM not in use.");
		return 0;
	}

	// Create a new registration for the application 

	if ((sinfo = _cti_ssh_newSshInfo()) == NULL)
	{
		// error already set
		return 0;
	}
	
	sinfo->launcher_pid = launcher_pid;

	// Get the layout from the proctable if directed, otherwise return the supplied layout.
	if (layout == NULL){
		if ((sinfo->layout = _cti_ssh_getLayout(launcher_pid)) == NULL)
		{
			// error already set
			_cti_ssh_consumeSshInfo(sinfo);
			return 0;
		}		
	}
	else{
		sinfo->layout = layout;
	}
	
	// Set the tool path
	if (asprintf(&sinfo->toolPath, SSH_TOOL_DIR) <= 0)
	{
		_cti_set_error("asprintf failed");
		_cti_ssh_consumeSshInfo(sinfo);
		return 0;
	}

	// Set the attribs path
	if (asprintf(&sinfo->attribsPath, SSH_TOOL_DIR) <= 0)
	{
		_cti_set_error("asprintf failed");
		_cti_ssh_consumeSshInfo(sinfo);
		return 0;
	}

	sinfo->appId = newAppId;

	return UniquePtrDestr<sshInfo_t>(sinfo, _cti_ssh_consumeSshInfo);
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
	mpir_id_t			mpir_id;
	cti_mpir_procTable_t *	proctable;
	const char*			launcher_path;
	
	if(!_cti_is_valid_environment()){
		// error already set
		return 0;
	}

	// get the launcher path
	launcher_path = _cti_pathFind(SRUN, NULL);
	if (launcher_path == NULL)
	{
		_cti_set_error("Required environment variable %s not set.", BASE_DIR_ENV_VAR);
		return 0;
	}

	// optionally open input file
	int input_fd = -1;
	if (inputFile == NULL) {
		inputFile = "/dev/null";
	}
	errno = 0;
	input_fd = open(inputFile, O_RDONLY);
	if (input_fd < 0) {
		_cti_set_error("Failed to open input file %s: %s", inputFile, strerror(errno));
		return 0;
	}
	
	// Create a new MPIR instance. We want to interact with it.
	if ((mpir_id = _cti_mpir_newLaunchInstance(launcher_path, launcher_argv, env_list, input_fd, stdout_fd, stderr_fd)) < 0)
	{
		_cti_set_error("Failed to launch %s", launcher_argv[0]);

		return 0;
	}
	
	// Harvest and process the MPIR_Proctable which holds application layout information
	if ((proctable = _cti_mpir_newProcTable(mpir_id)) == NULL)
	{
		_cti_set_error("failed to get proctable.\n");
		_cti_mpir_releaseInstance(mpir_id);
		
		return 0;
	}

	sshLayout_t* layout = _cti_ssh_createLayout(proctable);

	pid_t launcher_pid = _cti_mpir_getLauncherPid(mpir_id);
	
	// Register this app with the application interface
	UniquePtrDestr<sshInfo_t> sinfo;
	if ((sinfo = _cti_ssh_registerJob(launcher_pid, newAppId, layout)) == nullptr)
	{
		// Failed to register the jobid/stepid, error is already set.
		_cti_mpir_deleteProcTable(proctable);
		_cti_mpir_releaseInstance(mpir_id);
		
		return 0;
	}

	// set the inv
	sinfo->mpir_id = mpir_id;
	
	// Release the application from the startup barrier according to the doBarrier flag
	if (!doBarrier)
	{
		if (_cti_ssh_release(*sinfo))
		{
			return 0;
		}
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
static int
_cti_ssh_release(sshInfo_t& my_app)
{
	// call the release function
	if (_cti_mpir_releaseInstance(my_app.mpir_id))
	{
		_cti_set_error("srun barrier release operation failed.");
		return 1;
	}
	my_app.mpir_id = -1;
	
	return 0;
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
	for (int i = 0; i < my_app.layout->numNodes; ++i) {
		ManagedArgv killArgv;
		killArgv.add("kill");
		killArgv.add("-" + std::to_string(signum));
		for(int j = 0; j < my_app.layout->hosts[i].PEsHere; j++) {
			killArgv.add(std::to_string(my_app.layout->hosts[i].pids[j]));
		}

		SSHSession(my_app.layout->hosts[i].host).executeRemoteCommand(killArgv.get(), nullptr);
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
static const char * const *
_cti_ssh_extraFiles(sshInfo_t& my_app) {
	FILE *					myFile;
	char *					layoutPath;
	cti_layoutFileHeader_t	layout_hdr;
	cti_layoutFile_t		layout_entry;
	char *					pidPath = NULL;
	cti_pidFileheader_t		pid_hdr;
	cti_pidFile_t			pid_entry;
	int						i;
	int 					NUM_EXTRA_FILES = 2;
	
	// Sanity check the arguments
	if (my_app.layout == NULL){
		_cti_set_error("sshInfo_t layout is null.");
		return NULL;
	}
	
	// Return the extraFiles array if it has already been created
	if (my_app.extraFiles != NULL)
	{
		return (const char * const *)my_app.extraFiles;
	}
	
	// Check to see if we should create the staging directory
	if (my_app.stagePath == NULL)
	{
		if (!_cti_getCfgDir().empty())
		{
			_cti_set_error("Could not get CTI configuration directory.");
			return NULL;
		}
		
		// Prepare the path to the stage directory
		if (asprintf(&my_app.stagePath, "%s/%s", _cti_getCfgDir().c_str(), SSH_STAGE_DIR) <= 0)
		{
			_cti_set_error("asprintf failed.");
			my_app.stagePath = NULL;
			return NULL;
		}
		
		// Create the temporary directory for the manifest package
		if (mkdtemp(my_app.stagePath) == NULL)
		{
			_cti_set_error("mkdtemp failed.");
			free(my_app.stagePath);
			my_app.stagePath = NULL;
			return NULL;
		}
	}
	
	// Create layout file in staging directory for writing
	if (asprintf(&layoutPath, "%s/%s", my_app.stagePath, SSH_LAYOUT_FILE) <= 0)
	{
		_cti_set_error("asprintf failed.");
		return NULL;
	}
	
	if ((myFile = fopen(layoutPath, "wb")) == NULL)
	{
		_cti_set_error("Failed to open %s\n", layoutPath);
		free(layoutPath);
		return NULL;
	}

	memset(&layout_hdr, 0, sizeof(layout_hdr));
	memset(&layout_entry, 0, sizeof(layout_entry));
	memset(&pid_hdr, 0, sizeof(pid_hdr));
	memset(&pid_entry, 0, sizeof(pid_entry));

	//Construct layout file from internal sshLayout_t data structure
	layout_hdr.numNodes = my_app.layout->numNodes;
	
	if (fwrite(&layout_hdr, sizeof(cti_layoutFileHeader_t), 1, myFile) != 1)
	{
		_cti_set_error("Failed to write to %s\n", layoutPath);
		free(layoutPath);
		fclose(myFile);
		return NULL;
	}
	
	for (i=0; i < my_app.layout->numNodes; ++i)
	{
		memcpy(&layout_entry.host[0], my_app.layout->hosts[i].host, sizeof(layout_entry.host));
		layout_entry.PEsHere = my_app.layout->hosts[i].PEsHere;
		layout_entry.firstPE = my_app.layout->hosts[i].firstPE;
		
		if (fwrite(&layout_entry, sizeof(cti_layoutFile_t), 1, myFile) != 1)
		{
			_cti_set_error("Failed to write to %s\n", layoutPath);
			free(layoutPath);
			fclose(myFile);
			return NULL;
		}
	}
	
	fclose(myFile);
	
	// Create pid file in staging directory for writing
	if (asprintf(&pidPath, "%s/%s", my_app.stagePath, SSH_PID_FILE) <= 0)
	{
		_cti_set_error("asprintf failed.");
		free(layoutPath);
		return NULL;
	}

	fprintf(stderr, "PID FILE: %s\n", pidPath );

	if ((myFile = fopen(pidPath, "wb")) == NULL)
	{
		_cti_set_error("Failed to open %s\n", pidPath);
		free(layoutPath);
		free(pidPath);
		return NULL;
	}

	//Construct pid file from internal sshLayout_t data structure
	pid_hdr.numPids = my_app.layout->numPEs;
	
	if (fwrite(&pid_hdr, sizeof(cti_pidFileheader_t), 1, myFile) != 1)
	{
		_cti_set_error("Failed to write to %s\n", pidPath);
		free(layoutPath);
		free(pidPath);
		fclose(myFile);
		return NULL;
	}

	for (i=0; i < my_app.layout->numNodes; ++i)
	{
		int j;
		for(j=0; j<my_app.layout->hosts[i].PEsHere; j++){
			pid_entry.pid = my_app.layout->hosts[i].pids[j];
			
			if (fwrite(&pid_entry, sizeof(cti_pidFile_t), 1, myFile) != 1)
			{
				_cti_set_error("Failed to write to %s\n", pidPath);
				free(layoutPath);
				free(pidPath);
				fclose(myFile);
				return NULL;
			}	
		}
	}

	fclose(myFile);

	// Create the null terminated extraFiles array to store the paths to the files
	// that were just created
	if ((my_app.extraFiles = (decltype(my_app.extraFiles))calloc(NUM_EXTRA_FILES+1, sizeof(char *))) == NULL)
	{
		_cti_set_error("calloc failed.");
		free(layoutPath);
		return NULL;
	}
	
	my_app.extraFiles[0] = layoutPath;
	my_app.extraFiles[1] = pidPath;
	my_app.extraFiles[2] = NULL;
	
	return (const char * const *)my_app.extraFiles;
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
static int
_cti_ssh_ship_package(sshInfo_t& my_app, const char *package)
{
	// Sanity check the arguments
	if (my_app.layout == NULL)
	{
		_cti_set_error("sshInfo_t layout is null!");
		return 1;
	}
	
	if (package == NULL)
	{
		_cti_set_error("package string is null!");
		return 1;
	}
	
	if (my_app.layout->numNodes <= 0)
	{
		_cti_set_error("No nodes in application");
		return 1;
	}

	// Prepare the destination path for the package on the remote host
	char* destination;
	asprintf(&destination, "%s/%s", SSH_TOOL_DIR, _cti_pathToName(package));

	// Send the package to each of the hosts using SCP
	for (int i = 0; i < my_app.layout->numNodes; ++i) {
		SSHSession(my_app.layout->hosts[i].host).sendRemoteFile(package, destination, S_IRWXU | S_IRWXG | S_IRWXO);
	}

	return 0;
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
static int
_cti_ssh_start_daemon(sshInfo_t& my_app, const char* const args[])
{
	// Sanity check the arguments
	if (my_app.layout == NULL)
	{
		_cti_set_error("sshInfo_t layout is null!");
		return 1;
	}
	
	if (args == NULL)
	{
		_cti_set_error("args string is null!");
		return 1;
	}
	
	if (my_app.layout->numNodes <= 0)
	{
		_cti_set_error("Application does not have any nodes.");
		return 1;
	}

	// Transfer the dlaunch binary to the backends if it has not yet been transferred
	if (!my_app.dlaunch_sent)
	{
		if (!_cti_getDlaunchPath().empty())
		{
			_cti_set_error("Required environment variable %s not set.", BASE_DIR_ENV_VAR);
			return 1;
		}
		
		if (_cti_ssh_ship_package(my_app, _cti_getDlaunchPath().c_str()))
		{
			return 1;
		}
		
		my_app.dlaunch_sent = 1;
	}
	
	// Use location of existing launcher binary on compute node
	std::string const launcherPath(std::string(my_app.toolPath) + "/" + CTI_LAUNCHER);

	// Prepare the launcher arguments
	ManagedArgv launcherArgv;
	launcherArgv.add(launcherPath);
	if (args != NULL) {
		for (const char* const* arg = args; *arg != nullptr; arg++) {
			launcherArgv.add(*arg);
		}
	}

	// Execute the launcher on each of the hosts using SSH
	for (int i = 0; i < my_app.layout->numNodes; ++i) {
		SSHSession(my_app.layout->hosts[i].host).executeRemoteCommand(launcherArgv.get(), _cti_ssh_forwarded_env_vars);
	}

	return 0;
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
static int
_cti_ssh_getNumAppPEs(sshInfo_t& my_app) {
	
	// Sanity check the arguments
	if (my_app.layout == NULL)
	{
		_cti_set_error("getNumAppPEs operation failed.");
		return 0;
	}
	
	return my_app.layout->numPEs;
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
static int
_cti_ssh_getNumAppNodes(sshInfo_t& my_app) {
	
	// Sanity check the arguments
	if (my_app.layout == NULL)
	{
		_cti_set_error("getNumAppPEs operation failed.");
		return 0;
	}
	
	return my_app.layout->numNodes;
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
static char **
_cti_ssh_getAppHostsList(sshInfo_t& my_app) {
	char **				hosts;
	int					i;
	
	// Sanity check the arguments
	if (my_app.layout == NULL)
	{
		_cti_set_error("getNumAppPEs operation failed.");
		return NULL;
	}
	
	if (my_app.layout->numNodes <= 0)
	{
		_cti_set_error("Application does not have any nodes.");
		return NULL;
	}
	
	// Construct the null termintated hosts list from the internal sshLayout_t representation
	if ((hosts = (decltype(hosts))calloc(my_app.layout->numNodes + 1, sizeof(char *))) == NULL)
	{
		_cti_set_error("calloc failed.");
		return NULL;
	}
	
	for (i=0; i < my_app.layout->numNodes; ++i)
	{
		hosts[i] = strdup(my_app.layout->hosts[i].host);
	}
	
	hosts[i] = NULL;

	return hosts;
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

	// ensure numNodes is non-zero
	if (my_app.layout->numNodes <= 0) {
		throw std::runtime_error("Application does not have any nodes.");
	}

	// allocate space for the cti_hostsList_t struct
	result.reserve(my_app.layout->numNodes);

	// iterate through the hosts list
	for (int i = 0; i < my_app.layout->numNodes; ++i) {
		result.emplace_back(my_app.layout->hosts[i].host, my_app.layout->hosts[i].PEsHere);
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

/*
 * _cti_ssh_getToolPath - Gets the path of the directory used for staging files
 * 						  on the backend.
 *
 * Arguments
 *      my_app - A cti_wlm_obj that represents the info struct for the application
 *
 * Returns
 *      A C-string representing the path of the backend staging directory
 * 
 */
static const char *
_cti_ssh_getToolPath(sshInfo_t& my_app) {
	
	// Sanity check the arguments
	if (my_app.toolPath == NULL)
	{
		_cti_set_error("toolPath my_app missing from sinfo obj!");
		return NULL;
	}

	return (const char *)my_app.toolPath;
}

/*
 * _cti_ssh_getAttribsPath - Gets the location of the attribs file 
 * which holds host and pid information.
 * 
 * Detail
 *		This ssh based fallback implementation does not support PMI_ATTRIBS as 
 *		multiple launchers are supported, each with their own proprietary application IDs.
 *		To get the application layout, this implementation uses the SLURM_PID file
 *
 * Arguments
 *      my_app - A cti_wlm_obj that represents the info struct for the application
 *
 * Returns
 *      NULL to represent the attribs file not being used for this implementation
 * 
 */
static const char *
_cti_ssh_getAttribsPath(sshInfo_t& my_app)
{
	return NULL;
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
	std::vector<std::string> result;
	auto const extraFiles = _cti_ssh_extraFiles(getAppInfo(appId));
	for (const char* const* filePath = extraFiles; filePath != nullptr; filePath++) {
		result.emplace_back(*filePath);
	}
	return result;
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
	std::vector<std::string> result;
	auto const hosts = _cti_ssh_getAppHostsList(getAppInfo(appId));
	for (const char* const* host = hosts; host != nullptr; host++) {
		result.emplace_back(*host);
	}
	return result;
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
	appList[appId] = _cti_ssh_registerJob(launcher_pid, appId);
	return appId;
}