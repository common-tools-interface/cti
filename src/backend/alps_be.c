/*********************************************************************************\
 * alps_be.c - alps specific backend library functions.
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

#include <dlfcn.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "alps/alps.h"
#include "alps/alps_toolAssist.h"

#include "cti_defs.h"
#include "alps_be.h"
#include "pmi_attribs_parser.h"

// types used here
typedef struct
{
	void *	handle;
	int 	(*alps_get_placement_info)(uint64_t, alpsAppLayout_t *, int **, int **, int **, int **, struct in_addr **, int **, int **, int **, int **);
} cti_alps_funcs_t;

typedef struct
{
	int		nid;	// compute node id
} computeNode_t;

/* static prototypes */
static int 					_cti_alps_init(void);
static void					_cti_alps_fini(void);
static cti_pidList_t *		_cti_alps_findAppPids(void);
static char *				_cti_alps_getNodeHostname(void);
static int					_cti_alps_getNodeFirstPE(void);
static int					_cti_alps_getNodePEs(void);
static int					_cti_alps_get_placement_info(uint64_t, alpsAppLayout_t *, int **, int **, int **, int **, struct in_addr **, int **, int **, int **, int **);
static int					_cti_alps_getComputeNodeInfo(void);
static int					_cti_alps_getPlacementInfo(void);

/* alps wlm proto object */
cti_wlm_proto_t				_cti_alps_wlmProto =
{
	CTI_WLM_ALPS,				// wlm_type
	_cti_alps_init,				// wlm_init
	_cti_alps_fini,				// wlm_fini
	_cti_alps_findAppPids,		// wlm_findAppPids
	_cti_alps_getNodeHostname,	// wlm_getNodeHostname
	_cti_alps_getNodeFirstPE,	// wlm_getNodeFirstPE
	_cti_alps_getNodePEs		// wlm_getNodePEs
};

// Global vars
static cti_alps_funcs_t *	_cti_alps_ptr 	= NULL;	// libalps wrappers
static computeNode_t *		_cti_thisNode	= NULL;	// compute node information
static alpsAppLayout_t *	_cti_appLayout	= NULL;	// node application information
static pmi_attribs_t *		_cti_attrs 		= NULL;	// node pmi_attribs information
static uint64_t				_cti_apid 		= 0;	// global apid obtained from environment variable

/* Constructor/Destructor functions */

static int
_cti_alps_init(void)
{
	char *error;
	char *apid_str;

	// Only init once.
	if (_cti_alps_ptr != NULL)
		return 0;
		
	// Create a new cti_alps_funcs_t
	if ((_cti_alps_ptr = malloc(sizeof(cti_alps_funcs_t))) == NULL)
	{
		fprintf(stderr, "malloc failed.");
		return 1;
	}
	memset(_cti_alps_ptr, 0, sizeof(cti_alps_funcs_t));     // clear it to NULL
	
	if ((_cti_alps_ptr->handle = dlopen(ALPS_BE_LIB_NAME, RTLD_LAZY)) == NULL)
	{
		fprintf(stderr, "dlopen: %s", dlerror());
		free(_cti_alps_ptr);
		_cti_alps_ptr = NULL;
		return 1;
	}
	
	// Clear any existing error
	dlerror();
	
	// load alps_get_placement_info
	_cti_alps_ptr->alps_get_placement_info = dlsym(_cti_alps_ptr->handle, "alps_get_placement_info");
	if ((error = dlerror()) != NULL)
	{
		fprintf(stderr, "dlsym: %s", error);
		dlclose(_cti_alps_ptr->handle);
		free(_cti_alps_ptr);
		_cti_alps_ptr = NULL;
		return 1;
	}
	
	// read information from the environment set by dlaunch
	if ((apid_str = getenv(APID_ENV_VAR)) == NULL)
	{
		// Things were not setup properly, missing env vars!
		fprintf(stderr, "Env var %s not set!", APID_ENV_VAR);
		dlclose(_cti_alps_ptr->handle);
		free(_cti_alps_ptr);
		_cti_alps_ptr = NULL;
		return 1;
	} else
	{
		_cti_apid = (uint64_t)strtoull(apid_str, NULL, 10);
	}
	
	// done
	return 0;
}

static void
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

static int
_cti_alps_get_placement_info(uint64_t a1, alpsAppLayout_t *a2, int **a3, int **a4, int **a5, int **a6, struct in_addr **a7, int **a8, int **a9, int **a10, int **a11)
{
	// sanity check
	if (_cti_alps_ptr == NULL)
		return -1;
		
	return (*_cti_alps_ptr->alps_get_placement_info)(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11);
}

/* Static functions */

static int
_cti_alps_getComputeNodeInfo()
{
	FILE *alps_fd;			// ALPS NID file stream
	char file_buf[BUFSIZ];	// file read buffer
	computeNode_t *my_node;	// return struct containing compute node info
	
	// sanity
	if (_cti_thisNode != NULL)
		return 0;
	
	// allocate the computeNode_t object, its the callers responsibility to
	// free this.
	if ((my_node = malloc(sizeof(computeNode_t))) == (void *)0)
	{
		fprintf(stderr, "malloc failed.\n");
		return 1;
	}
	
	// open up the file defined in the alps header containing our node id (nid)
	if ((alps_fd = fopen(ALPS_XT_NID, "r")) == NULL)
	{
		fprintf(stderr, "_cti_alps_getComputeNodeInfo failed.\n");
		free(my_node);
		return 1;
	}
	
	// we expect this file to have a numeric value giving our current nid
	if (fgets(file_buf, BUFSIZ, alps_fd) == NULL)
	{
		fprintf(stderr, "_cti_alps_getComputeNodeInfo failed.\n");
		free(my_node);
		fclose(alps_fd);
		return 1;
	}
	// convert this to an integer value
	my_node->nid = atoi(file_buf);
	
	// close the file stream
	fclose(alps_fd);
	
	// set the global pointer
	_cti_thisNode = my_node;
	
	return 0;
}

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

/* ALPS related calls start here */

static cti_pidList_t *
_cti_alps_findAppPids()
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

static char *
_cti_alps_getNodeHostname()
{
	char * nidHost;

	// ensure the _cti_thisNode exists
	if (_cti_thisNode == NULL)
	{
		if (_cti_alps_getComputeNodeInfo())
		{
			// couldn't get the compute node info for some odd reason
			fprintf(stderr, "_cti_alps_getNodeHostname failed.\n");
			return NULL;
		}
	}
	
	// create the nid hostname string
	if (asprintf(&nidHost, ALPS_XT_HOSTNAME_FMT, _cti_thisNode->nid) <= 0)
	{
		fprintf(stderr, "_cti_alps_getNodeHostname failed.\n");
		return NULL;
	}
	
	// return the nid hostname
	return nidHost;
}

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

