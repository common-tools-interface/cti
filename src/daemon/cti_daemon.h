/*********************************************************************************\
 * cti_daemon.h - A header file for the daemon launcher.
 *
 * Copyright 2014 Cray Inc.  All Rights Reserved.
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

#ifndef _CTI_DAEMON_H
#define _CTI_DAEMON_H

#include "cti_defs.h"

// This is the wlm proto object that all wlm implementations should define.
// The noneness functions can be used if a function is not definable by your wlm,
// but that should only be used if an API call is truly incompatible with the wlm.
typedef struct
{
	cti_wlm_type		wlm_type;						// wlm type
	int					(*wlm_init)(void);				// wlm init - return true on error
	int					(*wlm_getNodeID)(void);			// get node ID of current compute node - return -1 on error
} cti_wlm_proto_t;

/* Noneness functions for wlm proto - Use these if your wlm proto doesn't define the function */
int	_cti_wlm_init_none(void);
int	_cti_wlm_getNodeID_none(void);

#endif /* _CTI_DAEMON_H */
