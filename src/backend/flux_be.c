/*********************************************************************************\
 * flux_be.c - flux specific backend library functions.
 *
 * Copyright 2021 Hewlett Packard Enterprise Development LP.
 * SPDX-License-Identifier: Linux-OpenIB
 *********************************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <dlfcn.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <ctype.h>

#include "cti_defs.h"
#include "cti_be.h"

typedef struct
{
    int     PEsHere;    // Number of PEs placed on this node
    int     firstPE;    // first PE on this node
} slurmLayout_t;

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
static int g_initialized = 0;
static slurmLayout_t *g_layout = NULL;

static void
_cti_cleanup_be_globals(void)
{
    if (g_layout != NULL) {
        free(g_layout);
        g_layout = NULL;
    }
}

/* Layout helper functions */

static slurmLayout_t*
_cti_be_pals_getLayoutFromFile(void)
{
    slurmLayout_t *result = NULL;

    slurmLayout_t *layout = NULL;
    char *hostname = NULL;
    char *file_dir = NULL;
    char *layout_path = NULL;
    FILE *layout_file = NULL;
    slurmLayoutFile_t *layout_contents = NULL;
    slurmLayoutFileHeader_t layout_header;
    int i;
    size_t hostname_len;

    // Allocate layout storage
    if ((layout = malloc(sizeof(slurmLayout_t))) == NULL) {
        fprintf(stderr, "malloc failed.\n");
        goto cleanup_getLayoutFromFile;
    }

    // Get hostname to look up
    if ((hostname = _cti_be_flux_getNodeHostname()) == NULL) {
        fprintf(stderr, "_cti_be_slurm_getNodeHostname failed.\n");
        goto cleanup_getLayoutFromFile;
    }
    hostname_len = strlen(hostname);

    // Get the file directory were we can find the layout file
    if ((file_dir = cti_be_getFileDir()) == NULL) {
        fprintf(stderr, "cti_be_getFileDir failed.\n");
        goto cleanup_getLayoutFromFile;
    }

    // Create the path to the layout file
    if (asprintf(&layout_path, "%s/%s", file_dir, SLURM_LAYOUT_FILE) <= 0) {
        fprintf(stderr, "asprintf failed.\n");
        goto cleanup_getLayoutFromFile;
    }

    // Open the layout file for reading
    if ((layout_file = fopen(layout_path, "rb")) == NULL) {
        fprintf(stderr, "Could not open %s for reading\n", layout_path);
        goto cleanup_getLayoutFromFile;
    }

    // Read the header from the file
    if (fread(&layout_header, sizeof(slurmLayoutFileHeader_t), 1, layout_file) != 1) {
        fprintf(stderr, "Could not read header from %s\n", layout_path);
        goto cleanup_getLayoutFromFile;
    }

    // allocate the layout contents based on the header
    if ((layout_contents = calloc(layout_header.numNodes, sizeof(slurmLayoutFile_t))) == NULL) {
        fprintf(stderr, "calloc failed.\n");
        goto cleanup_getLayoutFromFile;
    }

    // Read the layout file into contents
    if (fread(layout_contents, sizeof(slurmLayoutFile_t), layout_header.numNodes, layout_file)
        != layout_header.numNodes) {
        fprintf(stderr, "Couldn't read entire layout file at %s\n", layout_path);
        goto cleanup_getLayoutFromFile;
    }

    // Find the entry for this nid
    for (i = 0; i < layout_header.numNodes; ++i) {

        // Check if this entry corresponds to our nid
        if (strncmp(layout_contents[i].host, hostname, hostname_len) == 0) {

            // Hostname is a prefix of the entry, allow if entry is exactly equal,
            // or if entry is a full domain name (next character is not alphanumeric)
            if (isalnum(layout_contents[i].host[hostname_len])) {
                continue;
            }

            // Found it
            layout->PEsHere = layout_contents[i].PEsHere;
            layout->firstPE = layout_contents[i].firstPE;
            fprintf(stderr, "Found layout for %s: %d PEs, start at %d\n",
                hostname, layout->PEsHere, layout->firstPE);

            result = layout;

            goto cleanup_getLayoutFromFile;
        }
    }

    // Didn't find the host in the layout list!
    fprintf(stderr, "Could not find layout entry for hostname %s\n", hostname);
    for (i = 0; i < layout_header.numNodes; ++i) {
        fprintf(stderr, "%2d: %s\n", i, layout_contents[i].host);
    }

cleanup_getLayoutFromFile:
    if (layout_contents != NULL) {
        free(layout_contents);
        layout_contents = NULL;
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

    if ((result == NULL) && (layout != NULL)) {
        free(layout);
        layout = NULL;
    }

    return result;
}

static pid_t*
_cti_be_pals_getPidsFromFile(slurmLayout_t const* layout)
{
    pid_t *result = NULL;

    pid_t *pids = NULL;
    char *file_dir = NULL;
    char *pid_path = NULL;
    FILE *pid_file = NULL;
    slurmPidFile_t *pid_contents = NULL;
    slurmPidFileHeader_t pid_header;

    int i;

    // Get the file directory were we can find the PID file
    if ((file_dir = cti_be_getFileDir()) == NULL) {
        fprintf(stderr, "cti_be_getFileDir failed.\n");
        goto cleanup_getPidsFromFile;
    }

    // Create the path to the PID file
    if (asprintf(&pid_path, "%s/%s", file_dir, SLURM_PID_FILE) <= 0) {
        fprintf(stderr, "asprintf failed.\n");
        goto cleanup_getPidsFromFile;
    }

    // Open the PID file for reading
    if ((pid_file = fopen(pid_path, "rb")) == NULL) {
        fprintf(stderr, "Could not open %s for reading\n", pid_path);
    }

    // read the header from the file
    if (fread(&pid_header, sizeof(slurmPidFileHeader_t), 1, pid_file) != 1) {
        fprintf(stderr, "Could not read header from %s\n", pid_path);
        goto cleanup_getPidsFromFile;
    }

    // Ensure the file data is completely written
    if ((layout->firstPE + layout->PEsHere) > pid_header.numPids) {
        fprintf(stderr, "PID file %s is short\n", pid_path);
        goto cleanup_getPidsFromFile;
    }

    // Allocate the PID contents array
    if ((pid_contents = calloc(layout->PEsHere, sizeof(slurmPidFile_t))) == NULL) {
        fprintf(stderr, "calloc failed.\n");
        goto cleanup_getPidsFromFile;
    }

    // fseek to the start of the PID info for this compute node
    if (fseek(pid_file, layout->firstPE * sizeof(slurmPidFile_t), SEEK_CUR)) {
        fprintf(stderr, "fseek failed.\n");
        goto cleanup_getPidsFromFile;
    }

    // Read the pid info
    if (fread(pid_contents, sizeof(slurmPidFile_t), layout->PEsHere, pid_file) != layout->PEsHere) {
        fprintf(stderr, "Failed to read all PIDs from %s\n", pid_path);
        goto cleanup_getPidsFromFile;
    }

    // Allocate PID array
    if ((pids = calloc(layout->PEsHere, sizeof(pid_t))) == NULL) {
        fprintf(stderr, "calloc failed.\n");
        goto cleanup_getPidsFromFile;
    }

    // Set the pids
    for (i = 0; i < layout->PEsHere; ++i) {
        pids[i] = pid_contents[i].pid;
    }

    result = pids;

cleanup_getPidsFromFile:
    if (pid_contents != NULL) {
        free(pid_contents);
        pid_contents = NULL;
    }
    if (pid_file != NULL) {
        fclose(pid_file);
        pid_file = NULL;
    }
    if (pid_path != NULL) {
        free(pid_path);
        pid_path = NULL;
    }
    if (file_dir != NULL) {
        free(file_dir);
        file_dir = NULL;
    }

    if ((result == NULL) && (pids != NULL)) {
        free(pids);
        pids = NULL;
    }

    return result;
}

/* Constructor/Destructor functions */

static int
_cti_be_flux_init(void)
{
    int rc = 0;

    if (g_initialized) {
        goto cleanup__cti_be_flux_init;
    }

    g_layout = _cti_be_pals_getLayoutFromFile();
    if (g_layout == NULL) {
        rc = 1;
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
    cti_pidList_t *result = NULL;

    pid_t *pids = NULL;
    int i;

    // Get the global layout information
    if (g_layout == NULL) {
        goto cleanup__cti_be_flux_findAppPids;
    }

    // Get the PIDs on this node
    if ((pids = _cti_be_pals_getPidsFromFile(g_layout)) == NULL) {
        goto cleanup__cti_be_flux_findAppPids;
    }

    // Fill in the result structure
    result = (cti_pidList_t*)malloc(sizeof(cti_pidList_t));
    result->numPids = g_layout->PEsHere;
    result->pids = (cti_rankPidPair_t*)malloc(result->numPids * sizeof(cti_rankPidPair_t));

    for (i = 0; i < result->numPids; i++) {
        result->pids[i].pid = pids[i];
        result->pids[i].rank = g_layout->firstPE + i;
    }

cleanup__cti_be_flux_findAppPids:
    if (pids) {
        free(pids);
        pids = NULL;
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

    if (g_layout) {
        result = g_layout->firstPE;
    }

cleanup__cti_be_flux_getNodeFirstPE:
    return result;
}

static int
_cti_be_flux_getNodePEs()
{
    int result = -1;

    if (g_layout) {
        result = g_layout->PEsHere;
    }

cleanup__cti_be_flux_getNodePEs:
    return result;
}


