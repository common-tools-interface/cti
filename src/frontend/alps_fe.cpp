/******************************************************************************\
 * alps_fe.c - alps specific frontend library functions.
 *
 * Copyright 2014-2015 Cray Inc.  All Rights Reserved.
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

#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include <linux/limits.h>

#include "alps/apInfo.h"

#include "cti_defs.h"
#include "cti_fe.h"
#include "cti_error.h"

#include "frontend/Frontend.hpp"
#include "alps_fe.hpp"

#include "useful/cti_useful.h"
#include "useful/Dlopen.hpp"

/* Types used here */

typedef struct
{
	int				nid;		// service node id
} serviceNode_t;

typedef struct
{
	int 			pipe_r;
	int 			pipe_w;
	int 			sync_int;
} barrierCtl_t;

typedef struct
{
	pid_t				aprunPid;
	int					pipeOpen;
	barrierCtl_t		pipeCtl;
	cti_overwatch_t *	o_watch;		// overwatch handler to enforce cleanup
} aprunInv_t;

typedef struct
{
	cti_app_id_t		appId;			// CTI appid associated with this alpsInfo_t obj
	uint64_t			apid;			// ALPS apid
	int					pe0Node;		// ALPS PE0 node id
	appInfo_t			appinfo;		// ALPS application information
	cmdDetail_t *		cmdDetail;		// ALPS application command information (width, depth, memory, command name) of length appinfo.numCmds
	placeNodeList_t *	places;	 		// ALPS application placement information (nid, processors, PE threads) of length appinfo.numPlaces
	aprunInv_t *		inv;			// Optional object used for launched applications.
	char *				toolPath;		// Backend staging directory
	char *				attribsPath;	// Backend directory where pmi_attribs is located
	int					dlaunch_sent;	// True if we have already transfered the dlaunch utility
} alpsInfo_t;

/* static global variables */

static const char * const		_cti_alps_extra_libs[] = {
	ALPS_BE_LIB_NAME,
	NULL
};

static serviceNode_t *		_cti_alps_svcNid	= NULL;	// service node information
static char*				_cti_alps_launcher_name = NULL; //path to the launcher binary

/* dynamically loaded functions from libalps */

class LibALPS {
private: // types
	struct FnTypes {
		using alps_get_apid = uint64_t(int, pid_t);
		using alps_get_appinfo_ver2_err = int(uint64_t, appInfo_t *, cmdDetail_t **, placeNodeList_t **, char **, int *);
		using alps_launch_tool_helper = char*(uint64_t, int, int, int, int, char **);
		using alps_get_overlap_ordinal = int(uint64_t, char **, int *);
	};

private: // variables
	Dlopen::Handle libAlpsHandle;

public: // variables
	std::function<FnTypes::alps_get_apid>             alps_get_apid;
	std::function<FnTypes::alps_get_appinfo_ver2_err> alps_get_appinfo_ver2_err;
	std::function<FnTypes::alps_launch_tool_helper>   alps_launch_tool_helper;
	std::function<FnTypes::alps_get_overlap_ordinal>  alps_get_overlap_ordinal;

public: // interface
	LibALPS()
		: libAlpsHandle(ALPS_FE_LIB_NAME)
		, alps_get_apid(libAlpsHandle.load<FnTypes::alps_get_apid>("alps_get_apid"))
		, alps_get_appinfo_ver2_err(libAlpsHandle.load<FnTypes::alps_get_appinfo_ver2_err>("alps_get_appinfo_ver2_err"))
		, alps_launch_tool_helper(libAlpsHandle.load<FnTypes::alps_launch_tool_helper>("alps_launch_tool_helper"))
		, alps_get_overlap_ordinal(libAlpsHandle.load<FnTypes::alps_get_overlap_ordinal>("alps_get_overlap_ordinal")) {}
};
static const LibALPS libAlps;

/*
*       _cti_alps_getSvcNodeInfo - read nid from alps defined system locations
*
*       args: None.
*
*       return value: serviceNode_t pointer containing the service nodes nid,
*       or else NULL on error.
*
*/
static serviceNode_t *
_cti_alps_getSvcNodeInfo()
{
	FILE *alps_fd;	  // ALPS NID file stream
	char file_buf[BUFSIZ];  // file read buffer
	serviceNode_t *my_node; // return struct containing service node info
	
	// allocate the serviceNode_t object, its the callers responsibility to
	// free this.
	if ((my_node = (decltype(my_node))malloc(sizeof(serviceNode_t))) == (void *)0)
	{
		_cti_set_error("malloc failed.");
		return NULL;
	}
	memset(my_node, 0, sizeof(serviceNode_t));     // clear it to NULL
	
	// open up the file defined in the alps header containing our node id (nid)
	if ((alps_fd = fopen(ALPS_XT_NID, "r")) == NULL)
	{
		_cti_set_error("fopen of %s failed.", ALPS_XT_NID);
		free(my_node);
		return NULL;
	}
	
	// we expect this file to have a numeric value giving our current nid
	if (fgets(file_buf, BUFSIZ, alps_fd) == NULL)
	{
		_cti_set_error("fgets of %s failed.", ALPS_XT_NID);
		free(my_node);
		fclose(alps_fd);
		return NULL;
	}
	// convert this to an integer value
	my_node->nid = atoi(file_buf);
	
	// close the file stream
	fclose(alps_fd);
	
	return my_node;
}

static void
_cti_alps_consumeAprunInv(aprunInv_t *runPtr)
{
	// sanity
	if (runPtr == NULL)
		return;

	// close the open pipe fds
	if (runPtr->pipeOpen)
	{
		close(runPtr->pipeCtl.pipe_r);
		close(runPtr->pipeCtl.pipe_w);
	}
	
	// free the overwatch handler
	if (runPtr->o_watch != NULL)
	{
		_cti_exit_overwatch(runPtr->o_watch);
	}
	
	// free the object from memory
	free(runPtr);
}

static void
_cti_alps_consumeAlpsInfo(alpsInfo_t* alpsInfo)
{
	// sanity check
	if (alpsInfo == NULL)
		return;
	
	// cmdDetail and places were malloc'ed so we might find them tasty
	if (alpsInfo->cmdDetail != NULL)
		free(alpsInfo->cmdDetail);
	if (alpsInfo->places != NULL)
		free(alpsInfo->places);
	
	// try to free the inv object
	if (alpsInfo->inv != NULL)
		_cti_alps_consumeAprunInv(alpsInfo->inv);
	
	// free the toolPath
	if (alpsInfo->toolPath != NULL)
		free(alpsInfo->toolPath);
		
	// free the attribsPath
	if (alpsInfo->attribsPath != NULL)
		free(alpsInfo->attribsPath);
	
	free(alpsInfo);
}

static char *
_cti_alps_getJobId(alpsInfo_t* my_app)
{
	char *			rtn = NULL;
	
	// sanity check
	if (my_app == NULL)
	{
		_cti_set_error("Null wlm obj.");
		return NULL;
	}
	
	if (asprintf(&rtn, "%llu", (long long unsigned int)my_app->apid) <= 0)
	{
		_cti_set_error("asprintf failed.");
		return NULL;
	}
	
	return rtn;
}

static char *
_cti_alps_getLauncherName()
{
	char* launcher_name_env;
	if ((launcher_name_env = getenv(CTI_LAUNCHER_NAME)) != NULL)
	{
		_cti_alps_launcher_name = strdup(launcher_name_env);
	}
	else{
		_cti_alps_launcher_name = APRUN;
	}

	return _cti_alps_launcher_name;
}

// this function creates a new appEntry_t object for the app
// used by the alps_run functions
static alpsInfo_t *
_cti_alps_registerApid(uint64_t apid, Frontend::AppId newAppId)
{
	alpsInfo_t *	alpsInfo;
	char *			toolPath;
	char *			attribsPath;
	// Used to determine CLE version
	struct stat 	statbuf;

	// sanity check
	if (cti_current_wlm() != CTI_WLM_ALPS)
	{
		_cti_set_error("Invalid call. ALPS WLM not in use.");
		return 0;
	}

	// sanity check
	if (apid == 0)
	{
		_cti_set_error("Invalid apid %d.", (int)apid);
		return 0;
	}
	
	// create the new alpsInfo_t object
	if ((alpsInfo = (decltype(alpsInfo))malloc(sizeof(alpsInfo_t))) == NULL)
	{
		_cti_set_error("malloc failed.");
		return 0;
	}
	memset(alpsInfo, 0, sizeof(alpsInfo_t));     // clear it to NULL
	
	// set the apid
	alpsInfo->apid = apid;
	
	// retrieve detailed information about our app
	// save this information into the struct
	char *appinfo_err = NULL;
	if (libAlps.alps_get_appinfo_ver2_err(apid, &alpsInfo->appinfo, &alpsInfo->cmdDetail, &alpsInfo->places, &appinfo_err, nullptr) != 1)
	{
		// dlopen failed and we already set the error string in that case.
		if (appinfo_err != NULL) {
			_cti_set_error("alps_get_appinfo_ver2_err() failed: %s", appinfo_err);
		} else
		{
			_cti_set_error("alps_get_appinfo_ver2_err() failed.");
		}
		_cti_alps_consumeAlpsInfo(alpsInfo);
		return 0;
	}
	
	// Note that cmdDetail is a two dimensional array with appinfo.numCmds elements.
	// Note that places is a two dimensional array with appinfo.numPlaces elements.
	// These both were malloc'ed and need to be free'ed by the user.
	
	// save pe0 NID
	alpsInfo->pe0Node = alpsInfo->places[0].nid;
	
	// Check to see if this system is using the new OBS system for the alps
	// dependencies. This will affect the way we set the toolPath for the backend
	if (stat(ALPS_OBS_LOC, &statbuf) == -1)
	{
		// Could not stat ALPS_OBS_LOC, assume it's using the old format.
		if (asprintf(&toolPath, OLD_TOOLHELPER_DIR, (long long unsigned int)apid, (long long unsigned int)apid) <= 0)
		{
			_cti_set_error("asprintf failed");
			_cti_alps_consumeAlpsInfo(alpsInfo);
			return 0;
		}
		if (asprintf(&attribsPath, OLD_ATTRIBS_DIR, (long long unsigned int)apid) <= 0)
		{
			_cti_set_error("asprintf failed");
			_cti_alps_consumeAlpsInfo(alpsInfo);
			free(toolPath);
			return 0;
		}
	} else
	{
		// Assume it's using the OBS format
		if (asprintf(&toolPath, OBS_TOOLHELPER_DIR, (long long unsigned int)apid, (long long unsigned int)apid) <= 0)
		{
			_cti_set_error("asprintf failed");
			_cti_alps_consumeAlpsInfo(alpsInfo);
			return 0;
		}
		if (asprintf(&attribsPath, OBS_ATTRIBS_DIR, (long long unsigned int)apid) <= 0)
		{
			_cti_set_error("asprintf failed");
			_cti_alps_consumeAlpsInfo(alpsInfo);
			free(toolPath);
			return 0;
		}
	}
	
	// set the appid, tool and attribs path
	alpsInfo->toolPath = toolPath;
	alpsInfo->attribsPath = attribsPath;
	alpsInfo->appId = newAppId;

	return alpsInfo;
}

static uint64_t
_cti_alps_getApid(pid_t aprunPid)
{
	// sanity check
	if (cti_current_wlm() != CTI_WLM_ALPS)
	{
		_cti_set_error("Invalid call. ALPS WLM not in use.");
		return 0;
	}

	// sanity check
	if (aprunPid <= 0)
	{
		_cti_set_error("Invalid pid %d.", (int)aprunPid);
		return 0;
	}
		
	// ensure the _cti_alps_svcNid exists
	if (_cti_alps_svcNid == NULL)
	{
		if ((_cti_alps_svcNid = _cti_alps_getSvcNodeInfo()) == NULL)
		{
			// couldn't get the svcnode info for some odd reason
			// error string already set
			return 0;
		}
	}
	
	return libAlps.alps_get_apid(_cti_alps_svcNid->nid, aprunPid);
}

static ALPSFrontend::AprunInfo *
_cti_alps_getAprunInfo(alpsInfo_t* alpsInfo)
{
	ALPSFrontend::AprunInfo*	aprunInfo;

	// sanity check
	if (alpsInfo == NULL)
	{
		_cti_set_error("cti_alps_getAprunInfo: _wlmObj is NULL!");
		return NULL;
	}
	
	// allocate space for the cti_aprunProc_t struct
	if ((aprunInfo = (decltype(aprunInfo))malloc(sizeof(ALPSFrontend::AprunInfo))) == (void *)0)
	{
		// malloc failed
		_cti_set_error("malloc failed.");
		return NULL;
	}
	
	aprunInfo->apid = alpsInfo->apid;
	aprunInfo->aprunPid = alpsInfo->appinfo.aprunPid;
	
	return aprunInfo;
}

static char *
_cti_alps_getHostName(void)
{
	char *hostname;

	// ensure the _cti_alps_svcNid exists
	if (_cti_alps_svcNid == NULL)
	{
		if ((_cti_alps_svcNid = _cti_alps_getSvcNodeInfo()) == NULL)
		{
			// couldn't get the svcnode info for some odd reason
			// error string already set
			return NULL;
		}
	}
	
	if (asprintf(&hostname, ALPS_XT_HOSTNAME_FMT, _cti_alps_svcNid->nid) < 0)
	{
		_cti_set_error("asprintf failed.");
		return NULL;
	}
	
	return hostname;
}

static char *
_cti_alps_getLauncherHostName(alpsInfo_t* my_app)
{
	char *			hostname;

	// sanity check
	if (my_app == NULL)
	{
		_cti_set_error("getLauncherHostName operation failed.");
		return NULL;
	}
	
	if (asprintf(&hostname, ALPS_XT_HOSTNAME_FMT, my_app->appinfo.aprunNid) < 0)
	{
		_cti_set_error("asprintf failed.");
		return NULL;
	}

	return hostname;
}

static int
_cti_alps_getNumAppPEs(alpsInfo_t* my_app)
{
	int numPEs = 0;
	int i;

	// sanity check
	if (my_app == NULL)
	{
		_cti_set_error("getNumAppPEs operation failed.");
		return 0;
	}
	
	// sanity check
	if (my_app->places == NULL)
	{
		_cti_set_error("getNumAppPEs operation failed.");
		return 0;
	}
	
	// loop through the placement list
	for (i=0; i < my_app->appinfo.numPlaces; ++i)
	{
		numPEs += my_app->places[i].numPEs;
	}
	
	return numPEs;
}

static int
_cti_alps_getNumAppNodes(alpsInfo_t* my_app)
{

	// sanity check
	if (my_app == NULL)
	{
		_cti_set_error("getNumAppNodes operation failed.");
		return 0;
	}
	
	// sanity check
	if (my_app->places == NULL)
	{
		_cti_set_error("getNumAppNodes operation failed.");
		return 0;
	}
	
	return my_app->appinfo.numPlaces;
}

static char **
_cti_alps_getAppHostsList(alpsInfo_t* my_app)
{
	char **			hosts;
	int				i;
	
	// sanity check
	if (my_app == NULL)
	{
		_cti_set_error("getAppHostsList operation failed.");
		return 0;
	}
	
	// sanity check
	if (my_app->places == NULL)
	{
		_cti_set_error("getAppHostsList operation failed.");
		return 0;
	}
	
	// ensure my_app->appinfo.numPlaces is non-zero
	if ( my_app->appinfo.numPlaces <= 0 )
	{
		_cti_set_error("Application %d does not have any nodes.", (int)my_app->apid);
		// no nodes in the application
		return NULL;
	}
	
	// allocate space for the hosts list, add an extra entry for the null terminator
	if ((hosts = (decltype(hosts))calloc(my_app->appinfo.numPlaces + 1, sizeof(char *))) == (void *)0)
	{
		// calloc failed
		_cti_set_error("calloc failed.");
		return NULL;
	}
	memset(hosts, 0, (my_app->appinfo.numPlaces + 1) * sizeof(char *));
	
	// loop through the placement list
	for (i=0; i < my_app->appinfo.numPlaces; ++i)
	{
		if (asprintf(&hosts[i], ALPS_XT_HOSTNAME_FMT, my_app->places[i].nid) <= 0)
		{
			char **tmp = hosts;
			// asprintf failed
			_cti_set_error("asprintf failed.");
			while (*tmp != NULL)
			{
				free(*tmp++);
			}
			free(hosts);
			return NULL;
		}
	}
	
	// done
	return hosts;
}

static cti_hostsList_t *
_cti_alps_getAppHostsPlacement(alpsInfo_t* my_app)
{
	cti_hostsList_t *	placement_list;
	int					i;
	
	// sanity check
	if (my_app == NULL)
	{
		_cti_set_error("getAppHostsPlacement operation failed.");
		return 0;
	}
	
	// sanity check
	if (my_app->places == NULL)
	{
		_cti_set_error("getAppHostsPlacement operation failed.");
		return 0;
	}
	
	// ensure my_app->appinfo.numPlaces is non-zero
	if ( my_app->appinfo.numPlaces <= 0 )
	{
		// no nodes in the application
		_cti_set_error("Application %d does not have any nodes.", (int)my_app->apid);
		return NULL;
	}
	
	// allocate space for the cti_hostsList_t struct
	if ((placement_list = (decltype(placement_list))malloc(sizeof(cti_hostsList_t))) == (void *)0)
	{
		// malloc failed
		_cti_set_error("malloc failed.");
		return NULL;
	}
	
	// set the number of hosts for the application
	placement_list->numHosts = my_app->appinfo.numPlaces;
	
	// allocate space for the cti_host_t structs inside the placement_list
	if ((placement_list->hosts = (decltype(placement_list->hosts))malloc(placement_list->numHosts * sizeof(cti_host_t))) == (void *)0)
	{
		// malloc failed
		_cti_set_error("malloc failed.");
		free(placement_list);
		return NULL;
	}
	// clear the nodeHostPlacment_t memory
	memset(placement_list->hosts, 0, placement_list->numHosts * sizeof(cti_host_t));
	
	// loop through the placement list
	for (i=0; i < placement_list->numHosts; ++i)
	{
		// create the hostname string
		if (asprintf(&placement_list->hosts[i].hostname, ALPS_XT_HOSTNAME_FMT, my_app->places[i].nid) <= 0)
		{
			_cti_set_error("asprintf failed.");
			free(placement_list->hosts);
			free(placement_list);
			return NULL;
		}
		
		// set num PEs
		placement_list->hosts[i].numPEs = my_app->places[i].numPEs;
	}
	
	// done
	return placement_list;
}

static int	
_cti_alps_checkPathForWrappedAprun(char *aprun_path)
{
	char *			usr_aprun_path;
	char *			default_obs_realpath = NULL;
	struct stat		buf;
	
	// The following is used when a user sets the CRAY_APRUN_PATH environment
	// variable to the absolute location of aprun. It overrides the default
	// behavior.
	if ((usr_aprun_path = getenv(USER_DEF_APRUN_LOC_ENV_VAR)) != NULL)
	{
		// There is a path to aprun set, lets try to stat it to make sure it
		// exists
		if (stat(usr_aprun_path, &buf) == 0)
		{
			// We were able to stat it! Lets check aprun_path against it
			if (strncmp(aprun_path, usr_aprun_path, strlen(usr_aprun_path)))
			{
				// This is a wrapper. Return 1.
				return 1;
			}
			
			// This is a real aprun. Return 0.
			return 0;
		} else
		{
			// We were unable to stat the file pointed to by usr_aprun_path, lets
			// print a warning and fall back to using the default method.
			_cti_set_error("%s is set but cannot stat its value.", USER_DEF_APRUN_LOC_ENV_VAR);
		}
	}
	
	// check to see if the path points at the old aprun location
	if (strncmp(aprun_path, OLD_APRUN_LOCATION, strlen(OLD_APRUN_LOCATION)))
	{
		// it doesn't point to the old aprun location, so check the new OBS
		// location. Note that we need to resolve this location with a call to 
		// realpath.
		if ((default_obs_realpath = realpath(OBS_APRUN_LOCATION, NULL)) == NULL)
		{
			// Fix for BUG 810204 - Ensure that the OLD_APRUN_LOCATION exists before giving up.
			if ((default_obs_realpath = realpath(OLD_APRUN_LOCATION, NULL)) == NULL)
			{
				_cti_set_error("Could not resolve realpath of aprun.");
				// FIXME: Assume this is the real aprun...
				return 0;
			}
			// This is a wrapper. Return 1.
			free(default_obs_realpath);
			return 1;
		}
		// Check the string
		if (strncmp(aprun_path, default_obs_realpath, strlen(default_obs_realpath)))
		{
			// This is a wrapper. Return 1.
			free(default_obs_realpath);
			return 1;
		}
		// cleanup
		free(default_obs_realpath);
	}
	
	// This is a real aprun, return 0
	return 0;
}

static int
_cti_alps_filter_pid_entries(const struct dirent *a)
{
	unsigned long int pid;
	
	// We only want to get files that are of the format /proc/<pid>/
	// if the assignment succeeds then the file matches this type.
	return sscanf(a->d_name, "%lu", &pid);
}

/*
 * _cti_alps_set_dsl_env_var: Ensure DSL is enabled for the alps tool helper unless explicitly overridden
 *
 * Detail:
 * 		Sets the environment variable defined in LIBALPS_ENABLE_DSL_ENV_VAR which enables the DSL service
 *		in the alps tool helper. This can be overriden with the environment variable defined by
 *		CTI_LIBALPS_ENABLE_DSL_ENV_VAR. If this environment variable is set to 0, DSL will be disabled.
 *
 * Arguments:
 * 		None
 *
 */
static void 
_cti_alps_set_dsl_env_var()
{
	setenv(LIBALPS_ENABLE_DSL_ENV_VAR, "1", 1);
	char* cti_libalps_enable_dsl = getenv(CTI_LIBALPS_ENABLE_DSL_ENV_VAR);
	if(cti_libalps_enable_dsl != NULL)
	{
		if( strcmp(cti_libalps_enable_dsl,"0") == 0 )
		{
			unsetenv(LIBALPS_ENABLE_DSL_ENV_VAR);
		}		
	}
}

// This is the actual function that can do either a launch with barrier or one
// without.
static alpsInfo_t*
_cti_alps_launch_common(	const char * const launcher_argv[], int stdout_fd, int stderr_fd,
							const char *inputFile, const char *chdirPath,
							const char * const env_list[], int doBarrier, cti_app_id_t newAppId)
{
	aprunInv_t *		myapp;
	alpsInfo_t *		alpsInfo;
	uint64_t 			apid;
	pid_t				mypid;
	cti_args_t *		my_args;
	int					i, fd;
	// pipes for aprun
	int					aprunPipeR[2];
	int					aprunPipeW[2];
	// used for determining if the aprun binary is a wrapper script
	char *				aprun_proc_path = NULL;
	char *				aprun_exe_path;
	struct dirent **	file_list;
	int					file_list_len;
	char *				proc_stat_path = NULL;
	FILE *				proc_stat = NULL;
	int					proc_ppid;
	sigset_t *			mask;

	if(!_cti_is_valid_environment()){
		// error already set
		return 0;
	}

	// create a new aprunInv_t object
	if ((myapp = (decltype(myapp))malloc(sizeof(aprunInv_t))) == (void *)0)
	{
		// Malloc failed
		_cti_set_error("malloc failed.");
		return 0;
	}
	memset(myapp, 0, sizeof(aprunInv_t));     // clear it to NULL
	
	// only do the following if we are using the barrier variant
	if (doBarrier)
	{
		// make the pipes for aprun (tells aprun to hold the program at the initial 
		// barrier)
		if (pipe(aprunPipeR) < 0)
		{
			_cti_set_error("Pipe creation failure on aprunPipeR.");
			_cti_alps_consumeAprunInv(myapp);
			return 0;
		}
		if (pipe(aprunPipeW) < 0)
		{
			_cti_set_error("Pipe creation failure on aprunPipeW.");
			_cti_alps_consumeAprunInv(myapp);
			return 0;
		}
	
		// set my ends of the pipes in the aprunInv_t structure
		myapp->pipeOpen = 1;
		myapp->pipeCtl.pipe_r = aprunPipeR[1];
		myapp->pipeCtl.pipe_w = aprunPipeW[0];
	}
	
	// create the argv array for the actual aprun exec
	
	// create a new args obj
	if ((my_args = _cti_newArgs()) == NULL)
	{
		_cti_set_error("_cti_newArgs failed.");
		_cti_alps_consumeAprunInv(myapp);
		return 0;
	}
	
	// add the initial aprun argv
	if (_cti_addArg(my_args, "%s", _cti_alps_getLauncherName()))
	{
		_cti_set_error("_cti_addArg failed.");
		_cti_alps_consumeAprunInv(myapp);
		_cti_freeArgs(my_args);
		return 0;
	}

	_cti_alps_set_dsl_env_var();
	
	// only do the following if we are using the barrier variant
	if (doBarrier)
	{
		// Add the -P r,w args
		if (_cti_addArg(my_args, "-P"))
		{
			_cti_set_error("_cti_addArg failed.");
			_cti_alps_consumeAprunInv(myapp);
			_cti_freeArgs(my_args);
			return 0;
		}
		if (_cti_addArg(my_args, "%d,%d", aprunPipeW[1], aprunPipeR[0]))
		{
			_cti_set_error("_cti_addArg failed.");
			_cti_alps_consumeAprunInv(myapp);
			_cti_freeArgs(my_args);
			return 0;
		}
	}
	
	// set the rest of the argv for aprun from the passed in args
	if (launcher_argv != NULL)
	{
		for (i=0; launcher_argv[i] != NULL; ++i)
		{
			if (_cti_addArg(my_args, "%s", launcher_argv[i]))
			{
				_cti_set_error("_cti_addArg failed.");
				_cti_alps_consumeAprunInv(myapp);
				_cti_freeArgs(my_args);
				return 0;
			}
		}
	}
	
	// setup signals
	if ((mask = _cti_block_signals()) == NULL)
	{
		_cti_set_error("_cti_block_signals failed.");
		_cti_alps_consumeAprunInv(myapp);
		_cti_freeArgs(my_args);
		return 0;
	}
	
	const char *owatch_path = _cti_getOverwatchPath().c_str();
	if (owatch_path == NULL)
	{
		_cti_set_error("Required environment variable %s not set.", BASE_DIR_ENV_VAR);
		_cti_alps_consumeAprunInv(myapp);
		_cti_freeArgs(my_args);
		_cti_restore_signals(mask);
		return 0;
	}
	
	// setup overwatch to ensure aprun gets killed off on error
	if ((myapp->o_watch = _cti_create_overwatch(owatch_path)) == NULL)
	{
		_cti_set_error("_cti_create_overwatch failed.");
		_cti_alps_consumeAprunInv(myapp);
		_cti_freeArgs(my_args);
		_cti_restore_signals(mask);
		return 0;
	}
	
	// fork off a process to launch aprun
	mypid = fork();
	
	// error case
	if (mypid < 0)
	{
		_cti_set_error("Fatal fork error.");
		_cti_alps_consumeAprunInv(myapp);
		_cti_freeArgs(my_args);
		_cti_restore_signals(mask);
		return 0;
	}
	
	// child case
	// Note that this should not use the _cti_set_error() interface since it is
	// a child process.
	if (mypid == 0)
	{
		// only do the following if we are using the barrier variant
		if (doBarrier)
		{
			// close unused ends of pipe
			close(aprunPipeR[1]);
			close(aprunPipeW[0]);
		}
		
		// redirect stdout/stderr if directed - do this early so that we can
		// print out errors to the proper descriptor.
		if (stdout_fd != -1)
		{
			// dup2 stdout
			if (dup2(stdout_fd, STDOUT_FILENO) < 0)
			{
				// XXX: How to properly print this error? The parent won't be
				// expecting the error message on this stream since dup2 failed.
				fprintf(stderr, "CTI error: Unable to redirect aprun stdout.\n");
				_exit(1);
			}
		}
			
		if (stderr_fd != -1)
		{
			// dup2 stderr
			if (dup2(stderr_fd, STDERR_FILENO) < 0)
			{
				// XXX: How to properly print this error? The parent won't be
				// expecting the error message on this stream since dup2 failed.
				fprintf(stderr, "CTI error: Unable to redirect aprun stderr.\n");
				_exit(1);
			}
		}
		
		if (inputFile != NULL)
		{
			// open the provided input file if non-null and redirect it to
			// stdin
			if ((fd = open(inputFile, O_RDONLY)) < 0)
			{
				fprintf(stderr, "CTI error: Unable to open %s for reading.\n", inputFile);
				_exit(1);
			}
		} else
		{
			// we don't want this aprun to suck up stdin of the tool program
			if ((fd = open("/dev/null", O_RDONLY)) < 0)
			{
				fprintf(stderr, "CTI error: Unable to open /dev/null for reading.\n");
				_exit(1);
			}
		}
		
		// dup2 the fd onto STDIN_FILENO
		if (dup2(fd, STDIN_FILENO) < 0)
		{
			fprintf(stderr, "CTI error: Unable to redirect aprun stdin.\n");
			_exit(1);
		}
		close(fd);
		
		// chdir if directed
		if (chdirPath != NULL)
		{
			if (chdir(chdirPath))
			{
				fprintf(stderr, "CTI error: Unable to chdir to provided path.\n");
				_exit(1);
			}
		}
		
		// if env_list is not null, call putenv for each entry in the list
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
		
		// assign the overwatch process to our pid
		if (_cti_assign_overwatch(myapp->o_watch, getpid()))
		{
			// no way to guarantee cleanup
			fprintf(stderr, "CTI error: _cti_assign_overwatch failed.\n");
			_exit(1);
		}
		
		// restore signals
		if (_cti_child_setpgid_restore(mask))
		{
			// don't fail, but print out an error
			fprintf(stderr, "CTI error: _cti_child_setpgid_restore failed!\n");
		}
		
		// exec aprun
		execvp(_cti_alps_getLauncherName(), my_args->argv);
		
		// exec shouldn't return
		fprintf(stderr, "CTI error: Return from exec.\n");
		perror("execvp");
		_exit(1);
	}
	
	// parent case
	
	// only do the following if we are using the barrier variant
	if (doBarrier)
	{
		// close unused ends of pipe
		close(aprunPipeR[0]);
		close(aprunPipeW[1]);
	}
	
	// cleanup args
	_cti_freeArgs(my_args);
	
	// restore signals
	if (_cti_setpgid_restore(mypid, mask))
	{
		_cti_set_error("_cti_setpgid_restore failed.");
		// attempt to kill aprun since the caller will not recieve the aprun pid
		// just in case the aprun process is still hanging around.
		kill(mypid, DEFAULT_SIG);
		_cti_alps_consumeAprunInv(myapp);
		return 0;
	}
	
	// only do the following if we are using the barrier variant
	if (doBarrier)
	{
		// Wait on pipe read for app to start and get to barrier - once this happens
		// we know the real aprun is up and running
		if (read(myapp->pipeCtl.pipe_w, &myapp->pipeCtl.sync_int, sizeof(myapp->pipeCtl.sync_int)) <= 0)
		{
			_cti_set_error("Control pipe read failed.");
			// attempt to kill aprun since the caller will not recieve the aprun pid
			// just in case the aprun process is still hanging around.
			kill(mypid, DEFAULT_SIG);
			_cti_alps_consumeAprunInv(myapp);
			return 0;
		}
	} else
	{
		// sleep long enough for the forked process to exec itself so that the
		// check for wrapped aprun process doesn't fail.
		sleep(1);
	}
	
	// The following code was added to detect if a site is using a wrapper script
	// around aprun. Some sites use these as prologue/epilogue. I know this
	// functionality has been added to alps, but sites are still using the
	// wrapper. If this is no longer true in the future, rip this stuff out.
	
	// FIXME: This doesn't handle multiple layers of depth.
	
	// first read the link of the exe in /proc for the aprun pid.
	
	// create the path to the /proc/<pid>/exe location
	if (asprintf(&aprun_proc_path, "/proc/%lu/exe", (unsigned long)mypid) < 0)
	{
		_cti_set_error("asprintf failed.");
		goto continue_on_error;
	}
	
	// alloc size for the path buffer, base this on PATH_MAX. Note that /proc
	// is not posix compliant so trying to do the right thing by calling lstat
	// won't work.
	if ((aprun_exe_path = (decltype(aprun_exe_path))malloc(PATH_MAX)) == (void *)0)
	{
		// Malloc failed
		_cti_set_error("malloc failed.");
		free(aprun_proc_path);
		goto continue_on_error;
	}
	// set it to null, this also guarantees that we will have a null terminator.
	memset(aprun_exe_path, 0, PATH_MAX);
	
	// read the link
	if (readlink(aprun_proc_path, aprun_exe_path, PATH_MAX-1) < 0)
	{
		_cti_set_error("readlink failed on aprun %s.", aprun_proc_path);
		free(aprun_proc_path);
		free(aprun_exe_path);
		goto continue_on_error;
	}
	
	// check the link path to see if its the real aprun binary
	if (_cti_alps_checkPathForWrappedAprun(aprun_exe_path))
	{
		// aprun is wrapped, we need to start harvesting stuff out from /proc.
		
		// start by getting all the /proc/<pid>/ files
		if ((file_list_len = scandir("/proc", &file_list, _cti_alps_filter_pid_entries, NULL)) < 0)
		{
			_cti_set_error("Could not enumerate /proc for real aprun process.");
			free(aprun_proc_path);
			free(aprun_exe_path);
			// attempt to kill aprun since the caller will not recieve the aprun pid
			// just in case the aprun process is still hanging around.
			kill(mypid, DEFAULT_SIG);
			_cti_alps_consumeAprunInv(myapp);
			return 0;
		}
		
		// loop over each entry reading in its ppid from its stat file
		for (i=0; i <= file_list_len; ++i)
		{
			// if i is equal to file_list_len, then we are at an error condition
			// we did not find the child aprun process. We should error out at
			// this point since we will error out later in an alps call anyways.
			if (i == file_list_len)
			{
				_cti_set_error("Could not find child aprun process of wrapped aprun command.");
				free(aprun_proc_path);
				free(aprun_exe_path);
				// attempt to kill aprun since the caller will not recieve the aprun pid
				// just in case the aprun process is still hanging around.
				kill(mypid, DEFAULT_SIG);
				_cti_alps_consumeAprunInv(myapp);
				// free the file_list
				for (i=0; i < file_list_len; ++i)
				{
					free(file_list[i]);
				}
				free(file_list);
				return 0;
			}
		
			// create the path to the /proc/<pid>/stat for this entry
			if (asprintf(&proc_stat_path, "/proc/%s/stat", (file_list[i])->d_name) < 0)
			{
				_cti_set_error("asprintf failed.");
				free(aprun_proc_path);
				free(aprun_exe_path);
				// attempt to kill aprun since the caller will not recieve the aprun pid
				// just in case the aprun process is still hanging around.
				kill(mypid, DEFAULT_SIG);
				_cti_alps_consumeAprunInv(myapp);
				// free the file_list
				for (i=0; i < file_list_len; ++i)
				{
					free(file_list[i]);
				}
				free(file_list);
				return 0;
			}
			
			// open the stat file for reading
			if ((proc_stat = fopen(proc_stat_path, "r")) == NULL)
			{
				// ignore this entry and go onto the next
				free(proc_stat_path);
				proc_stat_path = NULL;
				continue;
			}
			
			// free the proc_stat_path
			free(proc_stat_path);
			proc_stat_path = NULL;
			
			// parse the stat file for the ppid
			if (fscanf(proc_stat, "%*d %*s %*c %d", &proc_ppid) != 1)
			{
				// could not get the ppid?? continue to the next entry
				fclose(proc_stat);
				proc_stat = NULL;
				continue;
			}
			
			// close the stat file
			fclose(proc_stat);
			proc_stat = NULL;
			
			// check to see if the ppid matches the pid of our child
			if (proc_ppid == mypid)
			{
				// it matches, check to see if this is the real aprun
				
				// free the existing aprun_proc_path
				free(aprun_proc_path);
				aprun_proc_path = NULL;
				
				// allocate the new aprun_proc_path
				if (asprintf(&aprun_proc_path, "/proc/%s/exe", (file_list[i])->d_name) < 0)
				{
					_cti_set_error("asprintf failed.");
					free(aprun_proc_path);
					free(aprun_exe_path);
					// attempt to kill aprun since the caller will not recieve the aprun pid
					// just in case the aprun process is still hanging around.
					kill(mypid, DEFAULT_SIG);
					_cti_alps_consumeAprunInv(myapp);
					// free the file_list
					for (i=0; i < file_list_len; ++i)
					{
						free(file_list[i]);
					}
					free(file_list);
					return 0;
				}
				
				// reset aprun_exe_path to null.
				memset(aprun_exe_path, 0, PATH_MAX);
				
				// read the exe link to get what its pointing at
				if (readlink(aprun_proc_path, aprun_exe_path, PATH_MAX-1) < 0)
				{
					// if the readlink failed, ignore the error and continue to
					// the next entry. Its possible that this could fail under
					// certain scenarios like the process is running as root.
					continue;
				}
				
				// check if this is the real aprun
				if (!_cti_alps_checkPathForWrappedAprun(aprun_exe_path))
				{
					// success! This is the real aprun
					// set the aprunPid of the real aprun in the aprunInv_t structure
					myapp->aprunPid = (pid_t)strtoul((file_list[i])->d_name, NULL, 10);
					
					// cleanup memory
					free(aprun_proc_path);
					free(aprun_exe_path);
					
					// free the file_list
					for (i=0; i < file_list_len; ++i)
					{
						free(file_list[i]);
					}
					free(file_list);
					// done
					break;
				}
			}
		}
	} else
	{
		// cleanup memory
		free(aprun_proc_path);
		free(aprun_exe_path);
	
continue_on_error:
		// set aprunPid in aprunInv_t structure
		myapp->aprunPid = mypid;
	}
	
	// set the apid associated with the pid of aprun
	if ((apid = cti_alps_getApid(myapp->aprunPid)) == 0)
	{
		_cti_set_error("Could not obtain apid associated with pid of aprun.");
		// attempt to kill aprun since the caller will not recieve the aprun pid
		// just in case the aprun process is still hanging around.
		kill(myapp->aprunPid, DEFAULT_SIG);
		_cti_alps_consumeAprunInv(myapp);
		return 0;
	}
	
	// register this app with the application interface
	if ((alpsInfo = _cti_alps_registerApid(apid, newAppId)) == nullptr)
	{
		// failed to register apid, error is already set
		kill(myapp->aprunPid, DEFAULT_SIG);
		_cti_alps_consumeAprunInv(myapp);
		return 0;
	}

	// set the inv
	alpsInfo->inv = myapp;
	
	// return the alpsInfo_t
	return alpsInfo;
}

static alpsInfo_t*
_cti_alps_launch(	const char * const a1[], int a2, int a3, const char *a4,
					const char *a5, const char * const a6[], cti_app_id_t newAppId)
{
	// call the common launch function
	return _cti_alps_launch_common(a1, a2, a3, a4, a5, a6, 0, newAppId);
}

static alpsInfo_t*
_cti_alps_launchBarrier(	const char * const a1[], int a2, int a3, const char *a4,
							const char *a5, const char * const a6[], cti_app_id_t newAppId)
{
	// call the common launch function
	return _cti_alps_launch_common(a1, a2, a3, a4, a5, a6, 1, newAppId);
}

int
_cti_alps_releaseBarrier(alpsInfo_t* my_app)
{
	// sanity check
	if (my_app == NULL)
	{
		_cti_set_error("Aprun barrier release operation failed.");
		return 1;
	}
	
	// sanity check
	if (my_app->inv == NULL)
	{
		_cti_set_error("Aprun barrier release operation failed.");
		return 1;
	}
	
	// Conduct a pipe write for alps to release app from the startup barrier.
	// Just write back what we read earlier.
	if (write(my_app->inv->pipeCtl.pipe_r, &my_app->inv->pipeCtl.sync_int, sizeof(my_app->inv->pipeCtl.sync_int)) <= 0)
	{
		_cti_set_error("Aprun barrier release operation failed.");
		return 1;
	}
	
	// done
	return 0;
}

static int
_cti_alps_killApp(alpsInfo_t* my_app, int signum)
{
	cti_args_t *	my_args;
	int				mypid;
	
	// sanity check
	if (my_app == NULL)
	{
		_cti_set_error("Aprun kill operation failed.");
		return 1;
	}
	
	// create a new args obj
	if ((my_args = _cti_newArgs()) == NULL)
	{
		_cti_set_error("_cti_newArgs failed.");
		return 1;
	}
	
	// first argument should be "apkill"
	if (_cti_addArg(my_args, "%s", APKILL))
	{
		_cti_set_error("_cti_addArg failed.");
		_cti_freeArgs(my_args);
		return 1;
	}
	
	// second argument is -signum
	if (_cti_addArg(my_args, "-%d", signum))
	{
		_cti_set_error("_cti_addArg failed.");
		_cti_freeArgs(my_args);
		return 1;
	}
	
	// third argument is apid
	if (_cti_addArg(my_args, "%llu", (long long unsigned int)my_app->apid))
	{
		_cti_set_error("_cti_addArg failed.");
		_cti_freeArgs(my_args);
		return 1;
	}
	
	// fork off a process to launch apkill
	mypid = fork();
	
	// error case
	if (mypid < 0)
	{
		_cti_set_error("Fatal fork error.");
		_cti_freeArgs(my_args);
		return 1;
	}
	
	// child case
	if (mypid == 0)
	{
		// exec apkill
		execvp(APKILL, my_args->argv);
		
		// exec shouldn't return
		fprintf(stderr, "CTI error: Return from exec.\n");
		perror("execvp");
		_exit(1);
	}
	
	// parent case
	
	// cleanup
	_cti_freeArgs(my_args);
	
	// wait until the apkill finishes
	waitpid(mypid, NULL, 0);
	
	return 0;
}

#define LAUNCH_TOOL_RETRY 5

static int
_cti_alps_ship_package(alpsInfo_t* my_app, const char *package)
{
	const char *	errmsg = NULL;					// errmsg that is possibly returned by call to alps_launch_tool_helper
	char *			p = (char *)package;	// discard const qualifier because alps isn't const correct
	
	// sanity check
	if (my_app == NULL)
	{
		_cti_set_error("WLM obj is null!");
		return 1;
	}
	
	// sanity check
	if (package == NULL)
	{
		_cti_set_error("package string is null!");
		return 1;
	}
	
	// now ship the tarball to the compute node - discard const qualifier because alps isn't const correct
	size_t checks = 0; // problem on crystal where alps_launch_tool_helper will report bad apid
	fflush(stderr); // suppress stderr for "gzip: broken pipe"
	int saved_stderr = dup(STDERR_FILENO);
	int new_stderr = open("/dev/null", O_WRONLY);
	dup2(new_stderr, STDERR_FILENO);
	close(new_stderr);
	while ((++checks < LAUNCH_TOOL_RETRY) && ((errmsg = libAlps.alps_launch_tool_helper(my_app->apid, my_app->pe0Node, 1, 0, 1, &p)) != NULL))
	{
		usleep(500000);
	}
	fflush(stderr);
	dup2(saved_stderr, STDERR_FILENO);
	close(saved_stderr);
	
	if (errmsg != NULL) {
		// we failed to ship the file to the compute nodes for some reason - catastrophic failure
		//
		// If we were ready, then set the error message. Otherwise we assume that
		// dlopen failed and we already set the error string in that case.
		_cti_set_error("alps_launch_tool_helper error: %s", errmsg);
		return 1;
	}
	
	return 0;
}

static int
_cti_alps_start_daemon(alpsInfo_t* my_app, cti_args_t * args)
{
	int				do_transfer;
	const char *	errmsg;				// errmsg that is possibly returned by call to alps_launch_tool_helper
	char *			launcher;
	char *			args_flat;
	char *			a;
	
	// sanity check
	if (my_app == NULL)
	{
		_cti_set_error("WLM obj is null!");
		return 1;
	}
	
	// sanity check
	if (args == NULL)
	{
		_cti_set_error("args string is null!");
		return 1;
	}
	
	// Create the launcher path based on the value of dlaunch_sent in alpsInfo_t. If this is
	// false, we have not yet transfered the dlaunch utility to the compute nodes, so we need
	// to find the location of it on our end and have alps transfer it.
	do_transfer = my_app->dlaunch_sent ? 0:1;
	if (do_transfer)
	{
		// Need to transfer launcher binary
		
		// Get the location of the daemon launcher
		if (_cti_getDlaunchPath().empty())
		{
			_cti_set_error("Required environment variable %s not set.", BASE_DIR_ENV_VAR);
			return 1;
		}
		launcher = strdup(_cti_getDlaunchPath().c_str());
	} else
	{
		// use existing launcher binary on compute node
		if (asprintf(&launcher, "%s/%s", my_app->toolPath, CTI_LAUNCHER) <= 0)
		{
			_cti_set_error("asprintf failed.");
			return 1;
		}
	}
	
	// get the flattened args string since alps needs to use that
	if ((args_flat = _cti_flattenArgs(args)) == NULL)
	{
		_cti_set_error("_cti_flattenArgs failed.");
		free(launcher);
		return 1;
	}
	
	// create the new args string
	if (asprintf(&a, "%s %s", launcher, args_flat) <= 0)
	{
		_cti_set_error("asprintf failed.");
		free(launcher);
		free(args_flat);
		return 1;
	}
	free(launcher);
	free(args_flat);
	
	// launch the tool daemon onto the compute nodes
	if ((errmsg = libAlps.alps_launch_tool_helper(my_app->apid, my_app->pe0Node, do_transfer, 1, 1, &a)) != NULL)
	{
		// we failed to launch the launcher on the compute nodes for some reason - catastrophic failure
		//
		// If we were ready, then set the error message. Otherwise we assume that
		// dlopen failed and we already set the error string in that case.
		_cti_set_error("alps_launch_tool_helper error: %s", errmsg);
		free(a);
		return 1;
	}
	free(a);
	
	if (do_transfer)
	{
		// set transfer value in my_app to true
		my_app->dlaunch_sent = 1;
	}
	
	return 0;
}

static int
_cti_alps_getAlpsOverlapOrdinal(alpsInfo_t* my_app)
{
	char *			errMsg = NULL;
	int				rtn;
	
	if (my_app == NULL)
	{
		_cti_set_error("cti_alps_getAlpsOverlapOrdinal: _wlmObj is NULL!");
		return -1;
	}
	
	rtn = libAlps.alps_get_overlap_ordinal(my_app->apid, &errMsg, NULL);
	if (rtn < 0)
	{
		if (errMsg != NULL)
		{
			_cti_set_error("%s", errMsg);
		} else
		{
			_cti_set_error("cti_alps_getAlpsOverlapOrdinal: Unknown alps_get_overlap_ordinal failure");
		}
	}
	
	return rtn;
}

static const char *
_cti_alps_getToolPath(alpsInfo_t* my_app)
{
	
	// sanity check
	if (my_app == NULL)
	{
		_cti_set_error("getToolPath operation failed.");
		return NULL;
	}
	
	// sanity check
	if (my_app->toolPath == NULL)
	{
		_cti_set_error("toolPath info missing from alps info obj!");
		return NULL;
	}

	return (const char *)my_app->toolPath;
}

static const char *
_cti_alps_getAttribsPath(alpsInfo_t* my_app)
{
	// sanity check
	if (my_app == NULL)
	{
		_cti_set_error("getAttribsPath operation failed.");
		return NULL;
	}
	
	// sanity check
	if (my_app->attribsPath == NULL)
	{
		_cti_set_error("attribsPath info missing from alps info obj!");
		return NULL;
	}

	return (const char *)my_app->attribsPath;
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

std::unordered_map<AppId, std::unique_ptr<alpsInfo_t>> appList;
static const AppId APP_ERROR = 0;
static AppId newAppId() noexcept {
	static AppId nextId = 1;
	return nextId++;
}

static alpsInfo_t*
getInfoPtr(AppId appId) {
	auto infoPtr = appList.find(appId);
	if (infoPtr != appList.end()) {
		return infoPtr->second.get();
	}

	throw std::runtime_error("invalid appId: " + std::to_string(appId));
}

bool
ALPSFrontend::appIsValid(AppId appId) const {
	return appList.find(appId) != appList.end();
}

void
ALPSFrontend::deregisterApp(AppId appId) const {
	appList.erase(appId);
}

cti_wlm_type
ALPSFrontend::getWLMType() const {
	return CTI_WLM_ALPS;
}

std::string
const ALPSFrontend::getJobId(AppId appId) const {
	return _cti_alps_getJobId(getInfoPtr(appId));
}

AppId
ALPSFrontend::launch(CArgArray launcher_argv, int stdout_fd, int stderr,
                     CStr inputFile, CStr chdirPath, CArgArray env_list) {
	auto const appId = newAppId();
	if (auto alpsInfoPtr = _cti_alps_launch(launcher_argv, stdout_fd, stderr, inputFile, chdirPath, env_list, appId)) {
		appList[appId] = std::unique_ptr<alpsInfo_t>(alpsInfoPtr);
		return appId;
	} else {
		throw std::runtime_error(std::string("launch: ") + cti_error_str());
	}
}

AppId
ALPSFrontend::launchBarrier(CArgArray launcher_argv, int stdout_fd, int stderr,
                            CStr inputFile, CStr chdirPath, CArgArray env_list) {
	auto const appId = newAppId();
	if (auto alpsInfoPtr = _cti_alps_launchBarrier(launcher_argv, stdout_fd, stderr, inputFile, chdirPath, env_list, appId)) {
		appList[appId] = std::unique_ptr<alpsInfo_t>(alpsInfoPtr);
		return appId;
	} else {
		throw std::runtime_error(std::string("launchBarrier: ") + cti_error_str());
	}
}

void
ALPSFrontend::releaseBarrier(AppId appId) {
	if (_cti_alps_releaseBarrier(getInfoPtr(appId))) {
		throw std::runtime_error(std::string("releaseBarrier: ") + cti_error_str());
	}
}

void
ALPSFrontend::killApp(AppId appId, int signal) {
	if (_cti_alps_killApp(getInfoPtr(appId), signal)) {
		throw std::runtime_error(std::string("releaseBarrier: ") + cti_error_str());
	}
}


std::vector<std::string> const
ALPSFrontend::getExtraLibraries() const {
	std::vector<std::string> result;
	for (const char* const* libPath = _cti_alps_extra_libs; *libPath != nullptr; libPath++) {
		result.emplace_back(*libPath);
	}
	return result;
}


void
ALPSFrontend::shipPackage(AppId appId, std::string const& tarPath) const {
	if (_cti_alps_ship_package(getInfoPtr(appId), tarPath.c_str())) {
		throw std::runtime_error(std::string("shipPackage: ") + cti_error_str());
	}
}

void
ALPSFrontend::startDaemon(AppId appId, CArgArray argv) const {
	auto cti_argv = _cti_newArgs();
	for (const char* const* arg = argv; *arg != nullptr; arg++) {
		_cti_addArg(cti_argv, *arg);
	}
	if (_cti_alps_start_daemon(getInfoPtr(appId), cti_argv)) {
		_cti_freeArgs(cti_argv);
		throw std::runtime_error(std::string("startDaemon: ") + cti_error_str());
	}
	_cti_freeArgs(cti_argv);
}


size_t
ALPSFrontend::getNumAppPEs(AppId appId) const {
	if (auto numAppPEs = _cti_alps_getNumAppPEs(getInfoPtr(appId))) {
		return numAppPEs;
	} else {
		throw std::runtime_error(std::string("getNumAppPEs: ") + cti_error_str());
	}
}

size_t
ALPSFrontend::getNumAppNodes(AppId appId) const {
	if (auto numAppNodes = _cti_alps_getNumAppNodes(getInfoPtr(appId))) {
		return numAppNodes;
	} else {
		throw std::runtime_error(std::string("getNumAppPEs: ") + cti_error_str());
	}
}

std::vector<std::string> const
ALPSFrontend::getAppHostsList(AppId appId) const {
	if (char** appHostsList = _cti_alps_getAppHostsList(getInfoPtr(appId))) {
		std::vector<std::string> result;
		for (char** host = appHostsList; *host != nullptr; host++) {
			result.emplace_back(*host);
			free(*host);
		}
		free(appHostsList);
		return result;
	} else {
		throw std::runtime_error(std::string("getNumAppPEs: ") + cti_error_str());
	}
}

std::vector<CTIHost> const
ALPSFrontend::getAppHostsPlacement(AppId appId) const {
	if (auto appHostsPlacement = _cti_alps_getAppHostsPlacement(getInfoPtr(appId))) {
		std::vector<CTIHost> result;
		for (int i = 0; i < appHostsPlacement->numHosts; i++) {
			result.emplace_back(appHostsPlacement->hosts[i].hostname, appHostsPlacement->hosts[i].numPEs);
		}
		cti_destroyHostsList(appHostsPlacement);
		return result;
	} else {
		throw std::runtime_error(std::string("getAppHostsPlacement: ") + cti_error_str());
	}
}

std::string const
ALPSFrontend::getHostName(void) const {
	if (auto hostname = _cti_alps_getHostName()) {
		return hostname;
	} else {
		throw std::runtime_error(std::string("getHostName: ") + cti_error_str());
	}
}

std::string const
ALPSFrontend::getLauncherHostName(AppId appId) const {
	if (auto launcherHostname = _cti_alps_getLauncherHostName(getInfoPtr(appId))) {
		return launcherHostname;
	} else {
		throw std::runtime_error(std::string("getLauncherHostName: ") + cti_error_str());
	}
}

std::string const
ALPSFrontend::getToolPath(AppId appId) const {
	if (auto toolPath = _cti_alps_getToolPath(getInfoPtr(appId))) {
		return toolPath;
	} else {
		throw std::runtime_error(std::string("getToolPath: ") + cti_error_str());
	}
}

std::string const
ALPSFrontend::getAttribsPath(AppId appId) const {
	if (auto attribsPath = _cti_alps_getAttribsPath(getInfoPtr(appId))) {
		return attribsPath;
	} else {
		throw std::runtime_error(std::string("getAttribsPath: ") + cti_error_str());
	}
}

/* extended frontend implementation */

ALPSFrontend::ALPSFrontend() {
	
}

ALPSFrontend::~ALPSFrontend() {
	
}

AppId
ALPSFrontend::registerApid(uint64_t apid) {
	// iterate through the _cti_alps_info list to try to find an existing entry for this apid
	for (auto const& appIdInfoPair : appList) {
		if (appIdInfoPair.second->apid == apid) {
			return appIdInfoPair.first;
		}
	}

	// aprun pid not found in the global _cti_alps_info list
	// so lets create a new appEntry_t object for it
	auto appId = newAppId();
	if (auto alpsInfoPtr = _cti_alps_registerApid(apid, appId)) {
		appList[appId] = std::unique_ptr<alpsInfo_t>(alpsInfoPtr);
		return appId;
	} else {
		throw std::runtime_error(std::string("registerApid: ") + cti_error_str());
	}
}

uint64_t
ALPSFrontend::getApid(pid_t appPid) {
	if (auto apid = _cti_alps_getApid(appPid)) {
		return apid;
	} else {
		throw std::runtime_error(std::string("getApid: ") + cti_error_str());
	}
}

ALPSFrontend::AprunInfo*
ALPSFrontend::getAprunInfo(AppId appId) {
	if (auto aprunInfo = _cti_alps_getAprunInfo(getInfoPtr(appId))) {
		return aprunInfo;
	} else {
		throw std::runtime_error(std::string("getAprunInfo: ") + cti_error_str());
	}
}

int
ALPSFrontend::getAlpsOverlapOrdinal(AppId appId) {
	if (auto alpsOverlapOrdinal = _cti_alps_getAlpsOverlapOrdinal(getInfoPtr(appId))) {
		return alpsOverlapOrdinal;
	} else {
		throw std::runtime_error(std::string("getAlpsOverlapOrdinal: ") + cti_error_str());
	}
}

