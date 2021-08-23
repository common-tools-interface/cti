/*********************************************************************************\
 * flux_be.c - flux specific backend library functions.
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
 *********************************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <dlfcn.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "cti_defs.h"
#include "cti_be.h"

#include "pmi_attribs_parser.h"

/* static prototypes */
static int            _cti_be_flux_init(void);
static void           _cti_be_flux_fini(void);
static cti_pidList_t* _cti_be_flux_findAppPids(void);
static char*          _cti_be_flux_getNodeHostname(void);
static int            _cti_be_flux_getNodeFirstPE(void);
static int            _cti_be_flux_getNodePEs(void);

/* flux wlm proto object */
cti_be_wlm_proto_t _cti_be_flux_wlmProto =
	{ CTI_WLM_FLUX                 // wlm_type
	, _cti_be_flux_init            // wlm_init
	, _cti_be_flux_fini            // wlm_fini
	, _cti_be_flux_findAppPids     // wlm_findAppPids
	, _cti_be_flux_getNodeHostname // wlm_getNodeHostname
	, _cti_be_flux_getNodeFirstPE   // wlm_getNodeFirstPE
	, _cti_be_flux_getNodePEs       // wlm_getNodePEs
};

// Global vars
static pmi_attribs_t* g_cti_attrs = NULL;
static int g_initialized = 0;

static void
_cti_cleanup_be_globals(void)
{
    if (g_cti_attrs != NULL) {
        _cti_be_freePmiAttribs(g_cti_attrs);
        g_cti_attrs = NULL;
    }
}

/* Constructor/Destructor functions */

static int
_cti_be_flux_init(void)
{
    int rc = 0;

    if (g_initialized) {
        goto cleanup__cti_be_flux_init;
    }

    // Read PMI attributes from file
    g_cti_attrs = _cti_be_getPmiAttribsInfo();
    if ((g_cti_attrs == NULL)
     || (g_cti_attrs->app_rankPidPairs == NULL)) {
        rc = -1;
        goto cleanup__cti_be_flux_init;
    }

    g_initialized = 1;

cleanup__cti_be_flux_init:
    if (rc) {
        _cti_cleanup_be_globals();
    }

    return rc;
}

static void
_cti_be_flux_fini(void)
{
    _cti_cleanup_be_globals();

    return;
}

/* API related calls start here */

static cti_pidList_t*
_cti_be_flux_findAppPids()
{
    int failed = 1;
    cti_pidList_t *result = NULL;

    assert(g_cti_attrs != NULL);

    // Allocate and fill in PID list
    result = malloc(sizeof(cti_pidList_t));
    result->numPids = g_cti_attrs->app_nodeNumRanks;

    if (result->numPids > 0) {
        result->pids = malloc(result->numPids * sizeof(cti_rankPidPair_t));
        for (int i = 0; i < result->numPids; i++) {
            result->pids[i].pid = g_cti_attrs->app_rankPidPairs[i].pid;
            result->pids[i].rank = g_cti_attrs->app_rankPidPairs[i].rank;
        }
    }

    failed = 0;

cleanup__cti_be_flux_findAppPids:
    if (failed) {
        if (result != NULL) {
            free(result);
            result = NULL;
        }
    }

    return result;
}

static char*
_cti_be_flux_getNodeHostname()
{
    int failed = 1;
    char *result = NULL;

    // Get the hostname
    char hostname[HOST_NAME_MAX+1];

    if (gethostname(hostname, HOST_NAME_MAX) < 0) {
        goto cleanup__cti_be_flux_getNodeHostname;
    }

    result = strdup(hostname);

    failed = 0;

cleanup__cti_be_flux_getNodeHostname:
    if (failed) {
        if (result != NULL) {
            free(result);
            result = NULL;
        }
    }

    return result;
}

static int
_cti_be_flux_getNodeFirstPE()
{
    int result = -1;

    assert(g_cti_attrs != NULL);

    if (g_cti_attrs->app_nodeNumRanks > 0) {
        result = g_cti_attrs->app_rankPidPairs[0].rank;
    }

cleanup__cti_be_flux_getNodeFirstPE:
    return result;
}

static int
_cti_be_flux_getNodePEs()
{
    int result = -1;

    assert(g_cti_attrs != NULL);

    result = g_cti_attrs->app_nodeNumRanks;

cleanup__cti_be_flux_getNodePEs:
    return result;
}


