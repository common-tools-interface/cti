/******************************************************************************\
 * cti_info_test.c - An example program which takes advantage of the Cray
 *			tools interface which will gather information from the WLM about a
 *          previously launched job.
 *
 * Copyright 2012-2014 Cray Inc.	All Rights Reserved.
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

#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>

#include "cray_tools_fe.h"
#include "cti_fe_common.h"

const struct option long_opts[] = {
			{"apid",		required_argument,	0, 'a'},
			{"jobid",		required_argument,	0, 'j'},
			{"stepid",		required_argument,	0, 's'},
			{"help",		no_argument,		0, 'h'},
			{0, 0, 0, 0}
			};

void
usage(char *name)
{
	fprintf(stdout, "USAGE: %s [OPTIONS]...\n", name);
	fprintf(stdout, "Gather information about a previously launched application\n");
	fprintf(stdout, "using the Cray tools interface.\n\n");
	
	fprintf(stdout, "\t-a, --apid      alps apid - ALPS WLM only\n");
	fprintf(stdout, "\t-j, --jobid     slurm job id - SLURM WLM only. Use with -s.\n");
	fprintf(stdout, "\t-s, --stepid    slurm step id - SLURM WLM only. Use with -j.\n");
	fprintf(stdout, "\t-h, --help      Display this text and exit\n\n");
	
	fprintf(stdout, "Written by andrewg@cray.com\n");
	return;
}

int
main(int argc, char **argv)
{
	int					opt_ind = 0;
	int					c;
	char *				eptr;
	int					a_arg = 0;
	int					j_arg = 0;
	int					s_arg = 0;
	uint64_t			apid = 0;
	uint32_t			job_id = 0;
	uint32_t			step_id = 0;
	// values returned by the tool_frontend library.
	cti_wlm_type		mywlm;
	cti_app_id_t		myapp;
	
	if (argc < 2)
	{
		usage(argv[0]);
		assert(argc > 2);
		return 1;
	}
	
	// process longopts
	while ((c = getopt_long(argc, argv, "a:j:s:h", long_opts, &opt_ind)) != -1)
	{
		switch (c)
		{
			case 0:
				// if this is a flag, do nothing
				break;
				
			case 'a':
				if (optarg == NULL)
				{
					usage(argv[0]);
					assert(0);
					return 1;
				}
				
				// This is the apid
				errno = 0;
				apid = (uint64_t)strtoll(optarg, &eptr, 10);
				
				// check for error
				if ((errno == ERANGE && apid == ULLONG_MAX)
						|| (errno != 0 && apid == 0))
				{
					perror("strtoll");
					assert(0);
					return 1;
				}
				
				// check for invalid input
				if (eptr == optarg || *eptr != '\0')
				{
					fprintf(stderr, "Invalid --apid argument.\n");
					assert(0);
					return 1;
				}
				
				a_arg = 1;
				
				break;
			
			case 'j':
				if (optarg == NULL)
				{
					usage(argv[0]);
					assert(0);
					return 1;
				}
				
				// This is the job id
				errno = 0;
				job_id = (uint32_t)strtol(optarg, &eptr, 10);
				
				// check for error
				if ((errno == ERANGE && job_id == ULONG_MAX)
						|| (errno != 0 && job_id == 0))
				{
					perror("strtol");
					assert(0);
					return 1;
				}
				
				// check for invalid input
				if (eptr == optarg || *eptr != '\0')
				{
					fprintf(stderr, "Invalid --jobid argument.\n");
					assert(0);
					return 1;
				}
				
				j_arg = 1;
				
				break;
				
			case 's':
				if (optarg == NULL)
				{
					usage(argv[0]);
					assert(0);
					return 1;
				}
				
				// This is the step id
				errno = 0;
				step_id = (uint32_t)strtol(optarg, &eptr, 10);
				
				// check for error
				if ((errno == ERANGE && step_id == ULONG_MAX)
						|| (errno != 0 && step_id == 0))
				{
					perror("strtol");
					assert(0);
					return 1;
				}
				
				// check for invalid input
				if (eptr == optarg || *eptr != '\0')
				{
					fprintf(stderr, "Invalid --stepid argument.\n");
					assert(0);
					return 1;
				}
				
				s_arg = 1;
				
				break;
				
			case 'h':
				usage(argv[0]);
				assert(0);
				return 0;
				
			default:
				usage(argv[0]);
				assert(0);
				return 1;
		}
	}
	
	/*
	 * cti_current_wlm - Obtain the current workload manager (WLM) in use on the 
	 *                   system.
	 */
	mywlm = cti_current_wlm();
	
	// Check the args to make sure they are valid given the wlm in use
	switch (mywlm)
	{
#if 0
		case CTI_WLM_ALPS:
			if (a_arg == 0)
			{
				fprintf(stderr, "Error: Missing --apid argument. This is required with the ALPS WLM.\n");
			}
			assert(a_arg != 0);
			myapp = cti_alps_registerApid(apid);
			if (myapp == 0)
			{
				fprintf(stderr, "Error: cti_alps_registerApid failed!\n");
				fprintf(stderr, "CTI error: %s\n", cti_error_str());
			}
			assert(myapp != 0);
			break;
#endif			
		case CTI_WLM_CRAY_SLURM:
		case CTI_WLM_SLURM:
			if (j_arg == 0 || s_arg == 0)
			{
				fprintf(stderr, "Error: Missing --jobid and --stepid argument. This is required for the SLURM WLM.\n");
			}
			assert(j_arg != 0 && s_arg != 0);
			myapp = cti_cray_slurm_registerJobStep(job_id, step_id);
			if (myapp == 0)
			{
				fprintf(stderr, "Error: cti_cray_slurm_registerJobStep failed!\n");
				fprintf(stderr, "CTI error: %s\n", cti_error_str());
			}
			assert(myapp != 0);
			break;
		
		case CTI_WLM_NONE:
			fprintf(stderr, "Error: Unsupported WLM in use!\n");
			assert(0);
			return 1;
	}
	
	// call the common FE tests
	cti_test_fe(myapp);
	
	// cleanup
	cti_deregisterApp(myapp);
	
	// ensure deregister worked.
	assert(cti_appIsValid(myapp) == 0);
	
	return 0;
}

