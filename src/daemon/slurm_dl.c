/******************************************************************************\
 * slurm_dl.c - Cluster slurm specific functions for the daemon launcher.
 *
 * Copyright 2016 Cray Inc.  All Rights Reserved.
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

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cti_daemon.h"

/* static prototypes */
static int	_cti_slurm_init(void);
static int	_cti_slurm_getNodeID(void);

/* cray slurm wlm proto object */
cti_wlm_proto_t		_cti_cray_slurm_wlmProto =
{
	CTI_WLM_SLURM,			// wlm_type
	_cti_slurm_init,		// wlm_init
	_cti_slurm_getNodeID	// wlm_getNodeID
};

/* functions start here */

static int
_cti_slurm_init(void)
{
	// NO-OP
	
	return 0;
}

static int
_cti_slurm_getNodeID(void)
{
	char host[HOST_NAME_MAX+1];

	if (gethostname(&host, HOST_NAME_MAX+1))
	{
		_cti_set_error("gethostname failed.");
		return NULL;
	}

	return strdup(host);
}

