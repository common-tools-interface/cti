/*********************************************************************************\
 * generic_ssh_be.c - SSH based workload manager specific backend library
 *                    functions.
*
 * Copyright 2016-2020 Hewlett Packard Enterprise Development LP.
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
#define _GNU_SOURCE
#include "cti_defs.h"

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>

#include <limits.h>
#include <errno.h>

#include "cti_be.h"
#include "pmi_attribs_parser.h"

// types used here

typedef struct
{
    int     PEsHere;    // Number of PEs placed on this node
    int     firstPE;    // first PE on this node
} cti_layout_t;

/* static prototypes */
static int                  _cti_be_generic_ssh_init(void);
static void                 _cti_be_generic_ssh_fini(void);
static int                  _cti_be_generic_ssh_getLayout(void);
static int                  _cti_be_generic_ssh_getPids(void);
static cti_pidList_t *      _cti_be_generic_ssh_findAppPids(void);
static char *               _cti_be_generic_ssh_getNodeHostname(void);
static int                  _cti_be_generic_ssh_getNodeFirstPE(void);
static int                  _cti_be_generic_ssh_getNodePEs(void);

/* wlm ssh proto object */
cti_be_wlm_proto_t          _cti_be_generic_ssh_wlmProto =
{
    CTI_WLM_SSH,                            // wlm_type
    _cti_be_generic_ssh_init,               // wlm_init
    _cti_be_generic_ssh_fini,               // wlm_fini
    _cti_be_generic_ssh_findAppPids,        // wlm_findAppPids
    _cti_be_generic_ssh_getNodeHostname,    // wlm_getNodeHostname
    _cti_be_generic_ssh_getNodeFirstPE,     // wlm_getNodeFirstPE
    _cti_be_generic_ssh_getNodePEs          // wlm_getNodePEs
};

// Global vars
static pmi_attribs_t *  _cti_attrs          = NULL; // node pmi_attribs information
static cti_layout_t *   _cti_layout         = NULL; // compute node layout for generic app
static pid_t *          _cti_generic_pids   = NULL; // array of pids here if pmi_attribs is not available

/* Constructor/Destructor functions */

static int
_cti_be_generic_ssh_init(void)
{
    return 0;
}

static void
_cti_be_generic_ssh_fini(void)
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

    if (_cti_generic_pids != NULL)
    {
        free(_cti_generic_pids);
        _cti_generic_pids = NULL;
    }

    return;
}

/* Static functions */

static int
_cti_be_generic_ssh_getLayout(void)
{
    cti_layout_t *          my_layout;
    char *                  file_dir;
    char *                  layoutPath;
    FILE *                  my_file;
    cti_layoutFileHeader_t  layout_hdr;
    cti_layoutFile_t *      layout;
    int                     i;

    // sanity
    if (_cti_layout != NULL)
        return 0;

    char* hostname = _cti_be_generic_ssh_getNodeHostname();

    // allocate the cti_layout_t object
    if ((my_layout = malloc(sizeof(cti_layout_t))) == NULL)
    {
        fprintf(stderr, "malloc failed.\n");
        return 1;
    }

    // get the file directory were we can find the layout file
    if ((file_dir = cti_be_getFileDir()) == NULL)
    {
        fprintf(stderr, "_cti_be_generic_ssh_getLayout failed.\n");
        free(my_layout);
        return 1;
    }

    // create the path to the layout file
    if (asprintf(&layoutPath, "%s/%s", file_dir, SSH_LAYOUT_FILE) <= 0)
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
    if (fread(&layout_hdr, sizeof(cti_layoutFileHeader_t), 1, my_file) != 1)
    {
        fprintf(stderr, "Could not read %s\n", layoutPath);
        free(my_layout);
        free(layoutPath);
        fclose(my_file);
        return 1;
    }

    // allocate the layout array based on the header
    if ((layout = calloc(layout_hdr.numNodes, sizeof(cti_layoutFile_t))) == NULL)
    {
        fprintf(stderr, "calloc failed.\n");
        free(my_layout);
        free(layoutPath);
        fclose(my_file);
        return 1;
    }

    // read the layout info
    if (fread(layout, sizeof(cti_layoutFile_t), layout_hdr.numNodes, my_file) != layout_hdr.numNodes)
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
_cti_be_generic_ssh_getPids(void)
{
    pid_t *                 my_pids;
    char *                  file_dir;
    char *                  pidPath;
    FILE *                  my_file;
    cti_pidFileheader_t     pid_hdr;
    cti_pidFile_t *         pids;
    int                     i;

    // sanity
    if (_cti_generic_pids != NULL)
        return 0;

    // make sure we have the layout
    if (_cti_layout == NULL)
    {
        // get the layout
        if (_cti_be_generic_ssh_getLayout())
        {
            return 1;
        }
    }

    // get the file directory were we can find the pid file
    if ((file_dir = cti_be_getFileDir()) == NULL)
    {
        fprintf(stderr, "_cti_be_generic_ssh_getPids failed.\n");
        return 1;
    }

    // create the path to the pid file
    if (asprintf(&pidPath, "%s/%s", file_dir, SSH_PID_FILE) <= 0)
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
    if (fread(&pid_hdr, sizeof(cti_pidFileheader_t), 1, my_file) != 1)
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
    if ((pids = calloc(_cti_layout->PEsHere, sizeof(cti_pidFile_t))) == NULL)
    {
        fprintf(stderr, "calloc failed.\n");
        free(pidPath);
        fclose(my_file);
        return 1;
    }

    // fseek to the start of the pid info for this compute node
    if (fseek(my_file, _cti_layout->firstPE * sizeof(cti_pidFile_t), SEEK_CUR))
    {
        fprintf(stderr, "fseek failed.\n");
        free(pidPath);
        fclose(my_file);
        free(pids);
        return 1;
    }

    // read the pid info
    if (fread(pids, sizeof(cti_pidFile_t), _cti_layout->PEsHere, my_file) != _cti_layout->PEsHere)
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
    _cti_generic_pids = my_pids;

    // cleanup
    free(pids);

    return 0;
}

/* API related calls start here */

static cti_pidList_t *
_cti_be_generic_ssh_findAppPids(void)
{
    cti_pidList_t * rtn;
    int             i;

    if (_cti_be_generic_ssh_getPids() == 0)
    {
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
            rtn->pids[i].pid = _cti_generic_pids[i];
            rtn->pids[i].rank = i + _cti_layout->firstPE;
        }

    } else
    {
        // generic_pid not found, so use the pmi_attribs file

        // Call _cti_be_getPmiAttribsInfo - We require the pmi_attribs file to exist
        // in order to function properly.
        if (_cti_attrs == NULL)
        {
            if ((_cti_attrs = _cti_be_getPmiAttribsInfo()) == NULL)
            {
                // Something messed up, so fail.
                fprintf(stderr, "_cti_be_generic_ssh_findAppPids failed (_cti_be_getPmiAttribsInfo NULL).\n");
                return NULL;
            }
        }

        // ensure the _cti_attrs object has a app_rankPidPairs array
        if (_cti_attrs->app_rankPidPairs == NULL)
        {
            // Something messed up, so fail.
            fprintf(stderr, "_cti_be_generic_ssh_findAppPids failed (app_rankPidPairs NULL).\n");
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
_cti_be_generic_ssh_getNodeHostname()
{
    static char *hostname = NULL; // Cache the result

    // Determined the answer previously?
    if (hostname) {
        return strdup(hostname);    // return cached value
    }

    // allocate memory for the hostname
    if ((hostname = malloc(HOST_NAME_MAX)) == NULL)
    {
        fprintf(stderr, "_cti_be_generic_ssh_getNodeHostname: malloc failed.\n");
        return NULL;
    }

    if (gethostname(hostname, HOST_NAME_MAX) < 0)
    {
        fprintf(stderr, "%s", "_cti_be_generic_ssh_getNodeHostname: gethostname() failed!\n");
        hostname = NULL;
        return NULL;
    }

    return strdup(hostname); // One way or the other
}

static int
_cti_be_generic_ssh_getNodeFirstPE()
{
    // make sure we have the layout
    if (_cti_layout == NULL)
    {
        // get the layout
        if (_cti_be_generic_ssh_getLayout())
        {
            return -1;
        }
    }

    return _cti_layout->firstPE;
}

static int
_cti_be_generic_ssh_getNodePEs()
{
    // make sure we have the layout
    if (_cti_layout == NULL)
    {
        // get the layout
        if (_cti_be_generic_ssh_getLayout())
        {
            return -1;
        }
    }

    return _cti_layout->PEsHere;
}

