/******************************************************************************\
 * slurm_dl.c - Cluster slurm specific functions for the daemon launcher.
 *
 * Copyright 2016-2017 Cray Inc.  All Rights Reserved.
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
cti_wlm_proto_t		_cti_slurm_wlmProto =
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
/*
 * _cti_slurm_getNodeID - Gets the id for the current node
 * 
 * Detail
 *      Returns an unique id for the current node. Currently, this function
 *      sums the characters in the hostname as a cheap hash unique to a particular node
 *		that won't collide in most cases. Apart from the hostname, I don't know of any other source of
 *		information that could reliably be used to differentiate nodes on clusters where
 *		the alps nid files do not exist. Currently, this is only used for uniquely naming backend debug logs.
 *		TODO: Perhaps this could be improved by using a real hash such as CRC
 *
 * Returns
 *      An int representing an unique id for the current node
 * 
 */
static int
_cti_slurm_getNodeID(void)
{
	char host[HOST_NAME_MAX+1];

	if (gethostname(host, HOST_NAME_MAX+1))
	{
		fprintf(stderr, "gethostname failed.\n");
		return 0;
	}

	int result = 0;
	int len = strlen(host);

	int i;
	for(i=0; i<len; i++){
		result += host[i];
	}

	return result;
}

