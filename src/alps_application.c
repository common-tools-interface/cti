/*********************************************************************************\
 * alps_application.c - A interface to the alps toolhelper functions. This provides
 *		      support functions for the other APIs defined by this
 *		      interface.
 *
 * © 2011-2012 Cray Inc.  All Rights Reserved.
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

#include "alps/libalps.h"

#include "alps_application.h"
#include "alps_run.h"

#include "useful/useful.h"

/* Static prototypes */
static appList_t *		growAppsList(void);
static void				reapAppsList(void);
static void				reapAppEntry(uint64_t);
static void				consumeAppEntry(appEntry_t *);
static serviceNode_t *	getSvcNodeInfo(void);

/* global variables */
static serviceNode_t *	svcNid = (serviceNode_t *)NULL;	// service node information
static appList_t *		my_apps = (appList_t *)NULL;	// global list pertaining to known aprun sessions

/* 
** This list may need to be updated with each new release of CNL.
*/
static const char * __ignored_libs[] = {
	"libdl.so.2",
	"libc.so.6",
	"libvolume_id.so.1",
	"libcidn.so.1",
	"libnsl.so.1",
	"librt.so.1",
	"libutil.so.1",
	"libpthread.so.0",
	"libudev.so.0",
	"libcrypt.so.1",
	"libz.so.1",
	"libm.so.6",
	"libnss_files.so.2",
	NULL
};

static appList_t *
growAppsList()
{
	appList_t *	newEntry;
	appList_t *	lstPtr;
	
	// alloc space for the new list entry
	if ((newEntry = malloc(sizeof(appList_t))) == (void *)0)
	{
		return (appList_t *)NULL;
	}
	memset(newEntry, 0, sizeof(appList_t));     // clear it to NULL
	
	// if my_apps is null, this is the new head of the list
	if ((lstPtr = my_apps) == (appList_t *)NULL)
	{
		my_apps = newEntry;
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

// this function is used to undo the operation performed by growAppsList
static void
reapAppsList()
{
	appList_t *	lstPtr;

	// sanity check
	if ((lstPtr = my_apps) == (appList_t *)NULL)
		return;
		
	// if this was the first entry, lets remove it
	if (lstPtr->nextEntry == (appList_t *)NULL)
	{
		free(lstPtr);
		my_apps = (appList_t *)NULL;
		return;
	}
	
	// iterate through until we find the entry whos next entry has a null next entry ;)
	// i.e. magic - this works because growAppsList always places the new appList_t entry
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
reapAppEntry(uint64_t apid)
{
	appList_t *	lstPtr;
	appList_t *	prePtr;
	
	// sanity check
	if (((lstPtr = my_apps) == (appList_t *)NULL) || (apid <= 0))
		return;
		
	prePtr = my_apps;
	
	// this shouldn't happen, but doing so will prevent a segfault if the list gets corrupted
	while (lstPtr->thisEntry == (appEntry_t *)NULL)
	{
		// if this is the only object in the list, then delete the entire list
		if ((lstPtr = lstPtr->nextEntry) == (appList_t *)NULL)
		{
			my_apps = (appList_t *)NULL;
			free(lstPtr);
			return;
		}
		// otherwise point my_apps to the lstPtr and free the corrupt entry
		my_apps = lstPtr;
		free(prePtr);
		prePtr = my_apps;
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
	
	// check to see if this was the first entry in the global my_apps list
	if (prePtr == lstPtr)
	{
		// point the global my_apps list to the next entry
		my_apps = lstPtr->nextEntry;
		// consume the appEntry_t object for this entry in the list
		consumeAppEntry(lstPtr->thisEntry);
		// free the list object
		free(lstPtr);
	} else
	{
		// we are at some point midway through the global my_apps list
		
		// point the previous entries next entry to the list pointers next entry
		// this bypasses the current list pointer
		prePtr->nextEntry = lstPtr->nextEntry;
		// consume the appEntry_t object for this entry in the list
		consumeAppEntry(lstPtr->thisEntry);
		// free the list object
		free(lstPtr);
	}
	
	// done
	return;
}

static void
consumeAppEntry(appEntry_t *entry)
{ 
	// sanity check
	if (entry == (appEntry_t *)NULL)
		return;
		
	// eat the alpsInfo_t member
	// cmdDetail and places were malloc'ed so we might find them tasty
	if (entry->alpsInfo.cmdDetail != (cmdDetail_t *)NULL)
		free(entry->alpsInfo.cmdDetail);
	if (entry->alpsInfo.places != (placeList_t *)NULL)
		free(entry->alpsInfo.places);
	
	// eat each of the string lists
	consumeStringList(entry->shipped_execs);
	consumeStringList(entry->shipped_libs);
	consumeStringList(entry->shipped_files);
	
	// nom nom the final appEntry_t object
	free(entry);
}

/*
*       getSvcNodeInfo - read cname and nid from alps defined system locations
*
*       args: None.
*
*       return value: serviceNode_t pointer containing the service nodes cname and
*       nid, or else NULL on error.
*
*/
static serviceNode_t *
getSvcNodeInfo()
{
	FILE *alps_fd;	  // ALPS NID/CNAME file stream
	char file_buf[BUFSIZ];  // file read buffer
	serviceNode_t *my_node; // return struct containing service node info
	
	// allocate the serviceNode_t object, its the callers responsibility to
	// free this.
	if ((my_node = malloc(sizeof(serviceNode_t))) == (void *)0)
	{
		return (serviceNode_t *)NULL;
	}
	memset(my_node, 0, sizeof(serviceNode_t));     // clear it to NULL
	
	// open up the file defined in the alps header containing our node id (nid)
	if ((alps_fd = fopen(ALPS_XT_NID, "r")) == NULL)
	{
		free(my_node);
		return (serviceNode_t *)NULL;
	}
	
	// we expect this file to have a numeric value giving our current nid
	if (fgets(file_buf, BUFSIZ, alps_fd) == NULL)
	{
		free(my_node);
		fclose(alps_fd);
		return (serviceNode_t *)NULL;
	}
	// convert this to an integer value
	my_node->nid = atoi(file_buf);
	
	// close the file stream
	fclose(alps_fd);
	
	// open up the cname file
	if ((alps_fd = fopen(ALPS_XT_CNAME, "r")) == NULL)
	{
		free(my_node);
		return (serviceNode_t *)NULL;
	}
	
	// we expect this file to contain a string which represents our interconnect hostname
	if (fgets(file_buf, BUFSIZ, alps_fd) == NULL)
	{
		free(my_node);
		fclose(alps_fd);
		return (serviceNode_t *)NULL;
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
findApp(uint64_t apid)
{
	appList_t *	lstPtr;
	
	// ensure my_apps isn't empty
	if ((lstPtr = my_apps) == (appList_t *)NULL)
		return (appEntry_t *)NULL;
	
	// iterate through the my_apps list
	while (lstPtr->thisEntry->apid != apid)
	{
		// make lstPtr point to the next entry
		if ((lstPtr = lstPtr->nextEntry) == (appList_t *)NULL)
		{
			// if lstPtr is null, we are at the end of the list
			// so the entry for apid doesn't exist
			return (appEntry_t *)NULL;
		}
	}
	
	// if we get here, we found the appEntry_t that corresponds to apid
	return lstPtr->thisEntry;
}

appEntry_t *
newApp(uint64_t apid)
{
	appList_t *		lstPtr;
	appEntry_t *	this;
	const char **	ignore_ptr;
	
	// grow the global my_apps list and get its new appList_t entry
	if ((lstPtr = growAppsList()) == (appList_t *)NULL)
	{
		return (appEntry_t *)NULL;
	}
	
	// create the new appEntry_t object
	if ((this = malloc(sizeof(appEntry_t))) == (void *)NULL)
	{
		// get rid of the appList_t object that we added to the list
		// since we failed
		reapAppsList();
		
		// its safe to return now without having a list corruption
		return (appEntry_t *)NULL;
	}
	memset(this, 0, sizeof(appEntry_t));     // clear it to NULL
	
	// set the apid member
	this->apid = apid;
	
	// retrieve detailed information about our app
	// save this information into the struct
	if (alps_get_appinfo(this->apid, &this->alpsInfo.appinfo, &this->alpsInfo.cmdDetail, &this->alpsInfo.places) != 1)
	{
		reapAppsList();
		consumeAppEntry(this);
		return (appEntry_t *)NULL;
	}
	
	// Note that cmdDetail is a two dimensional array with appinfo.numCmds elements.
	// Note that places is a two dimensional array with appinfo.numPlaces elements.
	// These both were malloc'ed and need to be free'ed by the user.
	
	// save pe0 NID
	this->alpsInfo.pe0Node = this->alpsInfo.places[0].nid;
	
	// create the stringList_t objects for the three saved arrays
	if ((this->shipped_execs = newStringList()) == (stringList_t *)NULL)
	{
		reapAppsList();
		consumeAppEntry(this);
		return (appEntry_t *)NULL;
	}
	if ((this->shipped_libs = newStringList()) == (stringList_t *)NULL)
	{
		reapAppsList();
		consumeAppEntry(this);
		return (appEntry_t *)NULL;
	}
	// Add the ignored library strings to the shipped_libs string list.
	for (ignore_ptr=__ignored_libs; *ignore_ptr != NULL; ++ignore_ptr)
	{
		if (addString(this->shipped_libs, *ignore_ptr))
		{
			reapAppsList();
			consumeAppEntry(this);
			return (appEntry_t *)NULL;
		}
	}
	if ((this->shipped_files = newStringList()) == (stringList_t *)NULL)
	{
		reapAppsList();
		consumeAppEntry(this);
		return (appEntry_t *)NULL;
	}
	
	// save the new appEntry_t object into the returned appList_t object that
	// the call to growAppsList gave us.
	lstPtr->thisEntry = this;
	
	return this;
}

// this function creates a new appEntry_t object for the app
// used by the alps_run functions
int
registerApid(uint64_t apid)
{
	// sanity check
	if (apid <= 0)
		return 1;
		
	// try to find an entry in the my_apps list for the apid
	if (findApp(apid) == (appEntry_t *)NULL)
	{
		// aprun pid not found in the global my_apps list
		// so lets create a new appEntry_t object for it
		if (newApp(apid) == (appEntry_t *)NULL)
		{
			// we failed to create a new appEntry_t entry - catastrophic failure
			return 1;
		}
	}

	return 0;
}

void
deregisterApid(uint64_t apid)
{
	// sanity check
	if (apid <= 0)
		return;
	
	// call the reapAppEntry function for this apid
	reapAppEntry(apid);
	
	// call the reapAprunInv function for this apid
	// This is for applications that were launched by this interface, but we
	// no longer want to control them.
	reapAprunInv(apid);
}

uint64_t
getApid(pid_t aprunPid)
{
	// sanity check
	if (aprunPid <= 0)
		return 0;
		
	// ensure the svcNid exists
	if (svcNid == (serviceNode_t *)NULL)
	{
		if ((svcNid = getSvcNodeInfo()) == (serviceNode_t *)NULL)
		{
			// couldn't get the svcnode info for some odd reason
			return 0;
		}
	}
		
	return alps_get_apid(svcNid->nid, aprunPid);
}

char *
getNodeCName()
{
	// ensure the svcNid exists
	if (svcNid == (serviceNode_t *)NULL)
	{
		if ((svcNid = getSvcNodeInfo()) == (serviceNode_t *)NULL)
		{
			// couldn't get the svcnode info for some odd reason
			return (char *)NULL;
		}
	}
	
	// return the cname
	return strdup(svcNid->cname);
}

int
getNodeNid()
{
	// ensure the svcNid exists
	if (svcNid == (serviceNode_t *)NULL)
	{
		if ((svcNid = getSvcNodeInfo()) == (serviceNode_t *)NULL)
		{
			// couldn't get the svcnode info for some odd reason
			return -1;
		}
	}
	
	// return the nid
	return svcNid->nid;
}

int
getAppNid(uint64_t apid)
{
	appEntry_t *	app_ptr;
	
	// sanity check
	if (apid <= 0)
		return -1;
		
	// try to find an entry in the my_apps list for the apid
	if ((app_ptr = findApp(apid)) == (appEntry_t *)NULL)
	{
		// couldn't find the entry associated with the apid
		return -1;
	}

	return app_ptr->alpsInfo.appinfo.aprunNid;
}

int
getNumAppPEs(uint64_t apid)
{
	appEntry_t *	app_ptr;
	
	// sanity check
	if (apid <= 0)
		return 0;
		
	// try to find an entry in the my_apps list for the apid
	if ((app_ptr = findApp(apid)) == (appEntry_t *)NULL)
	{
		// couldn't find the entry associated with the apid
		return 0;
	}
	
	return app_ptr->alpsInfo.cmdDetail->width;
}

int
getNumAppNodes(uint64_t apid)
{
	appEntry_t *	app_ptr;
	
	// sanity check
	if (apid <= 0)
		return 0;
		
	// try to find an entry in the my_apps list for the apid
	if ((app_ptr = findApp(apid)) == (appEntry_t *)NULL)
	{
		// couldn't find the entry associated with the apid
		return 0;
	}
	
	return app_ptr->alpsInfo.cmdDetail->nodeCnt;
}

char **
getAppHostsList(uint64_t apid)
{
	appEntry_t *	app_ptr;
	int				curNid, numNid;
	char **			hosts;
	char			hostEntry[ALPS_XT_HOSTNAME_LEN];
	int				i;
	
	// sanity check
	if (apid <= 0)
		return (char **)NULL;
		
	// try to find an entry in the my_apps list for the apid
	if ((app_ptr = findApp(apid)) == (appEntry_t *)NULL)
	{
		// couldn't find the entry associated with the apid
		return (char **)NULL;
	}
	
	// ensure app_ptr->alpsInfo.cmdDetail->nodeCnt is non-zero
	if ( app_ptr->alpsInfo.cmdDetail->nodeCnt <= 0 )
	{
		// no nodes in the application
		return (char **)NULL;
	}
	
	// allocate space for the hosts list, add an extra entry for the null terminator
	if ((hosts = calloc(app_ptr->alpsInfo.cmdDetail->nodeCnt + 1, sizeof(char *))) == (void *)0)
	{
		// calloc failed
		return (char **)NULL;
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
	hosts[app_ptr->alpsInfo.cmdDetail->nodeCnt] = (char *)NULL;
	
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

appHostPlacementList_t *
getAppHostsPlacement(uint64_t apid)
{
	appEntry_t *				app_ptr;
	int							curNid, numNid;
	int							numPe;
	nodeHostPlacement_t *		curHost;
	appHostPlacementList_t *	placement_list;
	char						hostEntry[ALPS_XT_HOSTNAME_LEN];
	int							i;
	
	// sanity check
	if (apid <= 0)
		return (appHostPlacementList_t *)NULL;
		
	// try to find an entry in the my_apps list for the apid
	if ((app_ptr = findApp(apid)) == (appEntry_t *)NULL)
	{
		// couldn't find the entry associated with the apid
		return (appHostPlacementList_t *)NULL;
	}

	// ensure the app_ptr->alpsInfo.cmdDetail->nodeCnt is non-zero
	if ( app_ptr->alpsInfo.cmdDetail->nodeCnt <= 0 )
	{
		// no nodes in the application
		return (appHostPlacementList_t *)NULL;
	}
	
	// allocate space for the appHostPlacementList_t struct
	if ((placement_list = malloc(sizeof(appHostPlacementList_t))) == (void *)0)
	{
		// malloc failed
		return (appHostPlacementList_t *)NULL;
	}
	
	// set the number of hosts for the application
	placement_list->numHosts = app_ptr->alpsInfo.cmdDetail->nodeCnt;
	
	// allocate space for the nodeHostPlacement_t structs inside the placement_list
	if ((placement_list->hosts = malloc(placement_list->numHosts * sizeof(nodeHostPlacement_t))) == (void *)0)
	{
		// malloc failed
		free(placement_list);
		return (appHostPlacementList_t *)NULL;
	}
	// clear the nodeHostPlacment_t memory
	memset(placement_list->hosts, 0, placement_list->numHosts * sizeof(nodeHostPlacement_t));
	
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
destroy_appHostPlacementList(appHostPlacementList_t *placement_list)
{
	// sanity check
	if (placement_list == (appHostPlacementList_t *)NULL)
		return;
		
	if (placement_list->hosts != (nodeHostPlacement_t *)NULL)
		free(placement_list->hosts);
		
	free(placement_list);
}

