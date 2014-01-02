/*********************************************************************************\
 * alps_info_demo.c - An example program which takes advantage of the Cray
 *			tools interface which will gather information from ALPS about a
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
 *********************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "cray_tools_fe.h"

void
usage(char *name)
{
	fprintf(stdout, "USAGE: %s [apid]\n", name);
	fprintf(stdout, "Gather information about a previously launched aprun session\n");
	fprintf(stdout, "using the Cray tools interface.\n");
	fprintf(stdout, "Written by andrewg@cray.com\n");
	return;
}

int
main(int argc, char **argv)
{
	uint64_t			myapid;
	// values returned by the tool_frontend library.
	char *				mycname;
	int					mynid;
	int					appnid;
	int					appnumpes;
	int					appnumnodes;
	char **				apphostlist;
	cti_hostsList_t *	apphostplacement;
	// internal variables
	char **	i;
	int		j;
	
	if (argc < 2)
	{
		usage(argv[0]);
		return 1;
	}
	
	// turn the argv string into an apid uint64_t
	
	myapid = (uint64_t)strtoull(argv[1], NULL, 10);
	
	if (cti_registerApid(myapid))
	{
		fprintf(stderr, "Error: Could not register apid!\n");
		return 1;
	}
	
	printf("Application apid %llu registered.\n", (long long unsigned int)myapid);
	
	/*
	* cti_getNodeCName - Returns the cabinet hostname of the active login node.
	*/
	if ((mycname = cti_getNodeCName()) == NULL)
	{
		fprintf(stderr, "Error: Could not query cname!\n");
		return 1;
	}
	
	/*
	* cti_getNodeNid - Returns the node id of the active login node.
	*/
	if ((mynid = cti_getNodeNid()) < 0)
	{
		fprintf(stderr, "Error: Could not query Nid!\n");
		return 1;
	}
	
	/*
	* cti_getAppNid - Returns the node id for the application associated with the apid.
	*/
	if ((appnid = cti_getAppNid(myapid)) < 0)
	{
		fprintf(stderr, "Error: Could not query application Nid!\n");
		return 1;
	}
	
	/*
	* cti_getNumAppPEs -	Returns the number of processing elements in the application
	*						associated with the apid.
	*/
	if ((appnumpes = cti_getNumAppPEs(myapid)) == 0)
	{
		fprintf(stderr, "Error: Could not query numAppPEs!\n");
		return 1;
	}
	
	/*
	* cti_getNumAppNodes -	Returns the number of compute nodes allocated for the
	*						application associated with the aprun pid.
	*/
	if ((appnumnodes = cti_getNumAppNodes(myapid)) == 0)
	{
		fprintf(stderr, "Error: Could not query numAppNodes!\n");
		return 1;
	}
	
	/*
	* cti_getAppHostsList - Returns a null terminated array of strings containing
	*						the hostnames of the compute nodes allocated by ALPS
	*						for the application associated with the aprun pid.
	*/
	if ((apphostlist = cti_getAppHostsList(myapid)) == NULL)
	{
		fprintf(stderr, "Error: Could not query appHostsList!\n");
		return 1;
	}
	
	/*
	* cti_getAppHostsPlacement -	Returns a cti_hostsList_t containing cti_host_t
	*								entries that contain the hostname of the compute
	*								nodes allocated by ALPS and the number of PEs
	*								assigned to that host for the application associated
	*								with the aprun pid.
 	*/
	if ((apphostplacement = cti_getAppHostsPlacement(myapid)) == NULL)
	{
		fprintf(stderr, "Error: Could not query appHostsPlacement!\n");
		return 1;
	}
	
	// print the above output to the screen
	printf("\nThe following is alps information about your application that the tool interface gathered:\n\n");
	
	printf("cname of login node where this utility is being run: %s\n", mycname);
	printf("NID number of login node where this utility is being run: %d\n", mynid);
	printf("\n");
	printf("apid of application: %llu\n", myapid);
	printf("NID number of login node where application aprun resides: %d\n", appnid);
	printf("Number of application PEs: %d\n", appnumpes);
	printf("Number of compute nodes used by application: %d\n", appnumnodes);
	printf("\n");
	
	printf("The following is a list of compute node hostnames returned by cti_getAppHostsList():\n\n");
	i = apphostlist;
	while (*i != NULL)
	{
		printf("%s\n", *i++);
	}
	
	printf("\nThe following information was returned by cti_getAppHostsPlacement():\n\n");
	printf("There are %d host(s) in the cti_hostsList_t struct.\n", apphostplacement->numHosts);
	for (j=0; j < apphostplacement->numHosts; ++j)
	{
		printf("On host %s there are %d PEs.\n", apphostplacement->hosts[j].hostname, apphostplacement->hosts[j].numPes);
	}
	
	// don't forget to cleanup memory that was malloc'ed
	free(mycname);
	free(apphostlist);
	cti_destroyHostsList(apphostplacement);
	
	return 0;
}

