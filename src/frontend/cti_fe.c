/******************************************************************************\
 * cti_fe.c - cti frontend library functions.
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
 ******************************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include "cti_fe.h"
#include "alps_fe.h"

// Global vars
static cti_wlm_type 	_cti_current_wlm = CTI_WLM_NONE;

// Constructor function
void __attribute__((constructor))
_cti_init(void)
{

	// TODO: Add wlm_detect here, then call proper init function
	
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
			// call alps based destructor
			break;
			
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

