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
static cti_wlm_type 	_cti_current_wlm = CTI_WLM_NONE;

// Constructor function
void __attribute__((constructor))
_cti_init(void)
{

	// TODO: Add wlm here based env var set by dlaunch, then call proper 
	// init function
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
		case CTI_WLM_NONE:
			break;
	}

	_cti_current_wlm = CTI_WLM_NONE;

	return;
}

cti_wlm_type
cti_current_wlm(void)
{
	return _cti_current_wlm;
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
	switch (_cti_current_wlm)
	{
		case CTI_WLM_ALPS:
			return _cti_alps_findAppPids();
			
		case CTI_WLM_CRAY_SLURM:
		case CTI_WLM_SLURM:
			fprintf(stderr, "Current WLM is not yet supported.");
			return NULL;
			
		case CTI_WLM_NONE:
			fprintf(stderr, "No valid workload manager detected.");
			return NULL;
	}
	
	// should not get here
	fprintf(stderr, "At impossible exit.");
	return NULL;
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
	switch (_cti_current_wlm)
	{
		case CTI_WLM_ALPS:
			return _cti_alps_getNodeHostname();
			
		case CTI_WLM_CRAY_SLURM:
		case CTI_WLM_SLURM:
			fprintf(stderr, "Current WLM is not yet supported.");
			return NULL;
			
		case CTI_WLM_NONE:
			fprintf(stderr, "No valid workload manager detected.");
			return NULL;
	}
	
	// should not get here
	fprintf(stderr, "At impossible exit.");
	return NULL;
}

int
cti_getNodeFirstPE()
{
	// Call the appropriate function based on the wlm
	switch (_cti_current_wlm)
	{
		case CTI_WLM_ALPS:
			return _cti_alps_getNodeFirstPE();
			
		case CTI_WLM_CRAY_SLURM:
		case CTI_WLM_SLURM:
			fprintf(stderr, "Current WLM is not yet supported.");
			return -1;
			
		case CTI_WLM_NONE:
			fprintf(stderr, "No valid workload manager detected.");
			return -1;
	}
	
	// should not get here
	fprintf(stderr, "At impossible exit.");
	return -1;
}

int
cti_getNodePEs()
{
	// Call the appropriate function based on the wlm
	switch (_cti_current_wlm)
	{
		case CTI_WLM_ALPS:
			return _cti_alps_getNodePEs();
			
		case CTI_WLM_CRAY_SLURM:
		case CTI_WLM_SLURM:
			fprintf(stderr, "Current WLM is not yet supported.");
			return -1;
			
		case CTI_WLM_NONE:
			fprintf(stderr, "No valid workload manager detected.");
			return -1;
	}
	
	// should not get here
	fprintf(stderr, "At impossible exit.");
	return -1;
}

