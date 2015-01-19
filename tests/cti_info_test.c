/******************************************************************************\
 * cti_info_test.c - An example program which takes advantage of the Cray
 *			tools interface which will gather information from the WLM about a
 *          previously launched job.
 *
 * © 2012-2014 Cray Inc.	All Rights Reserved.
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

#include "cray_tools_fe.h"

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
	int					rtn = 0;
	char *				eptr;
	int					a_arg = 0;
	int					j_arg = 0;
	int					s_arg = 0;
	uint64_t			apid = 0;
	uint32_t			job_id = 0;
	uint32_t			step_id = 0;
	// values returned by the tool_frontend library.
	cti_wlm_type		mywlm;
	char *				myhostname;
	cti_app_id_t		myapp;
	char *				mylauncherhostname;
	int					mynumpes;
	int					mynumnodes;
	char **				myhostlist;
	cti_hostsList_t *	myhostplacement;
	// internal variables
	char **	i;
	int		j;
	
	if (argc < 2)
	{
		usage(argv[0]);
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
					return 1;
				}
				
				// check for invalid input
				if (eptr == optarg || *eptr != '\0')
				{
					fprintf(stderr, "Invalid --apid argument.\n");
					return 1;
				}
				
				a_arg = 1;
				
				break;
			
			case 'j':
				if (optarg == NULL)
				{
					usage(argv[0]);
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
					return 1;
				}
				
				// check for invalid input
				if (eptr == optarg || *eptr != '\0')
				{
					fprintf(stderr, "Invalid --jobid argument.\n");
					return 1;
				}
				
				j_arg = 1;
				
				break;
				
			case 's':
				if (optarg == NULL)
				{
					usage(argv[0]);
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
					return 1;
				}
				
				// check for invalid input
				if (eptr == optarg || *eptr != '\0')
				{
					fprintf(stderr, "Invalid --stepid argument.\n");
					return 1;
				}
				
				s_arg = 1;
				
				break;
				
			case 'h':
				usage(argv[0]);
				return 0;
				
			default:
				usage(argv[0]);
				return 1;
		}
	}
	
	/*
	 * cti_current_wlm - Obtain the current workload manager (WLM) in use on the 
	 *                   system.
	 */
	mywlm = cti_current_wlm();
	
	printf("Current workload manager: %s\n", cti_wlm_type_toString(mywlm));
	
	/*
	 * cti_getHostname - Returns the hostname of the current login node.
	 */
	if ((myhostname = cti_getHostname()) == NULL)
	{
		fprintf(stderr, "Error: cti_getHostname failed!\n");
		fprintf(stderr, "CTI error: %s\n", cti_error_str());
		rtn = 1;
	} else
	{
		printf("Current hostname: %s\n", myhostname);
		free(myhostname);
	}
	
	// Check the args to make sure they are valid given the wlm in use
	switch (mywlm)
	{
		case CTI_WLM_ALPS:
			if (!a_arg)
			{
				fprintf(stderr, "Error: Missing --apid argument. This is required with the ALPS WLM.\n");
				return 1;
			}
			if ((myapp = cti_alps_registerApid(apid)) == 0)
			{
				fprintf(stderr, "Error: cti_alps_registerApid failed!\n");
				fprintf(stderr, "CTI error: %s\n", cti_error_str());
				return 1;
			}
			break;
			
		case CTI_WLM_CRAY_SLURM:
		case CTI_WLM_SLURM:
			if (!(j_arg && s_arg))
			{
				fprintf(stderr, "Error: Missing --jobid and --stepid argument. This is required for the SLURM WLM.\n");
				return 1;
			}
			if ((myapp = cti_cray_slurm_registerJobStep(job_id, step_id)) == 0)
			{
				fprintf(stderr, "Error: cti_cray_slurm_registerJobStep failed!\n");
				fprintf(stderr, "CTI error: %s\n", cti_error_str());
				return 1;
			}
			break;
		
		case CTI_WLM_NONE:
			fprintf(stderr, "Error: Unsupported WLM in use!\n");
			return 1;
	}
	
	printf("\nThe following is information about your application that the tool interface gathered:\n\n");
	
	// Conduct WLM specific calls
	switch (mywlm)
	{
		case CTI_WLM_ALPS:
		{
			cti_aprunProc_t *	myapruninfo;
			
			/*
			 * cti_alps_getAprunInfo - Obtain information about the aprun process
			 */
			if ((myapruninfo = cti_alps_getAprunInfo(myapp)) == NULL)
			{
				fprintf(stderr, "Error: cti_alps_getAprunInfo failed!\n");
				fprintf(stderr, "CTI error: %s\n", cti_error_str());
				rtn = 1;
			} else
			{
				printf("apid of application: %llu\n", (long long unsigned int)myapruninfo->apid);
				printf("pid_t of aprun: %d\n", myapruninfo->aprunPid);
				free(myapruninfo);
			}
		}
			break;
			
		case CTI_WLM_CRAY_SLURM:
		{
			cti_srunProc_t *	mysruninfo;
			
			/*
			 * cti_cray_slurm_getSrunInfo - Obtain information about the srun process
			 */
			 if ((mysruninfo = cti_cray_slurm_getSrunInfo(myapp)) == NULL)
			 {
			 	fprintf(stderr, "Error: cti_cray_slurm_getSrunInfo failed!\n");
				fprintf(stderr, "CTI error: %s\n", cti_error_str());
				rtn = 1;
			 } else
			 {
			 	printf("jobid of application:  %lu\n", (long unsigned int)mysruninfo->jobid);
			 	printf("stepid of application: %lu\n", (long unsigned int)mysruninfo->stepid);
				free(mysruninfo);
			 }
		}
			break;
			
		default:
			// do nothing
			break;
	}
	
	/*
	 * cti_getLauncherHostName - Returns the hostname of the login node where the
	 *                           application launcher process resides.
	 */
	if ((mylauncherhostname = cti_getLauncherHostName(myapp)) == NULL)
	{
		fprintf(stderr, "Error: cti_getLauncherHostName failed!\n");
		fprintf(stderr, "CTI error: %s\n", cti_error_str());
		rtn = 1;
	} else
	{
		printf("hostname where aprun resides: %s\n", mylauncherhostname);
		free(mylauncherhostname);
	}
	
	/*
	 * cti_getNumAppPEs -	Returns the number of processing elements in the application
	 *						associated with the apid.
	 */
	if ((mynumpes = cti_getNumAppPEs(myapp)) == 0)
	{
		fprintf(stderr, "Error: cti_getNumAppPEs failed!\n");
		fprintf(stderr, "CTI error: %s\n", cti_error_str());
		rtn = 1;
	} else
	{
		printf("Number of application PEs: %d\n", mynumpes);
	}
	
	/*
	 * cti_getNumAppNodes -	Returns the number of compute nodes allocated for the
	 *						application associated with the aprun pid.
	 */
	if ((mynumnodes = cti_getNumAppNodes(myapp)) == 0)
	{
		fprintf(stderr, "Error: cti_getNumAppNodes failed!\n");
		fprintf(stderr, "CTI error: %s\n", cti_error_str());
		rtn = 1;
	} else
	{
		printf("Number of compute nodes used by application: %d\n", mynumnodes);
	}
	
	/*
	 * cti_getAppHostsList - Returns a null terminated array of strings containing
	 *						the hostnames of the compute nodes allocated by ALPS
	 *						for the application associated with the aprun pid.
	 */
	if ((myhostlist = cti_getAppHostsList(myapp)) == NULL)
	{
		fprintf(stderr, "Error: cti_getAppHostsList failed!\n");
		fprintf(stderr, "CTI error: %s\n", cti_error_str());
		rtn = 1;
	} else
	{
		printf("\nThe following is a list of compute node hostnames returned by cti_getAppHostsList():\n\n");
		i = myhostlist;
		while (*i != NULL)
		{
			printf("%s\n", *i);
			free(*i++);
		}
		free(myhostlist);
	}
	
	/*
	 * cti_getAppHostsPlacement -	Returns a cti_hostsList_t containing cti_host_t
	 *								entries that contain the hostname of the compute
	 *								nodes allocated by ALPS and the number of PEs
	 *								assigned to that host for the application associated
	 *								with the aprun pid.
 	 */
	if ((myhostplacement = cti_getAppHostsPlacement(myapp)) == NULL)
	{
		fprintf(stderr, "Error: cti_getAppHostsPlacement failed!\n");
		fprintf(stderr, "CTI error: %s\n", cti_error_str());
		rtn = 1;
	} else
	{
		printf("\nThe following information was returned by cti_getAppHostsPlacement():\n\n");
		printf("There are %d host(s) in the cti_hostsList_t struct.\n", myhostplacement->numHosts);
		for (j=0; j < myhostplacement->numHosts; ++j)
		{
			printf("On host %s there are %d PEs.\n", myhostplacement->hosts[j].hostname, myhostplacement->hosts[j].numPes);
		}
		cti_destroyHostsList(myhostplacement);
	}
	
	cti_deregisterApp(myapp);
	
	return rtn;
}

