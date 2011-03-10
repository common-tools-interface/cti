/*********************************************************************************\
 * alps_application.c - A interface to the alps toolhelper functions. This provides
 *                      support functions for the other APIs defined by this
 *                      interface.
 *
 * © 2011 Cray Inc.  All Rights Reserved.
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "alps/libalps.h"

#include "alps_application.h"

/* Static prototypes */
static appList_t *      growAppsList(void);
static void             reapAppsList(void);
static void             reapAppEntry(pid_t);
static void             consumeAppEntry(appEntry_t *);
static int              growStringList(stringList_t *);
static serviceNode_t *  getSvcNodeInfo(void);

/* global variables */
static serviceNode_t *  svcNid = (serviceNode_t *)NULL;        // service node information
static appList_t *      my_apps = (appList_t *)NULL;           // global list pertaining to known aprun sessions

static int
growStringList(stringList_t *lst)
{
        int             i;
        char **         tmp_lst;
        
        if (lst == (stringList_t *)NULL)
                return 1;
                
        if (lst->list == (char **)NULL)
        {
                // allocate 10 new char ptrs for the list
                if ((lst->list = calloc(BLOCK_SIZE, sizeof(char *))) == 0)
                {
                        return 1;
                }
                // reset the len and num values
                lst->num = 0;
                lst->len = BLOCK_SIZE;
                
                return 0;
        }
        
        // ensure there is enough entries in the list for a future addition
        if ((lst->num + 1) > lst->len)
        {
                if ((tmp_lst = calloc(lst->len + BLOCK_SIZE, sizeof(char *))) == 0)
                {
                        return 1;
                }
                // copy the old list to the new one
                for (i = 0; i < lst->num; i++)
                {
                        tmp_lst[i] = lst->list[i];
                }
                // free the old list
                free(lst->list);
                // set the new list
                lst->list = tmp_lst;
                // increment the len value
                lst->len += BLOCK_SIZE;
                
                return 0;
        }

        return 0;
}

static appList_t *
growAppsList()
{
        appList_t *     newEntry;
        appList_t *     lstPtr;
        
        // alloc space for the new list entry
        if ((newEntry = malloc(sizeof(appList_t))) == (void *)0)
        {
                return (appList_t *)NULL;
        }
        
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
        appList_t *     lstPtr;

        // sanity check
        if ((lstPtr = my_apps) == (appList_t *)NULL)
                return;
                
        // if this was the first entry, lets remove it
        if (lstPtr->nextEntry == (appList_t *)NULL)
        {
                free(lstPtr);
                my_apps = (appList_t *)NULL;
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
reapAppEntry(pid_t aprunPid)
{
        appList_t *     lstPtr;
        appList_t *     prePtr;
        
        // sanity check
        if (((lstPtr = my_apps) == (appList_t *)NULL) || (aprunPid <= 0))
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
        while (lstPtr->thisEntry->aprunPid != aprunPid)
        {
                prePtr = lstPtr;
                if ((lstPtr = lstPtr->nextEntry) == (appList_t *)NULL)
                {
                        // there are no more entries and we didn't find the aprunPid
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
        int i;
        
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
        if (entry->shipped_execs.list != (char **)NULL)
        {
                for (i=0; i < entry->shipped_execs.num; i++)
                {
                        free(entry->shipped_execs.list[i]);
                }
                free(entry->shipped_execs.list);
        }
        if (entry->shipped_libs.list != (char **)NULL)
        {
                for (i=0; i < entry->shipped_libs.num; i++)
                {
                        free(entry->shipped_libs.list[i]);
                }
                free(entry->shipped_libs.list);
        }
        if (entry->shipped_files.list != (char **)NULL)
        {
                for (i=0; i < entry->shipped_files.num; i++)
                {
                        free(entry->shipped_files.list[i]);
                }
        }
        
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
        FILE *alps_fd;          // ALPS NID/CNAME file stream
        char file_buf[BUFSIZ];  // file read buffer
        serviceNode_t *my_node; // return struct containing service node info
        
        // allocate the serviceNode_t object, its the callers responsibility to
        // free this.
        if ((my_node = malloc(sizeof(serviceNode_t))) == (void *)0)
        {
                return (serviceNode_t *)NULL;
        }
        
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

int
searchStringList(stringList_t *lst, char *str)
{
        char **str_ptr;
        
        // sanity check
        if (lst == (stringList_t *)NULL || str == (char *)NULL)
                return 0;
                
        // shouldn't happen, but better safe then sorry
        if (lst->list == (char **)NULL)
                return 0;
                
        // set the str_ptr to the start of the list
        str_ptr = lst->list;
        // iterate through the list
        while (*str_ptr)
        {
                if (!strcmp(*str_ptr, str))
                        return 1;
                ++str_ptr;
        }
        
        // not found
        return 0;
}

int
addString(stringList_t *lst, char *str)
{
        // sanity check
        if (lst == (stringList_t *)NULL || str == (char *)NULL)
                return 1;
                
        // ensure room exists in the list
        if (growStringList(lst))
                return 1;
                
        // add the str to the list at the index num
        // post increment num
        lst->list[lst->num++] = strdup(str);
        
        return 0;
}

appEntry_t *
findApp(pid_t aprunPid)
{
        appList_t *     lstPtr;
        
        // ensure my_apps isn't empty
        if ((lstPtr = my_apps) == (appList_t *)NULL)
                return (appEntry_t *)NULL;
        
        // iterate through the my_apps list
        while (lstPtr->thisEntry->aprunPid != aprunPid)
        {
                // make lstPtr point to the next entry
                if ((lstPtr = lstPtr->nextEntry) == (appList_t *)NULL)
                {
                        // if lstPtr is null, we are at the end of the list
                        // so the entry for aprunPid doesn't exist
                        return (appEntry_t *)NULL;
                }
        }
        
        // if we get here, we found the appEntry_t that corresponds to aprunPid
        return lstPtr->thisEntry;
}

appEntry_t *
newApp(pid_t aprunPid)
{
        appList_t *     lstPtr;
        appEntry_t *    this;
        
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
        
        // ensure the svnNid exists
        if (svcNid == (serviceNode_t *)NULL)
        {
                if ((svcNid = getSvcNodeInfo()) == (serviceNode_t *)NULL)
                {
                        reapAppsList();
                        consumeAppEntry(this);
                        return (appEntry_t *)NULL;
                }
        }
        
        // set the aprun pid member
        this->aprunPid = aprunPid;
        
        // set the application id member
        if ((this->alpsInfo.apid = alps_get_apid(svcNid->nid, aprunPid)) == 0)
        {
                reapAppsList();
                consumeAppEntry(this);
                return (appEntry_t *)NULL;
        }
        
        // retrieve detailed information about our app
        // save this information into the struct
        if (alps_get_appinfo(this->alpsInfo.apid, &this->alpsInfo.appinfo, &this->alpsInfo.cmdDetail, &this->alpsInfo.places) != 1)
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
        
        // create the initial arrays for the three saved str lists
        if (growStringList(&this->shipped_execs))
        {
                reapAppsList();
                consumeAppEntry(this);
                return (appEntry_t *)NULL;
        }
        if (growStringList(&this->shipped_libs))
        {
                reapAppsList();
                consumeAppEntry(this);
                return (appEntry_t *)NULL;
        }
        if (growStringList(&this->shipped_files))
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
registerAprunPid(pid_t aprunPid)
{
        // sanity check
        if (aprunPid <= 0)
                return 1;
                
        // try to find an entry in the my_apps list for the aprunPid
        if (findApp(aprunPid) == (appEntry_t *)NULL)
        {
                // aprun pid not found in the global my_apps list
                // so lets create a new appEntry_t object for it
                if (newApp(aprunPid) == (appEntry_t *)NULL)
                {
                        // we failed to create a new appEntry_t entry - catastrophic failure
                        return 1;
                }
        }

        return 0;
}

int
deregisterAprunPid(pid_t aprunPid)
{
        // sanity check
        if (aprunPid <= 0)
                return 1;
        
        // call the reapAppEntry function for this pid
        reapAppEntry(aprunPid);
        
        return 0;
}

uint64_t
getApid(pid_t aprunPid)
{
        appEntry_t *    app_ptr;

        // sanity check
        if (aprunPid <= 0)
                return 0;
        
        // try to find an entry in the my_apps list for the aprunPid
        if ((app_ptr = findApp(aprunPid)) == (appEntry_t *)NULL)
        {
                // couldn't find the entry associated with the aprunPid
                return 0;
        }
        
        return app_ptr->alpsInfo.apid;
}

char *
getCName()
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
getNid()
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
getNumAppPEs(pid_t aprunPid)
{
        appEntry_t *    app_ptr;
        
        // sanity check
        if (aprunPid <= 0)
                return 0;
                
        // try to find an entry in the my_apps list for the aprunPid
        if ((app_ptr = findApp(aprunPid)) == (appEntry_t *)NULL)
        {
                // couldn't find the entry associated with the aprunPid
                return 0;
        }
        
        return app_ptr->alpsInfo.cmdDetail->width;
}

int
getNumAppNodes(pid_t aprunPid)
{
        appEntry_t *    app_ptr;
        
        // sanity check
        if (aprunPid <= 0)
                return 0;
                
        // try to find an entry in the my_apps list for the aprunPid
        if ((app_ptr = findApp(aprunPid)) == (appEntry_t *)NULL)
        {
                // couldn't find the entry associated with the aprunPid
                return 0;
        }
        
        return app_ptr->alpsInfo.cmdDetail->nodeCnt;
}

char **
getAppHostsList(pid_t aprunPid)
{
        appEntry_t *    app_ptr;
        int             curNid, numNid;
        char **         hosts;
        char            hostEntry[ALPS_XT_HOSTNAME_LEN];
        int             i;
        
        // sanity check
        if (aprunPid <= 0)
                return (char **)NULL;
                
        // try to find an entry in the my_apps list for the aprunPid
        if ((app_ptr = findApp(aprunPid)) == (appEntry_t *)NULL)
        {
                // couldn't find the entry associated with the aprunPid
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
