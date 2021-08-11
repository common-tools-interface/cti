/******************************************************************************\
 * flux_dl.c - Flux specific functions for the daemon launcher.
 *
 * Copyright 2021 Hewlett Packard Enterprise Development LP.
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 ******************************************************************************/

// This pulls in config.h
#include "cti_defs.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "cti_daemon.h"

/* static prototypes */
static int  _cti_flux_init(void);
static int  _cti_flux_getNodeID(void);

/* slurm wlm proto object */
cti_wlm_proto_t     _cti_flux_wlmProto =
{
    CTI_WLM_FLUX,          // wlm_type
    _cti_flux_init,        // wlm_init
    _cti_flux_getNodeID    // wlm_getNodeID
};

/* functions start here */

static int
_cti_flux_init(void)
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

static int
_cti_flux_getNodeID(void)
{
    static  int cachedNid = -1;

    // Determined the nid already?
    if (cachedNid != -1) {
        return cachedNid;
    }

    // Get the hostname
    char hostname[HOST_NAME_MAX+1];

    if (gethostname(hostname, HOST_NAME_MAX) < 0) {
        fprintf(stderr, "%s", "_cti_flux_getNodeID: gethostname() failed!\n");
        return -1;
    }

    // Hash the hostname
    char *p = hostname;
    unsigned int hash = 0;
    int c;

    while ((c = *(p++))) {
        hash = c + (hash << 6) + (hash << 16) - hash;
    }

    cachedNid = (int)hash;

    return cachedNid;
}
