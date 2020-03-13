/******************************************************************************\
 * pals_dl.c - PALS specific functions for the daemon launcher.
 *
 * Copyright 2014-2019 Cray Inc.  All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
 *
 ******************************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>

#include "cti_daemon.h"

#include "pals.h"

// types used here
typedef struct {
	void *handle;
	pals_rc_t (*pals_init)(pals_state_t *state);
	pals_rc_t (*pals_fini)(pals_state_t *state);
	pals_rc_t (*pals_get_nodeidx)(pals_state_t *state, int *nodeidx);
} cti_libpals_funcs_t;

/* static prototypes */
static int	_cti_pals_init(void);
static int	_cti_pals_getNodeID(void);

/* alps wlm proto object */
cti_wlm_proto_t				_cti_pals_wlmProto =
{
	CTI_WLM_PALS,			// wlm_type
	_cti_pals_init,			// wlm_init
	_cti_pals_getNodeID		// wlm_getNodeID
};

static int _cti_nodeidx = -1; // Current node index

/* functions start here */

static int
_cti_pals_init(void)
{
	return 0;
}

static int
_cti_pals_getNodeID(void)
{
	// If already found node index, use that
	if (_cti_nodeidx >= 0) {
		return _cti_nodeidx;
	}

	int result = -1;

	// dlopen libpals implementation
	cti_libpals_funcs_t _cti_libpals_funcs;
	_cti_libpals_funcs.handle = NULL;
	pals_state_t _cti_pals_state;
	int libpals_initialized = 0;

	// dlopen libpals
	_cti_libpals_funcs.handle = dlopen(PALS_BE_LIB_NAME, RTLD_LAZY);
	if (_cti_libpals_funcs.handle == NULL) {
		fprintf(stderr, "dlopen: %s\n", dlerror());
		goto cleanup__cti_pals_getNodeID;
	}

	// Load functions from libpals

	// pals_init
	dlerror(); // Clear any existing error
	_cti_libpals_funcs.pals_init = dlsym(_cti_libpals_funcs.handle, "pals_init");
	if (dlerror() != NULL) {
		fprintf(stderr, "dlsym: %s\n", dlerror());
		goto cleanup__cti_pals_getNodeID;
	}

	// pals_fini
	dlerror();
	_cti_libpals_funcs.pals_fini = dlsym(_cti_libpals_funcs.handle, "pals_init");
	if (dlerror() != NULL) {
		fprintf(stderr, "dlsym: %s\n", dlerror());
		goto cleanup__cti_pals_getNodeID;
	}

	// pals_get_nodeidx
	dlerror();
	_cti_libpals_funcs.pals_get_nodeidx = dlsym(_cti_libpals_funcs.handle, "pals_get_nodeidx");
	if (dlerror() != NULL) {
		fprintf(stderr, "dlsym: %s\n", dlerror());
		goto cleanup__cti_pals_getNodeID;
	}

	// Initialize global libpals state
	if (_cti_libpals_funcs.pals_init(&_cti_pals_state) != PALS_OK) {
		fprintf(stderr, "libpals initialization failed\n");
		goto cleanup__cti_pals_getNodeID;
	}
	libpals_initialized = 1;

	// Call pals_get_nodeidx
	if (_cti_libpals_funcs.pals_get_nodeidx(&_cti_pals_state, &result) != PALS_OK) {
		fprintf(stderr, "pals_get_nodeidx failed\n");
		goto cleanup__cti_pals_getNodeID;
	}

cleanup__cti_pals_getNodeID:
	// Deinitialize libpals state
	if (libpals_initialized) {
		_cti_libpals_funcs.pals_fini(&_cti_pals_state);
		libpals_initialized = 0;
	}

	// Close dlopen handle
	if (_cti_libpals_funcs.handle != NULL) {
		dlclose(_cti_libpals_funcs.handle);
		_cti_libpals_funcs.handle = NULL;
	}

	return -1;
}

