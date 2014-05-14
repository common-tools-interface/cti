/*********************************************************************************\
 * cti_be.c - A interface to interact with placement information on compute
 *		  nodes. This provides the tool developer with an easy to use interface 
 *        to obtain application information for backend tool daemons running on
 *        the compute nodes.
 *
 * © 2011-2014 Cray Inc.  All Rights Reserved.
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

#include "cti_be.h"
#include "alps_be.h"

// Global vars
/* noneness wlm proto object */
static cti_wlm_proto_t	_cti_nonenessProto =
{
	CTI_WLM_NONE,					// wlm_type
	_cti_wlm_init_none,				// wlm_init
	_cti_wlm_fini_none,				// wlm_fini
	_cti_wlm_findAppPids_none,		// wlm_findAppPids
	_cti_wlm_getNodeHostname_none,	// wlm_getNodeHostname
	_cti_wlm_getNodeFirstPE_none,	// wlm_getNodeFirstPE
	_cti_wlm_getNodePEs_none		// wlm_getNodePEs
};

/* global wlm proto object - this is initialized to noneness by default */
cti_wlm_proto_t *		_cti_wlmProto 	= &_cti_nonenessProto;

// Constructor function
void __attribute__((constructor))
_cti_init(void)
{

	// TODO: Add wlm here based env var set by dlaunch, then call proper 
	// init function
	// In the future this should be able to handle multiple WLM types.
	
	_cti_wlmProto = &_cti_alps_wlmProto;
	
	if (_cti_wlmProto->wlm_init())
	{
		// We failed to init, so ensure we set the WLM proto to noneness
		_cti_wlmProto = &_cti_nonenessProto;
		return;
	}
}

// Destructor function
void __attribute__ ((destructor))
_cti_fini(void)
{
	// call the wlm finish function
	_cti_wlmProto->wlm_fini();
	
	// reset the wlm proto to noneness
	_cti_wlmProto = &_cti_nonenessProto;
	
	return;
}

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

cti_pidList_t *
cti_findAppPids()
{
	// Call the appropriate function based on the wlm
	return _cti_wlmProto->wlm_findAppPids();
}

void
cti_destroyPidList(cti_pidList_t *lst)
{
	// sanity check
	if (lst == NULL)
		return;
		
	if (lst->pids != NULL)
		free(lst->pids);
		
	free(lst);
}

char *
cti_getNodeHostname()
{
	// Call the appropriate function based on the wlm
	return _cti_wlmProto->wlm_getNodeHostname();
}

int
cti_getNodeFirstPE()
{
	// Call the appropriate function based on the wlm
	return _cti_wlmProto->wlm_getNodeFirstPE();
}

int
cti_getNodePEs()
{
	// Call the appropriate function based on the wlm
	return _cti_wlmProto->wlm_getNodePEs();
}

/* Noneness functions for wlm proto */

int
_cti_wlm_init_none(void)
{
	fprintf(stderr, "wlm_init() not supported for %s", cti_wlm_type_toString(_cti_wlmProto->wlm_type));
	return 1;
}

void
_cti_wlm_fini_none(void)
{
	fprintf(stderr, "wlm_fini() not supported for %s", cti_wlm_type_toString(_cti_wlmProto->wlm_type));
	return;
}

cti_pidList_t *
_cti_wlm_findAppPids_none(void)
{
	fprintf(stderr, "wlm_findAppPids() not supported for %s", cti_wlm_type_toString(_cti_wlmProto->wlm_type));
	return NULL;
}

char *
_cti_wlm_getNodeHostname_none(void)
{
	fprintf(stderr, "wlm_getNodeHostname() not supported for %s", cti_wlm_type_toString(_cti_wlmProto->wlm_type));
	return NULL;
}

int
_cti_wlm_getNodeFirstPE_none(void)
{
	fprintf(stderr, "wlm_getNodeFirstPE() not supported for %s", cti_wlm_type_toString(_cti_wlmProto->wlm_type));
	return -1;
}

int
_cti_wlm_getNodePEs_none(void)
{
	fprintf(stderr, "wlm_getNodeFirstPE() not supported for %s", cti_wlm_type_toString(_cti_wlmProto->wlm_type));
	return -1;
}

