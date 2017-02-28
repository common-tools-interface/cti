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

#include "ssh_fe.h"
#include "cti_fe.h"
#include "cti_defs.h"
#include "cti_error.h"
#include "cti_useful.h"
#include "cti_path.h"

#include "gdb_MPIR_iface.h"
#include "cti_stringList.h"
#include <stdbool.h>
#include <stdlib.h>
#include <libssh/libssh.h>
#include <dlfcn.h>

/* Types used here */

typedef struct
{
	cti_gdb_id_t	gdb_id;
	pid_t			gdb_pid;			// pid of the gdb process for the mpir starter
} sshInv_t;

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
	sshInv_t *			inv;			// Object used to store the gdb pid information for interfacing with MPIR
	
	char *				toolPath;		// Backend staging directory
	char *				attribsPath;    // PMI_ATTRIBS location on the backend
	bool				dlaunch_sent;	// True if we have already transfered the dlaunch utility
	char *				stagePath;		// directory to stage this instance files in for transfer to BE
	char **				extraFiles;		// extra files to transfer to BE associated with this app
} sshInfo_t;

/* Static prototypes */
static int					_cti_ssh_init(void);
static void					_cti_ssh_fini(void);
static void 				_cti_ssh_destroy(cti_wlm_obj app_info);
static void					_cti_ssh_consumeSshLayout(sshLayout_t *this);
static sshInv_t * 			_cti_ssh_newSshInv(void);
static sshInfo_t * 			_cti_ssh_newSshInfo(void);
static void 				_cti_ssh_consumeSshInv(sshInv_t *this);
static void					_cti_ssh_consumeSshInfo(sshInfo_t *this);
static sshLayout_t* 	_cti_ssh_createLayout(cti_mpir_proctable_t* proctable);
static sshLayout_t* 	_cti_ssh_getLayout(pid_t launcher_pid);
static char * 				_cti_ssh_getJobId(cti_wlm_obj app_info);
cti_app_id_t 				_cti_ssh_registerJob(pid_t launcher_pid, bool get_layout, sshLayout_t* layout);
cti_app_id_t 				cti_ssh_registerJob(pid_t launcher_pid);
static cti_app_id_t 		_cti_ssh_launch_common(	const char * const launcher_argv[], int stdout_fd, int stderr_fd,
													const char *inputFile, const char *chdirPath,
													const char * const env_list[], int doBarrier	);
static cti_app_id_t 		_cti_ssh_launch(	const char * const launcher_argv[], int stdout_fd, int stderr_fd,
												const char *inputFile, const char *chdirPath,
												const char * const env_list[]		);
static cti_app_id_t 		_cti_ssh_launchBarrier(	const char * const launcher_argv[], int stdout_fd, int stderr_fd,
													const char *inputFile, const char *chdirPath,
													const char * const env_list[]	);
static int 					_cti_ssh_release(cti_wlm_obj app_info);
static int 					_cti_ssh_killApp(cti_wlm_obj app_info, int signum);
static const char * const * _cti_ssh_extraBinaries(cti_wlm_obj app_info);
static const char * const * _cti_ssh_extraLibraries(cti_wlm_obj app_info);
static const char * const * _cti_ssh_extraLibDirs(cti_wlm_obj app_info);
static const char * const * _cti_ssh_extraFiles(cti_wlm_obj app_info);
int 						_cti_ssh_verify_server(ssh_session session);
ssh_session 				_cti_ssh_start_session(char* hostname);
int 						_cti_ssh_execute_remote_command(ssh_session session, cti_args_t *args, char** environment);
int 						_cti_ssh_copy_file_to_remote(ssh_session session, const char* source_path, const char* destination_path, 
								int mode);
void 						_cti_ssh_end_session(ssh_session session);
static int 					_cti_ssh_ship_package(cti_wlm_obj app_info, const char *package);
static int 					_cti_ssh_start_daemon(cti_wlm_obj app_info, cti_args_t * args);
static int 					_cti_ssh_getNumAppPEs(cti_wlm_obj app_info);
static int 					_cti_ssh_getNumAppNodes(cti_wlm_obj app_info);
static char ** 				_cti_ssh_getAppHostsList(cti_wlm_obj app_info);
static cti_hostsList_t * 	_cti_ssh_getAppHostsPlacement(cti_wlm_obj app_info);
static char * 				_cti_ssh_getHostName(void);
static const char * 		_cti_ssh_getToolPath(cti_wlm_obj app_info);
static const char * 		_cti_ssh_getAttribsPath(cti_wlm_obj app_info);

/* cray ssh wlm proto object */
const cti_wlm_proto_t		_cti_ssh_wlmProto =
{
	CTI_WLM_SSH,						// wlm_type
	_cti_ssh_init,					// wlm_init
	_cti_ssh_fini,					// wlm_fini
	_cti_ssh_destroy,		// wlm_destroy
	_cti_ssh_getJobId,				// wlm_getJobId
	_cti_ssh_launch,					// wlm_launch
	_cti_ssh_launchBarrier,			// wlm_launchBarrier
	_cti_ssh_release,					// wlm_releaseBarrier
	_cti_ssh_killApp,					// wlm_killApp
	_cti_ssh_extraBinaries,			// wlm_extraBinaries
	_cti_ssh_extraLibraries,			// wlm_extraLibraries
	_cti_ssh_extraLibDirs,			// wlm_extraLibDirs
	_cti_ssh_extraFiles,				// wlm_extraFiles
	_cti_ssh_ship_package,			// wlm_shipPackage
	_cti_ssh_start_daemon,			// wlm_startDaemon
	_cti_ssh_getNumAppPEs,			// wlm_getNumAppPEs
	_cti_ssh_getNumAppNodes,			// wlm_getNumAppNodes
	_cti_ssh_getAppHostsList,			// wlm_getAppHostsList
	_cti_ssh_getAppHostsPlacement,	// wlm_getAppHostsPlacement
	_cti_ssh_getHostName,				// wlm_getHostName
	_cti_wlm_getLauncherHostName_none,	// wlm_getLauncherHostName
	_cti_ssh_getToolPath,				// wlm_getToolPath
	_cti_ssh_getAttribsPath			// wlm_getAttribsPath
};

const char * _cti_ssh_forwarded_env_vars[] = {
	DBG_LOG_ENV_VAR,
	DBG_ENV_VAR,
	LIBALPS_ENABLE_DSL_ENV_VAR,
	CTI_LIBALPS_ENABLE_DSL_ENV_VAR,
	NULL
};

typedef struct
{
	void* handle;
	int(*ssh_channel_close)(ssh_channel channel);
	void (*ssh_channel_free)(ssh_channel channel);
	ssh_channel(*ssh_channel_new)(ssh_session session);
	int(*ssh_channel_open_session)(ssh_channel channel);
	int (*ssh_channel_request_env) (ssh_channel channel, const char *name, const char *value);
	int (*ssh_channel_request_exec) (ssh_channel channel, const char *cmd);
	int (*ssh_channel_send_eof) (ssh_channel channel);
	int (*ssh_connect) (ssh_session session);
	void (*ssh_disconnect) (ssh_session session);
	void (*ssh_free) (ssh_session session);
	const char * (*ssh_get_error) (void *error);
	int (*ssh_is_server_known) (ssh_session session);
	ssh_session (*ssh_new) (void);
	int (*ssh_options_set) (ssh_session session, enum ssh_options_e type, const void *value);
	int (*ssh_scp_close) (ssh_scp scp);
	void (*ssh_scp_free) (ssh_scp scp);
	int (*ssh_scp_init) (ssh_scp scp);
	ssh_scp (*ssh_scp_new) (ssh_session session, int mode, const char *location);
	int (*ssh_scp_push_file) (ssh_scp scp, const char *filename, size_t size, int mode);
	int (*ssh_scp_write) (ssh_scp scp, const void *buffer, size_t len);
	int (*ssh_userauth_publickey_auto) (ssh_session session, const char *username, const char *passphrase);
	int (*ssh_write_knownhost) (ssh_session session);
} libssh_funcs_t;

libssh_funcs_t _cti_ssh_libssh_funcs = {
	.handle = NULL
};

static cti_list_t *	_cti_ssh_info	= NULL;	// list of sshInfo_t objects registered by this interface

/* Constructor/Destructor functions */

/*
 * cti_ssh_init - Initialize a ssh based cti session 
 *
 * Returns
 *      0 on success
 *
 */
static int
_cti_ssh_init(void)
{
	// create a new _cti_alps_info list
	if (_cti_ssh_info == NULL)
		_cti_ssh_info = _cti_newList();
	// done
	return 0;
}

/*
 * cti_ssh_fini - Deinitialize a ssh based cti session 
 *
 */
static void
_cti_ssh_fini(void)
{
	// force cleanup to happen on any pending srun launches - we do this to ensure
	// gdb instances don't get left hanging around.
	_cti_gdb_cleanupAll();
	
	if (_cti_ssh_info != NULL)
		_cti_consumeList(_cti_ssh_info, NULL);	// this should have already been cleared out.

	// done
	return;
}

/*
 * cti_ssh_destroy - Used to destroy the cti_wlm_obj defined by this impelementation
 *
 * Arguments
 *      app_info - A cti_wlm_obj that represents the info struct for the application
 *
 */
static void 
_cti_ssh_destroy(cti_wlm_obj app_info)
{
	sshInfo_t *	sinfo = (sshInfo_t *)app_info;

	// sanity
	if (sinfo == NULL)
		return;
		
	// remove this sinfo from the global list
	_cti_list_remove(_cti_ssh_info, sinfo);

	_cti_ssh_consumeSshLayout(sinfo->layout);
	_cti_ssh_consumeSshInv(sinfo->inv);
	
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
 *      this - A pointer to the sshLayout_t to destroy
 *
 */
static void
_cti_ssh_consumeSshLayout(sshLayout_t *this)
{
	int i;

	// sanity
	if (this == NULL)
		return;
		
	for (i=0; i < this->numNodes; ++i)
	{
		if (this->hosts[i].host != NULL)
		{
			free(this->hosts[i].host);
			free(this->hosts[i].pids);
		}
	}
	
	free(this->hosts);
	free(this);
}

/*
 * _cti_ssh_newSshInv - Creates a new sshInv_t object
 *
 * Returns
 *      The newly created sshInv_t object
 *
 */
static sshInv_t *
_cti_ssh_newSshInv(void)
{
	sshInv_t *	this;

	if ((this = malloc(sizeof(sshInv_t))) == NULL)
	{
		// Malloc failed
		_cti_set_error("malloc failed.");
		
		return NULL;
	}
	
	// init the members
	this->gdb_id = -1;
	this->gdb_pid = -1;
	
	return this;
}

/*
 * _cti_ssh_consumeSshInv - Destroy an sshInv_t object
 *
 * Arguments
 *      this - A pointer to the sshInv_t to destroy
 *
 */
static void
_cti_ssh_consumeSshInv(sshInv_t *this)
{
	free(this);
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
	sshInfo_t *	this;

	if ((this = malloc(sizeof(sshInfo_t))) == NULL)
	{
		// Malloc failed
		_cti_set_error("malloc failed.");
		
		return NULL;
	}

	this->layout = NULL;
	this->inv = NULL;
	this->toolPath = NULL;
	this->attribsPath = NULL;
	this->stagePath = NULL;
	this->extraFiles = NULL;
	this->dlaunch_sent = false;
	
	return this;
}

/*
 * _cti_ssh_consumeSshInfo - Destroy an sshInfo_t object
 *
 * Arguments
 *      this - A pointer to the sshInv_t to destroy
 *
 */
static void
_cti_ssh_consumeSshInfo(sshInfo_t *this)
{
	if(this == NULL){
		return;
	}

	if(this->layout != NULL){
		_cti_ssh_consumeSshLayout(this->layout);
	}

	if(this->inv != NULL){
		_cti_ssh_consumeSshInv(this->inv);
	}

	if(this->toolPath != NULL){
		free(this->toolPath);
	}

	if(this->attribsPath != NULL){
		free(this->attribsPath);
	}

	if(this->stagePath != NULL){
		free(this->stagePath);
	}

	if(this->extraFiles != NULL){
		int i=0;
		while(this->extraFiles[i] != NULL){
			free(this->extraFiles[i]);
			i++;
		}

		free(this->extraFiles);
	}

	free(this);
}

/*
 * _cti_ssh_createLayout - Transforms the cti_mpir_proctable_t harvested from the launcher
 *						   into the internal sshLayout_t data structure
 *
 * Arguments
 *      proctable - The cti_mpir_proctable_t to transform
 *
 * Returns
 *      A sshLayout_t* that contains the layout of the application
 * 
 */
static sshLayout_t* 
_cti_ssh_createLayout(cti_mpir_proctable_t* proctable)
{
	sshLayout_t * layout = malloc(sizeof(sshLayout_t));
	layout->numPEs = proctable->num_pids;
	layout->hosts = NULL;

	int i;
	int num_nodes = 0;
	int current_pe = 0;

	stringList_t* host_map = _cti_newStringList();

	// For each new host we see, add a host entry to the end of the layout's host list
	// and hash each hostname to its index into the host list 
	for(i=0; i<proctable->num_pids; i++){
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
			layout->hosts = realloc(layout->hosts, sizeof(sshHostEntry_t)*num_nodes);
			layout->hosts[host_index].host = current_node;
			layout->hosts[host_index].PEsHere = 1;
			layout->hosts[host_index].firstPE = current_pe;
			layout->hosts[host_index].pids = malloc(sizeof(pid_t));
			layout->hosts[host_index].pids[0] = current_pid;
		}
		// Host exists, update it to accomodate the new PE
		else{
			host_index = *index;
			layout->hosts[host_index].PEsHere++;
			layout->hosts[host_index].pids = realloc(layout->hosts[host_index].pids, sizeof(pid_t)*layout->hosts[host_index].PEsHere);
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
	cti_gdb_id_t		gdb_id;
	pid_t				gdb_pid;
	const char *		gdb_path;
	char *				usr_gdb_path = NULL;
	const char *		attach_path;
	cti_mpir_proctable_t*	proctable; // return object
	
	// sanity check
	if (launcher_pid <= 0)
	{
		_cti_set_error("Invalid launcher pid %d.", (int)launcher_pid);
		return NULL;
	}
	
	// get the gdb path
	if ((gdb_path = getenv(GDB_LOC_ENV_VAR)) != NULL)
	{
		// use the gdb set in the environment
		usr_gdb_path = strdup(gdb_path);
		gdb_path = (const char *)usr_gdb_path;
	} else
	{
		gdb_path = _cti_getGdbPath();
		if (gdb_path == NULL)
		{
			_cti_set_error("Required environment variable %s not set.", BASE_DIR_ENV_VAR);
			return NULL;
		}
	}
	
	// get the attach path
	attach_path = _cti_getAttachPath();
	if (attach_path == NULL)
	{
		_cti_set_error("Required environment variable %s not set.", BASE_DIR_ENV_VAR);
		if (usr_gdb_path != NULL)	free(usr_gdb_path);
		return NULL;
	}
	
	// Create a new gdb MPIR instance. We want to interact with it.
	if ((gdb_id = _cti_gdb_newInstance()) < 0)
	{
		// error already set
		if (usr_gdb_path != NULL)	free(usr_gdb_path);
		
		return NULL;
	}
	
	// fork off a process to start the mpir attach
	gdb_pid = fork();
	
	// error case
	if (gdb_pid < 0)
	{
		_cti_set_error("Fatal fork error.");
		_cti_gdb_cleanup(gdb_id);
		
		return NULL;
	}
	
	// child case
	// Note that this should not use the _cti_set_error() interface since it is
	// a child process.
	if (gdb_pid == 0)
	{
		// call the exec function - this should not return
		_cti_gdb_execAttach(gdb_id, attach_path, gdb_path, launcher_pid);
		
		// exec shouldn't return
		fprintf(stderr, "CTI error: Return from exec.\n");
		perror("execv");
		_exit(1);
	}
	
	// parent case
	
	// cleanup
	if (usr_gdb_path != NULL)	free(usr_gdb_path);
	
	// call the post fork setup - this will ensure gdb was started and this pid
	// is valid
	if (_cti_gdb_postFork(gdb_id))
	{
		// error message already set
		_cti_gdb_cleanup(gdb_id);
		waitpid(gdb_pid, NULL, 0);
		
		return NULL;
	}

	//Harvest and process the proctable to return
	if ((proctable = _cti_gdb_getProctable(gdb_id)) == NULL)
	{	
		return 0;
	}
	
	// Cleanup this gdb instance, we are done with it
	_cti_gdb_cleanup(gdb_id);
	waitpid(gdb_pid, NULL, 0);
	
	return _cti_ssh_createLayout(proctable);
}

/*
 * _cti_ssh_getJobId - Get the string of the job identifier
 * 
 *
 * Arguments
 *      app_info - A cti_wlm_obj that represents the info struct for the application
 *
 * Returns
 *      A C-string representing the job identifier
 * 
 */
static char *
_cti_ssh_getJobId(cti_wlm_obj app_info)
{
	sshInfo_t *	my_app = (sshInfo_t *)app_info;
	char *				rtn = NULL;
	
	// sanity check
	if (my_app == NULL)
	{
		_cti_set_error("Null wlm obj.");
		return NULL;
	}

	asprintf(&rtn, "%d", my_app->launcher_pid);

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
 *      get_layout - A bool representing whether or not this function should attach to the running
 *		launcher to harvest the layout information from the MPIR_Proctable or simply use
 *		the supplied layout information
 *
 * Returns
 *      A cti_app_id_t that contains the id registered in this interface. This
 *      app_id should be used in subsequent calls. 0 is returned on error.
 * 
 */
cti_app_id_t
_cti_ssh_registerJob(pid_t launcher_pid, bool get_layout, sshLayout_t* layout)
{
	appEntry_t *		this;
	sshInfo_t	*	sinfo;
	
	// Sanity check arguments
	if (cti_current_wlm() != CTI_WLM_SSH)
	{
		_cti_set_error("Invalid call. SSH WLM not in use.");
		return 0;
	}
	
	// Look for and return an existing registration if one exists
	_cti_list_reset(_cti_ssh_info);
	while ((sinfo = (sshInfo_t *)_cti_list_next(_cti_ssh_info)) != NULL)
	{
		// check if the apid's match
		if (sinfo->launcher_pid == launcher_pid)
		{
			// reference this appEntry and return the appid
			if (_cti_refAppEntry(sinfo->appId))
			{
				// somehow we have an invalid sinfo obj, so free it and
				// break to re-register this apid
				_cti_ssh_consumeSshInfo(sinfo);
				break;
			}
			return sinfo->appId;
		}
	}
	
	// Create a new registration for the application 

	if ((sinfo = _cti_ssh_newSshInfo()) == NULL)
	{
		// error already set
		return 0;
	}
	
	sinfo->launcher_pid = launcher_pid;

	// Get the layout from the proctable if directed, otherwise return the supplied layout.
	// This is needed because only one gdb can be attached to the launcher at a time and 
	// in the case of launch, there is already a gdb attached. So for the launch case the
	// layout is harvested using the already attached gdb and supplied to register.
	if(get_layout){
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
	
	// Create the new app entry
	if ((this = _cti_newAppEntry(&_cti_ssh_wlmProto, (cti_wlm_obj)sinfo)) == NULL)
	{
		// we failed to create a new appEntry_t entry - catastrophic failure
		// error string already set
		_cti_ssh_consumeSshInfo(sinfo);
		return 0;
	}

	sinfo->appId = this->appId;
	
	// Add the sinfo obj to our global list
	if(_cti_list_add(_cti_ssh_info, sinfo))
	{
		_cti_set_error("cti_ssh_registerJob: _cti_list_add() failed.");
		cti_deregisterApp(this->appId);
		return 0;
	}

	return this->appId;
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
 *      launcher_pid - The pid of the running launcher to which to attach
 *
 * Returns
 *      A cti_app_id_t that contains the id registered in this interface. This
 *      app_id should be used in subsequent calls. 0 is returned on error.
 * 
 */
cti_app_id_t
cti_ssh_registerJob(pid_t launcher_pid){
	return _cti_ssh_registerJob(launcher_pid, true, NULL);
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
static cti_app_id_t
_cti_ssh_launch_common(	const char * const launcher_argv[], int stdout_fd, int stderr_fd,
								const char *inputFile, const char *chdirPath,
								const char * const env_list[], int doBarrier	)
{
	sshInv_t *			myapp;
	appEntry_t *		appEntry;
	sshInfo_t	*		sinfo;
	int					i;
	sigset_t			mask, omask;	// used to ignore SIGINT
	pid_t				mypid;
	cti_mpir_proctable_t *	proctable;
	cti_app_id_t		rtn;
	const char *		gdb_path;
	char *				usr_gdb_path = NULL;
	const char *		starter_path;
	
	// Get the gdb location to pass to the starter
	if ((gdb_path = getenv(GDB_LOC_ENV_VAR)) != NULL)
	{
		usr_gdb_path = strdup(gdb_path);
		gdb_path = (const char *)usr_gdb_path;
	} else
	{
		gdb_path = _cti_getGdbPath();
		if (gdb_path == NULL)
		{
			_cti_set_error("Required environment variable %s not set.", BASE_DIR_ENV_VAR);
			return 0;
		}
	}
	
	starter_path = _cti_getStarterPath();
	if (starter_path == NULL)
	{
		_cti_set_error("Required environment variable %s not set.", BASE_DIR_ENV_VAR);
		if (usr_gdb_path != NULL)	free(usr_gdb_path);
		return 0;
	}

	if ((myapp = _cti_ssh_newSshInv()) == NULL)
	{
		// error already set
		if (usr_gdb_path != NULL)	free(usr_gdb_path);
		return 0;
	}
	
	// Create a new gdb MPIR instance for holding the launcher and harvesting the MPIR_Proctable
	if ((myapp->gdb_id = _cti_gdb_newInstance()) < 0)
	{
		// error already set
		if (usr_gdb_path != NULL)	free(usr_gdb_path);
		_cti_ssh_consumeSshInv(myapp);
		
		return 0;
	}

	// We don't want the launcher to pass along signals the caller recieves to the
	// application process. In order to stop this from happening we need to put
	// the child into a different process group.
	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);
	sigprocmask(SIG_BLOCK, &mask, &omask);
	
	// Fork off a process to become the MPIR starter
	mypid = fork();
	
	if (mypid < 0)
	{
		_cti_set_error("Fatal fork error.");
		_cti_ssh_consumeSshInv(myapp);
		
		return 0;
	}
	
	// Note that this should not use the _cti_set_error() interface since it is
	// a child process.
	if (mypid == 0)
	{
		const char *	i_file = NULL;
		
		// Set input file if directed
		if (inputFile != NULL)
		{
			i_file = inputFile;
		} else
		{
			i_file = "/dev/null";
		}
		
		// Chdir if directed
		if (chdirPath != NULL)
		{
			if (chdir(chdirPath))
			{
				fprintf(stderr, "CTI error: Unable to chdir to provided path.\n");
				_exit(1);
			}
		}
		
		// If env_list is not null, call putenv for each entry in the list
		if (env_list != NULL)
		{
			for (i=0; env_list[i] != NULL; ++i)
			{
				// putenv returns non-zero on error
				if (putenv(strdup(env_list[i])))
				{
					fprintf(stderr, "CTI error: Unable to putenv provided env_list.\n");
					_exit(1);
				}
			}
		}
		
		// Place this process in its own group to prevent signals being passed
		// to it. This is necessary in case the child code execs before the 
		// parent can put us into our own group. This is so that we won't get
		// the ctrl-c when aprun re-inits the signal handlers.
		setpgid(0, 0);

		char* launcher_name_env;
		char* launcher_name;
		if ((launcher_name_env = getenv(CTI_LAUNCHER_NAME)) != NULL)
		{
			launcher_name = strdup(launcher_name_env);
		}
		else{
			fprintf(stderr, "CTI error: could not get launcher name. Required environment variable %s not set.\n", CTI_LAUNCHER_NAME);
			_exit(1);
		}
		
		// call the exec function - this should not return
		_cti_gdb_execStarter(myapp->gdb_id, starter_path, gdb_path, launcher_name, launcher_argv, i_file);
		
		// exec shouldn't return
		fprintf(stderr, "CTI error: Return from exec.\n");
		perror("execv");
		_exit(1);
	}

	if (usr_gdb_path != NULL)	free(usr_gdb_path);
	
	// Place the child in its own group. We still need to block SIGINT in case
	// its delivered to us before we can do this. We need to do this again here
	// in case this code runs before the child code while we are still blocking 
	// ctrl-c
	setpgid(mypid, mypid);
	
	// save the pid for later so that we can waitpid() on it when finished
	myapp->gdb_pid = mypid;
	
	// Unblock ctrl-c
	sigprocmask(SIG_SETMASK, &omask, NULL);
	
	// Call the post fork setup - this will get us to the startup barrier
	if (_cti_gdb_postFork(myapp->gdb_id))
	{
		// error message already set
		_cti_ssh_consumeSshInv(myapp);
		
		return 0;
	}
	
	// Harvest and process the MPIR_Proctable which holds application layout information
	if ((proctable = _cti_gdb_getProctable(myapp->gdb_id)) == NULL)
	{
		// error already set
		_cti_ssh_consumeSshInv(myapp);
		
		return 0;
	}

	sshLayout_t* layout = _cti_ssh_createLayout(proctable);

	pid_t launcher_pid = _cti_gdb_getLauncherPid(myapp->gdb_id);
	
	// Register this app with the application interface
	if ((rtn = _cti_ssh_registerJob(launcher_pid, false, layout)) == 0)
	{
		// Failed to register the jobid/stepid, error is already set.
		_cti_ssh_consumeSshInv(myapp);
		_cti_gdb_freeProctable(proctable);
		
		return 0;
	}
	
	// Update entries in the application object
	if ((appEntry = _cti_findAppEntry(rtn)) == NULL)
	{
		_cti_set_error("impossible null appEntry error!\n");
		_cti_ssh_consumeSshInv(myapp);
		_cti_gdb_freeProctable(proctable);
		
		return 0;
	}
	
	sinfo = (sshInfo_t *)appEntry->_wlmObj;

	if (sinfo == NULL)
	{
		_cti_set_error("impossible null sinfo error!\n");
		_cti_ssh_consumeSshInv(myapp);
		_cti_gdb_freeProctable(proctable);
		cti_deregisterApp(appEntry->appId);
		
		return 0;
	}
	
	sinfo->inv = myapp;
	
	// Release the application from the startup barrier according to the doBarrier flag
	if (!doBarrier)
	{
		if (_cti_ssh_release(sinfo))
		{
			cti_deregisterApp(appEntry->appId);
			return 0;
		}
	}
	
	return rtn;
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
static cti_app_id_t
_cti_ssh_launch(	const char * const launcher_argv[], int stdout_fd, int stderr_fd,
					const char *inputFile, const char *chdirPath,
					const char * const env_list[]		)
{
	return _cti_ssh_launch_common(launcher_argv, stdout_fd, stderr_fd, inputFile, 
								  chdirPath, env_list, 0);
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
static cti_app_id_t
_cti_ssh_launchBarrier(	const char * const launcher_argv[], int stdout_fd, int stderr_fd,
						const char *inputFile, const char *chdirPath,
						const char * const env_list[]	)
{
	return _cti_ssh_launch_common(launcher_argv, stdout_fd, stderr_fd, inputFile, 
								  chdirPath, env_list, 1);
}

/*
 * _cti_ssh_release - Release an application from its startup barrier
 * 
 *
 * Arguments
 *      app_info - A cti_wlm_obj that represents the info struct for the application
 *
 * Returns
 *      1 on error, 0 on success
 * 
 */
static int
_cti_ssh_release(cti_wlm_obj app_info)
{
	sshInfo_t *	my_app = (sshInfo_t *)app_info;

	// Sanity check the arguments
	if (my_app == NULL)
	{
		_cti_set_error("barrier release operation failed.");
		return 1;
	}
	
	if (my_app->inv == NULL)
	{
		_cti_set_error("barrier release operation failed.");
		return 1;
	}
	
	// Instruct gdb to tell the launcher to release the application from the startup barrier
	if (_cti_gdb_release(my_app->inv->gdb_id))
	{
		_cti_set_error("barrier release operation failed.");
		return 1;
	}
	
	// cleanup the gdb instance, we are done with it. This will release memory
	// and free up the hash table for more possible gdb instances. It is
	// important to do this step here and not later on.
	_cti_gdb_cleanup(my_app->inv->gdb_id);
	my_app->inv->gdb_id = -1;
	
	// wait for the starter to exit
	waitpid(my_app->inv->gdb_pid, NULL, 0);
	my_app->inv->gdb_pid = -1;
	
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
 *      app_info - A cti_wlm_obj that represents the info struct for the application
 *		signum - An int representing the type of signal to send to the application
 *
 * Returns
 *      1 on error, 0 on success
 * 
 */
static int
_cti_ssh_killApp(cti_wlm_obj app_info, int signum)
{
	sshInfo_t *	my_app = (sshInfo_t *)app_info;
	cti_args_t* kill_args;

	//Connect through ssh to each node and send a kill command to every pid on that node
	int i;
	for (i=0; i < my_app->layout->numNodes; ++i)
	{
		ssh_session current_session = _cti_ssh_start_session(my_app->layout->hosts[i].host);
		if(current_session == NULL){
			// Something went wrong with creating the ssh session (error message is already set)
			return 1;
		}

		if ((kill_args = _cti_newArgs()) == NULL)
		{
			_cti_set_error("_cti_newArgs failed.");
			return 1;
		}

		if (_cti_addArg(kill_args, "%s", "kill"))
		{
			_cti_set_error("_cti_addArg failed.");
			return 1;
		}

		if (_cti_addArg(kill_args, "-%d", signum))
		{
			_cti_set_error("_cti_addArg failed.");
			return 1;
		}

		int j;
		for(j=0; j<my_app->layout->hosts[i].PEsHere; j++){

			if (_cti_addArg(kill_args, "%d", my_app->layout->hosts[i].pids[j]))
			{
				_cti_set_error("_cti_addArg failed.");
				return 1;
			}
		}

		if(_cti_ssh_execute_remote_command(current_session, kill_args, NULL)){
			// Something went wrong with the ssh command (error message is already set)
			return 1;
		}

		_cti_ssh_end_session(current_session);
	}

	return 0;
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
 *      app_info - A cti_wlm_obj that represents the info struct for the application
 *
 * Returns
 *      NULL to signify no extra binaries are needed
 * 
 */
static const char * const *
_cti_ssh_extraBinaries(cti_wlm_obj app_info)
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
 *      app_info - A cti_wlm_obj that represents the info struct for the application
 *
 * Returns
 *      NULL to signify no extra libraries are needed
 * 
 */
static const char * const *
_cti_ssh_extraLibraries(cti_wlm_obj app_info)
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
 *      app_info - A cti_wlm_obj that represents the info struct for the application
 *
 * Returns
 *      NULL to signify no extra library directories are needed
 * 
 */
static const char * const *
_cti_ssh_extraLibDirs(cti_wlm_obj app_info)
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
 *      app_info - A cti_wlm_obj that represents the info struct for the application
 *
 * Returns
 *      An array of paths to the two files created containing the path to the layout file
 *		and the path to the pid file
 * 
 */
static const char * const *
_cti_ssh_extraFiles(cti_wlm_obj app_info)
{
	sshInfo_t *		my_app = (sshInfo_t *)app_info;
	const char *			cfg_dir;
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
	if (my_app == NULL){
		_cti_set_error("WLM obj is null.");
		return NULL;
	}
		
	if (my_app->layout == NULL){
		_cti_set_error("sshInfo_t layout is null.");
		return NULL;
	}
	
	// Return the extraFiles array if it has already been created
	if (my_app->extraFiles != NULL)
	{
		return (const char * const *)my_app->extraFiles;
	}
	
	// Check to see if we should create the staging directory
	if (my_app->stagePath == NULL)
	{
		if ((cfg_dir = _cti_getCfgDir()) == NULL)
		{
			_cti_set_error("Could not get CTI configuration directory.");
			return NULL;
		}
		
		// Prepare the path to the stage directory
		if (asprintf(&my_app->stagePath, "%s/%s", cfg_dir, SSH_STAGE_DIR) <= 0)
		{
			_cti_set_error("asprintf failed.");
			my_app->stagePath = NULL;
			return NULL;
		}
		
		// Create the temporary directory for the manifest package
		if (mkdtemp(my_app->stagePath) == NULL)
		{
			_cti_set_error("mkdtemp failed.");
			free(my_app->stagePath);
			my_app->stagePath = NULL;
			return NULL;
		}
	}
	
	// Create layout file in staging directory for writing
	if (asprintf(&layoutPath, "%s/%s", my_app->stagePath, SSH_LAYOUT_FILE) <= 0)
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
	layout_hdr.numNodes = my_app->layout->numNodes;
	
	if (fwrite(&layout_hdr, sizeof(cti_layoutFileHeader_t), 1, myFile) != 1)
	{
		_cti_set_error("Failed to write to %s\n", layoutPath);
		free(layoutPath);
		fclose(myFile);
		return NULL;
	}
	
	for (i=0; i < my_app->layout->numNodes; ++i)
	{
		memcpy(&layout_entry.host[0], my_app->layout->hosts[i].host, sizeof(layout_entry.host));
		layout_entry.PEsHere = my_app->layout->hosts[i].PEsHere;
		layout_entry.firstPE = my_app->layout->hosts[i].firstPE;
		
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
	if (asprintf(&pidPath, "%s/%s", my_app->stagePath, SSH_PID_FILE) <= 0)
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
	pid_hdr.numPids = my_app->layout->numPEs;
	
	if (fwrite(&pid_hdr, sizeof(cti_pidFileheader_t), 1, myFile) != 1)
	{
		_cti_set_error("Failed to write to %s\n", pidPath);
		free(layoutPath);
		free(pidPath);
		fclose(myFile);
		return NULL;
	}

	for (i=0; i < my_app->layout->numNodes; ++i)
	{
		int j;
		for(j=0; j<my_app->layout->hosts[i].PEsHere; j++){
			pid_entry.pid = my_app->layout->hosts[i].pids[j];
			
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
	if ((my_app->extraFiles = calloc(NUM_EXTRA_FILES+1, sizeof(char *))) == NULL)
	{
		_cti_set_error("calloc failed.");
		free(layoutPath);
		return NULL;
	}
	
	my_app->extraFiles[0] = layoutPath;
	my_app->extraFiles[1] = pidPath;
	my_app->extraFiles[2] = NULL;
	
	return (const char * const *)my_app->extraFiles;
}

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
int _cti_ssh_verify_server(ssh_session session)
{
  int state;
  state = _cti_ssh_libssh_funcs.ssh_is_server_known(session);
  switch (state)
  {
    case SSH_SERVER_KNOWN_OK:
      break; /* ok */
    case SSH_SERVER_KNOWN_CHANGED:
      fprintf(stderr, "Host key for server changed: it is now:\n");
      fprintf(stderr, "For security reasons, connection will be stopped\n");
      return 1;
    case SSH_SERVER_FOUND_OTHER:
      fprintf(stderr, "The host key for this server was not found but an other"
        "type of key exists.\n");
      fprintf(stderr, "An attacker might change the default server key to"
        "confuse your client into thinking the key does not exist\n");
      fprintf(stderr, "For security reasons, connection will be stopped\n");
      return 1;
    case SSH_SERVER_FILE_NOT_FOUND:
      /* fallback to SSH_SERVER_NOT_KNOWN behavior */
    case SSH_SERVER_NOT_KNOWN:
      fprintf(stderr,"Warning: backend node not in known_hosts. Updating known_hosts.\n");
      if (_cti_ssh_libssh_funcs.ssh_write_knownhost(session) < 0)
      {
        fprintf(stderr, "Error %s\n", strerror(errno));
        return 1;
      }
      break;
    case SSH_SERVER_ERROR:
      fprintf(stderr, "Error %s", _cti_ssh_libssh_funcs.ssh_get_error(session));
      return 1;
  }
  return 0;
}

/*
 * _cti_ssh_start_session - Start and authenticate an ssh session with a remote host
 *
 * Detail
 *		Starts an ssh session with hostname, verifies the identity of the remote host,
 *		and authenticates the user using the public key method. This is the only supported
 *		ssh authentication method.
 *
 * Arguments
 *		hostname - hostname of remote host to which to connect
 *
 * Returns
 *      An ssh_session which is connected to the remote host and authenticated, or NULL on error
 * 
 */
ssh_session _cti_ssh_start_session(char* hostname)
{
	ssh_session my_ssh_session;
	int rc;
	if ( _cti_ssh_libssh_funcs.handle == NULL){
		if( (_cti_ssh_libssh_funcs.handle = dlopen("libssh.so", RTLD_LAZY)) == NULL){
			_cti_set_error("dlopen failed.");
			return NULL;
		}
		_cti_ssh_libssh_funcs.ssh_channel_close = dlsym(_cti_ssh_libssh_funcs.handle, "ssh_channel_close");
		_cti_ssh_libssh_funcs.ssh_channel_free = dlsym(_cti_ssh_libssh_funcs.handle, "ssh_channel_free");
		_cti_ssh_libssh_funcs.ssh_channel_new = dlsym(_cti_ssh_libssh_funcs.handle, "ssh_channel_new");
		_cti_ssh_libssh_funcs.ssh_channel_open_session = dlsym(_cti_ssh_libssh_funcs.handle, "ssh_channel_open_session");
		_cti_ssh_libssh_funcs.ssh_channel_request_env = dlsym(_cti_ssh_libssh_funcs.handle, "ssh_channel_request_env");
		_cti_ssh_libssh_funcs.ssh_channel_request_exec = dlsym(_cti_ssh_libssh_funcs.handle, "ssh_channel_request_exec");
		_cti_ssh_libssh_funcs.ssh_channel_send_eof = dlsym(_cti_ssh_libssh_funcs.handle, "ssh_channel_send_eof");
		_cti_ssh_libssh_funcs.ssh_connect = dlsym(_cti_ssh_libssh_funcs.handle, "ssh_connect");
		_cti_ssh_libssh_funcs.ssh_disconnect = dlsym(_cti_ssh_libssh_funcs.handle, "ssh_disconnect");
		_cti_ssh_libssh_funcs.ssh_free = dlsym(_cti_ssh_libssh_funcs.handle, "ssh_free");
		_cti_ssh_libssh_funcs.ssh_get_error = dlsym(_cti_ssh_libssh_funcs.handle, "ssh_get_error");
		_cti_ssh_libssh_funcs.ssh_is_server_known = dlsym(_cti_ssh_libssh_funcs.handle, "ssh_is_server_known");
		_cti_ssh_libssh_funcs.ssh_new = dlsym(_cti_ssh_libssh_funcs.handle, "ssh_new");
		_cti_ssh_libssh_funcs.ssh_options_set = dlsym(_cti_ssh_libssh_funcs.handle, "ssh_options_set");
		_cti_ssh_libssh_funcs.ssh_scp_close = dlsym(_cti_ssh_libssh_funcs.handle, "ssh_scp_close");
		_cti_ssh_libssh_funcs.ssh_scp_free = dlsym(_cti_ssh_libssh_funcs.handle, "ssh_scp_free");
		_cti_ssh_libssh_funcs.ssh_scp_init = dlsym(_cti_ssh_libssh_funcs.handle, "ssh_scp_init");
		_cti_ssh_libssh_funcs.ssh_scp_new = dlsym(_cti_ssh_libssh_funcs.handle, "ssh_scp_new");
		_cti_ssh_libssh_funcs.ssh_scp_push_file = dlsym(_cti_ssh_libssh_funcs.handle, "ssh_scp_push_file");
		_cti_ssh_libssh_funcs.ssh_scp_write = dlsym(_cti_ssh_libssh_funcs.handle, "ssh_scp_write");
		_cti_ssh_libssh_funcs.ssh_userauth_publickey_auto = dlsym(_cti_ssh_libssh_funcs.handle, "ssh_userauth_publickey_auto");
		_cti_ssh_libssh_funcs.ssh_write_knownhost = dlsym(_cti_ssh_libssh_funcs.handle, "ssh_write_knownhost");
	}
	
	// Open session and set hostname to which to connect
	my_ssh_session = _cti_ssh_libssh_funcs.ssh_new();
	if (my_ssh_session == NULL){
		_cti_set_error("Error allocating new ssh session: %s\n", _cti_ssh_libssh_funcs.ssh_get_error(my_ssh_session));
		return NULL;
	}
	_cti_ssh_libssh_funcs.ssh_options_set(my_ssh_session, SSH_OPTIONS_HOST, hostname);
	
	// Connect to remote host
	rc = _cti_ssh_libssh_funcs.ssh_connect(my_ssh_session);
	if (rc != SSH_OK)
	{
		_cti_set_error("ssh connection error: %s\n", _cti_ssh_libssh_funcs.ssh_get_error(my_ssh_session));
		_cti_ssh_libssh_funcs.ssh_free(my_ssh_session);
		return NULL;
	}
	
	// Verify the identity of the remote host
	if (_cti_ssh_verify_server(my_ssh_session))
	{
		_cti_set_error("Could not verify backend node identity: %s\n", _cti_ssh_libssh_funcs.ssh_get_error(my_ssh_session));
		_cti_ssh_libssh_funcs.ssh_disconnect(my_ssh_session);
		_cti_ssh_libssh_funcs.ssh_free(my_ssh_session);
		return NULL;
	}
	
	// Authenticate user with the remote host using public key authentication
	rc = _cti_ssh_libssh_funcs.ssh_userauth_publickey_auto(my_ssh_session, NULL, NULL);
	switch(rc)
	{
		case SSH_AUTH_PARTIAL:
		case SSH_AUTH_DENIED:
		case SSH_AUTH_ERROR:
	 		_cti_set_error("Authentication failed: %s. CTI requires paswordless (public key) ssh authentication to the backends. Contact your system administrator about setting this up.\n", _cti_ssh_libssh_funcs.ssh_get_error(my_ssh_session));   
			_cti_ssh_libssh_funcs.ssh_disconnect(my_ssh_session);
			_cti_ssh_libssh_funcs.ssh_free(my_ssh_session);
			return NULL;
			break;
	}

	return my_ssh_session;
}

/*
 * _cti_ssh_execute_remote_command - Execute a command on a remote host through an open ssh session
 *
 * Detail
 *		Executes a command with the specified arguments and environment on the remote host
 *		connected by the specified session.
 *
 * Arguments
 *      ssh_session - 	The ssh session on which the remote host is connected
 *		args - 			cti_args_t which holds the arguments array for the command to be executed
 *		environment - 	A list of environment variables to forward to the backend while executing 
 *						the command or NULL to forward no environment variables
 *
 * Returns
 *      1 on error, 0 on success
 * 
 */
int _cti_ssh_execute_remote_command(ssh_session session, cti_args_t *args, char** environment)
{
	ssh_channel channel;
	int rc;

	// Start a new ssh channel session
	channel = _cti_ssh_libssh_funcs.ssh_channel_new(session);
	if (channel == NULL){
		_cti_set_error("Error allocating ssh channel: %s\n", _cti_ssh_libssh_funcs.ssh_get_error(session));
		return 1;
	}
	
	rc = _cti_ssh_libssh_funcs.ssh_channel_open_session(channel);
	if (rc != SSH_OK)
	{
		_cti_set_error("Error starting session on ssh channel: %s\n", _cti_ssh_libssh_funcs.ssh_get_error(session));
		_cti_ssh_libssh_funcs.ssh_channel_free(channel);
		return 1;
	}

	// Forward environment variables before execution. May not be supported on 
	// all systems if user environments are disabled by the ssh server
	char** current = environment;
	while(current!= NULL && *current != NULL){
		const char* variable_value = getenv(*current);
		if(variable_value != NULL){
			rc = _cti_ssh_libssh_funcs.ssh_channel_request_env(channel, *current, variable_value);
		}

		current++;
	}

	// Request execution of the command on the remote host	
	rc = _cti_ssh_libssh_funcs.ssh_channel_request_exec(channel, _cti_flattenArgs(args));
	if (rc != SSH_OK)
	{
		_cti_set_error("Execution of ssh command failed: %s\n", _cti_ssh_libssh_funcs.ssh_get_error(session));
		_cti_ssh_libssh_funcs.ssh_channel_close(channel);
		_cti_ssh_libssh_funcs.ssh_channel_free(channel);
		return 1;
	}

	// End the channel
	_cti_ssh_libssh_funcs.ssh_channel_send_eof(channel);
	_cti_ssh_libssh_funcs.ssh_channel_close(channel);
	_cti_ssh_libssh_funcs.ssh_channel_free(channel);

	return 0;
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
 *      ssh_session - The ssh session on which the remote host is connected
 *		source_path - A C-string specifying the path to the file to ship
 *		destination_path- A C-string specifying the path of the destination on the remote host
 *		mode- POSIX mode for specifying permissions of new file on remote host
 *
 * Returns
 *      1 on error, 0 on success
 * 
 */
int _cti_ssh_copy_file_to_remote(ssh_session session, const char* source_path, const char* destination_path, 
								int mode)
{
	ssh_scp scp;
	int rc;

	// Start a new scp session
	scp = _cti_ssh_libssh_funcs.ssh_scp_new(session, SSH_SCP_WRITE, _cti_pathToDir(destination_path));
	if (scp == NULL)
	{
		_cti_set_error("Error allocating scp session: %s\n", _cti_ssh_libssh_funcs.ssh_get_error(session));
		return 1;
	}

	rc = _cti_ssh_libssh_funcs.ssh_scp_init(scp);
	if (rc != SSH_OK)
	{
		_cti_set_error("Error initializing scp session: %s\n", _cti_ssh_libssh_funcs.ssh_get_error(session));
		_cti_ssh_libssh_funcs.ssh_scp_free(scp);
		return 1;
	}

	//Get the length of the source file
	int fd = open(source_path, O_RDONLY);
	if (fd == -1) {
		_cti_set_error("Could not open source file for shipping to the backends\n");
		return 1;
	}

	struct stat stbuf;
	  
	if ((fstat(fd, &stbuf) != 0) || (!S_ISREG(stbuf.st_mode))) {
		_cti_set_error("Could not fstat source file for shipping to the backends\n");
		return 1;
	}

	close(fd);
	
	size_t file_size = stbuf.st_size;
	char* relative_destination;
	asprintf(&relative_destination, "/%s", _cti_pathToName(destination_path));

	// Create an empty file with the correct length on the remote host
	rc = _cti_ssh_libssh_funcs.ssh_scp_push_file(scp, relative_destination, file_size, mode);
	if (rc != SSH_OK)
	{
		_cti_set_error("Can't open remote file: %s\n", _cti_ssh_libssh_funcs.ssh_get_error(session));
		return 1;
	}

	// Write the contents of the source file to the destination file in blocks
	int block_size = 1024;
	FILE* source_file = fopen(source_path, "rb");
	if(source_file == NULL){
		_cti_set_error("Could not open source file for shipping to the backends\n");
		return 1;
	}

	size_t current_count = 0;
	char current_block[block_size];
	while( (current_count = fread(current_block, sizeof(char), block_size, source_file)) > 0){
		if( ferror(source_file) )
		{
		  _cti_set_error("Error in reading from file : %s\n", source_path);
		  return 1;
		}
		rc = _cti_ssh_libssh_funcs.ssh_scp_write(scp, current_block, current_count*sizeof(char));
		if (rc != SSH_OK)
		{
			_cti_set_error("Can't write to remote file: %s\n", _cti_ssh_libssh_funcs.ssh_get_error(session));
			return 1;
		}
	}

	_cti_ssh_libssh_funcs.ssh_scp_close(scp);
	_cti_ssh_libssh_funcs.ssh_scp_free(scp);
	return 0;
}

/*
 * _cti_ssh_end_session - End an open ssh session
 *
 * Arguments
 *      ssh_session - The ssh session to be ended
 *
 */
void _cti_ssh_end_session(ssh_session session)
{
	_cti_ssh_libssh_funcs.ssh_disconnect(session);
	_cti_ssh_libssh_funcs.ssh_free(session);
}

/*
 * _cti_ssh_ship_package - Ship the cti manifest package tarball to the backends.
 *
 * Detail
 *		Ships the cti manifest package specified by package to each backend node 
 *		in the application using SSH.
 *
 * Arguments
 *      app_info - A cti_wlm_obj that represents the info struct for the application
 *		package - A C-string specifying the path to the package to ship
 *
 * Returns
 *      1 on error, 0 on success
 * 
 */
static int
_cti_ssh_ship_package(cti_wlm_obj app_info, const char *package)
{
	sshInfo_t *	my_app = (sshInfo_t *)app_info;
	
	// Sanity check the arguments
	if (my_app == NULL)
	{
		_cti_set_error("WLM obj is null!");
		return 1;
	}
	
	if (my_app->layout == NULL)
	{
		_cti_set_error("sshInfo_t layout is null!");
		return 1;
	}
	
	if (package == NULL)
	{
		_cti_set_error("package string is null!");
		return 1;
	}
	
	if (my_app->layout->numNodes <= 0)
	{
		_cti_set_error("No nodes in application");
		return 1;
	}

	// Prepare the destination path for the package on the remote host
	char* destination;
	asprintf(&destination, "%s/%s", SSH_TOOL_DIR, _cti_pathToName(package));

	// Send the package to each of the hosts using SCP
	int i;
	for (i=0; i < my_app->layout->numNodes; ++i)
	{
		ssh_session current_session = _cti_ssh_start_session(my_app->layout->hosts[i].host);
		if(current_session == NULL){
			// Something went wrong with creating the ssh session (error message is already set)
			return 1;
		}
		if(_cti_ssh_copy_file_to_remote(current_session, package, destination, S_IRWXU | S_IRWXG | S_IRWXO)){
			// Something went wrong with the SCP (error message is already set)
			return 1;
		}
		_cti_ssh_end_session(current_session);
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
 *      app_info - A cti_wlm_obj that represents the info struct for the application
 *		args - A cti_args_t object holding the arguments to pass to the daemon
 *
 * Returns
 *      1 on error, 0 on success
 * 
 */
static int
_cti_ssh_start_daemon(cti_wlm_obj app_info, cti_args_t * args)
{
	sshInfo_t *	my_app = (sshInfo_t *)app_info;
	char *				launcher;
	int					i;
	
	// Sanity check the arguments
	if (my_app == NULL)
	{
		_cti_set_error("WLM obj is null!");
		return 1;
	}
	
	if (my_app->layout == NULL)
	{
		_cti_set_error("sshInfo_t layout is null!");
		return 1;
	}
	
	if (args == NULL)
	{
		_cti_set_error("args string is null!");
		return 1;
	}
	
	if (my_app->layout->numNodes <= 0)
	{
		_cti_set_error("Application does not have any nodes.");
		return 1;
	}

	// Transfer the dlaunch binary to the backends if it has not yet been transferred
	if (!my_app->dlaunch_sent)
	{
		const char *	launcher_path;
		if ((launcher_path = _cti_getDlaunchPath()) == NULL)
		{
			_cti_set_error("Required environment variable %s not set.", BASE_DIR_ENV_VAR);
			return 1;
		}
		
		if (_cti_ssh_ship_package(app_info, launcher_path))
		{
			return 1;
		}
		
		my_app->dlaunch_sent = 1;
	}
	
	// Use location of existing launcher binary on compute node
	if (asprintf(&launcher, "%s/%s", my_app->toolPath, CTI_LAUNCHER) <= 0)
	{
		_cti_set_error("asprintf failed.");
		return 1;
	}
	
	// Prepare the launcher arguments
	cti_args_t * my_args;
	if ((my_args = _cti_newArgs()) == NULL)
	{
		_cti_set_error("_cti_newArgs failed.");
		free(launcher);
		return 1;
	}

	if (_cti_addArg(my_args, "%s", launcher))
	{
		_cti_set_error("_cti_addArg failed.");
		free(launcher);
		_cti_freeArgs(my_args);
		return 1;
	}

	if (args != NULL)
	{
		if (_cti_mergeArgs(my_args, args))
		{
			_cti_set_error("_cti_mergeArgs failed.");
			_cti_freeArgs(my_args);
			return 1;
		}
	}

	free(launcher);

	// Execute the launcher on each of the hosts using SSH
	for (i=0; i < my_app->layout->numNodes; ++i)
	{
		ssh_session current_session = _cti_ssh_start_session(my_app->layout->hosts[i].host);
		if(current_session == NULL){
			// Something went wrong with creating the ssh session (error message is already set)
			return 1;
		}
		if(_cti_ssh_execute_remote_command(current_session, my_args, _cti_ssh_forwarded_env_vars)){
			// Something went wrong with the ssh command (error message is already set)
			return 1;
		}
		_cti_ssh_end_session(current_session);
	}	
	
	_cti_freeArgs(my_args);
	
	return 0;
}

/*
 * _cti_ssh_getNumAppPEs - Gets the number of PEs on which the application is running.
 *
 * Arguments
 *      app_info - A cti_wlm_obj that represents the info struct for the application
 *
 * Returns
 *      An int representing the number of PEs on which the application is running
 * 
 */
static int
_cti_ssh_getNumAppPEs(cti_wlm_obj app_info)
{
	sshInfo_t *	my_app = (sshInfo_t *)app_info;
	
	// Sanity check the arguments
	if (my_app == NULL)
	{
		_cti_set_error("getNumAppPEs operation failed.");
		return 0;
	}
	
	if (my_app->layout == NULL)
	{
		_cti_set_error("getNumAppPEs operation failed.");
		return 0;
	}
	
	return my_app->layout->numPEs;
}

/*
 * _cti_ssh_getNumAppNodes - Gets the number of nodes on which the application is running.
 *
 * Arguments
 *      app_info - A cti_wlm_obj that represents the info struct for the application
 *
 * Returns
 *      An int representing the number of nodes on which the application is running
 * 
 */
static int
_cti_ssh_getNumAppNodes(cti_wlm_obj app_info)
{
	sshInfo_t *	my_app = (sshInfo_t *)app_info;
	
	// Sanity check the arguments
	if (my_app == NULL)
	{
		_cti_set_error("getNumAppPEs operation failed.");
		return 0;
	}
	
	if (my_app->layout == NULL)
	{
		_cti_set_error("getNumAppPEs operation failed.");
		return 0;
	}
	
	return my_app->layout->numNodes;
}

/*
 * _cti_ssh_getAppHostsList - Gets a list of hostnames on which the application is running.
 *
 * Arguments
 *      app_info - A cti_wlm_obj that represents the info struct for the application
 *
 * Returns
 *      A NULL terminated array of C-strings representing the list of hostnames
 * 
 */
static char **
_cti_ssh_getAppHostsList(cti_wlm_obj app_info)
{
	sshInfo_t *	my_app = (sshInfo_t *)app_info;
	char **				hosts;
	int					i;
	
	// Sanity check the arguments
	if (my_app == NULL)
	{
		_cti_set_error("getNumAppPEs operation failed.");
		return NULL;
	}
	
	if (my_app->layout == NULL)
	{
		_cti_set_error("getNumAppPEs operation failed.");
		return NULL;
	}
	
	if (my_app->layout->numNodes <= 0)
	{
		_cti_set_error("Application does not have any nodes.");
		return NULL;
	}
	
	// Construct the null termintated hosts list from the internal sshLayout_t representation
	if ((hosts = calloc(my_app->layout->numNodes + 1, sizeof(char *))) == NULL)
	{
		_cti_set_error("calloc failed.");
		return NULL;
	}
	
	for (i=0; i < my_app->layout->numNodes; ++i)
	{
		hosts[i] = strdup(my_app->layout->hosts[i].host);
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
 *      app_info - A cti_wlm_obj that represents the info struct for the application
 *
 * Returns
 *      A pointer to a cti_hostsList_t containing the placement information
 * 
 */
static cti_hostsList_t *
_cti_ssh_getAppHostsPlacement(cti_wlm_obj app_info)
{
	sshInfo_t *	my_app = (sshInfo_t *)app_info;
	cti_hostsList_t *	placement_list;
	int					i;
	
	// Sanity check the arguments
	if (my_app == NULL)
	{
		_cti_set_error("getNumAppPEs operation failed.");
		return NULL;
	}
	
	if (my_app->layout == NULL)
	{
		_cti_set_error("getNumAppPEs operation failed.");
		return NULL;
	}
	
	if (my_app->layout->numNodes <= 0)
	{
		_cti_set_error("Application does not have any nodes.");
		return NULL;
	}
	
	// Construct the cti_hostsList_t output from the internal sshLayout_t representation

	if ((placement_list = malloc(sizeof(cti_hostsList_t))) == NULL)
	{
		_cti_set_error("malloc failed.");
		return NULL;
	}
	
	placement_list->numHosts = my_app->layout->numNodes;
	
	if ((placement_list->hosts = malloc(placement_list->numHosts * sizeof(cti_host_t))) == NULL)
	{
		_cti_set_error("malloc failed.");
		free(placement_list);
		return NULL;
	}

	memset(placement_list->hosts, 0, placement_list->numHosts * sizeof(cti_host_t));
	
	for (i=0; i < my_app->layout->numNodes; ++i)
	{
		placement_list->hosts[i].hostname = strdup(my_app->layout->hosts[i].host);
		placement_list->hosts[i].numPes = my_app->layout->hosts[i].PEsHere;
	}
	
	return placement_list;
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
 *      app_info - A cti_wlm_obj that represents the info struct for the application
 *
 * Returns
 *      A C-string representing the path of the backend staging directory
 * 
 */
static const char *
_cti_ssh_getToolPath(cti_wlm_obj app_info)
{
	sshInfo_t *	my_app = (sshInfo_t *)app_info;
	
	// Sanity check the arguments
	if (my_app == NULL)
	{
		_cti_set_error("getToolPath operation failed.");
		return NULL;
	}
	
	if (my_app->toolPath == NULL)
	{
		_cti_set_error("toolPath app_info missing from sinfo obj!");
		return NULL;
	}

	return (const char *)my_app->toolPath;
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
 *      app_info - A cti_wlm_obj that represents the info struct for the application
 *
 * Returns
 *      NULL to represent the attribs file not being used for this implementation
 * 
 */
static const char *
_cti_ssh_getAttribsPath(cti_wlm_obj app_info)
{
	return NULL;
}

