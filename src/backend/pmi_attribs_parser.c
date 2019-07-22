/******************************************************************************\
 * pmi_attribs_parser.c - A interface to parse the pmi_attribs file that exists
              on the compute node.
 *
 * Copyright 2011-2019 Cray Inc.  All Rights Reserved.
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

#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include "cti_be.h"
#include "pmi_attribs_parser.h"

pmi_attribs_t *
_cti_be_getPmiAttribsInfo(void)
{
    int                 i;
    FILE *              fp;
    char *              attribs_path;
    char                fileName[PATH_MAX];
    int                 int1;
    long int            longint1;
    pmi_attribs_t *     rtn;
    struct timespec     timer;
    int                 tcount = 0;
    unsigned long int   timeout = 0;
    unsigned long int   extra_timeout = 0;
    char *              env_var_str;

    // init the timer to .25 seconds
    timer.tv_sec    = 0;
    timer.tv_nsec   = 250000000;

    // TODO: There is a potential race condition here. For an attach scenario,
    // its possible to attach to the application before its at the startup
    // barrier. That means we could potentially read the pmi_attribs file before
    // its finished being written. This is only possible when there is no
    // startup barrier and an application is linked dynamically meaning it can
    // take a long time to startup at scale due to DVS issues.

    // get attribs path
    if ((attribs_path = _cti_be_getAttribsDir()) == NULL)
    {
        // failed to get the top level location of pmi_attribs file
        return NULL;
    }

    // create the path to the pmi_attribs file
    if (snprintf(fileName, PATH_MAX, "%s/%s", attribs_path, PMI_ATTRIBS_FILE_NAME) < 0)
    {
        // snprintf failed
        fprintf(stderr, "snprintf failed.\n");
        return NULL;
    }
    free(attribs_path);

    // try to open the pmi_attribs file
    while ((fp = fopen(fileName, "r")) == 0)
    {
        // If we failed to open the file, sleep for timer nsecs. Keep track of
        // the count and make sure that this does not equal the timeout value
        // in seconds.

        // Try to read the timeout value if this is the first time through
        if (timeout == 0)
        {
            // Query the environment variable setting in case the user set this.
            if ((env_var_str = getenv(PMI_ATTRIBS_TIMEOUT_ENV_VAR)) != NULL)
            {
                // var is set, so use this
                timeout = strtoul(env_var_str, NULL, 10);
                // if still 0, set this to default
                if (timeout == 0)
                {
                    timeout = PMI_ATTRIBS_DEFAULT_FOPEN_TIMEOUT;
                }
            } else
            {
                // Set the timeout to the default value
                timeout = PMI_ATTRIBS_DEFAULT_FOPEN_TIMEOUT;
            }
        }

        // If you modify the timer, make sure you modify the multiple of the
        // timeout value. The timeout value is in seconds, we are sleeping in
        // fractions of seconds.
        if (tcount++ < (4 * timeout))
        {
            if (nanosleep(&timer, NULL) < 0)
            {
                fprintf(stderr, "nanosleep failed.\n");
            }
            if (tcount%4 == 0)
            {
                fprintf(stderr, "Could not open pmi_attribs file after %d seconds.\n", tcount/4);
            }
        } else
        {
            // we couldn't open the pmi_attribs file, so return null
            return NULL;
        }
    }

    // if tcount is not zero, that means we failed to open the pmi_attribs file
    // lets sleep for a fraction of tcount to try and avoid a race condition here.
    // This can be user defined.
    // FIXME: This is nondeterministic and a bad idea...
    if (tcount != 0)
    {
        // Query the environment variable setting in case the user set this.
        if ((env_var_str = getenv(PMI_EXTRA_SLEEP_ENV_VAR)) != NULL)
        {
            // var is set, so use this
            extra_timeout = strtoul(env_var_str, NULL, 10);
            // if still 0, set this to default which is a fraction of tcount in
            // seconds
            if (extra_timeout == 0)
            {
                extra_timeout = (tcount/4)/10;
            }
        } else
        {
            // Set the extra timeout to the default value
            extra_timeout = (tcount/4)/10;
        }
        // sleep for the extra amount of time
        sleep(extra_timeout);
    }

    // we opened the file, so lets allocate the return object
    if ((rtn = malloc(sizeof(pmi_attribs_t))) == (void *)0)
    {
        fprintf(stderr, "malloc failed.\n");
        fclose(fp);
        return NULL;
    }

    // read in the pmi file version
    if (fscanf(fp, "%d\n", &rtn->pmi_file_ver) != 1)
    {
        fprintf(stderr, "Reading pmi_file_version failed.\n");
        free(rtn);
        fclose(fp);
        return NULL;
    }

    // read in the compute nodes nid number
    if (fscanf(fp, "%d\n", &rtn->cnode_nidNum) != 1)
    {
        fprintf(stderr, "Reading cnode_nidNum failed.\n");
        free(rtn);
        fclose(fp);
        return NULL;
    }

    // read in the MPMD command number this compute node cooresponds to in
    // the MPMD set
    if (fscanf(fp, "%d\n", &rtn->mpmd_cmdNum) != 1)
    {
        fprintf(stderr, "Reading mpmd_cmdNum failed.\n");
        free(rtn);
        fclose(fp);
        return NULL;
    }

    // read in the number of application ranks that exist on this node
    if (fscanf(fp, "%d\n", &rtn->app_nodeNumRanks) != 1)
    {
        fprintf(stderr, "Reading app_nodeNumRanks failed.\n");
        free(rtn);
        fclose(fp);
        return NULL;
    }

    // lets allocate the object to hold the rank/pid pairs we are about to
    // start reading in.
    if ((rtn->app_rankPidPairs = malloc(rtn->app_nodeNumRanks * sizeof(nodeRankPidPair_t))) == (void *)0)
    {
        fprintf(stderr, "malloc failed.\n");
        free(rtn);
        fclose(fp);
        return NULL;
    }

    for (i=0; i < rtn->app_nodeNumRanks; ++i)
    {
        // read in the rank and pid from the current line
        if (fscanf(fp, "%d %ld\n", &int1, &longint1) != 2)
        {
            fprintf(stderr, "Reading rank/pid pair %d failed.\n", i);
            free(rtn->app_rankPidPairs);
            free(rtn);
            fclose(fp);
            return NULL;
        }
        // note that there was previously a bug here since long int * is
        // not the size of pid_t. I was getting lucky for most sizes, but
        // I believe padding screwed this up and caused a segfault.
        // The new way of reading into a temp int and then writting fixed
        // the issue.
        rtn->app_rankPidPairs[i].rank = int1;
        rtn->app_rankPidPairs[i].pid  = (pid_t)longint1;
    }

    // close the fp
    fclose(fp);

    return rtn;
}

void
_cti_be_freePmiAttribs(pmi_attribs_t *attr)
{
    // sanity check
    if (attr == NULL)
        return;

    if (attr->app_rankPidPairs != NULL)
        free(attr->app_rankPidPairs);

    free(attr);
}

