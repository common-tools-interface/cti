/*********************************************************************************\
 * alps_backend.c - A interface to interact with alps placement information on
 *		  backend compute nodes. This provides the tool developer with
 *		  an easy to use interface to obtain application information
 *		  for backend tool daemons running on the compute nodes.
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

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "alps_backend.h"

#include "alps/libalpsutil.h"


/* static prototypes */
static computeNode_t *		getComputeNodeInfo(void);
static int					getAlpsPlacementInfo(void);

/* global variables */
static computeNode_t *		thisNode  = (computeNode_t *)NULL;		// compute node information
static alpsAppLayout_t *	appLayout = (alpsAppLayout_t *)NULL;	// node application information
static pmi_attribs_t *		attrs = (pmi_attribs_t *)NULL;			// node pmi_attribs information
static char *				apid_str = (char *)NULL;				// temporary pointer to apid str returned by getenv
static uint64_t				apid = 0;								// global apid obtained from environment variable

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
	FILE *alps_fd;			// ALPS NID/CNAME file stream
	char file_buf[BUFSIZ];	// file read buffer
	computeNode_t *my_node;	// return struct containing compute node info
	
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

static int
getAlpsPlacementInfo()
{
	alpsAppLayout_t *	tmpLayout;
	
	// sanity check
	if (apid <= 0)
		return 1;
	
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
findAppPids()
{
	nodeAppPidList_t * rtn;

	// Try to read the apid from the environment if we haven't done so already
	if (apid == 0)
	{
		apid_str = getenv(APID_ENV_VAR);
		if (apid_str == (char *)NULL)
			return (nodeAppPidList_t *)NULL;      
		
		apid = (uint64_t)strtoull(apid_str, NULL, 10);
	}
	
	// Call getPmiAttribsInfo - We now require the pmi_attribs file to exist
	// in order to function properly.
	if ((attrs = getPmiAttribsInfo(apid)) == (pmi_attribs_t *)NULL)
	{
		// Something messed up, so fail.
		return (nodeAppPidList_t *)NULL;
	}
	
	// ensure the attrs object has a app_rankPidPairs array
	if (attrs->app_rankPidPairs == (nodeRankPidPair_t *)NULL)
	{
		// something is messed up, so fail.
		return (nodeAppPidList_t *)NULL;
	}
	
	// allocate the return object
	if ((rtn = malloc(sizeof(nodeAppPidList_t))) == (void *)0)
	{
		fprintf(stderr, "malloc failed.\n");
		return (nodeAppPidList_t *)NULL;
	}
	
	rtn->numPairs = attrs->app_nodeNumRanks;
	
	// allocate the nodeRankPidPair_t array
	if ((rtn->rankPidPairs = malloc(rtn->numPairs * sizeof(nodeRankPidPair_t))) == (void *)0)
	{
		fprintf(stderr, "malloc failed.\n");
		free(rtn);
		return (nodeAppPidList_t *)NULL;
	}
	
	// memcpy the attrs rank/pid array to the rtn rank/pid array
	memcpy(rtn->rankPidPairs, attrs->app_rankPidPairs, rtn->numPairs * sizeof(nodeRankPidPair_t));
	
	return rtn;
}

void
destroy_nodeAppPidList(nodeAppPidList_t *lst)
{
	// sanity check
	if (lst == (nodeAppPidList_t *)NULL)
		return;
		
	if (lst->rankPidPairs != (nodeRankPidPair_t *)NULL)
		free(lst->rankPidPairs);
		
	free(lst);
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

char *
getNodeNidName()
{
	char * nidHost;

	// ensure the thisNode exists
	if (thisNode == (computeNode_t *)NULL)
	{
		if ((thisNode = getComputeNodeInfo()) == (computeNode_t *)NULL)
		{
			// couldn't get the compute node info for some odd reason
			return (char *)NULL;
		}
	}
	
	// allocate space for the nid hostname
	if ((nidHost = malloc(ALPS_XT_HOSTNAME_LEN*sizeof(char))) == (void *)0)
	{
		// malloc failed
		return (char *)NULL;
	}
	
	// create the nid hostname string
	snprintf(nidHost, ALPS_XT_HOSTNAME_LEN, ALPS_XT_HOSTNAME_FMT, thisNode->nid);
	
	// return the nid hostname
	return nidHost;
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
getFirstPE()
{
	// Try to read the apid from the environment if we haven't done so already
	if (apid == 0)
	{
		apid_str = getenv(APID_ENV_VAR);
		if (apid_str == (char *)NULL)
			return -1;      
		
		apid = (uint64_t)strtoull(apid_str, NULL, 10);
	}
	
	// make sure the appLayout object has been created
	if (appLayout == (alpsAppLayout_t *)NULL)
	{
		// make sure we got the alpsAppLayout_t object
		if (getAlpsPlacementInfo())
		{
			return -1;
		}
	}
	
	return appLayout->firstPe;
}

int
getPesHere()
{
	// Try to read the apid from the environment if we haven't done so already
	if (apid == 0)
	{
		apid_str = getenv(APID_ENV_VAR);
		if (apid_str == (char *)NULL)
			return -1;      
		
		apid = (uint64_t)strtoull(apid_str, NULL, 10);
	}
	
	// make sure the appLayout object has been created
	if (appLayout == (alpsAppLayout_t *)NULL)
	{
		// make sure we got the alpsAppLayout_t object
		if (getAlpsPlacementInfo())
		{
			return -1;
		}
	}
	
	return appLayout->numPesHere;
}

