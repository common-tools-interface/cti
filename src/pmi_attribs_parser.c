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
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#include "pmi_attribs_parser.h"

pmi_attribs_t *
getPmiAttribsInfo(uint64_t apid)
{
	int					i;
	FILE *				fp;
	char				fileName[PATH_MAX];
	int					int1;
	long int			longint1;
	pmi_attribs_t *		rtn;
	struct timespec		timer;
	int					tcount = 0;
	
	// init the timer to .25 seconds
	timer.tv_sec	= 0;
	timer.tv_nsec	= 250000000;

	// sanity check
	if (apid <= 0)
		return (pmi_attribs_t *)NULL;
		
	// TODO: There is a potential race condition here. For an attach scenario,
	// its possible to attach to the application before its at the startup
	// barrier. That means we could potentially read the pmi_attribs file before
	// its finished being written. This is only possible when there is no
	// startup barrier and an application is linked dynamically meaning it can
	// take a long time to startup at scale due to DVS issues.

	// create the path to the pmi_attribs file
	snprintf(fileName, PATH_MAX, PMI_ATTRIBS_FILE_PATH_FMT, (long long unsigned int)apid);
	
	// try to open the pmi_attribs file
	while ((fp = fopen(fileName, "r")) == 0)
	{
		// If we failed to open the file, sleep for timer nsecs. Keep track of
		// the count and make sure that this does not equal the timeout value
		// in seconds.
		// If you modify the timer, make sure you modify the multiple of the
		// timeout value. The timeout value is in seconds, we are sleeping in
		// fractions of seconds.
		if (tcount++ < (4 * PMI_ATTRIBS_FOPEN_TIMEOUT))
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
			return (pmi_attribs_t *)NULL;
		}
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

