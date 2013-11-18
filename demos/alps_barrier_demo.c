/******************************************************************************\
 * alps_barrier_demo.c - An example program which takes advantage of the Cray
 *			tools interface which will launch an aprun session from the given
 *			argv, display information about the job, and hold it at the 
 *			startup barrier.
 *
 * © 2011-2013 Cray Inc.	All Rights Reserved.
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
	fprintf(stdout, "USAGE: %s [APRUN STRING]\n", name);
	fprintf(stdout, "Launch an aprun session using the alps_transfer interface\n");
	fprintf(stdout, "and print out available information.\n");
	fprintf(stdout, "Written by andrewg@cray.com\n");
	return;
}

int
main(int argc, char **argv)
{
	// values returned by the tool_frontend library.
	cti_aprunProc_t *	myapp;
	char *				mycname;
	int					mynid;
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
	
	printf("Launching application...\n");
	
	/*
	* launchAprun_barrier - Start a new aprun session from the provided argv array
 	*						and have ALPS hold the application at its MPI startup
 	*						barrier.
	*/
	if ((myapp = cti_launchAprun_barrier(&argv[1],0,0,0,0,NULL,NULL,NULL)) == NULL)
	{
		fprintf(stderr, "Error: Could not launch aprun!\n");
		return 1;
	}
	
	printf("Application pid %d launched.\n", (int)myapp->aprunPid);
	
	/*
	* cti_getNodeCName - Returns the cabinet hostname of the active login node.
	*/
	if ((mycname = cti_getNodeCName()) == NULL)
	{
		fprintf(stderr, "Error: Could not query cname!\n");
		cti_killAprun(myapp->apid, 9);
		return 1;
	}
	
	/*
	* cti_getNid - Returns the node id of the active login node.
	*/
	if ((mynid = cti_getNodeNid()) < 0)
	{
		fprintf(stderr, "Error: Could not query Nid!\n");
		cti_killAprun(myapp->apid, 9);
		return 1;
	}
	
	/*
	* cti_getNumAppPEs -	Returns the number of processing elements in the application
	*						associated with the apid.
	*/
	if ((mynumpes = cti_getNumAppPEs(myapp->apid)) == 0)
	{
		fprintf(stderr, "Error: Could not query numAppPEs!\n");
		cti_killAprun(myapp->apid, 9);
		return 1;
	}
	
	/*
	* cti_getNumAppNodes -	Returns the number of compute nodes allocated for the
	*						application associated with the aprun pid.
	*/
	if ((mynumnodes = cti_getNumAppNodes(myapp->apid)) == 0)
	{
		fprintf(stderr, "Error: Could not query numAppNodes!\n");
		cti_killAprun(myapp->apid, 9);
		return 1;
	}
	
	/*
	* cti_getAppHostsList - Returns a null terminated array of strings containing
	*						the hostnames of the compute nodes allocated by ALPS
	*						for the application associated with the aprun pid.
	*/
	if ((myhostlist = cti_getAppHostsList(myapp->apid)) == NULL)
	{
		fprintf(stderr, "Error: Could not query appHostsList!\n");
		cti_killAprun(myapp->apid, 9);
		return 1;
	}
	
	/*
	* cti_getAppHostsPlacement -	Returns a cti_hostsList_t containing cti_host_t
	*								entries that contain the hostname of the compute
	*								nodes allocated by ALPS and the number of PEs
	*								assigned to that host for the application associated
	*								with the aprun pid.
 	*/
	if ((myhostplacement = cti_getAppHostsPlacement(myapp->apid)) == NULL)
	{
		fprintf(stderr, "Error: Could not query appHostsPlacement!\n");
		cti_killAprun(myapp->apid, 9);
		return 1;
	}
	
	// print the above output to the screen
	printf("\nThe following is alps information about your application that the tool interface gathered:\n\n");
	
	printf("apid of application: %llu\n", myapp->apid);
	printf("cname of login node where the apid resides: %s\n", mycname);
	printf("NID number of login node where the apid resides: %d\n", mynid);
	printf("Number of application PEs: %d\n", mynumpes);
	printf("Number of compute nodes used by application: %d\n", mynumnodes);
	printf("\n");
	
	printf("The following is a list of compute node hostnames returned by cti_getAppHostsList():\n\n");
	i = myhostlist;
	while (*i != NULL)
	{
		printf("%s\n", *i++);
	}
	
	printf("\nThe following information was returned by cti_getAppHostsPlacement():\n\n");
	printf("There are %d host(s) in the cti_hostsList_t struct.\n", myhostplacement->numHosts);
	for (j=0; j < myhostplacement->numHosts; ++j)
	{
		printf("On host %s there are %d PEs.\n", myhostplacement->hosts[j].hostname, myhostplacement->hosts[j].numPes);
	}
	
	// don't forget to cleanup memory that was malloc'ed
	free(mycname);
	free(myhostlist);
	cti_destroy_hostsList(myhostplacement);
	
	printf("\nHit return to release the application from the startup barrier...");
	
	// just read a single character from stdin then release the app/exit
	(void)getchar();
	
	if (cti_releaseAprun_barrier(myapp->apid))
	{
		fprintf(stderr, "Error: Failed to release app from barrier!\n");
		cti_killAprun(myapp->apid, 9);
		return 1;
	}
	
	return 0;
}
