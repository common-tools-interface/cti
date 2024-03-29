/******************************************************************************\
 * alps_dl.c - Alps specific functions for the daemon launcher.
 *
 * Copyright 2014-2020 Hewlett Packard Enterprise Development LP.
 * SPDX-License-Identifier: Linux-OpenIB
 ******************************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <stdio.h>
#include <stdlib.h>

#include "cti_daemon.h"

/* static prototypes */
static int	_cti_alps_init(void);
static int	_cti_alps_getNodeID(void);

/* alps wlm proto object */
cti_wlm_proto_t				_cti_alps_wlmProto =
{
	CTI_WLM_ALPS,			// wlm_type
	_cti_alps_init,			// wlm_init
	_cti_alps_getNodeID		// wlm_getNodeID
};

/* functions start here */

static int
_cti_alps_init(void)
{
	// Set LC_ALL to POSIX - on Cray platforms this has been shown to significantly
	// speed up load times if the tool daemon is invoking the shell.
	if (setenv("LC_ALL", "POSIX", 1) < 0)
	{
		// failure
		fprintf(stderr, "%s: setenv failed\n", CTI_BE_DAEMON_BINARY);
		return 1;
	}

	// set the SHELL environment variable to the shell included on the compute
	// node. Note that other shells other than /bin/sh are not currently supported
	// in CNL.
	if (setenv(SHELL_ENV_VAR, SHELL_PATH, 1) < 0)
	{
		// failure
		fprintf(stderr, "%s: setenv failed\n", CTI_BE_DAEMON_BINARY);
		return 1;
	}

	return 0;
}

static int
_cti_alps_getNodeID(void)
{
	FILE *	alps_fd;
	int		nid;
	char	file_buf[BUFSIZ];

	// read the nid from the system location
	// open up the file defined in the alps header containing our node id (nid)
	if ((alps_fd = fopen(ALPS_XT_NID, "r")) == NULL)
	{
		fprintf(stderr, "%s: %s not found.\n", CTI_BE_DAEMON_BINARY, ALPS_XT_NID);
		return -1;
	}

	// we expect this file to have a numeric value giving our current nid
	if (fgets(file_buf, BUFSIZ, alps_fd) == NULL)
	{
		fprintf(stderr, "%s: fgets failed.\n", CTI_BE_DAEMON_BINARY);
		return -1;
	}

	// convert this to an integer value
	nid = atoi(file_buf);

	// close the file stream
	fclose(alps_fd);

	return nid;
}

