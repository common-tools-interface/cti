/*********************************************************************************\
 * pals_be.c - pals specific backend library functions.
 *
 * Copyright 2014-2019 Cray Inc.  All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
 *
 *********************************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <dlfcn.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "cti_defs.h"
#include "cti_be.h"

#include "pals.h"
#include "pmi_attribs_parser.h"

// types used here
typedef struct {
	void *handle;
	pals_rc_t (*pals_init)(pals_state_t *state);
	pals_rc_t (*pals_fini)(pals_state_t *state);
	pals_rc_t (*pals_get_nodes)(pals_state_t *state, pals_node_t **nodes, int *nnodes);
	pals_rc_t (*pals_get_cmds)(pals_state_t *state, pals_cmd_t **cmds, int *ncmds);
	pals_rc_t (*pals_get_nodeidx)(pals_state_t *state, int *nodeidx);
} cti_libpals_funcs_t;

/* static prototypes */
static int            _cti_be_pals_init(void);
static void           _cti_be_pals_fini(void);
static cti_pidList_t* _cti_be_pals_findAppPids(void);
static char*          _cti_be_pals_getNodeHostname(void);
static int            _cti_be_pals_getNodeFirstPE(void);
static int            _cti_be_pals_getNodePEs(void);

/* pals wlm proto object */
cti_be_wlm_proto_t _cti_be_pals_wlmProto =
	{ CTI_WLM_PALS                 // wlm_type
	, _cti_be_pals_init            // wlm_init
	, _cti_be_pals_fini            // wlm_fini
	, _cti_be_pals_findAppPids     // wlm_findAppPids
	, _cti_be_pals_getNodeHostname // wlm_getNodeHostname
	, _cti_be_pals_getNodeFirstPE   // wlm_getNodeFirstPE
	, _cti_be_pals_getNodePEs       // wlm_getNodePEs
};

// Global vars
static cti_libpals_funcs_t* _cti_libpals_funcs = NULL; // libpals wrappers
static pals_state_t *_cti_pals_state = NULL; // libpals state
static pmi_attribs_t *_cti_pmi_attrs = NULL; // node pmi_attribs information

static void
cleanup_cti_be_globals(void)
{
	// Cleanup pmi_attribs storage
	if (_cti_pmi_attrs != NULL) {
		free(_cti_pmi_attrs);
		_cti_pmi_attrs = NULL;
	}

	// Cleanup libpals function struct
	if (_cti_libpals_funcs != NULL) {

		// Deinitialize libpals state
		if (_cti_pals_state != NULL) {
			_cti_libpals_funcs->pals_fini(_cti_pals_state);
			_cti_pals_state = NULL;
		}

		// Close dlopen handle
		if (_cti_libpals_funcs->handle != NULL) {
			dlclose(_cti_libpals_funcs->handle);
			_cti_libpals_funcs->handle = NULL;
		}

		// Free function struct storage
		free(_cti_libpals_funcs);
		_cti_libpals_funcs = NULL;
	}
}

/* Constructor/Destructor functions */

static int
_cti_be_pals_init(void)
{
	// Only init once.
	if (_cti_libpals_funcs != NULL) {
		return 0;
	}

	int rc = 1;

	// Zero-initialize libpals function struct
	_cti_libpals_funcs = (cti_libpals_funcs_t*)malloc(sizeof(cti_libpals_funcs_t));
	if (_cti_libpals_funcs == NULL) {
		fprintf(stderr, "malloc failed");
		goto cleanup__cti_be_pals_init;
	}
	memset(_cti_libpals_funcs, 0, sizeof(cti_libpals_funcs_t));

	// dlopen libpals
	_cti_libpals_funcs->handle = dlopen(PALS_BE_LIB_NAME, RTLD_LAZY);
	if (_cti_libpals_funcs == NULL) {
		fprintf(stderr, "dlopen: %s\n", dlerror());
		goto cleanup__cti_be_pals_init;
	}

	// Load functions from libpals

	// pals_init
	dlerror(); // Clear any existing error
	_cti_libpals_funcs->pals_init = dlsym(_cti_libpals_funcs->handle, "pals_init");
	if (dlerror() != NULL) {
		fprintf(stderr, "dlsym: %s\n", dlerror());
		goto cleanup__cti_be_pals_init;
	}

	// pals_fini
	dlerror();
	_cti_libpals_funcs->pals_fini = dlsym(_cti_libpals_funcs->handle, "pals_fini");
	if (dlerror() != NULL) {
		fprintf(stderr, "dlsym: %s\n", dlerror());
		goto cleanup__cti_be_pals_init;
	}

	// pals_get_nodes
	dlerror();
	_cti_libpals_funcs->pals_get_nodes = dlsym(_cti_libpals_funcs->handle, "pals_get_nodes");
	if (dlerror() != NULL) {
		fprintf(stderr, "dlsym: %s\n", dlerror());
		goto cleanup__cti_be_pals_init;
	}

	// pals_get_cmds
	dlerror();
	_cti_libpals_funcs->pals_get_cmds = dlsym(_cti_libpals_funcs->handle, "pals_get_cmds");
	if (dlerror() != NULL) {
		fprintf(stderr, "dlsym: %s\n", dlerror());
		goto cleanup__cti_be_pals_init;
	}

	// pals_get_nodeidx
	dlerror();
	_cti_libpals_funcs->pals_get_nodeidx = dlsym(_cti_libpals_funcs->handle, "pals_get_nodeidx");
	if (dlerror() != NULL) {
		fprintf(stderr, "dlsym: %s\n", dlerror());
		goto cleanup__cti_be_pals_init;
	}

	// Allocate global libpals state
	_cti_pals_state = (pals_state_t*)malloc(sizeof(pals_state_t));
	if (_cti_pals_state == NULL) {
		fprintf(stderr, "malloc failed");
		goto cleanup__cti_be_pals_init;
	}
	// Initialize global libpals state
	if (_cti_libpals_funcs->pals_init(_cti_pals_state) != PALS_OK) {
		fprintf(stderr, "libpals initialization failed\n");
		goto cleanup__cti_be_pals_init;
	}

	// Successful initialization
	rc = 0;

cleanup__cti_be_pals_init:
	if (rc) {
		cleanup_cti_be_globals();
	}

	return rc;
}

static void
_cti_be_pals_fini(void)
{
	cleanup_cti_be_globals();

	return;
}

/* API related calls start here */

static cti_pidList_t*
_cti_be_pals_findAppPids()
{
	int failed = 1;
	cti_pidList_t *result = NULL;

	// Get PMI attribs from system file
	if (_cti_pmi_attrs == NULL) {
		_cti_pmi_attrs = _cti_be_getPmiAttribsInfo();
		if (_cti_pmi_attrs == NULL) {
			fprintf(stderr, "_cti_be_getPmiAttribsInfo failed\n");
			goto cleanup__cti_be_pals_findAppPids;
		}
	}

	// Ensure the _cti_attrs object has a app_rankPidPairs array
	if (_cti_pmi_attrs->app_rankPidPairs == NULL) {
		fprintf(stderr, "_cti_be_getPmiAttribsInfo failed: no rank information returned\n");
		goto cleanup__cti_be_pals_findAppPids;
	}

	// Allocate result struct
	result = (cti_pidList_t*)malloc(sizeof(cti_pidList_t));
	if (result == NULL) {
		fprintf(stderr, "malloc failed\n");
		goto cleanup__cti_be_pals_findAppPids;
	}

	// Fil in result struct

	// Allocate the PID / rank pair array
	result->numPids = _cti_pmi_attrs->app_nodeNumRanks;
	result->pids = (cti_rankPidPair_t*)malloc(result->numPids * sizeof(cti_rankPidPair_t));
	if (result->pids == NULL) {
		fprintf(stderr, "malloc failed\n");
		goto cleanup__cti_be_pals_findAppPids;
	}

	// Copy all PID / rank pairs to result array
	for (int i = 0; i < result->numPids; i++) {
		result->pids[i].pid  = _cti_pmi_attrs->app_rankPidPairs[i].pid;
		result->pids[i].rank = _cti_pmi_attrs->app_rankPidPairs[i].rank;
	}

	// Successfully created result array
	failed = 0;

cleanup__cti_be_pals_findAppPids:
	if (failed) {
		if (result != NULL) {
			free(result);
			result = NULL;
		}
	}

	return result;
}

static char*
_cti_be_pals_getNodeHostname()
{
	int failed = 1;
	char *result = NULL;

cleanup__cti_be_pals_getNodeHostname:
	if (failed) {
		if (result != NULL) {
			free(result);
			result = NULL;
		}
	}

	return NULL;
}

static int
_cti_be_pals_getNodeFirstPE()
{
	int result = -1;

cleanup__cti_be_pals_getNodeFirstPE:
	return result;
}

static int
_cti_be_pals_getNodePEs()
{
	int result = -1;

cleanup__cti_be_pals_getNodePEs:
	return result;
}


