/*********************************************************************************\
 * alps_backend.c - A interface to interact with alps placement information on
 *                  backend compute nodes. This provides the tool developer with
 *                  an easy to use interface to obtain application information
 *                  for backend tool daemons running on the compute nodes.
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

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "alps_backend.h"

#include <job.h>
#include "alps/libalpsutil.h"


/* static prototypes */
static int              isTid(pid_t);
static computeNode_t *  getComputeNodeInfo(void);
static int              getAlpsPlacementInfo(uint64_t);

/* global variables */
static computeNode_t *          thisNode  = (computeNode_t *)NULL;      // compute node information
static alpsAppLayout_t *        appLayout = (alpsAppLayout_t *)NULL;    // node application information

/*
*       getComputeNodeInfo - read cname and nid from alps defined system locations
*
*       args: None.
*
*       return value: computeNode_t pointer containing the compute nodes cname and
*       nid, or else NULL on error.
*
*/
static computeNode_t *
getComputeNodeInfo()
{
        FILE *alps_fd;          // ALPS NID/CNAME file stream
        char file_buf[BUFSIZ];  // file read buffer
        computeNode_t *my_node; // return struct containing compute node info
        
        // allocate the computeNode_t object, its the callers responsibility to
        // free this.
        if ((my_node = malloc(sizeof(computeNode_t))) == (void *)0)
        {
                return (computeNode_t *)NULL;
        }
        
        // open up the file defined in the alps header containing our node id (nid)
        if ((alps_fd = fopen(ALPS_XT_NID, "r")) == NULL)
        {
                free(my_node);
                return (computeNode_t *)NULL;
        }
        
        // we expect this file to have a numeric value giving our current nid
        if (fgets(file_buf, BUFSIZ, alps_fd) == NULL)
        {
                free(my_node);
                fclose(alps_fd);
                return (computeNode_t *)NULL;
        }
        // convert this to an integer value
        my_node->nid = atoi(file_buf);
        
        // close the file stream
        fclose(alps_fd);
        
        // open up the cname file
        if ((alps_fd = fopen(ALPS_XT_CNAME, "r")) == NULL)
        {
                free(my_node);
                return (computeNode_t *)NULL;
        }
        
        // we expect this file to contain a string which represents our interconnect hostname
        if (fgets(file_buf, BUFSIZ, alps_fd) == NULL)
        {
                free(my_node);
                fclose(alps_fd);
                return (computeNode_t *)NULL;
        }
        // copy this to the cname ptr
        my_node->cname = strdup(file_buf);
        // we need to get rid of the newline
        my_node->cname[strlen(my_node->cname) - 1] = '\0';
        
        // close the file stream
        fclose(alps_fd);
        
        return my_node;
}

/*
 * Decide if 'id' is a tid versus a pid.
 *
 * This done by reading /proc/<id>/status and finding the
 * Tgid. If the Tgid is the same as id, then it is a pid.
 */
static int
isTid(pid_t id)
{
        FILE *  fp;
        pid_t   tgid;
        char *  p;
        char    fileName[PATH_MAX];
        char    buf[128];
        
        // Open /proc/<pid>/status
        snprintf(fileName, PATH_MAX, "/proc/%d/status", id);
        if ((fp = fopen(fileName, "r")) == 0)
        {
                fprintf(stderr, "Could not open %s. errno=%d\n", fileName, errno);
                return -1;
        }
        
        // read each line, looking for the Tgid
        while (fgets(buf, sizeof(buf), fp))
        {
                if ((p = strstr(buf, "Tgid:")))
                {
                        // found Tgid
                        sscanf(p, "Tgid: %d", &tgid);
                        fclose(fp);
                        return !(tgid == id);
                }
        }
        
        fprintf(stderr, "isTid was unable to locate Tgid.\n");
        return -1;        
}

static int
getAlpsPlacementInfo(uint64_t apid)
{
        alpsAppLayout_t *       tmpLayout;
        
        // malloc size for the struct
        if ((tmpLayout = malloc(sizeof(alpsAppLayout_t))) == (void *)0)
        {
                fprintf(stderr, "malloc failed.\n");
                return 1;
        }
        memset(tmpLayout, 0, sizeof(alpsAppLayout_t));     // clear it to NULL
        
        // get application information from alps
        if (alps_get_placement_info(apid, tmpLayout, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL) < 0)
        {
                fprintf(stderr, "apls_get_placement_info failed.\n");
                return 1;
        }
        
        // set the global pointer
        appLayout = tmpLayout;
        
        return 0;
}

nodeAppPidList_t *
findAppPids(uint64_t apid)
{
        jid_t                   jid;
        pid_t *                 dirtyPidList;
        int                     dirtyListLen, numJobPids, bufsize;
        int                     i, j;
        nodeAppPidList_t *      appPidList;
        
        // sanity check
        if (apid <= 0)
                return (nodeAppPidList_t *)NULL;

        // make sure the appLayout object has been created
        if (appLayout == (alpsAppLayout_t *)NULL)
        {
                // make sure we got the alpsAppLayout_t object
                if (getAlpsPlacementInfo(apid))
                {
                        return (nodeAppPidList_t *)NULL;
                }
        }
        
        // get the job id from the apid
        if ((jid = job_getapjid(apid)) == (jid_t)-1)
        {
                fprintf(stderr, "job_getapjid failed.\n");
                return (nodeAppPidList_t *)NULL;
        }
        
        // get the number of pids in the pagg container
        if ((numJobPids = job_getpidcnt(jid)) == -1)
        {
                fprintf(stderr, "job_getpidcnt failed.\n");
                return (nodeAppPidList_t *)NULL;
        }
        
        // alloc memory for the temporary pid list for our dirty job container (contains sheperd pid and cleanup TIDs)
        bufsize = sizeof(pid_t)*numJobPids;
        if ((dirtyPidList = malloc(bufsize)) == (void *)0)
        {
                fprintf(stderr, "malloc failed.\n");
                return (nodeAppPidList_t *)NULL;
        }
        
        /* 
         * get the pid list of our dirty job container
         * 
         * Note that the reason we call this job container "dirty" is because it can
         * contain pid's that are considered to be "outside" of our application.
         *
         * Any process that relates to this and only this job (i.e. the pid is mututally
         * exclusive with any other job container) will get placed into this apps job
         * container. The reason for this is so that when alps invokes its cleanup routines,
         * any process that has a direct dependency on this job will get killed off.
         *
         * For MPI jobs there may/will be more pids in the dirtyPidList than appLayout.numPesHere
         * indicates. One reason for this is that MPT 5.0 on up creates an error logging thread
         * for PE0 of the node which shows up in the job container. Another reason is because
         * MPI 3 and up has a shepherd process. The job container returns pid's in the order
         * that they were created, so by only using the last appLayout.numPesHere entries, the
         * shepherd (and other soon to die processes) have been stripped out. Note that we
         * must also ensure that any pid in the job container within the last appLayout.numPesHere
         * entries are not a tid in order to eliminate cleanup threads.
         */
        if ((dirtyListLen = job_getpidlist(jid, dirtyPidList, bufsize)) == -1)
        {
                fprintf(stderr, "job_getpidlist failed.\n");
                free(dirtyPidList);
                return (nodeAppPidList_t *)NULL;
        }
        
        // create the return object
        if ((appPidList = malloc(sizeof(nodeAppPidList_t))) == (void *)0)
        {
                fprintf(stderr, "malloc failed.\n");
                free(dirtyPidList);
                return (nodeAppPidList_t *)NULL;
        }
        memset(appPidList, 0, sizeof(nodeAppPidList_t));     // clear it to NULL
        
        // set the numPids member
        appPidList->numPids = appLayout->numPesHere;
        
        // create the list of application pid's
        if ((appPidList->peAppPids = calloc(appPidList->numPids, sizeof(pid_t))) == (void *)0)
        {
                fprintf(stderr, "calloc failed.\n");
                free(dirtyPidList);
                free(appPidList);
                return (nodeAppPidList_t *)NULL;
        }
        
        // itterate backwards through the dirty list, making sure to grab only pid's and not tid's
        i = 0;
        for (j = numJobPids-1; j > -1; j--)
        {
                // first check to see if i has reached appPidList->numPids
                if (i == appPidList->numPids)
                        break;
                        
                // check to see if j is a tid, if it is skip it
                if (isTid(dirtyPidList[j]))
                        continue;
                
                // this is a good pid, save it
                appPidList->peAppPids[i++] = dirtyPidList[j];
        }
        
        // done, free the dirty list
        free(dirtyPidList);
        
        return appPidList;
}

char *
getNodeCName()
{
        // ensure the thisNode exists
        if (thisNode == (computeNode_t *)NULL)
        {
                if ((thisNode = getComputeNodeInfo()) == (computeNode_t *)NULL)
                {
                        // couldn't get the compute node info for some odd reason
                        return (char *)NULL;
                }
        }
        
        // return the cname
        return strdup(thisNode->cname);
}

int
getNodeNid()
{
        // ensure the thisNode exists
        if (thisNode == (computeNode_t *)NULL)
        {
                if ((thisNode = getComputeNodeInfo()) == (computeNode_t *)NULL)
                {
                        // couldn't get the compute node info for some odd reason
                        return -1;
                }
        }
        
        // return the nid
        return thisNode->nid;
}

int
getFirstPE(uint64_t apid)
{
        // sanity check
        if (apid <= 0)
                return -1;
        
        // make sure the appLayout object has been created
        if (appLayout == (alpsAppLayout_t *)NULL)
        {
                // make sure we got the alpsAppLayout_t object
                if (getAlpsPlacementInfo(apid))
                {
                        return -1;
                }
        }
        
        return appLayout->firstPe;
}

int
getPesHere(uint64_t apid)
{
        // sanity check
        if (apid <= 0)
                return -1;
        
        // make sure the appLayout object has been created
        if (appLayout == (alpsAppLayout_t *)NULL)
        {
                // make sure we got the alpsAppLayout_t object
                if (getAlpsPlacementInfo(apid))
                {
                        return -1;
                }
        }
        
        return appLayout->numPesHere;
}

