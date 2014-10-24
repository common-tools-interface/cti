/******************************************************************************\
 * cti_fe.c - cti frontend library functions.
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

#include <dlfcn.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "wlm_detect.h"

#include "cti_fe.h"
#include "cti_defs.h"
#include "cti_error.h"
#include "alps_fe.h"
#include "cray_slurm_fe.h"

struct appList
{
	appEntry_t *		this;
	struct appList *	next;
};
typedef struct appList appList_t;

typedef struct
{
	void *			handle;
	char *			(*wlm_detect_get_active)(void);
	const char *	(*wlm_detect_get_default)(void);
} cti_wlm_detect_t;

/* Static prototypes */
static int				_cti_addAppEntry(appEntry_t *);
static void				_cti_consumeAppEntry(appEntry_t *);
static void				_cti_reapAppEntry(cti_app_id_t);

/* static global vars */
static cti_app_id_t		_cti_app_id 	= 1;	// start counting from 1
static appList_t *		_cti_my_apps	= NULL;	// global list pertaining to known application sessions
static cti_wlm_detect_t	_cti_wlm_detect = {0};	// wlm_detect functions for dlopen
static char *			_cti_cfg_dir	= NULL;	// config dir that we can use as temporary storage

/* noneness wlm proto object */
static const cti_wlm_proto_t	_cti_nonenessProto =
{
	CTI_WLM_NONE,
	_cti_wlm_init_none,
	_cti_wlm_fini_none,
	_cti_wlm_cmpJobId_none,
	_cti_wlm_getJobId_none,
	_cti_wlm_launchBarrier_none,
	_cti_wlm_releaseBarrier_none,
	_cti_wlm_killApp_none,
	_cti_wlm_verifyBinary_none,
	_cti_wlm_verifyLibrary_none,
	_cti_wlm_verifyLibDir_none,
	_cti_wlm_verifyFile_none,
	_cti_wlm_extraBinaries_none,
	_cti_wlm_extraLibraries_none,
	_cti_wlm_extraLibDirs_none,
	_cti_wlm_extraFiles_none,
	_cti_wlm_shipPackage_none,
	_cti_wlm_startDaemon_none,
	_cti_wlm_getNumAppPEs_none,
	_cti_wlm_getNumAppNodes_none,
	_cti_wlm_getAppHostsList_none,
	_cti_wlm_getAppHostsPlacement_none,
	_cti_wlm_getHostName_none,
	_cti_wlm_getLauncherHostName_none
};

/* global wlm proto object - this is initialized to noneness by default */
static const cti_wlm_proto_t *	_cti_wlmProto = &_cti_nonenessProto;

// Constructor function
void __attribute__((constructor))
_cti_init(void)
{
	char *	active_wlm;
	char *	error;
	int		use_default = 0;
	int		do_free = 1;

	// XXX: If wlm_detect doesn't work on your system, this will default to ALPS
	// TODO: Add env var to allow caller to specify what WLM they want to use.
	
	if ((_cti_wlm_detect.handle = dlopen(WLM_DETECT_LIB_NAME, RTLD_LAZY)) == NULL)
	{
		use_default = 1;
		goto wlm_detect_err;
	}
	
	// Clear any existing error
	dlerror();
	
	// load wlm_detect_get_active
	_cti_wlm_detect.wlm_detect_get_active = dlsym(_cti_wlm_detect.handle, "wlm_detect_get_active");
	if ((error = dlerror()) != NULL)
	{
		dlclose(_cti_wlm_detect.handle);
		use_default = 1;
		goto wlm_detect_err;
	}
	
	// try to get the active wlm
	active_wlm = (*_cti_wlm_detect.wlm_detect_get_active)();
	if (active_wlm == NULL)
	{
		// load wlm_detect_get_default
		_cti_wlm_detect.wlm_detect_get_default = dlsym(_cti_wlm_detect.handle, "wlm_detect_get_default");
		if ((error = dlerror()) != NULL)
		{
			dlclose(_cti_wlm_detect.handle);
			use_default = 1;
			goto wlm_detect_err;
		}
		// use the default wlm
		active_wlm = (char *)(*_cti_wlm_detect.wlm_detect_get_default)();
		do_free = 0;
	}
	
	// parse the returned result
	if (strncmp("ALPS", active_wlm, strlen("ALPS")) == 0)
	{
		_cti_wlmProto = &_cti_alps_wlmProto;
	} else if (strncmp("SLURM", active_wlm, strlen("SLURM")) == 0)
	{
		_cti_wlmProto = &_cti_cray_slurm_wlmProto;
	} else
	{
		// fallback to use the default
		use_default = 1;
	}
	
	// close the wlm_detect handle, we are done with it
	dlclose(_cti_wlm_detect.handle);
	
	// maybe cleanup the string
	if (do_free)
	{
		free(active_wlm);
	}
	
wlm_detect_err:

	// check if wlm_detect failed, in which case we should use the default
	if (use_default)
	{
		_cti_wlmProto = &_cti_alps_wlmProto;
	}
	
	if (_cti_wlmProto->wlm_init())
	{
		// We failed to init, so reset wlm proto to noneness
		_cti_wlmProto = &_cti_nonenessProto;
		return;
	}
}

// Destructor function
void __attribute__ ((destructor))
_cti_fini(void)
{
	// call the wlm proto fini function
	_cti_wlmProto->wlm_fini();
	
	// reset wlm proto to noneness
	_cti_wlmProto = &_cti_nonenessProto;

	return;
}

/*********************
** static functions 
*********************/

static int
_cti_addAppEntry(appEntry_t *entry)
{
	appList_t *	newEntry;
	appList_t *	lstPtr;
	
	// sanity check
	if (entry == NULL)
	{
		_cti_set_error("_cti_addAppEntry failed.");
		return 1;
	}
	
	// alloc space for the new list entry
	if ((newEntry = malloc(sizeof(appList_t))) == (void *)0)
	{
		_cti_set_error("malloc failed.");
		return 1;
	}
	memset(newEntry, 0, sizeof(appList_t));     // clear it to NULL
	
	// set the appEntry in the new list entry
	newEntry->this = entry;
	
	// if _cti_my_apps is null, this is the new head of the list
	if ((lstPtr = _cti_my_apps) == NULL)
	{
		_cti_my_apps = newEntry;
	} else
	{
		// we need to iterate through the list to find the open next entry
		while (lstPtr->next != NULL)
		{
			lstPtr = lstPtr->next;
		}
		lstPtr->next = newEntry;
	}
	
	// done
	return 0;
}

static void
_cti_consumeAppEntry(appEntry_t *entry)
{
	// sanity check
	if (entry == NULL)
		return;
		
	// Check to see if there is a wlm obj
	if (entry->_wlmObj != NULL)
	{
		// Call the wlm specific destroy function if there is one
		if (entry->_wlmDestroy != NULL)
		{
			(*(entry->_wlmDestroy))(entry->_wlmObj);
		}
	}
	
	entry->_wlmObj = NULL;
		
	// free the toolPath
	if (entry->toolPath != NULL)
	{
		free(entry->toolPath);
	}
	
	// Check to see if there is a _transferObj
	if (entry->_transferObj != NULL)
	{
		// Call the transfer destroy function if there is one
		if (entry->_transferDestroy != NULL)
		{
			(*(entry->_transferDestroy))(entry->_transferObj);
		}
	}
	
	entry->_transferObj = NULL;
	
	// nom nom the final appEntry_t object
	free(entry);
}

static void
_cti_reapAppEntry(cti_app_id_t appId)
{
	appList_t *	lstPtr;
	appList_t *	prePtr;
	
	// sanity check
	if (((lstPtr = _cti_my_apps) == NULL) || (appId == 0))
		return;
		
	prePtr = _cti_my_apps;
	
	// this shouldn't happen, but doing so will prevent a segfault if the list gets corrupted
	while (lstPtr->this == NULL)
	{
		// if this is the only object in the list, then delete the entire list
		if ((lstPtr = lstPtr->next) == NULL)
		{
			_cti_my_apps = NULL;
			free(lstPtr);
			return;
		}
		// otherwise point _cti_my_apps to the lstPtr and free the corrupt entry
		_cti_my_apps = lstPtr;
		free(prePtr);
		prePtr = _cti_my_apps;
	}
	
	// we need to locate the position of the appList_t object that we need to remove
	while (lstPtr->this->appId != appId)
	{
		prePtr = lstPtr;
		if ((lstPtr = lstPtr->next) == NULL)
		{
			// there are no more entries and we didn't find the appId
			return;
		}
	}
	
	// check to see if this was the first entry in the global _cti_my_apps list
	if (prePtr == lstPtr)
	{
		// point the global _cti_my_apps list to the next entry
		_cti_my_apps = lstPtr->next;
		// consume the appEntry_t object for this entry in the list
		_cti_consumeAppEntry(lstPtr->this);
		// free the list object
		free(lstPtr);
	} else
	{
		// we are at some point midway through the global _cti_my_apps list
		
		// point the previous entries next entry to the list pointers next entry
		// this bypasses the current list pointer
		prePtr->next = lstPtr->next;
		// consume the appEntry_t object for this entry in the list
		_cti_consumeAppEntry(lstPtr->this);
		// free the list object
		free(lstPtr);
	}
	
	// done
	return;
}

cti_app_id_t
_cti_newAppEntry(const cti_wlm_proto_t *wlmProto, const char *toolPath, void *wlm_obj, obj_destroy destroy)
{
	appEntry_t *	this;
	
	if (wlm_obj == NULL)
	{
		_cti_set_error("Null wlm_obj.");
		return 0;
	}
	
	if (toolPath == NULL)
	{
		_cti_set_error("Null toolPath.");
		return 0;
	}
	
	// create the new appEntry_t object
	if ((this = malloc(sizeof(appEntry_t))) == NULL)
	{
		_cti_set_error("malloc failed.");
		return 0;
	}
	memset(this, 0, sizeof(appEntry_t));     // clear it to NULL
	
	// set the members
	this->appId = _cti_app_id++;	// assign this to the next id.
	this->wlmProto = wlmProto;
	this->toolPath = strdup(toolPath);
	this->_wlmObj = wlm_obj;
	this->_wlmDestroy = destroy;
	
	// save the new appEntry_t into the global app list
	if (_cti_addAppEntry(this))
	{
		// error already set
		_cti_consumeAppEntry(this);
		return 0;
	}
	
	return this->appId;
}

appEntry_t *
_cti_findAppEntry(cti_app_id_t appId)
{
	appList_t *	lstPtr = _cti_my_apps;
	
	// iterate through the _cti_my_apps list
	while (lstPtr != NULL)
	{
		// return if the appId matches
		if (lstPtr->this->appId == appId)
			return lstPtr->this;
			
		// make lstPtr point to the next entry
		lstPtr = lstPtr->next;
	}
	
	// if we get here, an entry for appId doesn't exist
	_cti_set_error("The appId %d is not registered.", (int)appId);
	return NULL;
}

appEntry_t *
_cti_findAppEntryByJobId(void *wlm_id)
{
	appList_t *	lstPtr = _cti_my_apps;
	int			found = 0;
	
	// iterate through the _cti_my_apps list
	while (lstPtr != NULL)
	{
		// Call the appropriate find function based on the wlm
		found = lstPtr->this->wlmProto->wlm_cmpJobId(lstPtr->this->_wlmObj, wlm_id);
		
		if (found == -1)
		{
			// error already set
			return NULL;
		}
	
		// return if found
		if (found)
			return lstPtr->this;
		
		// make lstPtr point to the next entry
		lstPtr = lstPtr->next;
	}
	
	// if we get here, an entry for wlm_id was not found
	_cti_set_error("The wlm id is not registered.");
	return NULL;
}

int
_cti_setTransferObj(appEntry_t *app_ptr, void *transferObj, obj_destroy transferDestroy)
{
	// sanity
	if (app_ptr == NULL)
		return 1;
	
	// set the members
	app_ptr->_transferObj = transferObj;
	app_ptr->_transferDestroy = transferDestroy;
	
	return 0;
}

const cti_wlm_proto_t *
_cti_current_wlm_proto(void)
{
	return _cti_wlmProto;
}

const char *
_cti_getCfgDir(void)
{
	char *cfg_dir;

	// return if we already have the value
	if (_cti_cfg_dir != NULL)
		return _cti_cfg_dir;

	// read the value
	if ((cfg_dir = getenv(CFG_DIR_VAR)) == NULL)
	{
		_cti_set_error("Cannot getenv on %s. Ensure environment variables are set.", CFG_DIR_VAR);
		return NULL;
	}
	
	// set the global variable
	_cti_cfg_dir = strdup(cfg_dir);

	return _cti_cfg_dir;
}

/************************
* API defined functions
************************/

cti_wlm_type
cti_current_wlm(void)
{
	return _cti_wlmProto->wlm_type;
}

const char *
cti_wlm_type_toString(cti_wlm_type wlm_type)
{
	switch (wlm_type)
	{
		case CTI_WLM_ALPS:
			return "Cray ALPS";
			
		case CTI_WLM_CRAY_SLURM:
			return "Cray based SLURM";
	
		case CTI_WLM_SLURM:
			return "SLURM";
			
		case CTI_WLM_NONE:
			return "No WLM detected";
	}
	
	// Shouldn't get here
	return "Invalid WLM.";
}

void
cti_deregisterApp(cti_app_id_t appId)
{
	// sanity check
	if (appId == 0)
		return;
	
	// call the _cti_reapAppEntry function for this appId
	_cti_reapAppEntry(appId);
}

int
cti_getNumAppPEs(cti_app_id_t appId)
{
	appEntry_t *	app_ptr;
	
	// sanity check
	if (appId == 0)
	{
		_cti_set_error("Invalid appId %d.", (int)appId);
		return 0;
	}
		
	// try to find an entry in the _cti_my_apps list for the apid
	if ((app_ptr = _cti_findAppEntry(appId)) == NULL)
	{
		// couldn't find the entry associated with the apid
		// error string already set
		return 0;
	}
	
	// sanity check
	if (app_ptr->_wlmObj == NULL)
	{
		_cti_set_error("cti_getNumAppPEs: _wlmObj is NULL!");
		return 0;
	}
	
	// Call the appropriate function based on the wlm
	return app_ptr->wlmProto->wlm_getNumAppPEs(app_ptr->_wlmObj);
}

int
cti_getNumAppNodes(cti_app_id_t appId)
{
	appEntry_t *	app_ptr;
	
	// sanity check
	if (appId == 0)
	{
		_cti_set_error("Invalid appId %d.", (int)appId);
		return 0;
	}
	
	// try to find an entry in the _cti_my_apps list for the apid
	if ((app_ptr = _cti_findAppEntry(appId)) == NULL)
	{
		// couldn't find the entry associated with the apid
		// error string already set
		return 0;
	}
	
	// sanity check
	if (app_ptr->_wlmObj == NULL)
	{
		_cti_set_error("cti_getNumAppNodes: _wlmObj is NULL!");
		return 0;
	}
	
	// Call the appropriate function based on the wlm
	return app_ptr->wlmProto->wlm_getNumAppNodes(app_ptr->_wlmObj);
}

char **
cti_getAppHostsList(cti_app_id_t appId)
{
	appEntry_t *	app_ptr;
	
	// sanity check
	if (appId == 0)
	{
		_cti_set_error("Invalid appId %d.", (int)appId);
		return 0;
	}
	
	// try to find an entry in the _cti_my_apps list for the apid
	if ((app_ptr = _cti_findAppEntry(appId)) == NULL)
	{
		// couldn't find the entry associated with the apid
		// error string already set
		return 0;
	}
	
	// sanity check
	if (app_ptr->_wlmObj == NULL)
	{
		_cti_set_error("cti_getAppHostsList: _wlmObj is NULL!");
		return 0;
	}
	
	// Call the appropriate function based on the wlm
	return app_ptr->wlmProto->wlm_getAppHostsList(app_ptr->_wlmObj);
}

cti_hostsList_t *
cti_getAppHostsPlacement(cti_app_id_t appId)
{
	appEntry_t *	app_ptr;
	
	// sanity check
	if (appId == 0)
	{
		_cti_set_error("Invalid appId %d.", (int)appId);
		return 0;
	}
	
	// try to find an entry in the _cti_my_apps list for the apid
	if ((app_ptr = _cti_findAppEntry(appId)) == NULL)
	{
		// couldn't find the entry associated with the apid
		// error string already set
		return 0;
	}
	
	// sanity check
	if (app_ptr->_wlmObj == NULL)
	{
		_cti_set_error("cti_getAppHostsPlacement: _wlmObj is NULL!");
		return 0;
	}
	
	// Call the appropriate function based on the wlm
	return app_ptr->wlmProto->wlm_getAppHostsPlacement(app_ptr->_wlmObj);
}

void
cti_destroyHostsList(cti_hostsList_t *placement_list)
{
	// sanity check
	if (placement_list == NULL)
		return;
		
	if (placement_list->hosts != NULL)
		free(placement_list->hosts);
		
	free(placement_list);
}

char *
cti_getHostname()
{
	// call wlm proto function
	return _cti_wlmProto->wlm_getHostName();
}

char *
cti_getLauncherHostName(cti_app_id_t appId)
{
	appEntry_t *	app_ptr;
	
	// sanity check
	if (appId == 0)
	{
		_cti_set_error("Invalid appId %d.", (int)appId);
		return 0;
	}
	
	// try to find an entry in the _cti_my_apps list for the apid
	if ((app_ptr = _cti_findAppEntry(appId)) == NULL)
	{
		// couldn't find the entry associated with the apid
		// error string already set
		return 0;
	}
	
	// sanity check
	if (app_ptr->_wlmObj == NULL)
	{
		_cti_set_error("cti_getLauncherHostName: _wlmObj is NULL!");
		return 0;
	}
	
	// Call the appropriate function based on the wlm
	return app_ptr->wlmProto->wlm_getLauncherHostName(app_ptr->_wlmObj);
}


/* Noneness functions for wlm proto */

int
_cti_wlm_init_none(void)
{
	_cti_set_error("wlm_init() not supported for %s", cti_wlm_type_toString(_cti_wlmProto->wlm_type));
	return 1;
}

void
_cti_wlm_fini_none(void)
{
	_cti_set_error("wlm_fini() not supported for %s", cti_wlm_type_toString(_cti_wlmProto->wlm_type));
	return;
}

int
_cti_wlm_cmpJobId_none(void * a1, void * a2)
{
	_cti_set_error("wlm_cmpJobId() not supported for %s", cti_wlm_type_toString(_cti_wlmProto->wlm_type));
	return -1;
}

char *
_cti_wlm_getJobId_none(void *a1)
{
	_cti_set_error("wlm_getJobId() not supported for %s", cti_wlm_type_toString(_cti_wlmProto->wlm_type));
	return NULL;
}

cti_app_id_t
_cti_wlm_launchBarrier_none(const char * const a1[], int a2, int a3, int a4, int a5, const char *a6, const char *a7, const char * const a8[])
{
	_cti_set_error("wlm_launchBarrier() not supported for %s", cti_wlm_type_toString(_cti_wlmProto->wlm_type));
	return 0;
}

int
_cti_wlm_releaseBarrier_none(void *a1)
{
	_cti_set_error("wlm_releaseBarrier() not supported for %s", cti_wlm_type_toString(_cti_wlmProto->wlm_type));
	return 1;
}

int
_cti_wlm_killApp_none(void *a1, int a2)
{
	_cti_set_error("wlm_killApp() not supported for %s", cti_wlm_type_toString(_cti_wlmProto->wlm_type));
	return 1;
}

int
_cti_wlm_verifyBinary_none(const char *a1)
{
	_cti_set_error("wlm_verifyBinary() not supported for %s", cti_wlm_type_toString(_cti_wlmProto->wlm_type));
	return 0;
}

int
_cti_wlm_verifyLibrary_none(const char *a1)
{
	_cti_set_error("wlm_verifyLibrary() not supported for %s", cti_wlm_type_toString(_cti_wlmProto->wlm_type));
	return 0;
}

int
_cti_wlm_verifyLibDir_none(const char *a1)
{
	_cti_set_error("wlm_verifyLibDir() not supported for %s", cti_wlm_type_toString(_cti_wlmProto->wlm_type));
	return 0;
}

int
_cti_wlm_verifyFile_none(const char *a1)
{
	_cti_set_error("wlm_verifyFile() not supported for %s", cti_wlm_type_toString(_cti_wlmProto->wlm_type));
	return 0;
}

const char * const *
_cti_wlm_extraBinaries_none(void)
{
	_cti_set_error("wlm_extraBinaries() not supported for %s", cti_wlm_type_toString(_cti_wlmProto->wlm_type));
	return NULL;
}

const char * const *
_cti_wlm_extraLibraries_none(void)
{
	_cti_set_error("wlm_extraLibraries() not supported for %s", cti_wlm_type_toString(_cti_wlmProto->wlm_type));
	return NULL;
}

const char * const *
_cti_wlm_extraLibDirs_none(void)
{
	_cti_set_error("wlm_extraLibDirs() not supported for %s", cti_wlm_type_toString(_cti_wlmProto->wlm_type));
	return NULL;
}

const char * const *
_cti_wlm_extraFiles_none(void)
{
	_cti_set_error("wlm_extraFiles() not supported for %s", cti_wlm_type_toString(_cti_wlmProto->wlm_type));
	return NULL;
}

int
_cti_wlm_shipPackage_none(void *a1, const char *a2)
{
	_cti_set_error("wlm_shipPackage() not supported for %s", cti_wlm_type_toString(_cti_wlmProto->wlm_type));
	return 1;
}

int
_cti_wlm_startDaemon_none(void *a1, int a2, const char *a3, cti_args_t *a4)
{
	_cti_set_error("wlm_startDaemon() not supported for %s", cti_wlm_type_toString(_cti_wlmProto->wlm_type));
	return 1;
}

int
_cti_wlm_getNumAppPEs_none(void *a1)
{
	_cti_set_error("wlm_getNumAppPEs() not supported for %s", cti_wlm_type_toString(_cti_wlmProto->wlm_type));
	return 0;
}

int
_cti_wlm_getNumAppNodes_none(void *a1)
{
	_cti_set_error("wlm_getNumAppNodes() not supported for %s", cti_wlm_type_toString(_cti_wlmProto->wlm_type));
	return 0;
}

char **
_cti_wlm_getAppHostsList_none(void *a1)
{
	_cti_set_error("wlm_getAppHostsList() not supported for %s", cti_wlm_type_toString(_cti_wlmProto->wlm_type));
	return NULL;
}

cti_hostsList_t *
_cti_wlm_getAppHostsPlacement_none(void *a1)
{
	_cti_set_error("wlm_getAppHostsPlacement() not supported for %s", cti_wlm_type_toString(_cti_wlmProto->wlm_type));
	return NULL;
}

char *
_cti_wlm_getHostName_none(void)
{
	_cti_set_error("wlm_getHostName() not supported for %s", cti_wlm_type_toString(_cti_wlmProto->wlm_type));
	return NULL;
}

char *
_cti_wlm_getLauncherHostName_none(void *a1)
{
	_cti_set_error("wlm_getLauncherHostName() not supported for %s", cti_wlm_type_toString(_cti_wlmProto->wlm_type));
	return NULL;
}

