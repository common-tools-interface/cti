/******************************************************************************\
 * pmi_attribs_parser.c - A interface to parse the pmi_attribs file that exists
                          on the compute node.
 *
 * © 2011 Cray Inc.  All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
 *
 * $HeadURL$
 * $Date$
 * $Rev$
 * $Author$
 *
 ******************************************************************************/

#ifdef HAVE_CONFIG_H
#include        <config.h>
#endif /* HAVE_CONFIG_H */

#include <limits.h>
#include <stdlib.h>
#include <stdio.h>

#include "pmi_attribs_parser.h"

pmi_attribs_t *
getPmiAttribsInfo(uint64_t apid)
{
        int             i;
        FILE *          fp;
        char            fileName[PATH_MAX];
        pmi_attribs_t * rtn;
        int		int1;
        long int	longint1;

        // sanity check
        if (apid <= 0)
                return (pmi_attribs_t *)NULL;

        // create the path to the pmi_attribs file
        snprintf(fileName, PATH_MAX, PMI_ATTRIBS_FILE_PATH_FMT, (long long unsigned int)apid);
        
        // try to open the pmi_attribs file
        if ((fp = fopen(fileName, "r")) == 0)
        {
                // we couldn't open the pmi_attribs file, so return null
                return (pmi_attribs_t *)NULL;
        }
        
        // we opened the file, so lets allocate the return object
        if ((rtn = malloc(sizeof(pmi_attribs_t))) == (void *)0)
        {
                fprintf(stderr, "malloc failed.\n");
                fclose(fp);
                return (pmi_attribs_t *)NULL;
        }
        
        // set the apid
        rtn->apid = apid;
        
        // read in the pmi file version
        if (fscanf(fp, "%d\n", &rtn->pmi_file_ver) != 1)
        {
                fprintf(stderr, "Reading pmi_file_version failed.\n");
                free(rtn);
                fclose(fp);
                return (pmi_attribs_t *)NULL;
        }
        
        // read in the compute nodes nid number
        if (fscanf(fp, "%d\n", &rtn->cnode_nidNum) != 1)
        {
                fprintf(stderr, "Reading cnode_nidNum failed.\n");
                free(rtn);
                fclose(fp);
                return (pmi_attribs_t *)NULL;
        }
        
        // read in the MPMD command number this compute node cooresponds to in
        // the MPMD set
        if (fscanf(fp, "%d\n", &rtn->mpmd_cmdNum) != 1)
        {
                fprintf(stderr, "Reading mpmd_cmdNum failed.\n");
                free(rtn);
                fclose(fp);
                return (pmi_attribs_t *)NULL;
        }
        
        // read in the number of application ranks that exist on this node
        if (fscanf(fp, "%d\n", &rtn->app_nodeNumRanks) != 1)
        {
                fprintf(stderr, "Reading app_nodeNumRanks failed.\n");
                free(rtn);
                fclose(fp);
                return (pmi_attribs_t *)NULL;
        }
        
        // lets allocate the object to hold the rank/pid pairs we are about to
        // start reading in.
        if ((rtn->app_rankPidPairs = malloc(rtn->app_nodeNumRanks * sizeof(nodeRankPidPair_t))) == (void *)0)
        {
                fprintf(stderr, "malloc failed.\n");
                free(rtn);
                fclose(fp);
                return (pmi_attribs_t *)NULL;
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
                        return (pmi_attribs_t *)NULL;
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
freePmiAttribs(pmi_attribs_t *attr)
{
        // sanity check
        if (attr == (pmi_attribs_t *)NULL)
                return;
        
        if (attr->app_rankPidPairs != (nodeRankPidPair_t *)NULL)
                free(attr->app_rankPidPairs);
                
        free(attr);
}

