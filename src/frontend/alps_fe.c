/******************************************************************************\
 * alps_fe.c - alps specific frontend library functions.
 *
 * © 2014 Cray Inc.  All Rights Reserved.
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

/* Types used here */

typedef struct
{
	void *			handle;
	uint64_t    	(*alps_get_apid)(int, pid_t);
	int				(*alps_get_appinfo)(uint64_t, appInfo_t *, cmdDetail_t **, placeList_t **);
	const char *	(*alps_launch_tool_helper)(uint64_t, int, int, int, int, char **);
} cti_alps_funcs_t;

typedef struct
{
	int		nid;		// service node id
	char *	cname;		// service node hostname
} serviceNode_t;

typedef struct
{
	int pipe_r;
	int pipe_w;
	int sync_int;
} barrierCtl_t;

typedef struct
{
	pid_t				aprunPid;
	int					pipeOpen;
	barrierCtl_t		pipeCtl;
} aprunInv_t;

typedef struct
{
	uint64_t		apid;			// ALPS apid
	int				pe0Node;		// ALPS PE0 node id
	appInfo_t		appinfo;		// ALPS application information
	cmdDetail_t *	cmdDetail;		// ALPS application command information (width, depth, memory, command name)
	placeList_t *	places;	 		// ALPS application placement information (nid, processors, PE threads)
	aprunInv_t *	inv;			// Optional object used for launched applications.
} alpsInfo_t;

/* Static prototypes */
static int				_cti_alps_ready(void);
static uint64_t			_cti_alps_get_apid(int, pid_t);
static int				_cti_alps_get_appinfo(uint64_t, appInfo_t *, cmdDetail_t **, placeList_t **);
static const char *		_cti_alps_launch_tool_helper(uint64_t, int, int, int, int, char **);
static void				_cti_alps_consumeAlpsInfo(void *);
static void 			_cti_consumeAprunInv(aprunInv_t *);
static serviceNode_t *	_cti_getSvcNodeInfo(void);
static int				_cti_checkPathForWrappedAprun(char *);
static int				_cti_filter_pid_entries(const struct dirent *);

/* global variables */
static cti_alps_funcs_t *	_cti_alps_ptr = NULL;	// libalps wrappers
static serviceNode_t *		_cti_svcNid	= NULL;		// service node information

/* Constructor/Destructor functions */

int
_cti_alps_init(void)
{
	char *error;

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
	
	if ((_cti_alps_ptr->handle = dlopen("libalps.so", RTLD_LAZY)) == NULL)
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
	
	// load alps_get_appinfo
	_cti_alps_ptr->alps_get_appinfo = dlsym(_cti_alps_ptr->handle, "alps_get_appinfo");
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
	
	// done
	return 0;
}

void
_cti_alps_fini(void)
{
	// sanity check
	if (_cti_alps_ptr == NULL)
		return;
		
	// cleanup
	dlclose(_cti_alps_ptr->handle);
	free(_cti_alps_ptr);
	_cti_alps_ptr = NULL;
	
	return;
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
_cti_alps_get_appinfo(uint64_t arg1, appInfo_t *arg2, cmdDetail_t **arg3, placeList_t **arg4)
{
	// sanity check
	if (_cti_alps_ptr == NULL)
		return -1;
		
	return (*_cti_alps_ptr->alps_get_appinfo)(arg1, arg2, arg3, arg4);
}

static const char *
_cti_alps_launch_tool_helper(uint64_t arg1, int arg2, int arg3, int arg4, int arg5, char **arg6)
{
	// sanity check
	if (_cti_alps_ptr == NULL)
		return "_cti_alps_ptr is NULL!";
		
	return (*_cti_alps_ptr->alps_launch_tool_helper)(arg1, arg2, arg3, arg4, arg5, arg6);
}

/*
*       _cti_getSvcNodeInfo - read cname and nid from alps defined system locations
*
*       args: None.
*
*       return value: serviceNode_t pointer containing the service nodes cname and
*       nid, or else NULL on error.
*
*/
static serviceNode_t *
_cti_getSvcNodeInfo()
{
	FILE *alps_fd;	  // ALPS NID/CNAME file stream
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
	
	// open up the cname file
	if ((alps_fd = fopen(ALPS_XT_CNAME, "r")) == NULL)
	{
		_cti_set_error("fopen of %s failed.", ALPS_XT_CNAME);
		free(my_node);
		return NULL;
	}
	
	// we expect this file to contain a string which represents our interconnect hostname
	if (fgets(file_buf, BUFSIZ, alps_fd) == NULL)
	{
		_cti_set_error("fopen of %s failed.", ALPS_XT_CNAME);
		free(my_node);
		fclose(alps_fd);
		return NULL;
	}
	// copy this to the cname ptr
	my_node->cname = strdup(file_buf);
	// we need to get rid of the newline
	my_node->cname[strlen(my_node->cname) - 1] = '\0';
	
	// close the file stream
	fclose(alps_fd);
	
	return my_node;
}

static void
_cti_alps_consumeAlpsInfo(void *this)
{
	alpsInfo_t	*alpsInfo = (alpsInfo_t *)this;

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
		_cti_consumeAprunInv(alpsInfo->inv);
	
	free(alpsInfo);
}

static void
_cti_consumeAprunInv(aprunInv_t *runPtr)
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
	
	// free the object from memory
	free(runPtr);
}

int
_cti_alps_compJobId(void *this, void *id)
{
	alpsInfo_t *	my_app = (alpsInfo_t *)this;
	uint64_t		apid;
	
	// sanity check
	if (my_app == NULL)
	{
		_cti_set_error("Null wlm obj.");
		return 0;
	}
	
	// sanity check
	if (id == NULL)
	{
		_cti_set_error("Null job id.");
		return 0;
	}
	
	apid = *((uint64_t *)id);
	
	return my_app->apid == apid;
}

// this function creates a new appEntry_t object for the app
// used by the alps_run functions
cti_app_id_t
cti_registerApid(uint64_t apid)
{
	cti_app_id_t	appId = 0;
	alpsInfo_t *	alpsInfo;
	char *			toolPath;
	// Used to determine CLE version
	struct stat 	statbuf;

	// sanity check
	if (apid == 0)
	{
		_cti_set_error("Invalid apid %d.", (int)apid);
		return 0;
	}
	
	// sanity check
	if (cti_current_wlm() != CTI_WLM_ALPS)
	{
		_cti_set_error("Invalid call. ALPS WLM not in use.");
		return 0;
	}
		
	// try to find an entry in the _cti_my_apps list for the apid
	if (_cti_findAppEntryByJobId((void *)&apid) == NULL)
	{
		// aprun pid not found in the global _cti_my_apps list
		// so lets create a new appEntry_t object for it
	
		// create the new alpsInfo_t object
		if ((alpsInfo = malloc(sizeof(alpsInfo_t))) == NULL)
		{
			_cti_set_error("malloc failed.");
			return 0;
		}
		memset(alpsInfo, 0, sizeof(alpsInfo_t));     // clear it to NULL
	
		// retrieve detailed information about our app
		// save this information into the struct
		if (_cti_alps_get_appinfo(apid, &alpsInfo->appinfo, &alpsInfo->cmdDetail, &alpsInfo->places) != 1)
		{
			// If we were ready, then set the error message. Otherwise we assume that
			// dlopen failed and we already set the error string in that case.
			if (_cti_alps_ready())
			{
				_cti_set_error("alps_get_appinfo() failed.");
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
		} else
		{
			// Assume it's using the OBS format
			if (asprintf(&toolPath, OBS_TOOLHELPER_DIR, (long long unsigned int)apid, (long long unsigned int)apid) <= 0)
			{
				_cti_set_error("asprintf failed");
				_cti_alps_consumeAlpsInfo(alpsInfo);
				return 0;
			}
		}
		
		if ((appId = _cti_newAppEntry(CTI_WLM_ALPS, toolPath, (void *)alpsInfo, &_cti_alps_consumeAlpsInfo)) == 0)
		{
			// we failed to create a new appEntry_t entry - catastrophic failure
			// error string already set
			_cti_alps_consumeAlpsInfo(alpsInfo);
			free(toolPath);
			return 0;
		}
		
		free(toolPath);
	} else
	{
		// apid was already registerd. This is a failure.
		_cti_set_error("apid already registered");
		return 0;
	}

	return appId;
}

uint64_t
cti_getApid(pid_t aprunPid)
{
	// sanity check
	if (aprunPid == 0)
	{
		_cti_set_error("Invalid pid %d.", (int)aprunPid);
		return 0;
	}
	
	// sanity check
	if (cti_current_wlm() != CTI_WLM_ALPS)
	{
		_cti_set_error("Invalid call. ALPS WLM not in use.");
		return 0;
	}
		
	// ensure the _cti_svcNid exists
	if (_cti_svcNid == NULL)
	{
		if ((_cti_svcNid = _cti_getSvcNodeInfo()) == NULL)
		{
			// couldn't get the svcnode info for some odd reason
			// error string already set
			return 0;
		}
	}
		
	return _cti_alps_get_apid(_cti_svcNid->nid, aprunPid);
}

char *
_cti_alps_getHostName(void)
{
	char *hostname;

	// ensure the _cti_svcNid exists
	if (_cti_svcNid == NULL)
	{
		if ((_cti_svcNid = _cti_getSvcNodeInfo()) == NULL)
		{
			// couldn't get the svcnode info for some odd reason
			// error string already set
			return NULL;
		}
	}
	
	if (asprintf(&hostname, ALPS_XT_HOSTNAME_FMT, _cti_svcNid->nid) < 0)
	{
		_cti_set_error("asprintf failed.");
		return NULL;
	}
	
	return hostname;
}

char *
_cti_alps_getLauncherHostName(void *this)
{
	alpsInfo_t *	my_app = (alpsInfo_t *)this;
	char *			hostname;

	// sanity check
	if (my_app == NULL)
	{
		_cti_set_error("getNumAppPEs operation failed.");
		return 0;
	}
	
	if (asprintf(&hostname, ALPS_XT_HOSTNAME_FMT, my_app->appinfo.aprunNid) < 0)
	{
		_cti_set_error("asprintf failed.");
		return NULL;
	}

	return hostname;
}

int
_cti_alps_getNumAppPEs(void *this)
{
	alpsInfo_t *	my_app = (alpsInfo_t *)this;

	// sanity check
	if (my_app == NULL)
	{
		_cti_set_error("getNumAppPEs operation failed.");
		return 0;
	}
	
	// sanity check
	if (my_app->cmdDetail == NULL)
	{
		_cti_set_error("getNumAppPEs operation failed.");
		return 0;
	}
	
	return my_app->cmdDetail->width;
}

int
_cti_alps_getNumAppNodes(void *this)
{
	alpsInfo_t *	my_app = (alpsInfo_t *)this;

	// sanity check
	if (my_app == NULL)
	{
		_cti_set_error("getNumAppNodes operation failed.");
		return 0;
	}
	
	// sanity check
	if (my_app->cmdDetail == NULL)
	{
		_cti_set_error("getNumAppNodes operation failed.");
		return 0;
	}
	
	return my_app->cmdDetail->nodeCnt;
}

char **
_cti_alps_getAppHostsList(void *this)
{
	alpsInfo_t *	my_app = (alpsInfo_t *)this;
	int				curNid, numNid;
	char **			hosts;
	char			hostEntry[ALPS_XT_HOSTNAME_LEN];
	int				i;
	
	// sanity check
	if (my_app == NULL)
	{
		_cti_set_error("getAppHostsList operation failed.");
		return 0;
	}
	
	// sanity check
	if (my_app->cmdDetail == NULL)
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
	
	// ensure my_app->cmdDetail->nodeCnt is non-zero
	if ( my_app->cmdDetail->nodeCnt <= 0 )
	{
		_cti_set_error("Application %d does not have any nodes.", (int)my_app->apid);
		// no nodes in the application
		return NULL;
	}
	
	// allocate space for the hosts list, add an extra entry for the null terminator
	if ((hosts = calloc(my_app->cmdDetail->nodeCnt + 1, sizeof(char *))) == (void *)0)
	{
		// calloc failed
		_cti_set_error("calloc failed.");
		return NULL;
	}
	
	// set the first entry
	numNid = 1;
	curNid = my_app->places[0].nid;
	// create the hostname string for this entry and place it into the list
	snprintf(hostEntry, ALPS_XT_HOSTNAME_LEN, ALPS_XT_HOSTNAME_FMT, curNid);
	hosts[0] = strdup(hostEntry);
	// clear the buffer
	memset(hostEntry, 0, ALPS_XT_HOSTNAME_LEN);
	
	// set the final entry to null, calloc doesn't guarantee null'ed memory
	hosts[my_app->cmdDetail->nodeCnt] = NULL;
	
	// check to see if we can skip iterating through the places list due to there being only one nid allocated
	if (numNid == my_app->cmdDetail->nodeCnt)
	{
		// we are done
		return hosts;
	}
	
	// iterate through the placelist to find the node id's for the PEs
	for (i=1; i < my_app->appinfo.numPlaces; i++)
	{
		if (curNid == my_app->places[i].nid)
		{
			continue;
		}
		// we have a new unique nid
		curNid = my_app->places[i].nid;
		// create the hostname string for this entry and place it into the list
		snprintf(hostEntry, ALPS_XT_HOSTNAME_LEN, ALPS_XT_HOSTNAME_FMT, curNid);
		hosts[numNid++] = strdup(hostEntry);
		// clear the buffer
		memset(hostEntry, 0, ALPS_XT_HOSTNAME_LEN);
	}
	
	// done
	return hosts;
}

cti_hostsList_t *
_cti_alps_getAppHostsPlacement(void *this)
{
	alpsInfo_t *		my_app = (alpsInfo_t *)this;
	int					curNid, numNid;
	int					numPe;
	cti_host_t *		curHost;
	cti_hostsList_t *	placement_list;
	char				hostEntry[ALPS_XT_HOSTNAME_LEN];
	int					i;
	
	// sanity check
	if (my_app == NULL)
	{
		_cti_set_error("getAppHostsPlacement operation failed.");
		return 0;
	}
	
	// sanity check
	if (my_app->cmdDetail == NULL)
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
	
	// ensure the nodeCnt is non-zero
	if ( my_app->cmdDetail->nodeCnt <= 0 )
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
	placement_list->numHosts = my_app->cmdDetail->nodeCnt;
	
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
	
	// set the first entry
	numNid = 1;
	numPe  = 1;
	curNid = my_app->places[0].nid;
	curHost = &placement_list->hosts[0];
	
	// create the hostname string for this entry and place it into the list
	snprintf(hostEntry, ALPS_XT_HOSTNAME_LEN, ALPS_XT_HOSTNAME_FMT, curNid);
	curHost->hostname = strdup(hostEntry);
	
	// clear the buffer
	memset(hostEntry, 0, ALPS_XT_HOSTNAME_LEN);
	
	// check to see if we can skip iterating through the places list due to there being only one nid allocated
	if (numNid == my_app->cmdDetail->nodeCnt)
	{
		// we have no more hostnames to process
		// all nids in the places list will belong to our current host
		// so write the numPlaces into the current host type and return
		curHost->numPes = my_app->appinfo.numPlaces;
		return placement_list;
	}
	
	// iterate through the placelist to find the node id's for the PEs
	for (i=1; i < my_app->appinfo.numPlaces; i++)
	{
		if (curNid == my_app->places[i].nid)
		{
			++numPe;
			continue;
		}
		// new unique nid found
		// set the number of pes found
		curHost->numPes = numPe;
		// reset numPes
		numPe = 1;
		
		// set to the new current nid
		curNid = my_app->places[i].nid;
		// create the hostname string for this entry and place it into the list
		snprintf(hostEntry, ALPS_XT_HOSTNAME_LEN, ALPS_XT_HOSTNAME_FMT, curNid);
		// change to the next host entry
		curHost = &placement_list->hosts[numNid++];
		// set the hostname
		curHost->hostname = strdup(hostEntry);
		
		// clear the buffer
		memset(hostEntry, 0, ALPS_XT_HOSTNAME_LEN);
	}
	
	// we need to write the last numPE into the current host type
	curHost->numPes = numPe;
	
	// done
	return placement_list;
}

static int	
_cti_checkPathForWrappedAprun(char *aprun_path)
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
_cti_filter_pid_entries(const struct dirent *a)
{
	unsigned long int pid;
	
	// We only want to get files that are of the format /proc/<pid>/
	// if the assignment succeeds then the file matches this type.
	return sscanf(a->d_name, "%lu", &pid);
}

cti_app_id_t
_cti_alps_launchBarrier(	char **launcher_argv, int redirectOutput, int redirectInput, 
							int stdout_fd, int stderr_fd, char *inputFile, char *chdirPath,
							char **env_list	)
{
	aprunInv_t *	myapp;
	appEntry_t *	appEntry;
	alpsInfo_t *	alpsInfo;
	uint64_t 		apid;
	pid_t			mypid;
	char **			tmp;
	int				aprun_argc = 0;
	int				fd_len = 0;
	int				i, j, fd;
	char *			pipefd_buf;
	char **			my_argv;
	// pipes for aprun
	int				aprunPipeR[2];
	int				aprunPipeW[2];
	// used for determining if the aprun binary is a wrapper script
	char *			aprun_proc_path = NULL;
	char *			aprun_exe_path;
	struct dirent 	**file_list;
	int				file_list_len;
	char *			proc_stat_path = NULL;
	FILE *			proc_stat = NULL;
	int				proc_ppid;
	// used to ignore SIGINT
	sigset_t		mask, omask;
	// return object
	cti_app_id_t	rtn;

	// create a new aprunInv_t object
	if ((myapp = malloc(sizeof(aprunInv_t))) == (void *)0)
	{
		// Malloc failed
		_cti_set_error("malloc failed.");
		return 0;
	}
	memset(myapp, 0, sizeof(aprunInv_t));     // clear it to NULL
	
	// make the pipes for aprun (tells aprun to hold the program at the initial 
	// barrier)
	if (pipe(aprunPipeR) < 0)
	{
		_cti_set_error("Pipe creation failure on aprunPipeR.");
		_cti_consumeAprunInv(myapp);
		return 0;
	}
	if (pipe(aprunPipeW) < 0)
	{
		_cti_set_error("Pipe creation failure on aprunPipeW.");
		_cti_consumeAprunInv(myapp);
		return 0;
	}
	
	// set my ends of the pipes in the aprunInv_t structure
	myapp->pipeOpen = 1;
	myapp->pipeCtl.pipe_r = aprunPipeR[1];
	myapp->pipeCtl.pipe_w = aprunPipeW[0];
	
	// create the argv array for the actual aprun exec
	// figure out the length of the argv array
	// this is the number of args in the launcher_argv array passed to us plus 2 
	// for the -P w,r argument and 2 for aprun and null term
		
	// iterate through the launcher_argv array
	tmp = launcher_argv;
	while (*tmp++ != NULL)
	{
		++aprun_argc;
	}
		
	// allocate the new argv array. Need additional entry for null terminator
	if ((my_argv = calloc(aprun_argc+4, sizeof(char *))) == (void *)0)
	{
		// calloc failed
		_cti_set_error("calloc failed.");
		_cti_consumeAprunInv(myapp);
		return 0;
	}
		
	// add the initial aprun argv
	// FIXME: Make generic
	my_argv[0] = strdup(APRUN);
	
	// Add the -P r,w args
	my_argv[1] = strdup("-P");
	
	// determine length of the fds
	j = aprunPipeR[0];
	do{
		++fd_len;
	} while (j/=10);
		
	j = aprunPipeW[1];
	do{
		++fd_len;
	} while (j/=10);
	
	// need a final char for comma and null terminator
	fd_len += 2;
	
	// allocate space for the buffer including the terminating zero
	if ((pipefd_buf = malloc(sizeof(char)*fd_len)) == (void *)0)
	{
		// malloc failed
		_cti_set_error("malloc failed.");
		_cti_consumeAprunInv(myapp);
		// cleanup my_argv array
		tmp = my_argv;
		while (*tmp != NULL)
		{
			free(*tmp++);
		}
		free(my_argv);
		return 0;
	}
	
	// write the buffer
	snprintf(pipefd_buf, fd_len, "%d,%d", aprunPipeW[1], aprunPipeR[0]);
	
	my_argv[2] = pipefd_buf;
	
	// set the argv array for aprun
	// here we expect the final argument to be the program we wish to start
	// and we need to add our -P r,w argument before this happens
	for (i=3; i < aprun_argc+3; i++)
	{
		my_argv[i] = strdup(launcher_argv[i-3]);
	}
	
	// add the null terminator
	my_argv[i++] = NULL;
	
	// We don't want alps to pass along signals the caller recieves to the
	// application process. In order to stop this from happening we need to put
	// the child into a different process group.
	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);
	sigprocmask(SIG_BLOCK, &mask, &omask);
	
	// fork off a process to launch aprun
	mypid = fork();
	
	// error case
	if (mypid < 0)
	{
		_cti_set_error("Fatal fork error.");
		_cti_consumeAprunInv(myapp);
		// cleanup my_argv array
		tmp = my_argv;
		while (*tmp != NULL)
		{
			free(*tmp++);
		}
		free(my_argv);
		return 0;
	}
	
	// child case
	// Note that this should not use the _cti_set_error() interface since it is
	// a child process.
	if (mypid == 0)
	{
		// close unused ends of pipe
		close(aprunPipeR[1]);
		close(aprunPipeW[0]);
		
		// redirect stdout/stderr if directed - do this early so that we can
		// print out errors to the proper descriptor.
		if (redirectOutput)
		{
			// dup2 stdout
			if (dup2(stdout_fd, STDOUT_FILENO) < 0)
			{
				// XXX: How to properly print this error? The parent won't be
				// expecting the error message on this stream since dup2 failed.
				fprintf(stderr, "CTI error: Unable to redirect aprun stdout.\n");
				exit(1);
			}
			
			// dup2 stderr
			if (dup2(stderr_fd, STDERR_FILENO) < 0)
			{
				// XXX: How to properly print this error? The parent won't be
				// expecting the error message on this stream since dup2 failed.
				fprintf(stderr, "CTI error: Unable to redirect aprun stderr.\n");
				exit(1);
			}
		}
		
		if (redirectInput)
		{
			// open the provided input file if non-null and redirect it to
			// stdin
			if (inputFile == NULL)
			{
				fprintf(stderr, "CTI error: Provided inputFile argument is null.\n");
				exit(1);
			}
			if ((fd = open(inputFile, O_RDONLY)) < 0)
			{
				fprintf(stderr, "CTI error: Unable to open %s for reading.\n", inputFile);
				exit(1);
			}
		} else
		{
			// we don't want this aprun to suck up stdin of the tool program
			if ((fd = open("/dev/null", O_RDONLY)) < 0)
			{
				fprintf(stderr, "CTI error: Unable to open /dev/null for reading.\n");
				exit(1);
			}
		}
		
		// dup2 the fd onto STDIN_FILENO
		if (dup2(fd, STDIN_FILENO) < 0)
		{
			fprintf(stderr, "CTI error: Unable to redirect aprun stdin.\n");
			exit(1);
		}
		close(fd);
		
		// chdir if directed
		if (chdirPath != NULL)
		{
			if (chdir(chdirPath))
			{
				fprintf(stderr, "CTI error: Unable to chdir to provided path.\n");
				exit(1);
			}
		}
		
		// if env_list is not null, call putenv for each entry in the list
		if (env_list != (char **)NULL)
		{
			i = 0;
			while(env_list[i] != NULL)
			{
				// putenv returns non-zero on error
				if (putenv(env_list[i++]))
				{
					fprintf(stderr, "CTI error: Unable to putenv provided env_list.\n");
					exit(1);
				}
			}
		}
		
		// Place this process in its own group to prevent signals being passed
		// to it. This is necessary in case the child code execs before the 
		// parent can put us into our own group. This is so that we won't get
		// the ctrl-c when aprun re-inits the signal handlers.
		setpgid(0, 0);
		
		// exec aprun
		execvp(APRUN, my_argv);
		
		// exec shouldn't return
		fprintf(stderr, "CTI error: Return from exec.\n");
		perror("execvp");
		exit(1);
	}
	
	// parent case
	
	// Place the child in its own group. We still need to block SIGINT in case
	// its delivered to us before we can do this. We need to do this again here
	// in case this code runs before the child code while we are still blocking 
	// ctrl-c
	setpgid(mypid, mypid);
	
	// unblock ctrl-c
	sigprocmask(SIG_SETMASK, &omask, NULL);
	
	// close unused ends of pipe
	close(aprunPipeR[0]);
	close(aprunPipeW[1]);
	
	// cleanup my_argv array
	tmp = my_argv;
	while (*tmp != NULL)
	{
		free(*tmp++);
	}
	free(my_argv);
	
	// Wait on pipe read for app to start and get to barrier - once this happens
	// we know the real aprun is up and running
	if (read(myapp->pipeCtl.pipe_w, &myapp->pipeCtl.sync_int, sizeof(myapp->pipeCtl.sync_int)) <= 0)
	{
		_cti_set_error("Control pipe read failed.");
		// attempt to kill aprun since the caller will not recieve the aprun pid
		// just in case the aprun process is still hanging around.
		kill(mypid, DEFAULT_SIG);
		_cti_consumeAprunInv(myapp);
		return 0;
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
	if (_cti_checkPathForWrappedAprun(aprun_exe_path))
	{
		// aprun is wrapped, we need to start harvesting stuff out from /proc.
		
		// start by getting all the /proc/<pid>/ files
		if ((file_list_len = scandir("/proc", &file_list, _cti_filter_pid_entries, NULL)) < 0)
		{
			_cti_set_error("Could not enumerate /proc for real aprun process.");
			free(aprun_proc_path);
			free(aprun_exe_path);
			// attempt to kill aprun since the caller will not recieve the aprun pid
			// just in case the aprun process is still hanging around.
			kill(mypid, DEFAULT_SIG);
			_cti_consumeAprunInv(myapp);
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
				_cti_consumeAprunInv(myapp);
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
				_cti_consumeAprunInv(myapp);
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
					_cti_consumeAprunInv(myapp);
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
				if (!_cti_checkPathForWrappedAprun(aprun_exe_path))
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
	if ((apid = cti_getApid(myapp->aprunPid)) == 0)
	{
		_cti_set_error("Could not obtain apid associated with pid of aprun.");
		// attempt to kill aprun since the caller will not recieve the aprun pid
		// just in case the aprun process is still hanging around.
		kill(myapp->aprunPid, DEFAULT_SIG);
		_cti_consumeAprunInv(myapp);
		return 0;
	}
	
	// register this app with the application interface
	if ((rtn = cti_registerApid(apid)) == 0)
	{
		// failed to register apid, error is already set
		kill(myapp->aprunPid, DEFAULT_SIG);
		_cti_consumeAprunInv(myapp);
		return 0;
	}
	
	// assign the run specific objects to the application obj
	if ((appEntry = _cti_findAppEntry(rtn)) == NULL)
	{
		// this should never happen
		kill(myapp->aprunPid, DEFAULT_SIG);
		_cti_consumeAprunInv(myapp);
		return 0;
	}
	
	// sanity check
	alpsInfo = (alpsInfo_t *)appEntry->_wlmObj;
	if (alpsInfo == NULL)
	{
		// this should never happen
		kill(myapp->aprunPid, DEFAULT_SIG);
		_cti_consumeAprunInv(myapp);
		return 0;
	}
	
	// set the inv
	alpsInfo->inv = myapp;
	
	// return the apid and the pid of the aprun process we forked
	return rtn;
}

int
_cti_alps_releaseBarrier(void *this)
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

int
_cti_alps_killApp(void *this, int signum)
{
	alpsInfo_t *	my_app = (alpsInfo_t *)this;
	int				mypid;
	uint64_t		i;
	int				j;
	size_t			len;
	char *			sigStr;
	char *			apidStr;
	char **			my_argv;
	char **			tmp;
	
	// sanity check
	if (my_app == NULL)
	{
		_cti_set_error("Aprun kill operation failed.");
		return 1;
	}
	
	// create the string to pass to exec
	
	// allocate the argv array. Need additional entry for null terminator
	if ((my_argv = calloc(4, sizeof(char *))) == (void *)0)
	{
		// calloc failed
		_cti_set_error("calloc failed.");
		return 1;
	}
	
	// first argument should be "apkill"
	my_argv[0] = strdup(APKILL);
	
	// second argument is -signum
	// determine length of signum
	len = 0;
	j = signum;
	do {
		++len;
	} while (j/=10);
	
	// add 1 additional char for the '-' and another for the null terminator
	len += 2;
	
	// alloc space for sigStr
	if ((sigStr = malloc(len*sizeof(char))) == (void *)0)
	{
		// malloc failed
		
		// cleanup my_argv array
		tmp = my_argv;
		while (*tmp != NULL)
		{
			free(*tmp++);
		}
		free(my_argv);
		
		_cti_set_error("malloc failed.");
		return 1;
	}
	
	// write the signal string
	snprintf(sigStr, len, "-%d", signum);
	my_argv[1] = sigStr;
	
	// third argument is apid
	// determine length of apid
	len = 0;
	i = my_app->apid;
	do {
		++len;
	} while (i/=10);
	
	// add 1 additional char for the null terminator
	++len;
	
	// alloc space for apidStr
	if ((apidStr = malloc(len*sizeof(char))) == (void *)0)
	{
		// malloc failed
		
		// cleanup my_argv array
		tmp = my_argv;
		while (*tmp != NULL)
		{
			free(*tmp++);
		}
		free(my_argv);
		
		_cti_set_error("malloc failed.");
		return 1;
	}
	
	// write the apid string
	snprintf(apidStr, len, "%llu", (long long unsigned int)my_app->apid);
	my_argv[2] = apidStr;
	
	// set the final null terminator
	my_argv[3] = NULL;
	
	// fork off a process to launch apkill
	mypid = fork();
	
	// error case
	if (mypid < 0)
	{
		_cti_set_error("Fatal fork error.");
		// cleanup my_argv array
		tmp = my_argv;
		while (*tmp != NULL)
		{
			free(*tmp++);
		}
		free(my_argv);
		return 1;
	}
	
	// child case
	if (mypid == 0)
	{
		// exec apkill
		execvp(APKILL, my_argv);
		
		// exec shouldn't return
		fprintf(stderr, "CTI error: Return from exec.\n");
		exit(1);
	}
	
	// parent case
	// cleanup my_argv array
	tmp = my_argv;
	while (*tmp != NULL)
	{
		free(*tmp++);
	}
	free(my_argv);
	
	// wait until the apkill finishes
	waitpid(mypid, NULL, 0);
	
	return 0;
}

int
_cti_alps_ship_package(void *this, char *package)
{
	alpsInfo_t *	my_app = (alpsInfo_t *)this;
	const char *	errmsg;				// errmsg that is possibly returned by call to alps_launch_tool_helper
	
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
	
	// now ship the tarball to the compute node
	if ((errmsg = _cti_alps_launch_tool_helper(my_app->apid, my_app->pe0Node, 1, 0, 1, &package)) != NULL)
	{
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

int
_cti_alps_start_daemon(void *this, char *args, int transfer)
{
	alpsInfo_t *	my_app = (alpsInfo_t *)this;
	const char *	errmsg;				// errmsg that is possibly returned by call to alps_launch_tool_helper
	
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
	
	// launch the tool daemon onto the compute nodes.
	if ((errmsg = _cti_alps_launch_tool_helper(my_app->apid, my_app->pe0Node, transfer, 1, 1, &args)) != NULL)
	{
		// we failed to launch the launcher on the compute nodes for some reason - catastrophic failure
		//
		// If we were ready, then set the error message. Otherwise we assume that
		// dlopen failed and we already set the error string in that case.
		if (_cti_alps_ready())
		{
			_cti_set_error("alps_launch_tool_helper error: %s", errmsg);
		}
		return 0;
	}
	
	return 0;
}

