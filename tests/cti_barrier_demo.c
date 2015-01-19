/******************************************************************************\
 * cti_barrier_demo.c - An example program which takes advantage of the Cray
 *			tools interface which will launch an application from the given
 *			argv, display information about the job, and hold it at the 
 *			startup barrier.
 *
 * © 2011-2014 Cray Inc.	All Rights Reserved.
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "cray_tools_fe.h"

void
usage(char *name)
{
	fprintf(stdout, "USAGE: %s [LAUNCHER STRING]\n", name);
	fprintf(stdout, "Launch an application using the cti library\n");
	fprintf(stdout, "and print out information.\n");
	fprintf(stdout, "Written by andrewg@cray.com\n");
	return;
}

int
main(int argc, char **argv)
{
	int rtn = 0;
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
	
	printf("\nThe following is alps information about your application that the tool interface gathered:\n\n");
	
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
	
	/*
	 * cti_launchAppBarrier - Start an application using the application launcher
	 *                        with the provided argv array and have the launcher
	 *                        hold the application at its startup barrier for 
	 *                        MPI/SHMEM/UPC/CAF applications.
	 */
	if ((myapp = cti_launchAppBarrier((const char * const *)&argv[1],0,0,0,0,NULL,NULL,NULL)) == 0)
	{
		fprintf(stderr, "Error: cti_launchAppBarrier failed!\n");
		fprintf(stderr, "CTI error: %s\n", cti_error_str());
		return 1;
	}
	
	// Conduct WLM specific calls
	switch (mywlm)
	{
		case CTI_WLM_ALPS:
		{
			cti_aprunProc_t *	myapruninfo;
			int					ordinal;
		
			/*
			 * cti_getAprunInfo - Obtain information about the aprun process
			 */
			if ((myapruninfo = cti_alps_getAprunInfo(myapp)) == NULL)
			{
				fprintf(stderr, "Error: cti_getAprunInfo failed!\n");
				fprintf(stderr, "CTI error: %s\n", cti_error_str());
				rtn = 1;
			} else
			{
				printf("apid of application: %llu\n", (long long unsigned int)myapruninfo->apid);
				printf("pid_t of aprun: %d\n", myapruninfo->aprunPid);
				free(myapruninfo);
			}
			/*
			*  cti_alps_getAlpsOverlapOrdinal - Obtain overlap ordinal. This can fail on
			*                                   some systems.
			*/
			ordinal = cti_alps_getAlpsOverlapOrdinal(myapp);
			printf("alps overlap ordinal: %d\n", ordinal);
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
	
	printf("\nHit return to release the application from the startup barrier...");
	
	// just read a single character from stdin then release the app/exit
	(void)getchar();
	
	if (cti_releaseAppBarrier(myapp))
	{
		fprintf(stderr, "Error: cti_releaseAppBarrier failed!\n");
		fprintf(stderr, "CTI error: %s\n", cti_error_str());
		cti_killApp(myapp, 9);
		return 1;
	}
	
	cti_deregisterApp(myapp);
	
	return rtn;
}

