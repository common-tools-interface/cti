/*********************************************************************************\
 * alps_application.c - A interface to the alps toolhelper functions. This provides
 *		      support functions for the other APIs defined by this
 *		      interface.
 *
 * © 2011-2013 Cray Inc.  All Rights Reserved.
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
 *********************************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>

#include "alps/libalps.h"

#include "alps_application.h"
#include "alps_run.h"

#include "useful/useful.h"

/* Static prototypes */
static appList_t *		_cti_growAppsList(void);
static void				_cti_reapAppsList(void);
static void				_cti_reapAppEntry(uint64_t);
static void				_cti_consumeAppEntry(appEntry_t *);
static serviceNode_t *	_cti_getSvcNodeInfo(void);

/* global variables */
static serviceNode_t *	_cti_svcNid		= NULL;	// service node information
static appList_t *		_cti_my_apps	= NULL;	// global list pertaining to known aprun sessions

static appList_t *
_cti_growAppsList()
{
	appList_t *	newEntry;
	appList_t *	lstPtr;
	
	// alloc space for the new list entry
	if ((newEntry = malloc(sizeof(appList_t))) == (void *)0)
	{
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
_cti_reapAppEntry(uint64_t apid)
{
	appList_t *	lstPtr;
	appList_t *	prePtr;
	
	// sanity check
	if (((lstPtr = _cti_my_apps) == (appList_t *)NULL) || (apid <= 0))
		return;
		
	prePtr = _cti_my_apps;
	
	// this shouldn't happen, but doing so will prevent a segfault if the list gets corrupted
	while (lstPtr->thisEntry == NULL)
	{
		// if this is the only object in the list, then delete the entire list
		if ((lstPtr = lstPtr->nextEntry) == (appList_t *)NULL)
		{
			_cti_my_apps = (appList_t *)NULL;
			free(lstPtr);
			return;
		}
		// otherwise point _cti_my_apps to the lstPtr and free the corrupt entry
		_cti_my_apps = lstPtr;
		free(prePtr);
		prePtr = _cti_my_apps;
	}
	
	// we need to locate the position of the appList_t object that we need to remove
	while (lstPtr->thisEntry->apid != apid)
	{
		prePtr = lstPtr;
		if ((lstPtr = lstPtr->nextEntry) == (appList_t *)NULL)
		{
			// there are no more entries and we didn't find the apid
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

static void
_cti_consumeAppEntry(appEntry_t *entry)
{ 
	// sanity check
	if (entry == NULL)
		return;
		
	// eat the alpsInfo_t member
	// cmdDetail and places were malloc'ed so we might find them tasty
	if (entry->alpsInfo.cmdDetail != (cmdDetail_t *)NULL)
		free(entry->alpsInfo.cmdDetail);
	if (entry->alpsInfo.places != (placeList_t *)NULL)
		free(entry->alpsInfo.places);
		
	// free the toolPath
	if (entry->toolPath != NULL)
	{
		free(entry->toolPath);
	}
		
	// Check to see if there is a _transferObj
	if (entry->_transferObj != NULL)
	{
		// Ensure that there is a destroy function defined
		if (entry->_destroyObj != NULL)
		{
			// Call the destroy function
			(*(entry->_destroyObj))(entry->_transferObj);
		}
	}
	
	// nom nom the final appEntry_t object
	free(entry);
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
		return NULL;
	}
	memset(my_node, 0, sizeof(serviceNode_t));     // clear it to NULL
	
	// open up the file defined in the alps header containing our node id (nid)
	if ((alps_fd = fopen(ALPS_XT_NID, "r")) == NULL)
	{
		free(my_node);
		return NULL;
	}
	
	// we expect this file to have a numeric value giving our current nid
	if (fgets(file_buf, BUFSIZ, alps_fd) == NULL)
	{
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
		free(my_node);
		return NULL;
	}
	
	// we expect this file to contain a string which represents our interconnect hostname
	if (fgets(file_buf, BUFSIZ, alps_fd) == NULL)
	{
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

/* API defined functions start here */

appEntry_t *
_cti_findApp(uint64_t apid)
{
	appList_t *	lstPtr;
	
	// ensure _cti_my_apps isn't empty
	if ((lstPtr = _cti_my_apps) == NULL)
		return NULL;
		
	// BUG 795026 fix - If the head thisEntry is NULL, then we were interrupted
	// while in the _cti_newApp routine. Reap the list of this junk entry.
	if (lstPtr->thisEntry == NULL)
	{
		_cti_reapAppsList();
		// There are no entries in the list.
		return NULL;
	}
	
	// iterate through the _cti_my_apps list
	while (lstPtr->thisEntry->apid != apid)
	{
		// make lstPtr point to the next entry
		if ((lstPtr = lstPtr->nextEntry) == NULL)
		{
			// if lstPtr is null, we are at the end of the list
			// so the entry for apid doesn't exist
			return NULL;
		}
		
		// BUG 795026 fix - if thisEntry is NULL, we had a valid app in the list
		// and were interrupted at some point after that. Reap the list of this
		// junk entry. It should be the last entry on the list since this is the
		// first thing called when registering a new apid.
		if (lstPtr->thisEntry == NULL)
		{
			_cti_reapAppsList();
			// There are no more entires in the list
			return NULL;
		}
	}
	
	// if we get here, we found the appEntry_t that corresponds to apid
	return lstPtr->thisEntry;
}

appEntry_t *
_cti_newApp(uint64_t apid)
{
	appList_t *		lstPtr;
	appEntry_t *	this;
	// Used to determine CLE version
	struct stat 	statbuf;
	
	// grow the global _cti_my_apps list and get its new appList_t entry
	if ((lstPtr = _cti_growAppsList()) == NULL)
	{
		return NULL;
	}
	
	// create the new appEntry_t object
	if ((this = malloc(sizeof(appEntry_t))) == NULL)
	{
		// get rid of the appList_t object that we added to the list
		// since we failed
		_cti_reapAppsList();
		
		// its safe to return now without having a list corruption
		return NULL;
	}
	memset(this, 0, sizeof(appEntry_t));     // clear it to NULL
	
	// set the apid member
	this->apid = apid;
	
	// retrieve detailed information about our app
	// save this information into the struct
	if (alps_get_appinfo(this->apid, &this->alpsInfo.appinfo, &this->alpsInfo.cmdDetail, &this->alpsInfo.places) != 1)
	{
		_cti_reapAppsList();
		_cti_consumeAppEntry(this);
		return NULL;
	}
	
	// Note that cmdDetail is a two dimensional array with appinfo.numCmds elements.
	// Note that places is a two dimensional array with appinfo.numPlaces elements.
	// These both were malloc'ed and need to be free'ed by the user.
	
	// save pe0 NID
	this->alpsInfo.pe0Node = this->alpsInfo.places[0].nid;
	
	// Check to see if this system is using the new OBS system for the alps
	// dependencies. This will affect the way we set the toolPath for the backend
	if (stat(ALPS_OBS_LOC, &statbuf) == -1)
	{
		// Could not stat ALPS_OBS_LOC, assume it's using the old format.
		if (asprintf(&this->toolPath, OLD_TOOLHELPER_DIR, (long long unsigned int)apid, (long long unsigned int)apid) <= 0)
		{
			fprintf(stderr, "asprintf failed\n");
			_cti_reapAppsList();
			_cti_consumeAppEntry(this);
			return NULL;
		}
	} else
	{
		// Assume it's using the OBS format
		if (asprintf(&this->toolPath, OBS_TOOLHELPER_DIR, (long long unsigned int)apid, (long long unsigned int)apid) <= 0)
		{
			fprintf(stderr, "asprintf failed\n");
			_cti_reapAppsList();
			_cti_consumeAppEntry(this);
			return NULL;
		}
	}
	
	// save the new appEntry_t object into the returned appList_t object that
	// the call to _cti_growAppsList gave us.
	lstPtr->thisEntry = this;
	
	return this;
}

// this function creates a new appEntry_t object for the app
// used by the alps_run functions
int
cti_registerApid(uint64_t apid)
{
	// sanity check
	if (apid <= 0)
		return 1;
		
	// try to find an entry in the _cti_my_apps list for the apid
	if (_cti_findApp(apid) == NULL)
	{
		// aprun pid not found in the global _cti_my_apps list
		// so lets create a new appEntry_t object for it
		if (_cti_newApp(apid) == NULL)
		{
			// we failed to create a new appEntry_t entry - catastrophic failure
			return 1;
		}
	}

	return 0;
}

void
cti_deregisterApid(uint64_t apid)
{
	// sanity check
	if (apid <= 0)
		return;
	
	// call the _cti_reapAppEntry function for this apid
	_cti_reapAppEntry(apid);
	
	// call the _cti_reapAprunInv function for this apid
	// This is for applications that were launched by this interface, but we
	// no longer want to control them.
	_cti_reapAprunInv(apid);
}

uint64_t
cti_getApid(pid_t aprunPid)
{
	// sanity check
	if (aprunPid <= 0)
		return 0;
		
	// ensure the _cti_svcNid exists
	if (_cti_svcNid == NULL)
	{
		if ((_cti_svcNid = _cti_getSvcNodeInfo()) == NULL)
		{
			// couldn't get the svcnode info for some odd reason
			return 0;
		}
	}
		
	return alps_get_apid(_cti_svcNid->nid, aprunPid);
}

char *
cti_getNodeCName()
{
	// ensure the _cti_svcNid exists
	if (_cti_svcNid == NULL)
	{
		if ((_cti_svcNid = _cti_getSvcNodeInfo()) == NULL)
		{
			// couldn't get the svcnode info for some odd reason
			return NULL;
		}
	}
	
	// return the cname
	return strdup(_cti_svcNid->cname);
}

int
cti_getNodeNid()
{
	// ensure the _cti_svcNid exists
	if (_cti_svcNid == NULL)
	{
		if ((_cti_svcNid = _cti_getSvcNodeInfo()) == NULL)
		{
			// couldn't get the svcnode info for some odd reason
			return -1;
		}
	}
	
	// return the nid
	return _cti_svcNid->nid;
}

int
cti_getAppNid(uint64_t apid)
{
	appEntry_t *	app_ptr;
	
	// sanity check
	if (apid <= 0)
		return -1;
		
	// try to find an entry in the _cti_my_apps list for the apid
	if ((app_ptr = _cti_findApp(apid)) == NULL)
	{
		// couldn't find the entry associated with the apid
		return -1;
	}

	return app_ptr->alpsInfo.appinfo.aprunNid;
}

int
cti_getNumAppPEs(uint64_t apid)
{
	appEntry_t *	app_ptr;
	
	// sanity check
	if (apid <= 0)
		return 0;
		
	// try to find an entry in the _cti_my_apps list for the apid
	if ((app_ptr = _cti_findApp(apid)) == NULL)
	{
		// couldn't find the entry associated with the apid
		return 0;
	}
	
	return app_ptr->alpsInfo.cmdDetail->width;
}

int
cti_getNumAppNodes(uint64_t apid)
{
	appEntry_t *	app_ptr;
	
	// sanity check
	if (apid <= 0)
		return 0;
		
	// try to find an entry in the _cti_my_apps list for the apid
	if ((app_ptr = _cti_findApp(apid)) == NULL)
	{
		// couldn't find the entry associated with the apid
		return 0;
	}
	
	return app_ptr->alpsInfo.cmdDetail->nodeCnt;
}

char **
cti_getAppHostsList(uint64_t apid)
{
	appEntry_t *	app_ptr;
	int				curNid, numNid;
	char **			hosts;
	char			hostEntry[ALPS_XT_HOSTNAME_LEN];
	int				i;
	
	// sanity check
	if (apid <= 0)
		return NULL;
		
	// try to find an entry in the _cti_my_apps list for the apid
	if ((app_ptr = _cti_findApp(apid)) == NULL)
	{
		// couldn't find the entry associated with the apid
		return NULL;
	}
	
	// ensure app_ptr->alpsInfo.cmdDetail->nodeCnt is non-zero
	if ( app_ptr->alpsInfo.cmdDetail->nodeCnt <= 0 )
	{
		// no nodes in the application
		return NULL;
	}
	
	// allocate space for the hosts list, add an extra entry for the null terminator
	if ((hosts = calloc(app_ptr->alpsInfo.cmdDetail->nodeCnt + 1, sizeof(char *))) == (void *)0)
	{
		// calloc failed
		return NULL;
	}
	
	// set the first entry
	numNid = 1;
	curNid = app_ptr->alpsInfo.places[0].nid;
	// create the hostname string for this entry and place it into the list
	snprintf(hostEntry, ALPS_XT_HOSTNAME_LEN, ALPS_XT_HOSTNAME_FMT, curNid);
	hosts[0] = strdup(hostEntry);
	// clear the buffer
	memset(hostEntry, 0, ALPS_XT_HOSTNAME_LEN);
	
	// set the final entry to null, calloc doesn't guarantee null'ed memory
	hosts[app_ptr->alpsInfo.cmdDetail->nodeCnt] = NULL;
	
	// check to see if we can skip iterating through the places list due to there being only one nid allocated
	if (numNid == app_ptr->alpsInfo.cmdDetail->nodeCnt)
	{
		// we are done
		return hosts;
	}
	
	// iterate through the placelist to find the node id's for the PEs
	for (i=1; i < app_ptr->alpsInfo.appinfo.numPlaces; i++)
	{
		if (curNid == app_ptr->alpsInfo.places[i].nid)
		{
			continue;
		}
		// we have a new unique nid
		curNid = app_ptr->alpsInfo.places[i].nid;
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
cti_getAppHostsPlacement(uint64_t apid)
{
	appEntry_t *		app_ptr;
	int					curNid, numNid;
	int					numPe;
	cti_host_t *		curHost;
	cti_hostsList_t *	placement_list;
	char				hostEntry[ALPS_XT_HOSTNAME_LEN];
	int					i;
	
	// sanity check
	if (apid <= 0)
		return NULL;
		
	// try to find an entry in the _cti_my_apps list for the apid
	if ((app_ptr = _cti_findApp(apid)) == NULL)
	{
		// couldn't find the entry associated with the apid
		return NULL;
	}

	// ensure the app_ptr->alpsInfo.cmdDetail->nodeCnt is non-zero
	if ( app_ptr->alpsInfo.cmdDetail->nodeCnt <= 0 )
	{
		// no nodes in the application
		return NULL;
	}
	
	// allocate space for the cti_hostsList_t struct
	if ((placement_list = malloc(sizeof(cti_hostsList_t))) == (void *)0)
	{
		// malloc failed
		return NULL;
	}
	
	// set the number of hosts for the application
	placement_list->numHosts = app_ptr->alpsInfo.cmdDetail->nodeCnt;
	
	// allocate space for the cti_host_t structs inside the placement_list
	if ((placement_list->hosts = malloc(placement_list->numHosts * sizeof(cti_host_t))) == (void *)0)
	{
		// malloc failed
		free(placement_list);
		return NULL;
	}
	// clear the nodeHostPlacment_t memory
	memset(placement_list->hosts, 0, placement_list->numHosts * sizeof(cti_host_t));
	
	// set the first entry
	numNid = 1;
	numPe  = 1;
	curNid = app_ptr->alpsInfo.places[0].nid;
	curHost = &placement_list->hosts[0];
	
	// create the hostname string for this entry and place it into the list
	snprintf(hostEntry, ALPS_XT_HOSTNAME_LEN, ALPS_XT_HOSTNAME_FMT, curNid);
	curHost->hostname = strdup(hostEntry);
	
	// clear the buffer
	memset(hostEntry, 0, ALPS_XT_HOSTNAME_LEN);
	
	// check to see if we can skip iterating through the places list due to there being only one nid allocated
	if (numNid == app_ptr->alpsInfo.cmdDetail->nodeCnt)
	{
		// we have no more hostnames to process
		// all nids in the places list will belong to our current host
		// so write the numPlaces into the current host type and return
		curHost->numPes = app_ptr->alpsInfo.appinfo.numPlaces;
		return placement_list;
	}
	
	// iterate through the placelist to find the node id's for the PEs
	for (i=1; i < app_ptr->alpsInfo.appinfo.numPlaces; i++)
	{
		if (curNid == app_ptr->alpsInfo.places[i].nid)
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
		curNid = app_ptr->alpsInfo.places[i].nid;
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

void
cti_destroy_hostsList(cti_hostsList_t *placement_list)
{
	// sanity check
	if (placement_list == NULL)
		return;
		
	if (placement_list->hosts != NULL)
		free(placement_list->hosts);
		
	free(placement_list);
}

