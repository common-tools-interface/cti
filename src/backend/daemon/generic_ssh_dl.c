/******************************************************************************\
 * generic_ssh_dl.c - SSH based workload manager specific functions for the
 *              daemon launcher.
 *
 * Copyright 2016-2020 Hewlett Packard Enterprise Development LP.
 * SPDX-License-Identifier: Linux-OpenIB
 ******************************************************************************/

// This pulls in config.h
#include "cti_defs.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cti_daemon.h"

/* static prototypes */
static int  _cti_generic_ssh_init(void);
static int  _cti_generic_ssh_getNodeID(void);

/* generic ssh wlm proto object */
cti_wlm_proto_t     _cti_generic_ssh_wlmProto =
{
    CTI_WLM_SSH,                // wlm_type
    _cti_generic_ssh_init,      // wlm_init
    _cti_generic_ssh_getNodeID  // wlm_getNodeID
};

/* functions start here */

static int
_cti_generic_ssh_init(void)
{
    // NO-OP

    return 0;
}

/******************************************************************************
   _cti_generic_ssh_getNodeID - Gets the id for the current node

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
_cti_generic_ssh_getNodeID(void)
{
    static  int cachedNid = -1;
    FILE *  nid_fd;
    char    file_buf[BUFSIZ];

    // Determined the nid already?
    if (cachedNid != -1)
        return cachedNid;

    // read the nid from the system location
    // open up the file containing our node id (nid)
    if (((nid_fd = fopen(CRAY_XT_NID_FILE, "r"))     != NULL) ||
        ((nid_fd = fopen(CRAY_SHASTA_NID_FILE, "r")) != NULL))
    {
        // we expect this file to have a numeric value giving our current nid
        if (fgets(file_buf, BUFSIZ, nid_fd) == NULL)
        {
            fprintf(stderr, "%s: _cti_generic_ssh_getNodeID:fgets failed.\n", CTI_BE_DAEMON_BINARY);
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
            fprintf(stderr, "%s", "_cti_generic_ssh_getNodeID: gethostname() failed!\n");
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
