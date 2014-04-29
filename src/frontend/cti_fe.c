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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "cti_fe.h"
#include "cti_defs.h"
#include "cti_error.h"
#include "alps_fe.h"

struct appList
{
	appEntry_t *		thisEntry;
	struct appList *	nextEntry;
};
typedef struct appList appList_t;

/* Static prototypes */
static appList_t *		_cti_growAppsList(void);
static void				_cti_reapAppsList(void);
static void				_cti_consumeAppEntry(appEntry_t *);
static void				_cti_reapAppEntry(cti_app_id_t);

// Global vars
static cti_app_id_t		_cti_app_id = 1;	// start counting from 1
static cti_wlm_type 	_cti_current_wlm = CTI_WLM_NONE;
static appList_t *		_cti_my_apps	= NULL;	// global list pertaining to known application sessions

// Constructor function
void __attribute__((constructor))
_cti_init(void)
{

	// TODO: Add wlm_detect here, then call proper init function
	// In the future this should be able to handle multiple WLM types.
	
	_cti_current_wlm = CTI_WLM_ALPS;
	if (_cti_alps_init())
	{
		// We failed to init, so ensure we set the WLM to none.
		_cti_current_wlm = CTI_WLM_NONE;
		return;
	}
}

// Destructor function
void __attribute__ ((destructor))
_cti_fini(void)
{
	switch (_cti_current_wlm)
	{
		case CTI_WLM_ALPS:
			_cti_alps_fini();
			break;
			
		case CTI_WLM_CRAY_SLURM:
		case CTI_WLM_SLURM:
		case CTI_WLM_MULTI:
		case CTI_WLM_NONE:
			break;
	}

	_cti_current_wlm = CTI_WLM_NONE;

	return;
}

/* static functions */

static appList_t *
_cti_growAppsList()
{
	appList_t *	newEntry;
	appList_t *	lstPtr;
	
	// alloc space for the new list entry
	if ((newEntry = malloc(sizeof(appList_t))) == (void *)0)
	{
		_cti_set_error("malloc failed.");
		return NULL;
	}
	memset(newEntry, 0, sizeof(appList_t));     // clear it to NULL
	
	// if _cti_my_apps is null, this is the new head of the list
	if ((lstPtr = _cti_my_apps) == (appList_t *)NULL)
	{
		_cti_my_apps = newEntry;
	} else
	{
		// we need to iterate through the list to find the open next entry
		while (lstPtr->nextEntry != (appList_t *)NULL)
		{
			lstPtr = lstPtr->nextEntry;
		}
		lstPtr->nextEntry = newEntry;
	}
	
	// return the pointer to the new entry
	return newEntry;
}

// this function is used to undo the operation performed by _cti_growAppsList
static void
_cti_reapAppsList()
{
	appList_t *	lstPtr;

	// sanity check
	if ((lstPtr = _cti_my_apps) == (appList_t *)NULL)
		return;
		
	// if this was the first entry, lets remove it
	if (lstPtr->nextEntry == (appList_t *)NULL)
	{
		free(lstPtr);
		_cti_my_apps = (appList_t *)NULL;
		return;
	}
	
	// iterate through until we find the entry whos next entry has a null next entry ;)
	// i.e. magic - this works because _cti_growAppsList always places the new appList_t entry
	// at the end of the list
	while (lstPtr->nextEntry->nextEntry != (appList_t *)NULL)
	{
		lstPtr = lstPtr->nextEntry;
	}
	// the next entry has a null next entry so we need to free the next entry
	free(lstPtr->nextEntry);
	// now we need to set this entries next entry to null
	lstPtr->nextEntry = (appList_t *)NULL;
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
	while (lstPtr->thisEntry == NULL)
	{
		// if this is the only object in the list, then delete the entire list
		if ((lstPtr = lstPtr->nextEntry) == NULL)
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
	while (lstPtr->thisEntry->appId != appId)
	{
		prePtr = lstPtr;
		if ((lstPtr = lstPtr->nextEntry) == NULL)
		{
			// there are no more entries and we didn't find the appId
			return;
		}
	}
	
	// check to see if this was the first entry in the global _cti_my_apps list
	if (prePtr == lstPtr)
	{
		// point the global _cti_my_apps list to the next entry
		_cti_my_apps = lstPtr->nextEntry;
		// consume the appEntry_t object for this entry in the list
		_cti_consumeAppEntry(lstPtr->thisEntry);
		// free the list object
		free(lstPtr);
	} else
	{
		// we are at some point midway through the global _cti_my_apps list
		
		// point the previous entries next entry to the list pointers next entry
		// this bypasses the current list pointer
		prePtr->nextEntry = lstPtr->nextEntry;
		// consume the appEntry_t object for this entry in the list
		_cti_consumeAppEntry(lstPtr->thisEntry);
		// free the list object
		free(lstPtr);
	}
	
	// done
	return;
}

/* API defined functions start here */

cti_app_id_t
_cti_newAppEntry(cti_wlm_type wlm, const char *toolPath, void *wlm_obj, obj_destroy destroy)
{
	appList_t *		lstPtr;
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
	
	// grow the global _cti_my_apps list and get its new appList_t entry
	if ((lstPtr = _cti_growAppsList()) == NULL)
	{
		// error string is already set
		return 0;
	}
	
	// create the new appEntry_t object
	if ((this = malloc(sizeof(appEntry_t))) == NULL)
	{
		_cti_set_error("malloc failed.");
		// get rid of the appList_t object that we added to the list
		// since we failed
		_cti_reapAppsList();
		
		// its safe to return now without having a list corruption
		return 0;
	}
	memset(this, 0, sizeof(appEntry_t));     // clear it to NULL
	
	// set the members
	this->appId = _cti_app_id++;	// assign this to the next id.
	this->wlm = wlm;
	this->toolPath = strdup(toolPath);
	this->_wlmObj = wlm_obj;
	this->_wlmDestroy = destroy;
	
	// save the new appEntry_t object into the returned appList_t object that
	// the call to _cti_growAppsList gave us.
	lstPtr->thisEntry = this;
	
	return this->appId;
}

appEntry_t *
_cti_findAppEntry(cti_app_id_t appId)
{
	appList_t *	lstPtr;
	
	// ensure _cti_my_apps isn't empty
	if ((lstPtr = _cti_my_apps) == NULL)
	{
		_cti_set_error("The appId %d is not registered.", (int)appId);
		return NULL;
	}
		
	// BUG 795026 fix - If the head thisEntry is NULL, then we were interrupted
	// while in the _cti_newAppEntry routine. Reap the list of this junk entry.
	if (lstPtr->thisEntry == NULL)
	{
		_cti_set_error("The appId %d is not registered.", (int)appId);
		_cti_reapAppsList();
		// There are no entries in the list.
		return NULL;
	}
	
	// iterate through the _cti_my_apps list
	while (lstPtr->thisEntry->appId != appId)
	{
		// make lstPtr point to the next entry
		if ((lstPtr = lstPtr->nextEntry) == NULL)
		{
			// if lstPtr is null, we are at the end of the list
			// so the entry for appId doesn't exist
			_cti_set_error("The appId %d is not registered.", (int)appId);
			return NULL;
		}
		
		// BUG 795026 fix - if thisEntry is NULL, we had a valid app in the list
		// and were interrupted at some point after that. Reap the list of this
		// junk entry. It should be the last entry on the list since this is the
		// first thing called when registering a new appId.
		if (lstPtr->thisEntry == NULL)
		{
			// There are no more entires in the list
			_cti_set_error("The appId %d is not registered.", (int)appId);
			_cti_reapAppsList();
			return NULL;
		}
	}
	
	// if we get here, we found the appEntry_t that corresponds to appId
	return lstPtr->thisEntry;
}

appEntry_t *
_cti_findAppEntryByJobId(void *wlm_id)
{
	appList_t *	lstPtr;
	int			found = 0;
	
	// ensure _cti_my_apps isn't empty
	if ((lstPtr = _cti_my_apps) == NULL)
	{
		_cti_set_error("The wlm id is not registered.");
		return NULL;
	}
		
	// BUG 795026 fix - If the head thisEntry is NULL, then we were interrupted
	// while in the _cti_newAppEntry routine. Reap the list of this junk entry.
	if (lstPtr->thisEntry == NULL)
	{
		_cti_set_error("The wlm id is not registered.");
		_cti_reapAppsList();
		// There are no entries in the list.
		return NULL;
	}
	
	// iterate through the _cti_my_apps list
	while (1)
	{
		// Call the appropriate find function based on the wlm
		switch (lstPtr->thisEntry->wlm)
		{
			case CTI_WLM_ALPS:
				found = _cti_alps_compJobId(lstPtr->thisEntry->_wlmObj, wlm_id);
				break;
			
			case CTI_WLM_CRAY_SLURM:
			case CTI_WLM_SLURM:
			case CTI_WLM_MULTI:
			case CTI_WLM_NONE:
				// do nothing
				break;
		}
	
		// break if found
		if (found)
			break;
		
		// make lstPtr point to the next entry
		if ((lstPtr = lstPtr->nextEntry) == NULL)
		{
			// if lstPtr is null, we are at the end of the list
			// so the entry for appId doesn't exist
			_cti_set_error("The wlm id is not registered.");
			return NULL;
		}
		
		// BUG 795026 fix - if thisEntry is NULL, we had a valid app in the list
		// and were interrupted at some point after that. Reap the list of this
		// junk entry. It should be the last entry on the list since this is the
		// first thing called when registering a new appId.
		if (lstPtr->thisEntry == NULL)
		{
			// There are no more entires in the list
			_cti_set_error("The wlm id is not registered.");
			_cti_reapAppsList();
			return NULL;
		}
	}
	
	// if we get here, we found the appEntry_t that corresponds to appId
	return lstPtr->thisEntry;
}

int
_cti_setTransferObj(cti_app_id_t appId, void *transferObj, obj_destroy transferDestroy)
{
	appEntry_t *	app_ptr;
	
	// try to find an entry in the _cti_my_apps list for the appId
	if ((app_ptr = _cti_findAppEntry(appId)) == NULL)
	{
		// couldn't find the entry associated with the appId
		// error string already set
		return 1;
	}
	
	// set the members
	app_ptr->_transferObj = transferObj;
	app_ptr->_transferDestroy = transferDestroy;
	
	return 0;
}

cti_wlm_type
cti_current_wlm(void)
{
	return _cti_current_wlm;
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
	switch (app_ptr->wlm)
	{
		case CTI_WLM_ALPS:
			return _cti_alps_getNumAppPEs(app_ptr->_wlmObj);
			
		case CTI_WLM_CRAY_SLURM:
		case CTI_WLM_SLURM:
			_cti_set_error("Current WLM is not yet supported.");
			return 0;
			
		case CTI_WLM_MULTI:
			// TODO - add argument to allow caller to select preferred wlm if there is
			// ever multiple WLMs present on the system.
			_cti_set_error("Multiple workload managers present! This is not yet supported.");
			return 0;
			
		case CTI_WLM_NONE:
			_cti_set_error("No valid workload manager detected.");
			return 0;
	}
	
	// should not get here
	_cti_set_error("At impossible exit.");
	return 0;
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
	switch (app_ptr->wlm)
	{
		case CTI_WLM_ALPS:
			return _cti_alps_getNumAppNodes(app_ptr->_wlmObj);
			
		case CTI_WLM_CRAY_SLURM:
		case CTI_WLM_SLURM:
			_cti_set_error("Current WLM is not yet supported.");
			return 0;
			
		case CTI_WLM_MULTI:
			// TODO - add argument to allow caller to select preferred wlm if there is
			// ever multiple WLMs present on the system.
			_cti_set_error("Multiple workload managers present! This is not yet supported.");
			return 0;
			
		case CTI_WLM_NONE:
			_cti_set_error("No valid workload manager detected.");
			return 0;
	}
	
	// should not get here
	_cti_set_error("At impossible exit.");
	return 0;
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
	switch (app_ptr->wlm)
	{
		case CTI_WLM_ALPS:
			return _cti_alps_getAppHostsList(app_ptr->_wlmObj);
			
		case CTI_WLM_CRAY_SLURM:
		case CTI_WLM_SLURM:
			_cti_set_error("Current WLM is not yet supported.");
			return 0;
			
		case CTI_WLM_MULTI:
			// TODO - add argument to allow caller to select preferred wlm if there is
			// ever multiple WLMs present on the system.
			_cti_set_error("Multiple workload managers present! This is not yet supported.");
			return 0;
			
		case CTI_WLM_NONE:
			_cti_set_error("No valid workload manager detected.");
			return 0;
	}
	
	// should not get here
	_cti_set_error("At impossible exit.");
	return 0;
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
	switch (app_ptr->wlm)
	{
		case CTI_WLM_ALPS:
			return _cti_alps_getAppHostsPlacement(app_ptr->_wlmObj);
			
		case CTI_WLM_CRAY_SLURM:
		case CTI_WLM_SLURM:
			_cti_set_error("Current WLM is not yet supported.");
			return 0;
			
		case CTI_WLM_MULTI:
			// TODO - add argument to allow caller to select preferred wlm if there is
			// ever multiple WLMs present on the system.
			_cti_set_error("Multiple workload managers present! This is not yet supported.");
			return 0;
			
		case CTI_WLM_NONE:
			_cti_set_error("No valid workload manager detected.");
			return 0;
	}
	
	// should not get here
	_cti_set_error("At impossible exit.");
	return 0;
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
cti_getHostName()
{
	// Call the appropriate function based on the wlm
	switch (_cti_current_wlm)
	{
		case CTI_WLM_ALPS:
			return _cti_alps_getHostName();
			
		case CTI_WLM_CRAY_SLURM:
		case CTI_WLM_SLURM:
			_cti_set_error("Current WLM is not yet supported.");
			return NULL;
			
		case CTI_WLM_MULTI:
			// TODO - add argument to allow caller to select preferred wlm if there is
			// ever multiple WLMs present on the system.
			_cti_set_error("Multiple workload managers present! This is not yet supported.");
			return NULL;
			
		case CTI_WLM_NONE:
			_cti_set_error("No valid workload manager detected.");
			return NULL;
	}
	
	// should not get here
	_cti_set_error("At impossible exit.");
	return NULL;
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
	switch (app_ptr->wlm)
	{
		case CTI_WLM_ALPS:
			return _cti_alps_getLauncherHostName(app_ptr->_wlmObj);
			
		case CTI_WLM_CRAY_SLURM:
		case CTI_WLM_SLURM:
			_cti_set_error("Current WLM is not yet supported.");
			return 0;
			
		case CTI_WLM_MULTI:
			// TODO - add argument to allow caller to select preferred wlm if there is
			// ever multiple WLMs present on the system.
			_cti_set_error("Multiple workload managers present! This is not yet supported.");
			return 0;
			
		case CTI_WLM_NONE:
			_cti_set_error("No valid workload manager detected.");
			return 0;
	}
	
	// should not get here
	_cti_set_error("At impossible exit.");
	return 0;
}

