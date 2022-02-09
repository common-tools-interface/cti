/*********************************************************************************\
 * pals_be.c - pals specific backend library functions.
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
 *********************************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <dlfcn.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>
#include <wait.h>

#include "cti_defs.h"
#include "cti_be.h"

#include "pals.h"
#include "pmi_attribs_parser.h"

// types used here
typedef struct {
	void *handle;
	const char * (*pals_errmsg)(pals_state_t *state); // Returns static string, do not free
	pals_rc_t (*pals_init)(pals_state_t *state);
	pals_rc_t (*pals_fini)(pals_state_t *state);

	pals_rc_t (*pals_get_nodes)(pals_state_t *state, pals_node_t **nodes, int *nnodes);
	pals_rc_t (*pals_get_nodeidx)(pals_state_t *state, int *nodeidx);

	pals_rc_t (*pals_get_pes)(pals_state_t *state, pals_pe_t **pes, int *npes);
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

static int _cti_node_idx = -1; // node index for PALS accessors
static pals_node_t *_cti_pals_nodes = NULL; // list of job nodes
static int _cti_pals_num_nodes = -1; // number of job nodes

static pals_pe_t *_cti_pals_pes = NULL; // list of PEs
static int _cti_pals_num_pes = -1; // number of PEs

static void
_cti_cleanup_be_globals(void)
{
	// Cleanup PEs list
	if (_cti_pals_pes != NULL) {
		free(_cti_pals_pes);
		_cti_pals_pes = NULL;
	}

	// Cleanup node list
	if (_cti_pals_nodes != NULL) {
		free(_cti_pals_nodes);
		_cti_pals_nodes = NULL;
	}

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

// Use pkg-config to detect the location of the libpals library,
// or use the system default directory upon failure.
static int
_cti_be_pals_detect_libpals(char* path, size_t path_cap)
{
	if (path == NULL) {
		return 1;
	}

	char const* detected_path = NULL;

	int pkgconfig_pipe[2];
	pid_t pkgconfig_pid = -1;
	int read_cursor = 0;
	char libpals_libdir[PATH_MAX];

	// Set up pkgconfig pipe
	if (pipe(pkgconfig_pipe) < 0) {
		perror("pipe");
		goto cleanup__cti_be_pals_detect_libpals;
	}

	// Fork pkgconfig
	pkgconfig_pid = fork();
	if (pkgconfig_pid < 0) {
		perror("fork");
		goto cleanup__cti_be_pals_detect_libpals;

	// Query pkgconfig for libpals' libdir
	} else if (pkgconfig_pid == 0) {
		char const* pkgconfig_argv[] = {"pkg-config", "--variable=libdir", "libpals", NULL};

		// Set up pkgconfig output
		close(pkgconfig_pipe[0]);
		pkgconfig_pipe[0] = -1;
		dup2(pkgconfig_pipe[1], STDOUT_FILENO);

		// Exec pkgconfig
		execvp("pkg-config", (char* const*)pkgconfig_argv);
		perror("execvp");
		return -1;
	}

	// Set up pkgconfig input
	close(pkgconfig_pipe[1]);
	pkgconfig_pipe[1] = -1;

	// Read pkgconfig output
	read_cursor = 0;
	while (1) {
		errno = 0;
		int read_rc = read(pkgconfig_pipe[0], libpals_libdir + read_cursor,
			sizeof(libpals_libdir) - read_cursor - 1);

		if (read_rc < 0) {

			// Retry if applicable
			if (errno == EINTR) {
				continue;

			} else {
				perror("read");
				goto cleanup__cti_be_pals_detect_libpals;
			}

		// Return result if EOF
		} else if (read_rc == 0) {

			// No data was read
			if (read_cursor == 0) {
				fprintf(stderr, "pkg-config: no output\n");
				break;
			}

			// Remove trailing newline
			libpals_libdir[read_cursor - 1] = '\0';

			detected_path = libpals_libdir;
			fprintf(stderr, "pkg-config: %s\n", detected_path);
			break;

		// Update cursor with number of bytes read
		} else {
			read_cursor += read_rc;

			// pkgconfig output is larger than maximum path size
			if (read_cursor >= (sizeof(libpals_libdir) - 1)) {
				fprintf(stderr, "pkg-config: output larger than PATH_MAX\n");
				goto cleanup__cti_be_pals_detect_libpals;
			}
		}
	}

cleanup__cti_be_pals_detect_libpals:

	close(pkgconfig_pipe[0]);
	pkgconfig_pipe[0] = -1;

	// Wait and check for pkgconfig return code
	if (pkgconfig_pid > 0) {

		// Reset SIGCHLD to default
		int old_action_valid = 0;
		struct sigaction old_action;

		// Back up old signal disposition
		struct sigaction sa;
		sigemptyset(&sa.sa_mask);
		sa.sa_flags = 0;
		sa.sa_handler = SIG_DFL;

		// Ignore invalid sigaction and let processing continue
		if (sigaction(SIGCHLD, &sa, &old_action) < 0) {
			perror("sigaction");
		} else {
			old_action_valid = 1;
		}

		// Wait for pkgconfig to exit
		while (1) {
			int status;

			// Get exit code
			errno = 0;
			if (waitpid(pkgconfig_pid, &status, 0) < 0) {

				// Retry wait if applicable
				if (errno == EAGAIN) {
					continue;

				// pkgconfig failed, use system default
				} else if (errno != ECHILD) {
					perror("waitpid");
					detected_path = NULL;
					break;

				} else {
					break;
				}
			}

			// Check exit code
			if (WEXITSTATUS(status)) {
				fprintf(stderr, "pkg-config failed with status %d\n", WEXITSTATUS(status));
				detected_path = NULL;
				break;
			}
		}

		// Restore old SIGCHLD disposition
		if (old_action_valid) {
			if (sigaction(SIGCHLD, &old_action, NULL) < 0) {
				perror("sigaction");
			}
			old_action_valid = 0;
		}
	}

	// If failed, use system default library path
	if (detected_path == NULL) {
		detected_path = PALS_BE_LIB_DEFAULT_PATH;
	}

	// Format detected path with libpals library
	int snprintf_rc = snprintf(path, path_cap,
		"%s/%s", detected_path, PALS_BE_LIB_NAME);
	if ((snprintf_rc < 0) || (snprintf_rc >= path_cap)) {
		perror("snprintf");
		return 1;
	}

	return 0;
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

	char const* dl_err = NULL;

	// Detect location of libpals
	char libpals_path[PATH_MAX];
	if (_cti_be_pals_detect_libpals(libpals_path, sizeof(libpals_path))) {
		fprintf(stderr, "pals_be " PALS_BE_LIB_NAME " failed to detect libpals path\n");
		goto cleanup__cti_be_pals_init;
	} else {
		fprintf(stderr, "Using detected libpals at: %s\n", libpals_path);
	}

	// dlopen libpals
	_cti_libpals_funcs->handle = dlopen(libpals_path, RTLD_LAZY);
	dl_err = dlerror();
	if (_cti_libpals_funcs == NULL) {
		fprintf(stderr, "pals_be " PALS_BE_LIB_NAME " dlopen: %s\n", dl_err);
		goto cleanup__cti_be_pals_init;
	}

	// Load functions from libpals

	// pals_errmsg
	dlerror(); // Clear any existing error
	_cti_libpals_funcs->pals_errmsg = dlsym(_cti_libpals_funcs->handle, "pals_errmsg");
	dl_err = dlerror();
	if (dl_err != NULL) {
		fprintf(stderr, "pals_be " PALS_BE_LIB_NAME " dlsym: %s\n", dl_err);
		goto cleanup__cti_be_pals_init;
	}

	// pals_init
	dlerror(); // Clear any existing error
	_cti_libpals_funcs->pals_init = dlsym(_cti_libpals_funcs->handle, "pals_init");
	dl_err = dlerror();
	if (dl_err != NULL) {
		fprintf(stderr, "pals_be " PALS_BE_LIB_NAME " dlsym: %s\n", dl_err);
		goto cleanup__cti_be_pals_init;
	}

	// pals_fini
	dlerror();
	_cti_libpals_funcs->pals_fini = dlsym(_cti_libpals_funcs->handle, "pals_fini");
	dl_err = dlerror();
	if (dl_err != NULL) {
		fprintf(stderr, "pals_be " PALS_BE_LIB_NAME " dlsym: %s\n", dl_err);
		goto cleanup__cti_be_pals_init;
	}

	// pals_get_nodes
	dlerror();
	_cti_libpals_funcs->pals_get_nodes = dlsym(_cti_libpals_funcs->handle, "pals_get_nodes");
	dl_err = dlerror();
	if (dl_err != NULL) {
		fprintf(stderr, "pals_be " PALS_BE_LIB_NAME " dlsym: %s\n", dl_err);
		goto cleanup__cti_be_pals_init;
	}

	// pals_get_nodeidx
	dlerror();
	_cti_libpals_funcs->pals_get_nodeidx = dlsym(_cti_libpals_funcs->handle, "pals_get_nodeidx");
	dl_err = dlerror();
	if (dl_err != NULL) {
		fprintf(stderr, "pals_be " PALS_BE_LIB_NAME " dlsym: %s\n", dl_err);
		goto cleanup__cti_be_pals_init;
	}

	// pals_get_pes
	dlerror();
	_cti_libpals_funcs->pals_get_pes = dlsym(_cti_libpals_funcs->handle, "pals_get_pes");
	dl_err = dlerror();
	if (dl_err != NULL) {
		fprintf(stderr, "pals_be " PALS_BE_LIB_NAME " dlsym: %s\n", dl_err);
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
		fprintf(stderr, "libpals initialization failed: %s\n", _cti_libpals_funcs->pals_errmsg(_cti_pals_state));
		goto cleanup__cti_be_pals_init;
	}

	// Successful initialization
	rc = 0;

cleanup__cti_be_pals_init:
	if (rc) {
		_cti_cleanup_be_globals();
	}

	return rc;
}

static void
_cti_be_pals_fini(void)
{
	_cti_cleanup_be_globals();

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

static int
_cti_get_nodes_info()
{
	int rc = -1;

	// Check libpals functions
	if ((_cti_libpals_funcs == NULL) || (_cti_libpals_funcs->pals_get_nodes == NULL)) {
		goto cleanup__cti_get_nodes_info;
	}

	// Call libpals accessor
	if (_cti_libpals_funcs->pals_get_nodes(_cti_pals_state, &_cti_pals_nodes, &_cti_pals_num_nodes) != PALS_OK) {
		fprintf(stderr, "pals_be libpals pals_get_nodes failed: %s\n", _cti_libpals_funcs->pals_errmsg(_cti_pals_state));
		goto cleanup__cti_get_nodes_info;
	}

	// Successfully retrieved node information
	rc = 0;

cleanup__cti_get_nodes_info:
	return rc;
}

static int
_cti_get_node_idx()
{
	int rc = -1;

	// Check libpals functions
	if ((_cti_libpals_funcs == NULL) || (_cti_libpals_funcs->pals_get_nodeidx == NULL)) {
		goto cleanup__cti_get_node_idx;
	}

	// Call libpals accessor
	if (_cti_libpals_funcs->pals_get_nodeidx(_cti_pals_state, &_cti_node_idx) != PALS_OK) {
		fprintf(stderr, "pals_be libpals pals_get_nodeidx failed: %s\n", _cti_libpals_funcs->pals_errmsg(_cti_pals_state));
		goto cleanup__cti_get_node_idx;
	}

	// Successfully retrieved node index
	rc = 0;

cleanup__cti_get_node_idx:
	return rc;
}

static char*
_cti_be_pals_getNodeHostname()
{
	int failed = 1;
	char *result = NULL;

	// Ensure nodes array and current node index is filled in
	if (_cti_pals_nodes == NULL) {
		if (_cti_get_nodes_info() || (_cti_pals_nodes == NULL)) {
			fprintf(stderr, "_cti_get_nodes_info failed\n");
			goto cleanup__cti_be_pals_getNodeHostname;
		}
	}
	if (_cti_node_idx < 0) {
		if (_cti_get_node_idx() || (_cti_node_idx < 0)) {
			fprintf(stderr, "pals_get_nodeidx failed\n");
			goto cleanup__cti_be_pals_getNodeHostname;
		}
	}

	// Ensure information for current node is available
	if (_cti_pals_num_nodes <= _cti_node_idx) {
		fprintf(stderr, "libpals reported current node index %d, but only have %d entries\n",
			_cti_node_idx, _cti_pals_num_nodes);
		goto cleanup__cti_be_pals_getNodeHostname;
	}

	// Get hostname of node
	result = strdup(_cti_pals_nodes[_cti_node_idx].hostname);

	// Successfully obtained hostname
	failed = 0;

cleanup__cti_be_pals_getNodeHostname:
	if (failed) {
		if (result != NULL) {
			free(result);
			result = NULL;
		}
	}

	return result;
}

static int
_cti_get_pes_info()
{
	int rc = -1;

	// Check libpals functions
	if ((_cti_libpals_funcs == NULL) || (_cti_libpals_funcs->pals_get_pes == NULL)) {
		goto cleanup__cti_get_pes_info;
	}

	// Call libpals accessor
	if (_cti_libpals_funcs->pals_get_pes(_cti_pals_state, &_cti_pals_pes, &_cti_pals_num_pes) != PALS_OK) {
		fprintf(stderr, "pals_be libpals pals_get_pes failed: %s\n", _cti_libpals_funcs->pals_errmsg(_cti_pals_state));
		goto cleanup__cti_get_pes_info;
	}

	// Successfully retrieved PE information
	rc = 0;

cleanup__cti_get_pes_info:
	return rc;
}

static int
_cti_be_pals_getNodeFirstPE()
{
	int result = -1;

	// Ensure PEs array is filled in
	if (_cti_pals_pes == NULL) {
		if (_cti_get_pes_info() || (_cti_pals_pes == NULL)) {
			fprintf(stderr, "_cti_get_pes_info failed\n");
			goto cleanup__cti_be_pals_getNodeFirstPE;
		}
	}

	// Find first PE index that is running on this node
	for (int i = 0; i < _cti_pals_num_pes; i++) {
		if (_cti_pals_pes[i].nodeidx == _cti_node_idx) {
			result = i;
			break;
		}
	}

cleanup__cti_be_pals_getNodeFirstPE:
	return result;
}

static int
_cti_be_pals_getNodePEs()
{
	int failed = 1;
	int num_node_pes = 0;

	// Ensure PEs array is filled in
	if (_cti_pals_pes == NULL) {
		if (_cti_get_pes_info() || (_cti_pals_pes == NULL)) {
			fprintf(stderr, "_cti_get_pes_info failed\n");
			goto cleanup__cti_be_pals_getNodePEs;
		}
	}

	// Count all PEs running on this node
	for (int i = 0; i < _cti_pals_num_pes; i++) {
		if (_cti_pals_pes[i].nodeidx == _cti_node_idx) {
			num_node_pes++;
		}
	}

	// Successfully counted PEs
	failed = 0;

cleanup__cti_be_pals_getNodePEs:
	if (failed) {
		return -1;
	}

	return num_node_pes;
}


