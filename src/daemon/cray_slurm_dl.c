/******************************************************************************\
 * cray_slurm_dl.c - Cray native slurm specific functions for the daemon 
 *                   launcher.
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
 ******************************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <stdio.h>
#include <stdlib.h>

#include "cti_daemon.h"

/* static prototypes */
static int	_cti_cray_slurm_init(void);
static int	_cti_cray_slurm_getNodeID(void);

/* cray slurm wlm proto object */
cti_wlm_proto_t		_cti_cray_slurm_wlmProto =
{
	CTI_WLM_CRAY_SLURM,			// wlm_type
	_cti_cray_slurm_init,		// wlm_init
	_cti_cray_slurm_getNodeID	// wlm_getNodeID
};

/* functions start here */

static int
_cti_cray_slurm_init(void)
{
	// Set LC_ALL to POSIX - on Cray platforms this has been shown to significantly
	// speed up load times if the tool daemon is invoking the shell.
	if (setenv("LC_ALL", "POSIX", 1) < 0)
	{
		// failure
		fprintf(stderr, "setenv failed\n");
		return 1;
	}
	
	return 0;
}

/*
** XXX: This is the same thing as what we do for alps since we can rely on
**      a similar setup.
*/
static int
_cti_cray_slurm_getNodeID(void)
{
	FILE *	nid_fd;
	int		nid;
	char	file_buf[BUFSIZ];

	// read the nid from the system location
	// open up the file containing our node id (nid)
	if ((nid_fd = fopen(ALPS_XT_NID, "r")) == NULL)
	{
		fprintf(stderr, "%s: %s not found.\n", CTI_LAUNCHER, ALPS_XT_NID);
		return -1;
	}
		
	// we expect this file to have a numeric value giving our current nid
	if (fgets(file_buf, BUFSIZ, nid_fd) == NULL)
	{
		fprintf(stderr, "%s: fgets failed.\n", CTI_LAUNCHER);
		return -1;
	}
		
	// convert this to an integer value
	nid = atoi(file_buf);
	
	// close the file stream
	fclose(nid_fd);
	
	return nid;
}

