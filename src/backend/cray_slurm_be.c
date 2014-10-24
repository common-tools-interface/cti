/*********************************************************************************\
 * cray_slurm_be.c - Cray native slurm specific backend library functions.
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
 *********************************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "cti_defs.h"
#include "cti_be.h"
#include "pmi_attribs_parser.h"

// types used here
typedef struct
{
	int		nid;	// compute node id
} computeNode_t;

/* static prototypes */
static int 					_cti_cray_slurm_init(void);
static void					_cti_cray_slurm_fini(void);
//static cti_pidList_t *		_cti_cray_slurm_findAppPids(void);
static char *				_cti_cray_slurm_getNodeHostname(void);
//static int					_cti_cray_slurm_getNodeFirstPE(void);
//static int					_cti_cray_slurm_getNodePEs(void);

/* cray slurm wlm proto object */
cti_wlm_proto_t				_cti_cray_slurm_wlmProto =
{
	CTI_WLM_CRAY_SLURM,					// wlm_type
	_cti_cray_slurm_init,				// wlm_init
	_cti_cray_slurm_fini,				// wlm_fini
	_cti_wlm_findAppPids_none,//_cti_cray_slurm_findAppPids,		// wlm_findAppPids
	_cti_cray_slurm_getNodeHostname,	// wlm_getNodeHostname
	_cti_wlm_getNodeFirstPE_none,//_cti_cray_slurm_getNodeFirstPE,		// wlm_getNodeFirstPE
	_cti_wlm_getNodePEs_none,//_cti_cray_slurm_getNodePEs			// wlm_getNodePEs
};

// Global vars
static computeNode_t *		_cti_thisNode	= NULL;	// compute node information
static pmi_attribs_t *		_cti_attrs 		= NULL;	// node pmi_attribs information
static uint32_t				_cti_jobid 		= 0;	// global jobid obtained from environment variable
static uint32_t				_cti_stepid		= 0;	// global stepid obtained from environment variable

/* Constructor/Destructor functions */

static int
_cti_cray_slurm_init(void)
{
	char *	apid_str;
	char *	ptr;

	// read information from the environment set by dlaunch
	if ((ptr = getenv(APID_ENV_VAR)) == NULL)
	{
		// Things were not setup properly, missing env vars!
		fprintf(stderr, "Env var %s not set!", APID_ENV_VAR);
		return 1;
	}
	
	// make a copy of the env var
	apid_str = strdup(ptr);
	
	// find the '.' that seperates jobid from stepid
	if ((ptr = strchr(apid_str, '.')) == NULL)
	{
		// Things were not setup properly!
		fprintf(stderr, "Env var %s has invalid value!", APID_ENV_VAR);
		free(apid_str);
		return 1;
	}
	
	// set the '.' to a null term
	*ptr++ = '\0';
	
	// get the jobid and stepid
	_cti_jobid = (uint32_t)strtoul(apid_str, NULL, 10);
	_cti_stepid = (uint32_t)strtoul(ptr, NULL, 10);
	
	// done
	return 0;
}

static void
_cti_cray_slurm_fini(void)
{
	// do nothing
	return;
}

/* Static functions */

static int
_cti_cray_slurm_getComputeNodeInfo()
{
	FILE *			nid_fd;				// NID file stream
	char 			file_buf[BUFSIZ];	// file read buffer
	computeNode_t *	my_node;			// struct containing compute node info
	
	// sanity
	if (_cti_thisNode != NULL)
		return 0;
	
	// allocate the computeNode_t object
	if ((my_node = malloc(sizeof(computeNode_t))) == NULL)
	{
		fprintf(stderr, "malloc failed.\n");
		return 1;
	}
	
	// open up the file containing our node id (nid)
	if ((nid_fd = fopen(ALPS_XT_NID, "r")) == NULL)
	{
		fprintf(stderr, "_cti_cray_slurm_getComputeNodeInfo failed.\n");
		free(my_node);
		return 1;
	}
	
	// we expect this file to have a numeric value giving our current nid
	if (fgets(file_buf, BUFSIZ, nid_fd) == NULL)
	{
		fprintf(stderr, "_cti_nid_fd_getComputeNodeInfo failed.\n");
		free(my_node);
		fclose(nid_fd);
		return 1;
	}
	// convert this to an integer value
	my_node->nid = atoi(file_buf);
	
	// close the file stream
	fclose(nid_fd);
	
	// set the global pointer
	_cti_thisNode = my_node;
	
	return 0;
}

/*
static int
_cti_alps_getPlacementInfo()
{
	alpsAppLayout_t *	tmpLayout;
	
	// sanity check
	if (_cti_appLayout != NULL)
		return 0;
	
	// sanity check
	if (_cti_apid == 0)
	{
		fprintf(stderr, "_cti_alps_getPlacementInfo failed.\n");
		return 1;
	}
	
	// malloc size for the struct
	if ((tmpLayout = malloc(sizeof(alpsAppLayout_t))) == (void *)0)
	{
		fprintf(stderr, "malloc failed.\n");
		return 1;
	}
	memset(tmpLayout, 0, sizeof(alpsAppLayout_t));     // clear it to NULL
	
	// get application information from alps
	if (_cti_alps_get_placement_info(_cti_apid, tmpLayout, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL) < 0)
	{
		fprintf(stderr, "_cti_alps_getPlacementInfo failed.\n");
		return 1;
	}
	
	// set the global pointer
	_cti_appLayout = tmpLayout;
	
	return 0;
}
*/

/* API related calls start here */
/*
static cti_pidList_t *
_cti_cray_slurm_findAppPids()
{
	cti_pidList_t * rtn;
	int i;
	
	// Call _cti_getPmiAttribsInfo - We require the pmi_attribs file to exist
	// in order to function properly.
	if (_cti_attrs == NULL)
	{
		if ((_cti_attrs = _cti_getPmiAttribsInfo(_cti_apid)) == NULL)
		{
			// Something messed up, so fail.
			fprintf(stderr, "_cti_alps_findAppPids failed.\n");
			return NULL;
		}
	}
	
	// ensure the _cti_attrs object has a app_rankPidPairs array
	if (_cti_attrs->app_rankPidPairs == NULL)
	{
		// Something messed up, so fail.
		fprintf(stderr, "_cti_alps_findAppPids failed.\n");
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
*/

static char *
_cti_cray_slurm_getNodeHostname()
{
	char * nidHost;

	// ensure the _cti_thisNode exists
	if (_cti_thisNode == NULL)
	{
		if (_cti_cray_slurm_getComputeNodeInfo())
		{
			// couldn't get the compute node info for some odd reason
			fprintf(stderr, "_cti_cray_slurm_getNodeHostname failed.\n");
			return NULL;
		}
	}
	
	// create the nid hostname string
	if (asprintf(&nidHost, ALPS_XT_HOSTNAME_FMT, _cti_thisNode->nid) <= 0)
	{
		fprintf(stderr, "_cti_cray_slurm_getNodeHostname failed.\n");
		return NULL;
	}
	
	// return the nid hostname
	return nidHost;
}

/*
static int
_cti_alps_getNodeFirstPE()
{
	// make sure the _cti_appLayout object has been created
	if (_cti_appLayout == (alpsAppLayout_t *)NULL)
	{
		// make sure we got the alpsAppLayout_t object
		if (_cti_alps_getPlacementInfo())
		{
			return -1;
		}
	}
	
	return _cti_appLayout->firstPe;
}

static int
_cti_alps_getNodePEs()
{
	// make sure the _cti_appLayout object has been created
	if (_cti_appLayout == NULL)
	{
		// make sure we got the alpsAppLayout_t object
		if (_cti_alps_getPlacementInfo())
		{
			return -1;
		}
	}
	
	return _cti_appLayout->numPesHere;
}
*/

