/*********************************************************************************\
 * pals_be.c - pals specific backend library functions.
 *
 * Copyright 2020 Hewlett Packard Enterprise Development LP.
 * SPDX-License-Identifier: Linux-OpenIB
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
#include <ctype.h>

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

// Initialized in _cti_be_pals_init
static cti_libpals_funcs_t* _cti_libpals_funcs = NULL; // libpals wrappers
static pals_state_t *_cti_pals_state = NULL; // libpals state

// node pmi_attribs information
static pmi_attribs_t *_cti_pmi_attrs = NULL;
static pmi_attribs_t* _cti_get_pmi_attrs_no_retry()
{
	// If _cti_be_getPmiAttribsInfo fails once due to missing
	// pmi_attribs, don't repeatedly try it on subsequent calls
	static int initialized = 0;

	if (!initialized) {
		initialized = 1;

		_cti_pmi_attrs = _cti_be_getPmiAttribsInfoNoRetry();
		if (_cti_pmi_attrs == NULL) {
			fprintf(stderr, "_cti_be_getPmiAttribsInfo failed\n");
			goto cleanup__cti_get_pmi_attrs;
		}

		// Ensure the _cti_attrs object has a app_rankPidPairs array
		if (_cti_pmi_attrs->app_rankPidPairs == NULL) {
			fprintf(stderr, "_cti_be_getPmiAttribsInfo failed: no rank information returned\n");
			free(_cti_pmi_attrs);
			_cti_pmi_attrs = NULL;
			goto cleanup__cti_get_pmi_attrs;
		}
	}

cleanup__cti_get_pmi_attrs:
	return _cti_pmi_attrs;
}

// node index for PALS accessors
static int _cti_node_idx = -1;
static int _cti_get_node_idx()
{
	static int initialized = 0;

	if (!initialized) {
		initialized = 1;

		// Check libpals functions
		if ((_cti_libpals_funcs == NULL) || (_cti_libpals_funcs->pals_get_nodeidx == NULL)) {
			goto cleanup__cti_get_node_idx;
		}

		// Call libpals accessor
		if (_cti_libpals_funcs->pals_get_nodeidx(_cti_pals_state, &_cti_node_idx) != PALS_OK) {
			fprintf(stderr, "pals_be libpals pals_get_nodeidx failed: %s\n", _cti_libpals_funcs->pals_errmsg(_cti_pals_state));
			goto cleanup__cti_get_node_idx;
		}
	}

cleanup__cti_get_node_idx:
	return _cti_node_idx;
}

// number / list of job nodes
static pals_node_t *_cti_pals_nodes = NULL;
static int _cti_pals_num_nodes = -1;
static int _cti_get_pals_nodes(pals_node_t** pals_nodes, int* num_nodes)
{
	static int initialized = 0;

	int rc = -1;

	if (!initialized) {
		initialized = 1;

		// Check libpals functions
		if ((_cti_libpals_funcs == NULL) || (_cti_libpals_funcs->pals_get_nodes == NULL)) {
			goto cleanup__cti_get_pals_nodes;
		}

		// Call libpals accessor
		if (_cti_libpals_funcs->pals_get_nodes(_cti_pals_state, &_cti_pals_nodes, &_cti_pals_num_nodes) != PALS_OK) {
			fprintf(stderr, "pals_be libpals pals_get_nodes failed: %s\n", _cti_libpals_funcs->pals_errmsg(_cti_pals_state));
			goto cleanup__cti_get_pals_nodes;
		}
	}

	// Successfully retrieved node information
	*pals_nodes = _cti_pals_nodes;
	*num_nodes = _cti_pals_num_nodes;
	rc = 0;

cleanup__cti_get_pals_nodes:
	return rc;
}

// list of PEs
static pals_pe_t *_cti_pals_pes = NULL;
static int _cti_pals_num_pes = -1;
static int _cti_get_pals_pes(pals_pe_t** pals_pes, int* num_pes)
{
	static int initialized = 0;

	int rc = -1;

	if (!initialized) {
		initialized = 1;

		// Check libpals functions
		if ((_cti_libpals_funcs == NULL) || (_cti_libpals_funcs->pals_get_pes == NULL)) {
			goto cleanup__cti_get_pals_pes;
		}

		// Call libpals accessor
		if (_cti_libpals_funcs->pals_get_pes(_cti_pals_state, &_cti_pals_pes, &_cti_pals_num_pes) != PALS_OK) {
			fprintf(stderr, "pals_be libpals pals_get_pes failed: %s\n", _cti_libpals_funcs->pals_errmsg(_cti_pals_state));
			goto cleanup__cti_get_pals_pes;
		}
	}

	// Successfully retrieved node information
	*pals_pes = _cti_pals_pes;
	*num_pes = _cti_pals_num_pes;
	rc = 0;

cleanup__cti_get_pals_pes:
	return rc;
}

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

	// Format detected path with libpals library and check for existence
	if (detected_path != NULL) {

		// Format path
		int snprintf_rc = snprintf(path, path_cap,
			"%s/%s", detected_path, PALS_BE_LIB_NAME);
		if ((snprintf_rc < 0) || (snprintf_rc >= path_cap)) {
			perror("snprintf");
			return 1;
		}

		// Check for libpals library at path
		fprintf(stderr, "Checking at %s... ", path);
		if (access(path, R_OK) == 0) {
			fprintf(stderr, "succeeded\n");
			return 0;
		}
		fprintf(stderr, "failed\n");
	}

	// Detection failed or libpals was not in detected directory
	// Check in default paths
	char const* pals_default_paths[] = {
		"/opt/cray/pe/pals/default/lib",
		"/opt/cray/pals/default/lib",
		"/usr/lib64",
		NULL
	};
	for (char const** default_path = pals_default_paths; *default_path != NULL; default_path++) {

		// Format path
		int snprintf_rc = snprintf(path, path_cap,
			"%s/%s", *default_path, PALS_BE_LIB_NAME);
		if ((snprintf_rc < 0) || (snprintf_rc >= path_cap)) {
			perror("snprintf");
			return 1;
		}

		// Check for libpals library at path
		fprintf(stderr, "Checking at %s... ", path);
		if (access(path, R_OK) == 0) {
			fprintf(stderr, "succeeded\n");
			return 0;
		}
		fprintf(stderr, "failed\n");
	}

	// Failed to automatically detect
	return 1;
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

	// Try to dlopen default libpals
	_cti_libpals_funcs->handle = dlopen(PALS_BE_LIB_NAME, RTLD_LAZY);
	dl_err = dlerror();
	if (_cti_libpals_funcs == NULL) {

		// Detect location of libpals
		char libpals_path[PATH_MAX];
		libpals_path[0] = '\0';
		if (_cti_be_pals_detect_libpals(libpals_path, sizeof(libpals_path))) {

			// Failed to automatically detect using pkg-config and in default paths
			fprintf(stderr, "pals_be " PALS_BE_LIB_NAME " failed to detect libpals path\n");
			goto cleanup__cti_be_pals_init;

		// Successfully detected path
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


static cti_pidList_t*
_cti_be_pals_getRanksFromFile(void)
{
    cti_pidList_t *result = NULL;

    char *hostname = NULL;
    char *file_dir = NULL;
    char *layout_path = NULL;
    FILE *layout_file = NULL;
    palsLayoutEntry_t layout_entry;
    palsLayoutFileHeader_t layout_header;
    int i;
    size_t hostname_len;

    // Get hostname to look up
    if ((hostname = _cti_be_pals_getNodeHostname()) == NULL) {
        fprintf(stderr, "_cti_be_slurm_getNodeHostname failed.\n");
        goto cleanup_getRanksFromFile;
    }
    hostname_len = strlen(hostname);

    // Get the file directory were we can find the layout file
    if ((file_dir = cti_be_getFileDir()) == NULL) {
        fprintf(stderr, "cti_be_getFileDir failed.\n");
        goto cleanup_getRanksFromFile;
    }

    // Create the path to the layout file
    if (asprintf(&layout_path, "%s/%s", file_dir, SLURM_LAYOUT_FILE) <= 0) {
        fprintf(stderr, "asprintf failed.\n");
        goto cleanup_getRanksFromFile;
    }

    // Open the layout file for reading
    if ((layout_file = fopen(layout_path, "rb")) == NULL) {
        fprintf(stderr, "Could not open %s for reading\n", layout_path);
        goto cleanup_getRanksFromFile;
    }

    // Read the header from the file
    if (fread(&layout_header, sizeof(palsLayoutFileHeader_t), 1, layout_file) != 1) {
        fprintf(stderr, "Could not read header from %s\n", layout_path);
        goto cleanup_getRanksFromFile;
    }

    // Find the entry for this nid
    for (i = 0; i < layout_header.numNodes; ++i) {

        // Read the layout entry
        if (fread(&layout_entry, sizeof(palsLayoutEntry_t), 1, layout_file) != 1) {
            fprintf(stderr, "Could not read layout entry %d from %s\n", i, layout_path);
            goto cleanup_getRanksFromFile;
        }

        // Get size of rank / pid array
        size_t rankPidSize = sizeof(cti_rankPidPair_t) * layout_entry.numRanks;

        // Check if this entry corresponds to our nid
        if (strncmp(layout_entry.host, hostname, hostname_len) == 0) {

            // Hostname is a prefix of the entry, allow if entry is exactly equal,
            // or if entry is a full domain name (next character is not alphanumeric)
            if (isalnum(layout_entry.host[hostname_len])) {

                // Advance to next entry
                fseek(layout_file, rankPidSize, SEEK_CUR);

                continue;
            }

            // Found it

            // Allocate rank list
           if (((result = malloc(sizeof(cti_pidList_t))) == NULL)
            || ((result->pids = malloc(rankPidSize)) == NULL)) {

               fprintf(stderr, "malloc failed.\n");
               goto cleanup_getRanksFromFile;
            }

            // Read rank list
            result->numPids = layout_entry.numRanks;
            if (fread(result->pids, sizeof(cti_rankPidPair_t), layout_entry.numRanks, layout_file) != layout_entry.numRanks) {
                fprintf(stderr, "Could not read PID list %d of size %d from %s\n", i, layout_entry.numRanks, layout_path);
                goto cleanup_getRanksFromFile;
            }

            goto cleanup_getRanksFromFile;
        }

        // Advance to next entry
        fseek(layout_file, rankPidSize, SEEK_CUR);
    }

    // Didn't find the host in the layout list!
    fprintf(stderr, "Could not find layout entry for hostname %s\n", hostname);

cleanup_getRanksFromFile:
    if (result && (result->pids == NULL)) {
        free(result);
        result = NULL;
    }
    if (layout_file != NULL) {
        fclose(layout_file);
        layout_file = NULL;
    }
    if (layout_path != NULL) {
        free(layout_path);
        layout_path = NULL;
    }
    if (file_dir != NULL) {
        free(file_dir);
        file_dir = NULL;
    }
    if (hostname != NULL) {
        free(hostname);
        hostname = NULL;
    }

    return result;
}

/* API related calls start here */

static cti_pidList_t*
_cti_be_pals_findAppPids()
{
	cti_pidList_t *result = NULL;

	pmi_attribs_t *cti_pmi_attrs = NULL; // Copy of global pointer, don't free
	int i;

	// Try to get PMI attribs from system file
	if ((cti_pmi_attrs = _cti_get_pmi_attrs_no_retry())) {

                fprintf(stderr, "Found PMI attribs file\n");

		// Allocate result struct
		result = (cti_pidList_t*)malloc(sizeof(cti_pidList_t));
		if (result == NULL) {
			fprintf(stderr, "malloc failed\n");
			goto cleanup__cti_be_pals_findAppPids;
		}

		// Allocate the PID / rank pair array
		result->numPids = cti_pmi_attrs->app_nodeNumRanks;
		result->pids = (cti_rankPidPair_t*)malloc(result->numPids * sizeof(cti_rankPidPair_t));
		if (result->pids == NULL) {
			fprintf(stderr, "malloc failed\n");
			goto cleanup__cti_be_pals_findAppPids;
		}

		// Copy all PID / rank pairs to result array
		for (i = 0; i < result->numPids; i++) {
			result->pids[i].pid  = cti_pmi_attrs->app_rankPidPairs[i].pid;
			result->pids[i].rank = cti_pmi_attrs->app_rankPidPairs[i].rank;
		}

	// No PMI attributes, use the shipped layout files
	} else {

                fprintf(stderr, "Didn't find PMI attribs file, falling back to shipped layout\n");

		// Get the global layout information
                result = _cti_be_pals_getRanksFromFile();
	}

cleanup__cti_be_pals_findAppPids:
	if (result && (result->pids == NULL)) {
		free(result);
		result = NULL;
	}

	return result;
}

static char*
_cti_be_pals_getNodeHostname()
{
	int failed = 1;
	char *result = NULL;

	// Get and check nodes information
	pals_node_t *pals_nodes = NULL;
	int num_nodes = -1;
	if ((_cti_get_pals_nodes(&pals_nodes, &num_nodes) < 0)
	 || (pals_nodes == NULL) || (num_nodes < 0)) {
		goto cleanup__cti_be_pals_getNodeHostname;
	}

	// Get and check node index
	int cti_node_idx = _cti_get_node_idx();
	if (cti_node_idx < 0) {
		goto cleanup__cti_be_pals_getNodeHostname;
	}

	// Ensure information for current node is available
	if (num_nodes <= cti_node_idx) {
		fprintf(stderr, "libpals reported current node index %d, but only have %d entries\n",
			cti_node_idx, num_nodes);
		goto cleanup__cti_be_pals_getNodeHostname;
	}

	// Get hostname of node
	result = strdup(pals_nodes[cti_node_idx].hostname);

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
_cti_be_pals_getNodeFirstPE()
{
	int result = -1;

	// Get and check PEs information
	pals_pe_t *pals_pes = NULL;
	int num_pes = -1;
	if ((_cti_get_pals_pes(&pals_pes, &num_pes) < 0)
	 || (pals_pes == NULL) || (num_pes < 0)) {
		goto cleanup__cti_be_pals_getNodeFirstPE;
	}

	// Get and check node index
	int cti_node_idx = _cti_get_node_idx();
	if (cti_node_idx < 0) {
		goto cleanup__cti_be_pals_getNodeFirstPE;
	}

	// Find first PE index that is running on this node
	for (int i = 0; i < num_pes; i++) {
		if (pals_pes[i].nodeidx == cti_node_idx) {
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

	// Get and check PEs information
	pals_pe_t *pals_pes = NULL;
	int num_pes = -1;
	if ((_cti_get_pals_pes(&pals_pes, &num_pes) < 0)
	 || (pals_pes == NULL) || (num_pes < 0)) {
		goto cleanup__cti_be_pals_getNodePEs;
	}

	// Get and check node index
	int cti_node_idx = _cti_get_node_idx();
	if (cti_node_idx < 0) {
		goto cleanup__cti_be_pals_getNodePEs;
	}

	// Count all PEs running on this node
	for (int i = 0; i < num_pes; i++) {
		if (pals_pes[i].nodeidx == cti_node_idx) {
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


