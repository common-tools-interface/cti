/******************************************************************************\
 * localhost_dl.c - single node workload manager specific functions for the
 *              daemon launcher.
 *
 * Copyright 2023 Hewlett Packard Enterprise Development LP.
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
static int  _cti_localhost_init(void);
static int  _cti_localhost_getNodeID(void);

/* generic ssh wlm proto object */
cti_wlm_proto_t     _cti_localhost_wlmProto =
{
    CTI_WLM_LOCALHOST,                // wlm_type
    _cti_localhost_init,      // wlm_init
    _cti_localhost_getNodeID  // wlm_getNodeID
};

/* functions start here */

static int
_cti_localhost_init(void)
{
    // NO-OP

    return 0;
}

/******************************************************************************
   _cti_localhost_getNodeID - Gets the id for the current node

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
_cti_localhost_getNodeID(void)
{
    return 0;
}
