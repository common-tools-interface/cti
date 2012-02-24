/*********************************************************************************\
 * alps_barrier_demo.c - An example program which takes advantage of the CrayTool
 *			Interface which will launch an aprun session from the given
 *			argv, display information about the job, and hold it at the 
 *			startup barrier.
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
 *********************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "tool_frontend.h"

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
	pid_t		mypid;
	uint64_t	myapid;
	char *		mycname;
	int		mynid;
	int		mynumpes;
	int		mynumnodes;
	char **		myhostlist;
	appHostPlacementList_t * myhostplacement;
	// internal variables
	char **		i;
	int		j;
	
	if (argc < 2)
	{
		usage(argv[0]);
		return 1;
	}
	
	printf("Launching application...\n");
	
	/*
	* launchAprun_barrier - Start a new aprun session from the provided argv array
 	*                       and have ALPS hold the application at its MPI startup
 	*                       barrier.
	*/
	if ((mypid = launchAprun_barrier(&argv[1],0,0,0,0,NULL)) <= 0)
	{
		fprintf(stderr, "Error: Could not launch aprun!\n");
		return 1;
	}
	
	printf("Application %d launched. Harvesting application info from alps.\n", mypid);
	
	/*
	* getApid - Obtain the apid associated with the aprun pid.
	*/
	if ((myapid = getApid(mypid)) == 0)
	{
		fprintf(stderr, "Error: Could not query apid!\n");
		killAprun(mypid, 9);
		return 1;
	}
	
	/*
	* getCName - Returns the cabinet hostname of the active login node.
	*/
	if ((mycname = getCName()) == (char *)NULL)
	{
		fprintf(stderr, "Error: Could not query cname!\n");
		killAprun(mypid, 9);
		return 1;
	}
	
	/*
	* getNid - Returns the node id of the active login node.
	*/
	if ((mynid = getNid()) < 0)
	{
		fprintf(stderr, "Error: Could not query Nid!\n");
		killAprun(mypid, 9);
		return 1;
	}
	
	/*
	* getNumAppPEs - Returns the number of processing elements in the application
	*		 associated with the aprun pid.
	*/
	if ((mynumpes = getNumAppPEs(mypid)) == 0)
	{
		fprintf(stderr, "Error: Could not query numAppPEs!\n");
		killAprun(mypid, 9);
		return 1;
	}
	
	/*
	* getNumAppNodes - Returns the number of compute nodes allocated for the
	*                  application associated with the aprun pid.
	*/
	if ((mynumnodes = getNumAppNodes(mypid)) == 0)
	{
		fprintf(stderr, "Error: Could not query numAppNodes!\n");
		killAprun(mypid, 9);
		return 1;
	}
	
	/*
	* getAppHostsList - Returns a null terminated array of strings containing
	*                   the hostnames of the compute nodes allocated by ALPS
	*                   for the application associated with the aprun pid.
	*/
	if ((myhostlist = getAppHostsList(mypid)) == (char **)NULL)
	{
		fprintf(stderr, "Error: Could not query appHostsList!\n");
		killAprun(mypid, 9);
		return 1;
	}
	
	/*
	* getAppHostsPlacement - Returns a appHostPlacementList_t struct containing
	*                        nodeHostPlacement_t entries that contain the hostname
	*                        of the compute nodes allocated by ALPS and the number
	*                        of PEs assigned to that host for the application 
	*                        associated with the aprun pid.
 	*/
	if ((myhostplacement = getAppHostsPlacement(mypid)) == (appHostPlacementList_t *)NULL)
	{
		fprintf(stderr, "Error: Could not query appHostsPlacement!\n");
		killAprun(mypid, 9);
		return 1;
	}
	
	// print the above output to the screen
	printf("\nThe following is alps information about your application that the tool interface gathered:\n\n");
	
	printf("apid of application: %llu\n", myapid);
	printf("cname of login node where the apid resides: %s\n", mycname);
	printf("NID number of login node where the apid resides: %d\n", mynid);
	printf("Number of application PEs: %d\n", mynumpes);
	printf("Number of compute nodes used by application: %d\n", mynumnodes);
	printf("\n");
	
	printf("The following is a list of compute node hostnames returned by getAppHostsList():\n\n");
	i = myhostlist;
	while (*i != (char *)NULL)
	{
		printf("%s\n", *i++);
	}
	
	printf("\nThe following information was returned by getAppHostsPlacement():\n\n");
	printf("There are %d host(s) in the appHostPlacementList_t struct.\n", myhostplacement->numHosts);
	for (j=0; j < myhostplacement->numHosts; ++j)
	{
		printf("On host %s there are %d PEs.\n", myhostplacement->hosts[j].hostname, myhostplacement->hosts[j].numPes);
	}
	
	// don't forget to cleanup memory that was malloc'ed
	free(mycname);
	free(myhostlist);
	destroy_appHostPlacementList(myhostplacement);
	
	printf("\nHit return to release the application from the startup barrier...");
	
	// just read a single character from stdin then release the app/exit
	(void)getchar();
	
	if (releaseAprun_barrier(mypid))
	{
		fprintf(stderr, "Error: Failed to release app from barrier!\n");
		killAprun(mypid, 9);
		return 1;
	}
	
	return 0;
}
