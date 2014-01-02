/*********************************************************************************\
 * alps_transfer_demo.c - An example program which takes advantage of the Cray
 *			tools interface which will launch an aprun session from the given
 *			argv and transfer demo files.
 *
 * © 2011-2014 Cray Inc.  All Rights Reserved.
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
	fprintf(stdout, "USAGE: %s [APRUN STRING]\n", name);
	fprintf(stdout, "Launch an aprun session using the alps_transfer interface\n");
	fprintf(stdout, "Written by andrewg@cray.com\n");
	return;
}

int
main(int argc, char **argv)
{
	cti_aprunProc_t *	myapp;
	cti_manifest_id_t	mymid;
	cti_session_id_t	mysid;
	char *				file_loc;
	
	if (argc < 2)
	{
		usage(argv[0]);
		return 1;
	}
	
	printf("Launching application...\n");
	
	if ((myapp = cti_launchAprunBarrier(&argv[1],0,0,0,0,NULL,NULL,NULL)) <= 0)
	{
		fprintf(stderr, "Error: Could not launch aprun!\n");
		return 1;
	}
	
	// Create a new manifest for the file
	if ((mymid = cti_createNewManifest(0)) == 0)
	{
		fprintf(stderr, "Error: Could not create a new manifest!\n");
		cti_killAprun(myapp->apid, 9);
		return 1;
	}
	
	// Add the file to the manifest
	if (cti_addManifestFile(mymid, "testing.info"))
	{
		fprintf(stderr, "Error: Could not add testing.info to manifest!\n");
		cti_destroyManifest(mymid);
		cti_killAprun(myapp->apid, 9);
		return 1;
	}
	
	// Send the manifest to the compute node
	if ((mysid = cti_sendManifest(myapp->apid, mymid, 0)) == 0)
	{
		fprintf(stderr, "Error: Could not ship manifest!\n");
		cti_destroyManifest(mymid);
		cti_killAprun(myapp->apid, 9);
		return 1;
	}
	
	// Get the location of the directory where the file now resides on the
	// compute node
	if ((file_loc = cti_getSessionFileDir(mysid)) == NULL)
	{
		fprintf(stderr, "Error: Could not get session file directory!\n");
		cti_killAprun(myapp->apid, 9);
		return 1;
	}
	
	printf("Sent testing.info to the directory %s on the compute node(s).\n", file_loc);
	
	printf("\nHit return to release the application from the startup barrier...");
	
	// just read a single character from stdin then release the app/exit
	(void)getchar();
	
	if (cti_releaseAprunBarrier(myapp->apid))
	{
		fprintf(stderr, "Error: Failed to release app from barrier!\n");
		cti_killAprun(myapp->apid, 9);
		return 1;
	}
	
	return 0;
}
