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
#include <dlfcn.h>
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

#include "alps_fe.h"
#include "cti_fe.h"
#include "cti_defs.h"
#include "cti_error.h"
#include "cti_useful.h"

/* Types used here */

typedef struct
{
	void *			handle;
	uint64_t    	(*alps_get_apid)(int, pid_t);
	int				(*alps_get_appinfo_ver2_err)(uint64_t, appInfo_t *, cmdDetail_t **, placeNodeList_t **, char **, int *);
	const char *	(*alps_launch_tool_helper)(uint64_t, int, int, int, int, char **);
	int				(*alps_get_overlap_ordinal)(uint64_t, char **, int *);
} cti_alps_funcs_t;

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

/* Static prototypes */
static int					_cti_alps_init(void);
static void					_cti_alps_fini(void);
static char *				_cti_alps_getJobId(cti_wlm_obj);
static cti_app_id_t			_cti_alps_launch_common(const char * const [], int, int, const char *, const char *, const char * const [], int);
static cti_app_id_t			_cti_alps_launch(const char * const [], int, int, const char *, const char *, const char * const []);
static cti_app_id_t			_cti_alps_launchBarrier(const char * const [], int, int, const char *, const char *, const char * const []);
static int					_cti_alps_releaseBarrier(cti_wlm_obj);
static int					_cti_alps_killApp(cti_wlm_obj, int);
static const char * const *	_cti_alps_extraBinaries(cti_wlm_obj);
static const char * const *	_cti_alps_extraLibraries(cti_wlm_obj);
static const char * const *	_cti_alps_extraLibDirs(cti_wlm_obj);
static const char * const *	_cti_alps_extraFiles(cti_wlm_obj);
static int					_cti_alps_ship_package(cti_wlm_obj, const char *);
static int					_cti_alps_start_daemon(cti_wlm_obj, cti_args_t *);
static int					_cti_alps_getNumAppPEs(cti_wlm_obj);
static int					_cti_alps_getNumAppNodes(cti_wlm_obj);
static char **				_cti_alps_getAppHostsList(cti_wlm_obj);
static cti_hostsList_t *	_cti_alps_getAppHostsPlacement(cti_wlm_obj);
static char *				_cti_alps_getHostName(void);
static char *				_cti_alps_getLauncherHostName(cti_wlm_obj);
static int					_cti_alps_ready(void);
static uint64_t				_cti_alps_get_apid(int, pid_t);
static int					_cti_alps_get_appinfo_ver2_err(uint64_t, appInfo_t *, cmdDetail_t **, placeNodeList_t **, char **);
static const char *			_cti_alps_launch_tool_helper(uint64_t, int, int, int, int, char **);
static int					_cti_alps_get_overlap_ordinal(uint64_t, char **, int *);
static void					_cti_alps_consumeAlpsInfo(cti_wlm_obj);
static void 				_cti_alps_consumeAprunInv(aprunInv_t *);
static serviceNode_t *		_cti_alps_getSvcNodeInfo(void);
static int					_cti_alps_checkPathForWrappedAprun(char *);
static int					_cti_alps_filter_pid_entries(const struct dirent *);
static const char *			_cti_alps_getToolPath(cti_wlm_obj);
static const char *			_cti_alps_getAttribsPath(cti_wlm_obj);

/* alps wlm proto object */
const cti_wlm_proto_t		_cti_alps_wlmProto =
{
	CTI_WLM_ALPS,					// wlm_type
	_cti_alps_init,					// wlm_init
	_cti_alps_fini,					// wlm_fini
	_cti_alps_consumeAlpsInfo,		// wlm_destroy
	_cti_alps_getJobId,				// wlm_getJobId
	_cti_alps_launch,				// wlm_launch
	_cti_alps_launchBarrier,		// wlm_launchBarrier
	_cti_alps_releaseBarrier,		// wlm_releaseBarrier
	_cti_alps_killApp,				// wlm_killApp
	_cti_alps_extraBinaries,		// wlm_extraBinaries
	_cti_alps_extraLibraries,		// wlm_extraLibraries
	_cti_alps_extraLibDirs,			// wlm_extraLibDirs
	_cti_alps_extraFiles,			// wlm_extraFiles
	_cti_alps_ship_package,			// wlm_shipPackage
	_cti_alps_start_daemon,			// wlm_startDaemon
	_cti_alps_getNumAppPEs,			// wlm_getNumAppPEs
	_cti_alps_getNumAppNodes,		// wlm_getNumAppNodes
	_cti_alps_getAppHostsList,		// wlm_getAppHostsList
	_cti_alps_getAppHostsPlacement,	// wlm_getAppHostsPlacement
	_cti_alps_getHostName,			// wlm_getHostName
	_cti_alps_getLauncherHostName,	// wlm_getLauncherHostName
	_cti_alps_getToolPath,			// wlm_getToolPath
	_cti_alps_getAttribsPath		// wlm_getAttribsPath
};

/* static global variables */

static const char * const		_cti_alps_extra_libs[] = {
	ALPS_BE_LIB_NAME,
	NULL
};

static cti_list_t *			_cti_alps_info		= NULL;	// list of alpsInfo_t objects registered by this interface
static cti_alps_funcs_t *	_cti_alps_ptr 		= NULL;	// libalps wrappers
static serviceNode_t *		_cti_alps_svcNid	= NULL;	// service node information
static char*				_cti_alps_launcher_name = NULL; //path to the launcher binary

/* Constructor/Destructor functions */

static int
_cti_alps_init(void)
{
	char *error;
	
	// create a new _cti_alps_info list
	if (_cti_alps_info == NULL)
		_cti_alps_info = _cti_newList();

	// Only init once.
	if (_cti_alps_ptr != NULL)
		return 0;
			
	// Create a new cti_alps_funcs_t
	if ((_cti_alps_ptr = malloc(sizeof(cti_alps_funcs_t))) == NULL)
	{
		_cti_set_error("malloc failed.");
		return 1;
	}
	memset(_cti_alps_ptr, 0, sizeof(cti_alps_funcs_t));     // clear it to NULL
	
	if ((_cti_alps_ptr->handle = dlopen(ALPS_FE_LIB_NAME, RTLD_LAZY)) == NULL)
	{
		_cti_set_error("dlopen: %s", dlerror());
		free(_cti_alps_ptr);
		_cti_alps_ptr = NULL;
		return 1;
	}
	
	// Clear any existing error
	dlerror();
	
	// load alps_get_apid
	_cti_alps_ptr->alps_get_apid = dlsym(_cti_alps_ptr->handle, "alps_get_apid");
	if ((error = dlerror()) != NULL)
	{
		_cti_set_error("dlsym: %s", error);
		dlclose(_cti_alps_ptr->handle);
		free(_cti_alps_ptr);
		_cti_alps_ptr = NULL;
		return 1;
	}
	
	// load alps_get_appinfo_ver2_err
	_cti_alps_ptr->alps_get_appinfo_ver2_err = dlsym(_cti_alps_ptr->handle, "alps_get_appinfo_ver2_err");
	if ((error = dlerror()) != NULL)
	{
		_cti_set_error("dlsym: %s", error);
		dlclose(_cti_alps_ptr->handle);
		free(_cti_alps_ptr);
		_cti_alps_ptr = NULL;
		return 1;
	}
	
	// load alps_launch_tool_helper
	_cti_alps_ptr->alps_launch_tool_helper = dlsym(_cti_alps_ptr->handle, "alps_launch_tool_helper");
	if ((error = dlerror()) != NULL)
	{
		_cti_set_error("dlsym: %s", error);
		dlclose(_cti_alps_ptr->handle);
		free(_cti_alps_ptr);
		_cti_alps_ptr = NULL;
		return 1;
	}
	
	// load alps_get_overlap_ordinal
	// XXX: It is alright for this load to fail as some versions of libalps
	//       will be missing this function. In that case, we should always
	//       return error
	_cti_alps_ptr->alps_get_overlap_ordinal = dlsym(_cti_alps_ptr->handle, "alps_get_overlap_ordinal");
	
	// done
	return 0;
}

static void
_cti_alps_fini(void)
{
	if (_cti_alps_info != NULL)
		_cti_consumeList(_cti_alps_info, NULL);	// this should have already been cleared out.

	if (_cti_alps_ptr != NULL)
	{
		// cleanup
		dlclose(_cti_alps_ptr->handle);
		free(_cti_alps_ptr);
		_cti_alps_ptr = NULL;
	}
}

/* dlopen related wrappers */

// This returns true if init finished okay, otherwise it returns false. We assume in that
// case the the cti_error was already set.
static int
_cti_alps_ready(void)
{
	return (_cti_alps_ptr != NULL);
}

static uint64_t
_cti_alps_get_apid(int arg1, pid_t arg2)
{
	// sanity check
	if (_cti_alps_ptr == NULL)
		return 0;
	
	return (*_cti_alps_ptr->alps_get_apid)(arg1, arg2);
}

static int
_cti_alps_get_appinfo_ver2_err(uint64_t arg1, appInfo_t *arg2, cmdDetail_t **arg3, placeNodeList_t **arg4, char **arg5)
{
	// sanity check
	if (_cti_alps_ptr == NULL)
	{
		if (arg5 != NULL)
		{
			*arg5 = "_cti_alps_ptr is NULL!";
		}
		return -1;
	}
	
	return (*_cti_alps_ptr->alps_get_appinfo_ver2_err)(arg1, arg2, arg3, arg4, arg5, NULL);
}

static const char *
_cti_alps_launch_tool_helper(uint64_t arg1, int arg2, int arg3, int arg4, int arg5, char **arg6)
{
	// sanity check
	if (_cti_alps_ptr == NULL)
		return "_cti_alps_ptr is NULL!";
		
	return (*_cti_alps_ptr->alps_launch_tool_helper)(arg1, arg2, arg3, arg4, arg5, arg6);
}

static int
_cti_alps_get_overlap_ordinal(uint64_t arg1, char **arg2, int *arg3)
{
	// sanity check
	if (_cti_alps_ptr == NULL)
	{
		if (arg2 != NULL)
		{
			*arg2 = "_cti_alps_get_overlap_ordinal: _cti_alps_ptr is NULL!";
		}
		if (arg3 != NULL)
		{
			*arg3 = -1;
		}
		return -1;
	}
	
	// Ensure that the function pointer is not null, if it is null we need to
	// exit with error. We expect some alps libraries not to support this function.
	if (_cti_alps_ptr->alps_get_overlap_ordinal == NULL)
	{
		if (arg2 != NULL)
		{
			*arg2 = "alps_get_overlap_ordinal() not supported.";
		}
		if (arg3 != NULL)
		{
			*arg3 = -1;
		}
		return -1;
	}
	
	return (*_cti_alps_ptr->alps_get_overlap_ordinal)(arg1, arg2, arg3);
}

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
	if ((my_node = malloc(sizeof(serviceNode_t))) == (void *)0)
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
_cti_alps_consumeAlpsInfo(cti_wlm_obj this)
{
	alpsInfo_t	*alpsInfo = (alpsInfo_t *)this;

	// sanity check
	if (alpsInfo == NULL)
		return;
		
	// remove this alpsInfo from the global list
	_cti_list_remove(_cti_alps_info, alpsInfo);
	
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

static char *
_cti_alps_getJobId(cti_wlm_obj this)
{
	alpsInfo_t *	my_app = (alpsInfo_t *)this;
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
cti_app_id_t
cti_alps_registerApid(uint64_t apid)
{
	appEntry_t *	this;
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
	
	// iterate through the _cti_alps_info list to try to find an entry for this
	// apid
	_cti_list_reset(_cti_alps_info);
	while ((alpsInfo = (alpsInfo_t *)_cti_list_next(_cti_alps_info)) != NULL)
	{
		// check if the apid's match
		if (alpsInfo->apid == apid)
		{
			// reference this appEntry and return the appid
			if (_cti_refAppEntry(alpsInfo->appId))
			{
				// somehow we have an invalid alpsInfo obj, so free it and
				// break to re-register this apid
				_cti_alps_consumeAlpsInfo(alpsInfo);
				break;
			}
			return alpsInfo->appId;
		}
	}
	
	// aprun pid not found in the global _cti_alps_info list
	// so lets create a new appEntry_t object for it
	
	// create the new alpsInfo_t object
	if ((alpsInfo = malloc(sizeof(alpsInfo_t))) == NULL)
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
	if (_cti_alps_get_appinfo_ver2_err(apid, &alpsInfo->appinfo, &alpsInfo->cmdDetail, &alpsInfo->places, &appinfo_err) != 1)
	{
		// If we were ready, then set the error message. Otherwise we assume that
		// dlopen failed and we already set the error string in that case.
		if (_cti_alps_ready())
		{
			if (appinfo_err != NULL)
			{
				_cti_set_error("_cti_alps_get_appinfo_ver2_err() failed: %s", appinfo_err);
			} else
			{
				_cti_set_error("_cti_alps_get_appinfo_ver2_err() failed.");
			}
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
	
	// set the tool and attribs path
	alpsInfo->toolPath = toolPath;
	alpsInfo->attribsPath = attribsPath;
	
	if ((this = _cti_newAppEntry(&_cti_alps_wlmProto, (cti_wlm_obj)alpsInfo)) == NULL)
	{
		// we failed to create a new appEntry_t entry - catastrophic failure
		// error string already set
		_cti_alps_consumeAlpsInfo(alpsInfo);
		return 0;
	}
	
	// set the appid in the alpsInfo obj
	alpsInfo->appId = this->appId;
	
	// add the alpsInfo obj to our global list
	if(_cti_list_add(_cti_alps_info, alpsInfo))
	{
		_cti_set_error("cti_alps_registerApid: _cti_list_add() failed.");
		cti_deregisterApp(this->appId);
		return 0;
	}
	
	return this->appId;
}

uint64_t
cti_alps_getApid(pid_t aprunPid)
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
	
	return _cti_alps_get_apid(_cti_alps_svcNid->nid, aprunPid);
}

cti_aprunProc_t *
cti_alps_getAprunInfo(cti_app_id_t appId)
{
	appEntry_t *		app_ptr;
	alpsInfo_t *		alpsInfo;
	cti_aprunProc_t *	aprunInfo;
	
	// sanity check
	if (appId == 0)
	{
		_cti_set_error("Invalid appId %d.", (int)appId);
		return NULL;
	}
	
	// try to find an entry in the _cti_my_apps list for the apid
	if ((app_ptr = _cti_findAppEntry(appId)) == NULL)
	{
		// couldn't find the entry associated with the apid
		// error string already set
		return NULL;
	}
	
	// sanity check
	if (app_ptr->wlmProto->wlm_type != CTI_WLM_ALPS)
	{
		_cti_set_error("Invalid call. ALPS WLM not in use.");
		return NULL;
	}
	
	// sanity check
	alpsInfo = (alpsInfo_t *)app_ptr->_wlmObj;
	if (alpsInfo == NULL)
	{
		_cti_set_error("cti_alps_getAprunInfo: _wlmObj is NULL!");
		return NULL;
	}
	
	// allocate space for the cti_aprunProc_t struct
	if ((aprunInfo = malloc(sizeof(cti_aprunProc_t))) == (void *)0)
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
_cti_alps_getLauncherHostName(cti_wlm_obj this)
{
	alpsInfo_t *	my_app = (alpsInfo_t *)this;
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
_cti_alps_getNumAppPEs(cti_wlm_obj this)
{
	alpsInfo_t *	my_app = (alpsInfo_t *)this;
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
_cti_alps_getNumAppNodes(cti_wlm_obj this)
{
	alpsInfo_t *	my_app = (alpsInfo_t *)this;

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
_cti_alps_getAppHostsList(cti_wlm_obj this)
{
	alpsInfo_t *	my_app = (alpsInfo_t *)this;
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
	if ((hosts = calloc(my_app->appinfo.numPlaces + 1, sizeof(char *))) == (void *)0)
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
_cti_alps_getAppHostsPlacement(cti_wlm_obj this)
{
	alpsInfo_t *		my_app = (alpsInfo_t *)this;
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
	if ((placement_list = malloc(sizeof(cti_hostsList_t))) == (void *)0)
	{
		// malloc failed
		_cti_set_error("malloc failed.");
		return NULL;
	}
	
	// set the number of hosts for the application
	placement_list->numHosts = my_app->appinfo.numPlaces;
	
	// allocate space for the cti_host_t structs inside the placement_list
	if ((placement_list->hosts = malloc(placement_list->numHosts * sizeof(cti_host_t))) == (void *)0)
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
		placement_list->hosts[i].numPes = my_app->places[i].numPEs;
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
static cti_app_id_t
_cti_alps_launch_common(	const char * const launcher_argv[], int stdout_fd, int stderr_fd,
							const char *inputFile, const char *chdirPath,
							const char * const env_list[], int doBarrier	)
{
	aprunInv_t *		myapp;
	appEntry_t *		appEntry;
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
	// return object
	cti_app_id_t		rtn;

	if(!_cti_is_valid_environment()){
		// error already set
		return 0;
	}

	// create a new aprunInv_t object
	if ((myapp = malloc(sizeof(aprunInv_t))) == (void *)0)
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
	
	const char *owatch_path = _cti_getOverwatchPath();
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
	if ((aprun_exe_path = malloc(PATH_MAX)) == (void *)0)
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
	if ((rtn = cti_alps_registerApid(apid)) == 0)
	{
		// failed to register apid, error is already set
		kill(myapp->aprunPid, DEFAULT_SIG);
		_cti_alps_consumeAprunInv(myapp);
		return 0;
	}
	
	// assign the run specific objects to the application obj
	if ((appEntry = _cti_findAppEntry(rtn)) == NULL)
	{
		// this should never happen
		kill(myapp->aprunPid, DEFAULT_SIG);
		_cti_alps_consumeAprunInv(myapp);
		return 0;
	}
	
	// sanity check
	alpsInfo = (alpsInfo_t *)appEntry->_wlmObj;
	if (alpsInfo == NULL)
	{
		// this should never happen
		kill(myapp->aprunPid, DEFAULT_SIG);
		_cti_alps_consumeAprunInv(myapp);
		return 0;
	}
	
	// set the inv
	alpsInfo->inv = myapp;
	
	// return the cti_app_id_t
	return rtn;
}

static cti_app_id_t
_cti_alps_launch(	const char * const a1[], int a2, int a3, const char *a4,
					const char *a5, const char * const a6[]	)
{
	// call the common launch function
	return _cti_alps_launch_common(a1, a2, a3, a4, a5, a6, 0);
}

static cti_app_id_t
_cti_alps_launchBarrier(	const char * const a1[], int a2, int a3, const char *a4,
							const char *a5, const char * const a6[]	)
{
	// call the common launch function
	return _cti_alps_launch_common(a1, a2, a3, a4, a5, a6, 1);
}

int
_cti_alps_releaseBarrier(cti_wlm_obj this)
{
	alpsInfo_t *	my_app = (alpsInfo_t *)this;

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
_cti_alps_killApp(cti_wlm_obj this, int signum)
{
	alpsInfo_t *	my_app = (alpsInfo_t *)this;
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

static const char * const *
_cti_alps_extraBinaries(cti_wlm_obj this)
{
	// no extra binaries needed
	return NULL;
}

static const char * const *
_cti_alps_extraLibraries(cti_wlm_obj this)
{
	return _cti_alps_extra_libs;
}

static const char * const *
_cti_alps_extraLibDirs(cti_wlm_obj this)
{
	// no extra library directories needed
	return NULL;
}

static const char * const *
_cti_alps_extraFiles(cti_wlm_obj this)
{
	// no extra files needed
	return NULL;
}

static int
_cti_alps_ship_package(cti_wlm_obj this, const char *package)
{
	alpsInfo_t *	my_app = (alpsInfo_t *)this;
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
	while ((++checks < LAUNCH_TOOL_RETRY) && ((errmsg = _cti_alps_launch_tool_helper(my_app->apid, my_app->pe0Node, 1, 0, 1, &p)) != NULL))
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
		if (_cti_alps_ready())
		{
			_cti_set_error("alps_launch_tool_helper error: %s", errmsg);
		}
		return 1;
	}
	
	return 0;
}

static int
_cti_alps_start_daemon(cti_wlm_obj this, cti_args_t * args)
{
	alpsInfo_t *	my_app = (alpsInfo_t *)this;
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
		const char * launcher_path;
		
		// Get the location of the daemon launcher
		if ((launcher_path = _cti_getDlaunchPath()) == NULL)
		{
			_cti_set_error("Required environment variable %s not set.", BASE_DIR_ENV_VAR);
			return 1;
		}
		launcher = strdup(launcher_path);
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
	if ((errmsg = _cti_alps_launch_tool_helper(my_app->apid, my_app->pe0Node, do_transfer, 1, 1, &a)) != NULL)
	{
		// we failed to launch the launcher on the compute nodes for some reason - catastrophic failure
		//
		// If we were ready, then set the error message. Otherwise we assume that
		// dlopen failed and we already set the error string in that case.
		if (_cti_alps_ready())
		{
			_cti_set_error("alps_launch_tool_helper error: %s", errmsg);
		}
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

int
cti_alps_getAlpsOverlapOrdinal(cti_app_id_t appId)
{
	appEntry_t *	app_ptr;
	alpsInfo_t *	my_app;
	char *			errMsg = NULL;
	int				rtn;
	
	// sanity check
	if (appId == 0)
	{
		_cti_set_error("Invalid appId %d.", (int)appId);
		return -1;
	}
	
	// try to find an entry in the _cti_my_apps list for the apid
	if ((app_ptr = _cti_findAppEntry(appId)) == NULL)
	{
		// couldn't find the entry associated with the apid
		// error string already set
		return -1;
	}
	
	// sanity check
	if (app_ptr->wlmProto->wlm_type != CTI_WLM_ALPS)
	{
		_cti_set_error("cti_alps_getAlpsOverlapOrdinal: WLM mismatch.");
		return -1;
	}
	
	// sanity check
	my_app = (alpsInfo_t *)app_ptr->_wlmObj;
	if (my_app == NULL)
	{
		_cti_set_error("cti_alps_getAlpsOverlapOrdinal: _wlmObj is NULL!");
		return -1;
	}
	
	rtn = _cti_alps_get_overlap_ordinal(my_app->apid, &errMsg, NULL);
	if (rtn < 0)
	{
		if (errMsg != NULL)
		{
			_cti_set_error("%s", errMsg);
		} else
		{
			_cti_set_error("cti_alps_getAlpsOverlapOrdinal: Unknown _cti_alps_get_overlap_ordinal failure");
		}
	}
	
	return rtn;
}

static const char *
_cti_alps_getToolPath(cti_wlm_obj this)
{
	alpsInfo_t *	my_app = (alpsInfo_t *)this;
	
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
_cti_alps_getAttribsPath(cti_wlm_obj this)
{
	alpsInfo_t *	my_app = (alpsInfo_t *)this;
	
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

