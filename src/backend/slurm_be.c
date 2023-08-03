/*********************************************************************************\
 * slurm_be.c - SLURM specific backend library functions.
 *
 * Copyright 2014-2020 Hewlett Packard Enterprise Development LP.
 * SPDX-License-Identifier: Linux-OpenIB
 ******************************************************************************/

// This pulls in config.h
#define _GNU_SOURCE
#include "cti_defs.h"

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <wait.h>
#include <signal.h>
#include <ctype.h>

#include <sys/types.h>
#include <sys/stat.h>

#include "cti_be.h"
#include "pmi_attribs_parser.h"

// types used here
typedef struct
{
    int     PEsHere;    // Number of PEs placed on this node
    int     firstPE;    // first PE on this node
} slurmLayout_t;

/* static prototypes */
static int                  _cti_be_slurm_init(void);
static void                 _cti_be_slurm_fini(void);
static int                  _cti_be_slurm_getLayout(void);
static int                  _cti_be_slurm_getPids(void);
static cti_pidList_t *      _cti_be_slurm_findAppPids(void);
static char *               _cti_be_slurm_getNodeHostname(void);
static int                  _cti_be_slurm_getNodeFirstPE(void);
static int                  _cti_be_slurm_getNodePEs(void);

/* slurm wlm proto object */
cti_be_wlm_proto_t          _cti_be_slurm_wlmProto =
{
    CTI_WLM_SLURM,                  // wlm_type
    _cti_be_slurm_init,             // wlm_init
    _cti_be_slurm_fini,             // wlm_fini
    _cti_be_slurm_findAppPids,      // wlm_findAppPids
    _cti_be_slurm_getNodeHostname,  // wlm_getNodeHostname
    _cti_be_slurm_getNodeFirstPE,   // wlm_getNodeFirstPE
    _cti_be_slurm_getNodePEs        // wlm_getNodePEs
};

// Global vars
static pmi_attribs_t *      _cti_attrs          = NULL; // node pmi_attribs information
static slurmLayout_t *      _cti_layout         = NULL; // compute node layout for slurm app
static pid_t *              _cti_slurm_pids     = NULL; // array of pids here if pmi_attribs is not available
static uint32_t             _cti_jobid          = 0;    // global jobid obtained from environment variable
static uint32_t             _cti_stepid         = 0;    // global stepid obtained from environment variable
static bool                 _cti_slurm_isInit   = false;// Has init been called?

/* Constructor/Destructor functions */

static int
_cti_be_slurm_init(void)
{
    char *  apid_str;
    char *  ptr;

    // Have we already called init?
    if (_cti_slurm_isInit)
        return 0;

    // read information from the environment set by dlaunch
    if ((ptr = getenv(APID_ENV_VAR)) == NULL)
    {
        // Things were not setup properly, missing env vars!
        fprintf(stderr, "Env var %s not set!", APID_ENV_VAR);
        return 1;
    }

    // make a copy of the env var
    apid_str = strdup(ptr);

    // find the '.' that seperates jobid from stepid
    if ((ptr = strchr(apid_str, '.')) == NULL)
    {
        // Things were not setup properly!
        fprintf(stderr, "Env var %s has invalid value!", APID_ENV_VAR);
        free(apid_str);
        return 1;
    }

    // set the '.' to a null term
    *ptr++ = '\0';

    // get the jobid and stepid
    _cti_jobid = (uint32_t)strtoul(apid_str, NULL, 10);
    _cti_stepid = (uint32_t)strtoul(ptr, NULL, 10);

    _cti_slurm_isInit = true;

    // done
    return 0;
}

static void
_cti_be_slurm_fini(void)
{
    // cleanup
    if (_cti_attrs != NULL)
    {
        _cti_be_freePmiAttribs(_cti_attrs);
        _cti_attrs = NULL;
    }

    if (_cti_layout != NULL)
    {
        free(_cti_layout);
        _cti_layout = NULL;
    }

    if (_cti_slurm_pids != NULL)
    {
        free(_cti_slurm_pids);
        _cti_slurm_pids = NULL;
    }

    return;
}

/* Static functions */

static int
_cti_be_slurm_getLayout(void)
{
    slurmLayout_t *         my_layout;
    char *                  file_dir;
    char *                  layoutPath;
    FILE *                  my_file;
    slurmLayoutFileHeader_t layout_hdr;
    slurmLayoutFile_t *     layout;
    int                     i;

    // sanity
    if (_cti_layout != NULL)
        return 0;

    char* hostname = _cti_be_slurm_getNodeHostname();
    if (!hostname)
    {
        fprintf(stderr, "_cti_be_slurm_getNodeHostname failed.\n");
        return 1;
    }

    // allocate the slurmLayout_t object
    if ((my_layout = malloc(sizeof(slurmLayout_t))) == NULL)
    {
        fprintf(stderr, "malloc failed.\n");
        return 1;
    }

    // get the file directory were we can find the layout file
    if ((file_dir = cti_be_getFileDir()) == NULL)
    {
        fprintf(stderr, "cti_be_getFileDir failed.\n");
        free(my_layout);
        return 1;
    }

    // create the path to the layout file
    if (asprintf(&layoutPath, "%s/%s", file_dir, SLURM_LAYOUT_FILE) <= 0)
    {
        fprintf(stderr, "asprintf failed.\n");
        free(my_layout);
        free(file_dir);
        return 1;
    }
    // cleanup
    free(file_dir);

    // open the layout file for reading
    if ((my_file = fopen(layoutPath, "rb")) == NULL)
    {
        fprintf(stderr, "Could not open %s for reading\n", layoutPath);
        free(my_layout);
        free(layoutPath);
        return 1;
    }

    // read the header from the file
    if (fread(&layout_hdr, sizeof(slurmLayoutFileHeader_t), 1, my_file) != 1)
    {
        fprintf(stderr, "Could not read %s\n", layoutPath);
        free(my_layout);
        free(layoutPath);
        fclose(my_file);
        return 1;
    }

    // allocate the layout array based on the header
    if ((layout = calloc(layout_hdr.numNodes, sizeof(slurmLayoutFile_t))) == NULL)
    {
        fprintf(stderr, "calloc failed.\n");
        free(my_layout);
        free(layoutPath);
        fclose(my_file);
        return 1;
    }

    // read the layout info
    if (fread(layout, sizeof(slurmLayoutFile_t), layout_hdr.numNodes, my_file) != layout_hdr.numNodes)
    {
        fprintf(stderr, "Bad data in %s\n", layoutPath);
        free(my_layout);
        free(layoutPath);
        fclose(my_file);
        free(layout);
        return 1;
    }

    // done reading the file
    free(layoutPath);
    fclose(my_file);

    // find the entry for this nid
    for (i=0; i < layout_hdr.numNodes; ++i)
    {
        // check if this entry corresponds to our nid
        if (strncmp(layout[i].host, hostname, strlen(hostname)) == 0)
        {
            // found it
            my_layout->PEsHere = layout[i].PEsHere;
            my_layout->firstPE = layout[i].firstPE;

            // cleanup
            free(layout);

            // set global value
            _cti_layout = my_layout;

            // done
            return 0;
        }
    }

    // if we get here, we didn't find the host in the layout list!
    fprintf(stderr, "Could not find layout entry for hostname %s\n", hostname);

    for (i=0; i < layout_hdr.numNodes; ++i) {
        fprintf(stderr, "%2d: %s\n", i, layout[i].host);
    }

    free(my_layout);
    free(layout);
    return 1;
}

static int
_cti_be_slurm_getPids(void)
{
    pid_t *                 my_pids;
    char *                  file_dir;
    char *                  pidPath;
    FILE *                  my_file;
    slurmPidFileHeader_t    pid_hdr;
    slurmPidFile_t *        pids;
    int                     i;

    // sanity
    if (_cti_slurm_pids != NULL)
        return 0;

    // make sure we have the layout
    if (_cti_layout == NULL)
    {
        // get the layout
        if (_cti_be_slurm_getLayout())
        {
            return 1;
        }
    }

    // get the file directory were we can find the pid file
    if ((file_dir = cti_be_getFileDir()) == NULL)
    {
        fprintf(stderr, "_cti_be_slurm_getPids failed.\n");
        return 1;
    }

    // create the path to the pid file
    if (asprintf(&pidPath, "%s/%s", file_dir, SLURM_PID_FILE) <= 0)
    {
        fprintf(stderr, "asprintf failed.\n");
        free(file_dir);
        return 1;
    }
    // cleanup
    free(file_dir);

    // open the pid file for reading
    if ((my_file = fopen(pidPath, "rb")) == NULL)
    {
        fprintf(stderr, "Could not open %s for reading\n", pidPath);
        free(pidPath);
        return 1;
    }

    // read the header from the file
    if (fread(&pid_hdr, sizeof(slurmPidFileHeader_t), 1, my_file) != 1)
    {
        fprintf(stderr, "Could not read %s\n", pidPath);
        free(pidPath);
        fclose(my_file);
        return 1;
    }

    // ensure the file data is in bounds
    if ((_cti_layout->firstPE + _cti_layout->PEsHere) > pid_hdr.numPids)
    {
        // data out of bounds
        fprintf(stderr, "Data out of bounds in %s\n", pidPath);
        free(pidPath);
        fclose(my_file);
        return 1;
    }

    // allocate the pids array based on the number of PEsHere
    if ((pids = calloc(_cti_layout->PEsHere, sizeof(slurmPidFile_t))) == NULL)
    {
        fprintf(stderr, "calloc failed.\n");
        free(pidPath);
        fclose(my_file);
        return 1;
    }

    // fseek to the start of the pid info for this compute node
    if (fseek(my_file, _cti_layout->firstPE * sizeof(slurmPidFile_t), SEEK_CUR))
    {
        fprintf(stderr, "fseek failed.\n");
        free(pidPath);
        fclose(my_file);
        free(pids);
        return 1;
    }

    // read the pid info
    if (fread(pids, sizeof(slurmPidFile_t), _cti_layout->PEsHere, my_file) != _cti_layout->PEsHere)
    {
        fprintf(stderr, "Bad data in %s\n", pidPath);
        free(pidPath);
        fclose(my_file);
        free(pids);
        return 1;
    }

    // done reading the file
    free(pidPath);
    fclose(my_file);

    // allocate an array of pids
    if ((my_pids = calloc(_cti_layout->PEsHere, sizeof(pid_t))) == NULL)
    {
        fprintf(stderr, "calloc failed.\n");
        free(pids);
        return 1;
    }

    // set the pids
    for (i=0; i < _cti_layout->PEsHere; ++i)
    {
        my_pids[i] = pids[i].pid;
    }

    // set global value
    _cti_slurm_pids = my_pids;

    // cleanup
    free(pids);

    return 0;
}

/* API related calls start here */

static cti_pidList_t *
_cti_be_slurm_findAppPids(void)
{
    char *          tool_path;
    char *          file_path;
    struct stat     statbuf;
    cti_pidList_t * rtn;
    int             i;

    // First lets check to see if the pmi_attribs file exists
    if ((tool_path = _cti_be_getToolDir()) == NULL)
    {
        // Something messed up, so fail.
        fprintf(stderr, "_cti_be_getToolDir failed.\n");
        return NULL;
    }
    if (asprintf(&file_path, "%s/%s", tool_path, PMI_ATTRIBS_FILE_NAME) <= 0)
    {
        fprintf(stderr, "asprintf failed.\n");
        return NULL;
    }
    free(tool_path);
    if (stat(file_path, &statbuf) == -1)
    {
        // pmi_attribs file doesn't exist
        char *  file_dir;

        free(file_path);

        // Check if the SLURM_PID_FILE exists and use that if we don't see
        // the pmi_attribs file right away, otherwise we will fallback and use
        // the pmi_attribs method because we probably hit the race condition.

        // get the file directory were we can find the pid file
        if ((file_dir = cti_be_getFileDir()) == NULL)
        {
            fprintf(stderr, "_cti_be_slurm_findAppPids failed.\n");
            return NULL;
        }

        // create the path to the pid file
        if (asprintf(&file_path, "%s/%s", file_dir, SLURM_PID_FILE) <= 0)
        {
            fprintf(stderr, "asprintf failed.\n");
            free(file_dir);
            return NULL;
        }
        // cleanup
        free(file_dir);

        if (stat(file_path, &statbuf) == -1)
        {
            // use the pmi_attribs method
            free(file_path);
            goto use_pmi_attribs;
        }
        free(file_path);

        // the pid file exists, so lets use that for now

        if (_cti_slurm_pids == NULL)
        {
            // get the pids
            if (_cti_be_slurm_getPids())
            {
                return NULL;
            }
        }

        // allocate the return object
        if ((rtn = malloc(sizeof(cti_pidList_t))) == (void *)0)
        {
            fprintf(stderr, "malloc failed.\n");
            return NULL;
        }

        rtn->numPids = _cti_layout->PEsHere;

        // allocate the cti_rankPidPair_t array
        if ((rtn->pids = malloc(rtn->numPids * sizeof(cti_rankPidPair_t))) == (void *)0)
        {
            fprintf(stderr, "malloc failed.\n");
            free(rtn);
            return NULL;
        }

        // set the rtn rank/pid array
        for (i=0; i < rtn->numPids; ++i)
        {
            rtn->pids[i].pid = _cti_slurm_pids[i];
            rtn->pids[i].rank = i + _cti_layout->firstPE;
        }

    } else
    {

use_pmi_attribs:

        // use the pmi_attribs file

        // Call _cti_be_getPmiAttribsInfo - We require the pmi_attribs file to exist
        // in order to function properly.
        if (_cti_attrs == NULL)
        {
            if ((_cti_attrs = _cti_be_getPmiAttribsInfo()) == NULL)
            {
                // Something messed up, so fail.
                fprintf(stderr, "_cti_be_slurm_findAppPids failed.\n");
                return NULL;
            }
        }

        // ensure the _cti_attrs object has a app_rankPidPairs array
        if (_cti_attrs->app_rankPidPairs == NULL)
        {
            // Something messed up, so fail.
            fprintf(stderr, "_cti_be_slurm_findAppPids failed.\n");
            return NULL;
        }

        // allocate the return object
        if ((rtn = malloc(sizeof(cti_pidList_t))) == (void *)0)
        {
            fprintf(stderr, "malloc failed.\n");
            return NULL;
        }

        rtn->numPids = _cti_attrs->app_nodeNumRanks;

        // allocate the cti_rankPidPair_t array
        if ((rtn->pids = malloc(rtn->numPids * sizeof(cti_rankPidPair_t))) == (void *)0)
        {
            fprintf(stderr, "malloc failed.\n");
            free(rtn);
            return NULL;
        }

        // set the _cti_attrs rank/pid array to the rtn rank/pid array
        for (i=0; i < rtn->numPids; ++i)
        {
            rtn->pids[i].pid = _cti_attrs->app_rankPidPairs[i].pid;
            rtn->pids[i].rank = _cti_attrs->app_rankPidPairs[i].rank;
        }
    }

    return rtn;
}

// If lhs is NULL, allocate a new lhs, copy rhs to lhs, and update lhs_len accordingly
// Otherwise, allocate a new lhs, append rhs to lhs, and update lhs_len accordingly
// Note: lhs_len and rhs_len are the lengths of lhs and rhs excluding NULL-terminator
static int
realloc_append(char** lhs, size_t* lhs_len, char const* rhs, size_t rhs_len)
{
    int rc = -1;

    // Calculate new lhs length
    size_t new_lhs_len = *lhs_len + rhs_len + 1; // Include terminator

    // Allocate new lhs
    char* new_lhs = (char*)malloc(new_lhs_len);
    if (new_lhs == NULL) {
        perror("malloc");
        goto cleanup_realloc_append;
    }

    // If existing lhs was provided, store it in new_lhs
    if (*lhs != NULL) {
        memcpy(new_lhs, *lhs, *lhs_len);
        free(*lhs);
    }

    // Append rhs to new_lhs
    memcpy(new_lhs + *lhs_len, rhs, rhs_len);
    new_lhs[new_lhs_len - 1] = '\0';

    // Update lhs with the new_lhs
    *lhs = new_lhs;
    new_lhs = NULL;
    *lhs_len = new_lhs_len - 1; // Exclude terminator

    rc = 0;

cleanup_realloc_append:
    return rc;
}

// Read the next line from provided file pointer
static char*
file_read_line(FILE* file)
{
    char* result = NULL;
    size_t result_len = 0;

    while (1) {

        // Read next buf
        char buf[4096];
        errno = 0;
        if (fgets(buf, sizeof(buf), file) == NULL) { // fgets NULL-terminated

            // Retry if applicable
            if (errno == EAGAIN) {
                continue;

            // Return result if EOF
            } else if (errno == 0) {
                break;

            // Got some error from fgets
            } else {
                perror("fgets");
                goto cleanup_file_read_line;
            }
        }

        // Remove trailing newline from buffer
        size_t buf_len = strlen(buf);

        // Newline indicates completion
        int line_completed = 0;
        if (buf[buf_len - 1] == '\n') {
            buf[buf_len - 1] = '\0';
            buf_len--;
            line_completed = 1;
        }

        // Append the latest buffer to the result string
        realloc_append(&result, &result_len, buf, buf_len);

        // Newline indicated completion
        if (line_completed) {
            break;
        }
    }

cleanup_file_read_line:
    return result;
}

/* Try to find the current hostname as reported by the system
   on the list of Slurm nodes associated with the given job.
   This is necessary on HPCM Slurm systems where the node ID
   file is unavailable (present on Shasta / XC Slurm).
   If the Slurm node name is different than the hostname,
   the node name must be detected, so that the proper information
   in the Slurm-generated PMI attributes file can be found.
*/

static char*
_cti_be_slurm_getNodeName(char const* job_id, char const* hostname)
{
    if (job_id == NULL) {
        return NULL;
    }

    char* result = NULL;
    char* nodenames = NULL;

    int squeue_pipe[2] = {-1, -1};
    FILE* squeue_file = NULL;
    pid_t squeue_pid = -1;

    char* scontrol_line = NULL;
    char* current_node_name = NULL;
    char* current_host_name = NULL;
    int scontrol_pipe[2] = {-1, -1};
    FILE* scontrol_file = NULL;
    pid_t scontrol_pid = -1;

    // Set up squeue pipe
    if (pipe(squeue_pipe) < 0) {
        perror("pipe");
        goto cleanup__cti_be_slurm_getNodeName;
    }

    // Fork squeue
    squeue_pid = fork();
    if (squeue_pid < 0) {
        perror("fork");
        goto cleanup__cti_be_slurm_getNodeName;

    // Query node list for current job from squeue
    } else if (squeue_pid == 0) {
        char const* squeue_argv[] = {"squeue", "-h", "-o" "%N", "-j", job_id, NULL};

        // Set up squeue output
        close(squeue_pipe[0]);
        squeue_pipe[0] = -1;
        dup2(squeue_pipe[1], STDOUT_FILENO);

        // Exec squeue
        execvp("squeue", (char* const*)squeue_argv);
        perror("execvp");
        return NULL;
    }

    // Set up squeue input
    close(squeue_pipe[1]);
    squeue_pipe[1] = -1;
    squeue_file = fdopen(squeue_pipe[0], "r");

    // First line of squeue output is node names for job
    nodenames = file_read_line(squeue_file);
    if (nodenames == NULL) {
        fprintf(stderr, "squeue failed to read node list for job ID %s\n", job_id);
        goto cleanup__cti_be_slurm_getNodeName;
    }

    // Set up scontrol pipe
    if (pipe(scontrol_pipe) < 0) {
        perror("pipe");
        goto cleanup__cti_be_slurm_getNodeName;
    }

    // Fork scontrol
    scontrol_pid = fork();
    if (scontrol_pid < 0) {
        perror("fork");
        goto cleanup__cti_be_slurm_getNodeName;

    // Query node info for nodes associated with the current job
    } else if (scontrol_pid == 0) {
        char const* scontrol_argv[] = {"scontrol", "show", "node", nodenames, NULL};

        // Set up scontrol output
        close(scontrol_pipe[0]);
        scontrol_pipe[0] = -1;
        dup2(scontrol_pipe[1], STDOUT_FILENO);

        // Exec scontrol
        execvp("scontrol", (char* const*)scontrol_argv);
        perror("execvp");
        return NULL;
    }

    // Set up scontrol input
    close(scontrol_pipe[1]);
    scontrol_pipe[1] = -1;
    scontrol_file = fdopen(scontrol_pipe[0], "r");
    size_t hostname_len = strlen(hostname);

    // Read each line of scontrol output
    while (1) {
        scontrol_line = file_read_line(scontrol_file);
        if (scontrol_line == NULL) {
            break;
        }

        // Parse NodeHostName entry in line
        char const* node_host_name = strstr(scontrol_line, "NodeHostName");
        if ((node_host_name != NULL)
         && (sscanf(node_host_name, "NodeHostName=%m[^ ]%*c", &current_host_name) == 1)) {

            // NodeName always appears in a line before NodeHostName in output
            if (current_node_name == NULL) {
                fprintf(stderr, "scontrol found node host name before node name\n");
                goto cleanup__cti_be_slurm_getNodeName;
            }

            // Match if hostname is a prefix of Slurm node name, or vice versa
            // This supports FQDN hostnames / node names
            size_t current_host_name_len = strlen(current_host_name);
            int match = 0;
            if (hostname_len == current_host_name_len) {
                match = (strncmp(hostname, current_host_name, hostname_len) == 0);

            // Check case where hostnames are not zero-aligned. Accept non-numeric
            // suffixes (such as delimiters) that wouldn't be a node number
            } else if (hostname_len < current_host_name_len) {
                if (strncmp(current_host_name, hostname, hostname_len) == 0) {
                    match = !isdigit(current_host_name[hostname_len]);
                }

            // Check case where node names are not zero-aligned
            } else if (current_host_name_len < hostname_len) {
                if (strncmp(hostname, current_host_name, current_host_name_len) == 0) {
                    match = !isdigit(hostname[current_host_name_len]);
                }
            }

            if (match) {
                result = strdup(current_node_name);
                break;
            }

            // This hostname does not match, clear the current node name
            free(current_node_name);
            current_node_name = NULL;

        // Parse NodeName entry in line
        } else if (sscanf(scontrol_line, "NodeName=%m[^ ]%*c", &current_node_name) == 1) {

            // Clear the current host name, if set
            if (current_host_name != NULL) {
                free(current_host_name);
                current_host_name = NULL;
            }
        }

        // Clear the scontrol output
        free(scontrol_line);
        scontrol_line = NULL;
    }

    if (result == NULL) {
        fprintf(stderr, "Could not find the Slurm node name for hostname %s\n", hostname);
    }

cleanup__cti_be_slurm_getNodeName:

    // Clean up scontrol subprocess
    if (scontrol_pid > 0) {
        kill(scontrol_pid, SIGKILL);
        waitpid(scontrol_pid, NULL, 0);
        scontrol_pid = -1;
    }

    // Clean up scontrol pipe
    if (scontrol_file != NULL) {
        fclose(scontrol_file);
        scontrol_file = NULL;
    }
    if (scontrol_pipe[0] >= 0) {
        close(scontrol_pipe[0]);
        scontrol_pipe[0] = -1;
    }
    if (scontrol_pipe[1] >= 0) {
        close(scontrol_pipe[1]);
        scontrol_pipe[1] = -1;
    }

    // Clean up squeue subprocess
    if (squeue_pid > 0) {
        kill(squeue_pid, SIGKILL);
        waitpid(squeue_pid, NULL, 0);
        squeue_pid = -1;
    }

    if (scontrol_line != NULL) {
        free(scontrol_line);
        scontrol_line = NULL;
    }
    if (current_node_name != NULL) {
        free(current_node_name);
        current_node_name = NULL;
    }
    if (current_host_name != NULL) {
        free(current_host_name);
        current_host_name = NULL;
    }

    // Clean up squeue pipe
    if (squeue_file != NULL) {
        fclose(squeue_file);
        squeue_file = NULL;
    }
    if (squeue_pipe[0] >= 0) {
        close(squeue_pipe[0]);
        squeue_pipe[0] = -1;
    }
    if (squeue_pipe[1] >= 0) {
        close(squeue_pipe[1]);
        squeue_pipe[1] = -1;
    }

    if (nodenames != NULL) {
        free(nodenames);
        nodenames = NULL;
    }

    return result;
}

/*
   I return a pointer to the hostname of the node I am running
   on. On Cray nodes this can be done with very little overhead
   by reading the nid number out of /proc. If that is not
   available I fall back to just doing a libc gethostname call
   to get the name. If the fall back is used, the name will
   not necessarily be in the form of "nidxxxxx".

   The caller is responsible for freeing the returned
   string.

   As an opaque implementation detail, I cache the results
   for successive calls.
 */
static char *
_cti_be_slurm_getNodeHostname()
{
    static char *hostname = NULL; // Cache the result

    // Determined the answer previously?
    if (hostname) {
        return strdup(hostname);    // return cached value
    }

    // Allocate and get hostname
    if ((hostname = malloc(HOST_NAME_MAX)) == NULL) {
        fprintf(stderr, "_cti_be_slurm_getNodeHostname: malloc failed.\n");
        return NULL;
    }
    if (gethostname(hostname, HOST_NAME_MAX) < 0) {
        fprintf(stderr, "%s", "_cti_be_slurm_getNodeHostname: gethostname() failed!\n");
        free(hostname);
        hostname = NULL;
        return NULL;
    }

    // If job ID is available, query Slurm for current node
    char const *slurm_job_id = getenv("SLURM_JOB_ID");
    if (slurm_job_id != NULL) {

        // Try to determine the Slurm node name for the provided hostname and job ID
        char *job_id = strdup(slurm_job_id);
        char* nodename = _cti_be_slurm_getNodeName(job_id, hostname);
        free(job_id);
        job_id = NULL;

        // Store and return node name in cached hostname if successful
        if (nodename != NULL) {
            free(hostname);
            hostname = nodename;

            return strdup(hostname);
        }
    }

    // Fallback to standard hostname
    return strdup(hostname);
}


static int
_cti_be_slurm_getNodeFirstPE()
{
    // make sure we have the layout
    if (_cti_layout == NULL)
    {
        // get the layout
        if (_cti_be_slurm_getLayout())
        {
            return -1;
        }
    }

    return _cti_layout->firstPE;
}

static int
_cti_be_slurm_getNodePEs()
{
    // make sure we have the layout
    if (_cti_layout == NULL)
    {
        // get the layout
        if (_cti_be_slurm_getLayout())
        {
            return -1;
        }
    }

    return _cti_layout->PEsHere;
}

