/*********************************************************************************\
 * alps_backend.c - A interface to interact with alps placement information on
 *		  backend compute nodes. This provides the tool developer with
 *		  an easy to use interface to obtain application information
 *		  for backend tool daemons running on the compute nodes.
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

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "alps_backend.h"

#include "alps/libalpsutil.h"

typedef struct
{
	int		nid;	// compute node id
	char *	cname;	// compute node hostname
} computeNode_t;

/* static prototypes */
static computeNode_t *		_cti_getComputeNodeInfo(void);
static int					_cti_getAlpsPlacementInfo(void);

/* global variables */
static computeNode_t *		_cti_thisNode	= NULL;	// compute node information
static alpsAppLayout_t *	_cti_appLayout	= NULL;	// node application information
static pmi_attribs_t *		_cti_attrs 		= NULL;	// node pmi_attribs information
static uint64_t				_cti_apid 		= 0;	// global apid obtained from environment variable

/*
*       _cti_getComputeNodeInfo - read cname and nid from alps defined system locations
*
*       args: None.
*
*       return value: computeNode_t pointer containing the compute nodes cname and
*       nid, or else NULL on error.
*
*/
static computeNode_t *
_cti_getComputeNodeInfo()
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
_cti_getAlpsPlacementInfo()
{
	alpsAppLayout_t *	tmpLayout;
	
	// sanity check
	if (_cti_apid == 0)
		return 1;
	
	// malloc size for the struct
	if ((tmpLayout = malloc(sizeof(alpsAppLayout_t))) == (void *)0)
	{
		fprintf(stderr, "malloc failed.\n");
		return 1;
	}
	memset(tmpLayout, 0, sizeof(alpsAppLayout_t));     // clear it to NULL
	
	// get application information from alps
	if (alps_get_placement_info(_cti_apid, tmpLayout, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL) < 0)
	{
		fprintf(stderr, "apls_get_placement_info failed.\n");
		return 1;
	}
	
	// set the global pointer
	_cti_appLayout = tmpLayout;
	
	return 0;
}

cti_pidList_t *
cti_findAppPids()
{
	cti_pidList_t * rtn;
	int i;

	// Try to read the _cti_apid from the environment if we haven't done so already
	if (_cti_apid == 0)
	{
		char *apid_str = getenv(APID_ENV_VAR);
		if (apid_str == NULL)
			return NULL;      
		
		_cti_apid = (uint64_t)strtoull(apid_str, NULL, 10);
	}
	
	// Call _cti_getPmiAttribsInfo - We require the pmi_attribs file to exist
	// in order to function properly.
	if (_cti_attrs == NULL)
	{
		if ((_cti_attrs = _cti_getPmiAttribsInfo(_cti_apid)) == NULL)
		{
			// Something messed up, so fail.
			return NULL;
		}
	}
	
	// ensure the _cti_attrs object has a app_rankPidPairs array
	if (_cti_attrs->app_rankPidPairs == NULL)
	{
		// something is messed up, so fail.
		return NULL;
	}
	
	// allocate the return object
	if ((rtn = malloc(sizeof(cti_pidList_t))) == (void *)0)
	{
		fprintf(stderr, "malloc failed.\n");
		return NULL;
	}
	
	rtn->numPids = _cti_attrs->app_nodeNumRanks;
	
	// allocate the cti_rankPidPair_t array
	if ((rtn->pids = malloc(rtn->numPids * sizeof(cti_rankPidPair_t))) == (void *)0)
	{
		fprintf(stderr, "malloc failed.\n");
		free(rtn);
		return NULL;
	}
	
	// set the _cti_attrs rank/pid array to the rtn rank/pid array
	for (i=0; i < rtn->numPids; ++i)
	{
		rtn->pids[i].pid = _cti_attrs->app_rankPidPairs[i].pid;
		rtn->pids[i].rank = _cti_attrs->app_rankPidPairs[i].rank;
	}
	
	return rtn;
}

void
cti_destroy_pidList(cti_pidList_t *lst)
{
	// sanity check
	if (lst == NULL)
		return;
		
	if (lst->pids != NULL)
		free(lst->pids);
		
	free(lst);
}

char *
cti_getNodeCName()
{
	// ensure the _cti_thisNode exists
	if (_cti_thisNode == NULL)
	{
		if ((_cti_thisNode = _cti_getComputeNodeInfo()) == NULL)
		{
			// couldn't get the compute node info for some odd reason
			return NULL;
		}
	}
	
	// return the cname
	return strdup(_cti_thisNode->cname);
}

char *
cti_getNodeNidName()
{
	char * nidHost;

	// ensure the _cti_thisNode exists
	if (_cti_thisNode == NULL)
	{
		if ((_cti_thisNode = _cti_getComputeNodeInfo()) == NULL)
		{
			// couldn't get the compute node info for some odd reason
			return NULL;
		}
	}
	
	// allocate space for the nid hostname
	if ((nidHost = malloc(ALPS_XT_HOSTNAME_LEN*sizeof(char))) == (void *)0)
	{
		// malloc failed
		return NULL;
	}
	
	// create the nid hostname string
	snprintf(nidHost, ALPS_XT_HOSTNAME_LEN, ALPS_XT_HOSTNAME_FMT, _cti_thisNode->nid);
	
	// return the nid hostname
	return nidHost;
}

int
cti_getNodeNid()
{
	// ensure the _cti_thisNode exists
	if (_cti_thisNode == NULL)
	{
		if ((_cti_thisNode = _cti_getComputeNodeInfo()) == NULL)
		{
			// couldn't get the compute node info for some odd reason
			return -1;
		}
	}
	
	// return the nid
	return _cti_thisNode->nid;
}

int
cti_getFirstPE()
{
	// Try to read the _cti_apid from the environment if we haven't done so already
	if (_cti_apid == 0)
	{
		char *apid_str = getenv(APID_ENV_VAR);
		if (apid_str == NULL)
			return -1;      
		
		_cti_apid = (uint64_t)strtoull(apid_str, NULL, 10);
	}
	
	// make sure the _cti_appLayout object has been created
	if (_cti_appLayout == (alpsAppLayout_t *)NULL)
	{
		// make sure we got the alpsAppLayout_t object
		if (_cti_getAlpsPlacementInfo())
		{
			return -1;
		}
	}
	
	return _cti_appLayout->firstPe;
}

int
cti_getPesHere()
{
	// Try to read the _cti_apid from the environment if we haven't done so already
	if (_cti_apid == 0)
	{
		char *apid_str = getenv(APID_ENV_VAR);
		if (apid_str == NULL)
			return -1;      
		
		_cti_apid = (uint64_t)strtoull(apid_str, NULL, 10);
	}
	
	// make sure the _cti_appLayout object has been created
	if (_cti_appLayout == NULL)
	{
		// make sure we got the alpsAppLayout_t object
		if (_cti_getAlpsPlacementInfo())
		{
			return -1;
		}
	}
	
	return _cti_appLayout->numPesHere;
}

