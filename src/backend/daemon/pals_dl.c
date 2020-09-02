/******************************************************************************\
 * pals_dl.c - PALS specific functions for the daemon launcher.
 *
 * Copyright 2020 Hewlett Packard Enterprise Development LP.
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

	char const* dl_err = NULL;

	// dlopen libpals
	_cti_libpals_funcs.handle = dlopen(PALS_BE_LIB_NAME, RTLD_LAZY);
	dl_err = dlerror();
	if (_cti_libpals_funcs.handle == NULL) {
		fprintf(stderr, "pals_dl " PALS_BE_LIB_NAME " dlopen: %s\n", dl_err);
		goto cleanup__cti_pals_getNodeID;
	}

	// Load functions from libpals

	// pals_init
	dlerror(); // Clear any existing error
	_cti_libpals_funcs.pals_init = dlsym(_cti_libpals_funcs.handle, "pals_init");
	dl_err = dlerror();
	if (dl_err != NULL) {
		fprintf(stderr, "pals_dl " PALS_BE_LIB_NAME " dlsym: %s\n", dl_err);
		goto cleanup__cti_pals_getNodeID;
	}

	// pals_fini
	dlerror();
	_cti_libpals_funcs.pals_fini = dlsym(_cti_libpals_funcs.handle, "pals_init");
	dl_err = dlerror();
	if (dl_err != NULL) {
		fprintf(stderr, "pals_dl " PALS_BE_LIB_NAME " dlsym: %s\n", dl_err);
		goto cleanup__cti_pals_getNodeID;
	}

	// pals_get_nodeidx
	dlerror();
	_cti_libpals_funcs.pals_get_nodeidx = dlsym(_cti_libpals_funcs.handle, "pals_get_nodeidx");
	dl_err = dlerror();
	if (dl_err != NULL) {
		fprintf(stderr, "pals_dl " PALS_BE_LIB_NAME " dlsym: %s\n", dl_err);
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
		fprintf(stderr, "pals_dl pals_get_nodeidx: %s\n", _cti_pals_state.errbuf);
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

	return result;
}

