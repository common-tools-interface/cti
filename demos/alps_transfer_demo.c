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
	fprintf(stdout, "Launch an application using the Cray Tools Interface\n");
	fprintf(stdout, "and transfer a test file to the compute node.\n");
	fprintf(stdout, "Written by andrewg@cray.com\n");
	return;
}

int
main(int argc, char **argv)
{
	cti_app_id_t		myapp;
	cti_manifest_id_t	mymid;
	cti_session_id_t	mysid;
	cti_aprunProc_t *	myapruninfo;
	char *				file_loc;
	
	if (argc < 2)
	{
		usage(argv[0]);
		return 1;
	}
	
	printf("Launching application...\n");
	
	if ((myapp = cti_launchAppBarrier(&argv[1],0,0,0,0,NULL,NULL,NULL)) == 0)
	{
		fprintf(stderr, "Error: cti_launchAppBarrier failed!\n");
		fprintf(stderr, "CTI error: %s\n", cti_error_str());
		return 1;
	}
	
	// Create a new manifest for the file
	if ((mymid = cti_createNewManifest(0)) == 0)
	{
		fprintf(stderr, "Error: cti_createNewManifest failed!\n");
		fprintf(stderr, "CTI error: %s\n", cti_error_str());
		cti_killApp(myapp, 9);
		return 1;
	}
	
	// Add the file to the manifest
	if (cti_addManifestFile(mymid, "testing.info"))
	{
		fprintf(stderr, "Error: cti_addManifestFile failed!\n");
		fprintf(stderr, "CTI error: %s\n", cti_error_str());
		cti_destroyManifest(mymid);
		cti_killApp(myapp, 9);
		return 1;
	}
	
	// Add the library directory to the manifest
	/*
	if (cti_addManifestLibDir(mymid, "/lib/xcrypt/"))
	{
		fprintf(stderr, "Error: cti_addManifestLibDir failed!\n");
		fprintf(stderr, "CTI error: %s\n", cti_error_str());
		cti_destroyManifest(mymid);
		cti_killApp(myapp, 9);
		return 1;
	}
	*/
	
	// Send the manifest to the compute node
	if ((mysid = cti_sendManifest(myapp, mymid, 0)) == 0)
	{
		fprintf(stderr, "Error: cti_sendManifest failed!\n");
		fprintf(stderr, "CTI error: %s\n", cti_error_str());
		cti_destroyManifest(mymid);
		cti_killApp(myapp, 9);
		return 1;
	}
	
	// Get the location of the directory where the file now resides on the
	// compute node
	if ((file_loc = cti_getSessionFileDir(mysid)) == NULL)
	{
		fprintf(stderr, "Error: cti_getSessionFileDir failed!\n");
		fprintf(stderr, "CTI error: %s\n", cti_error_str());
		cti_killApp(myapp, 9);
		return 1;
	}
	
	printf("Sent testing.info to the directory %s on the compute node(s).\n", file_loc);
	
	if ((myapruninfo = cti_getAprunInfo(myapp)) == NULL)
	{
		fprintf(stderr, "Error: cti_getAprunInfo failed!\n");
		fprintf(stderr, "CTI error: %s\n", cti_error_str());
	} else
	{
		printf("\nVerify by issuing the following commands in another terminal:\n\n");
		printf("module load nodehealth\n");
		printf("pcmd -a %llu \"ls %s\"\n", (long long unsigned int)myapruninfo->apid, file_loc);
	}
	
	free(file_loc);
	
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
	
	return 0;
}
