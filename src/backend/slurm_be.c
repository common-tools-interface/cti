/*********************************************************************************\
 * slurm_be.c - SLURM specific backend library functions.
 *
 * Copyright 2014-2019 Cray Inc. All Rights Reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
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

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

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
    int                     i, offset;

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

    // find the entry for this nid, we need to offset into the host name based on
    // this nid
    offset = strlen(layout[0].host) - strlen(hostname);

    for (i=0; i < layout_hdr.numNodes; ++i)
    {
        // check if this entry corresponds to our nid
        if (strncmp(layout[i].host + offset, hostname, strlen(hostname)) == 0)
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
    if (hostname)
        return strdup(hostname);    // return cached value

    // Try the Cray /proc extension short cut
    FILE *nid_fp;             // NID file stream
        if (((nid_fp = fopen(CRAY_XT_NID_FILE, "r"))     != NULL) ||
            ((nid_fp = fopen(CRAY_SHASTA_NID_FILE, "r")) != NULL))
        {
            // Set hostname format based on XC or Shasta
            char const* hostname_fmt = (access(CRAY_XT_NID_FILE, F_OK) != -1)
                ? CRAY_XT_HOSTNAME_FMT
                : CRAY_SHASTA_HOSTNAME_FMT;

        // we expect this file to have a numeric value giving our current nid
        char file_buf[BUFSIZ];   // file read buffer
        if (fgets(file_buf, BUFSIZ, nid_fp) == NULL)
        {
            fprintf(stderr, "_cti_be_slurm_getNodeHostname fgets failed.\n");
            fclose(nid_fp);
            return NULL;
        }

        // close the file stream
        fclose(nid_fp);

        // convert this to an integer value
        errno = 0;
        char *  eptr;
        int nid = (int)strtol(file_buf, &eptr, 10);

        // check for error
        if ((errno == ERANGE && nid == INT_MAX)
                || (errno != 0 && nid == 0))
        {
            fprintf(stderr, "_cti_be_slurm_getNodeHostname: strtol failed.\n");
            return NULL;
        }

        // check for invalid input
        if (eptr == file_buf)
        {
            fprintf(stderr, "_cti_be_slurm_getNodeHostname: Bad data in %s\n", CRAY_XT_NID_FILE);
            return NULL;
        }

        // create the nid hostname string
        if (asprintf(&hostname, hostname_fmt, nid) <= 0)
        {
            fprintf(stderr, "_cti_be_slurm_getNodeHostname asprintf failed.\n");
            free(hostname);
            hostname = NULL;
            return NULL;
        }
    }

    else // Fallback to standard hostname
    {
        // allocate memory for the hostname
        if ((hostname = malloc(HOST_NAME_MAX)) == NULL)
        {
            fprintf(stderr, "_cti_be_slurm_getNodeHostname: malloc failed.\n");
            return NULL;
        }

        if (gethostname(hostname, HOST_NAME_MAX) < 0)
        {
            fprintf(stderr, "%s", "_cti_be_slurm_getNodeHostname: gethostname() failed!\n");
            hostname = NULL;
            return NULL;
        }
    }

    return strdup(hostname); // One way or the other
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

