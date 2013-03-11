/*********************************************************************************\
 * alps_transfer_demo.c - An example program which takes advantage of the CrayTool
 *			Interface which will launch an aprun session from the given
 *			argv and transfer demo files.
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
	fprintf(stdout, "Written by andrewg@cray.com\n");
	return;
}

int
main(int argc, char **argv)
{
	aprunProc_t *myapp;
	
	if (argc < 2)
	{
		usage(argv[0]);
		return 1;
	}
	
	printf("Launching application...\n");
	
	if ((myapp = launchAprun_barrier(&argv[1],0,0,0,0,NULL,NULL,NULL)) <= 0)
	{
		fprintf(stderr, "Error: Could not launch aprun!\n");
		return 1;
	}
	
	if (sendCNodeFile(myapp->apid, "testing.info"))
	{
		fprintf(stderr, "Error: Failed to send file to cnodes!\n");
		killAprun(myapp->apid, 9);
		return 1;
	}
	
	printf("Sent testing.info to the toolhelper directory on the compute node(s).\n");
	
	printf("\nHit return to release the application from the startup barrier...");
	
	// just read a single character from stdin then release the app/exit
	(void)getchar();
	
	if (releaseAprun_barrier(myapp->apid))
	{
		fprintf(stderr, "Error: Failed to release app from barrier!\n");
		killAprun(myapp->apid, 9);
		return 1;
	}
	
	return 0;
}
