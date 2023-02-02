/*********************************************************************************\
 * localhost_be.c - SSH based workload manager specific backend library
 *                    functions.
*
 * Copyright 2016-2023 Hewlett Packard Enterprise Development LP.
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

/* static prototypes */
static int                  _cti_be_localhost_init(void);
static void                 _cti_be_localhost_fini(void);
static cti_pidList_t *      _cti_be_localhost_findAppPids(void);
static char *               _cti_be_localhost_getNodeHostname(void);
static int                  _cti_be_localhost_getNodeFirstPE(void);
static int                  _cti_be_localhost_getNodePEs(void);

static int*                 _cti_be_localhost_pids = NULL;
static int                  _cti_be_localhost_numPids = 0;

static int                  _cti_be_localhost_initPids(void);

/* wlm ssh proto object */
cti_be_wlm_proto_t          _cti_be_localhost_wlmProto =
{
    CTI_WLM_SSH,                            // wlm_type
    _cti_be_localhost_init,               // wlm_init
    _cti_be_localhost_fini,               // wlm_fini
    _cti_be_localhost_findAppPids,        // wlm_findAppPids
    _cti_be_localhost_getNodeHostname,    // wlm_getNodeHostname
    _cti_be_localhost_getNodeFirstPE,     // wlm_getNodeFirstPE
    _cti_be_localhost_getNodePEs          // wlm_getNodePEs
};

/* Constructor/Destructor functions */

static int
_cti_be_localhost_init(void)
{
    return 0;
}

static void
_cti_be_localhost_fini(void)
{
    free(_cti_be_localhost_pids);
    _cti_be_localhost_pids = NULL;
}

/* Static functions */

static int _cti_be_localhost_initPids(void)
{
    char *                  file_dir;
    char *                  pidPath;
    FILE *                  my_file;
    int                     i, pid, numPids;
    int *                   pids;

    if (_cti_be_localhost_pids != NULL)
    {
        return 0;
    }

    // get the file directory were we can find the pid file
    if ((file_dir = cti_be_getFileDir()) == NULL)
    {
        fprintf(stderr, "_cti_be_localhost_initPids failed.\n");
        return 1;
    }

    // create the path to the pid file
    if (asprintf(&pidPath, "%s/%s", file_dir, LOCALHOST_PID_FILE) <= 0)
    {
        fprintf(stderr, "asprintf failed.\n");
        free(file_dir);
        return 1;
    }
    // cleanup
    free(file_dir);

    // open the pid file for reading
    if ((my_file = fopen(pidPath, "r")) == NULL)
    {
        fprintf(stderr, "Could not open %s for reading\n", pidPath);
        free(pidPath);
        return 1;
    }

    if (fscanf(my_file, "%d", &numPids) != 1)
    {
        fprintf(stderr, "Could not read %s\n", pidPath);
        free(pidPath);
        fclose(my_file);
        return 1;
        
    }

    if ((pids = malloc(numPids * sizeof(int))) == NULL)
    {
        fprintf(stderr, "Could not allocate pid list of length  %d\n", numPids);
        free(pidPath);
        fclose(my_file);
        return 1;
    }

    for (i=0; i<numPids; ++i) {
        if (fscanf(my_file, "%d", &pid) != 1) {
            fprintf(stderr, "Could not read pid\n");
            free(pidPath);
            free(pids);
            fclose(my_file);
            return 1;
        }
        pids[i] = pid;
    }

    free(pidPath);
    fclose(my_file);
    _cti_be_localhost_pids = pids;
    _cti_be_localhost_numPids = numPids;
    
    return 0;
}

/* API related calls start here */

static cti_pidList_t *
_cti_be_localhost_findAppPids(void)
{
    cti_pidList_t * rtn;
    int             i;

    if (_cti_be_localhost_initPids())
    {
        return NULL;
    }
    
    // allocate the return object
    if ((rtn = malloc(sizeof(cti_pidList_t))) == (void *)0)
    {
        fprintf(stderr, "malloc failed.\n");
        return NULL;
    }

    rtn->numPids = _cti_be_localhost_numPids;

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
        rtn->pids[i].pid = _cti_be_localhost_pids[i];
        rtn->pids[i].rank = i;
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
_cti_be_localhost_getNodeHostname()
{
    static char *hostname = NULL; // Cache the result

    // Determined the answer previously?
    if (hostname) {
        return strdup(hostname);    // return cached value
    }

    // allocate memory for the hostname
    if ((hostname = malloc(HOST_NAME_MAX)) == NULL)
    {
        fprintf(stderr, "_cti_be_localhost_getNodeHostname: malloc failed.\n");
        return NULL;
    }

    if (gethostname(hostname, HOST_NAME_MAX) < 0)
    {
        fprintf(stderr, "%s", "_cti_be_localhost_getNodeHostname: gethostname() failed!\n");
        hostname = NULL;
        return NULL;
    }

    return strdup(hostname); // One way or the other
}

static int
_cti_be_localhost_getNodeFirstPE()
{
    return 0;
}

static int
_cti_be_localhost_getNodePEs()
{
    // make sure we have the layout
    if (_cti_be_localhost_initPids() == 0)
    {
        return _cti_be_localhost_numPids;
    } else {
        return -1;
    }
}

