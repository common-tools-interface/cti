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

/******************************************************************************
   _cti_cray_slurm_getNodeID - Gets the id for the current node
   
   Detail
        I return a unique id for the current node.

        On Cray nodes this can be done with very little overhead
        by reading the nid number out of /proc. If that is not
        available I fall back to just doing a libc gethostname call
        to get the name and then return a hash of that name.

        As an opaque implementation detail, I cache the results
        for successive calls.

   Returns
        An int representing an unique id for the current node
   
*/
static int
_cti_cray_slurm_getNodeID(void)
{
	static  int cachedNid = -1;
	FILE *	nid_fd;
	char	file_buf[BUFSIZ];

    // Determined the nid already?
    if (cachedNid != -1)
        return cachedNid;

	// read the nid from the system location
	// open up the file containing our node id (nid)
	if ((nid_fd = fopen(CRAY_NID_FILE, "r")) != NULL)
	{
	    // we expect this file to have a numeric value giving our current nid
	    if (fgets(file_buf, BUFSIZ, nid_fd) == NULL)
	    {
		    fprintf(stderr, "%s: _cti_cray_slurm_getNodeID:fgets failed.\n", CTI_DLAUNCH_BINARY);
		    return -1;
	    }
		
	    // convert this to an integer value
	    cachedNid = atoi(file_buf);
	
	    // close the file stream
	    fclose(nid_fd);
	
    }

    else // Fallback to hash of standard hostname
    {
        // Get the hostname
        char hostname[HOST_NAME_MAX+1];

        if (gethostname(hostname, HOST_NAME_MAX) < 0)
        {
            fprintf(stderr, "%s", "_cti_cray_slurm_getNodeID: gethostname() failed!\n");
            return -1;
        }

        // Hash the hostname
        char *p = hostname;
        unsigned int hash = 0;
        int c;

        while ((c = *(p++)))
            hash = c + (hash << 6) + (hash << 16) - hash;

        cachedNid = (int)hash;
    }

    return cachedNid;
}
