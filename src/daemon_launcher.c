/*********************************************************************************\
 * daemon_launcher.c - A wrapper program used to launch tool daemons on the
 *		     compute nodes. This will ensure that the path and
 *		     ld_library_path env variables point to the proper location
 *		     and allows users to specify environment variable settings
 *		     that a tool daemon should inherit.
 *
 * © 2011-2012 Cray Inc.  All Rights Reserved.
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <fcntl.h>
#include <getopt.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "useful/useful.h"

#define ALPS_XT_NID		"/proc/cray_xt/nid"
#define APID_ENV_VAR		"CRAYTOOL_APID"
#define APID_STR_BUF_LEN	32
#define SCRATCH_ENV_VAR	"TMPDIR"
#define SHELL_ENV_VAR		"SHELL"
#define SHELL_VAR			"/bin/sh"

static int debug_flag = 0;

const struct option long_opts[] = {
			{"binary",	required_argument,	0, 'b'},
			{"env",		required_argument,	0, 'e'},
			{"help",	no_argument,		0, 'h'},
			{"debug",	no_argument,		&debug_flag, 1},
			{0, 0, 0, 0}
			};
				
void
usage(char *name)
{
	fprintf(stdout, "Usage: %s [OPTIONS]...\n", name);
	fprintf(stdout, "Launch a program on a compute node. Chdir's to the toolhelper\n");
	fprintf(stdout, "directory and add it to PATH and LD_LIBRARY_PATH. Sets optional\n");
	fprintf(stdout, "specified variables in the environment of the process.\n\n");
	
	fprintf(stdout, "\t-b, --binary	   Binary file to execute\n");
	fprintf(stdout, "\t-e, --env       Specify an environment variable to set\n");
	fprintf(stdout, "\t                The argument provided to this option must be issued\n");
	fprintf(stdout, "\t                with var=val, for example: -e myVar=myVal\n");
	fprintf(stdout, "\t    --debug     Turn on debug logging to a file. (STDERR/STDOUT to file)\n");
	fprintf(stdout, "\t-h, --help      Display this text and exit\n");
}

int
main(int argc, char **argv)
{
	int			opt_ind = 0;
	int			c, nid;
	FILE *		alps_fd;	// ALPS NID file stream
	FILE *		log;
	char		file_buf[BUFSIZ];	// file read buffer
	uint64_t	apid = 0;
	char		apid_str[APID_STR_BUF_LEN];
	size_t		len;
	char		*end, *tool_path;
	char *		binary = NULL; 
	char *		binary_path;
	char		*env, *val;

	// we require at least 1 argument beyond argv[0]
	if (argc < 2)
	{
		usage(argv[0]);
		return 1;
	}

	/*
	* The ALPS Tool Helper closes channels 0-2 to keep things "clean".
	* Unfortuantely, that means that any file opens that I do will
	* have them available. If I open my log file, for instance, it
	* will get channel 0 and that just doesn't seem safe. So, the lines
	* below open (and waste) three channels such that I am guaranteed
	* that future opens will not get them 0-2. Note that the opens may
	* or may not get all/any of 0-2, but should they be available, they
	* will be gotten.
	* This, of course, must happen "early" before any other opens.
	*/
	open("/dev/null", O_RDONLY);
	open("/dev/null", O_WRONLY);
	open("/dev/null", O_WRONLY);
	
	while ((c = getopt_long(argc, argv, "b:e:h", long_opts, &opt_ind)) != -1)
	{
		switch (c)
		{
			case 0:
				// if this is a flag, do nothing
				break;
			case 'b':
				if (optarg == (char *)NULL)
				{
					usage(argv[0]);
					return 1;
				}
				
				// this is the name of the binary we will exec
				binary = strdup(optarg);
				
				break;
				
			case 'e':
				if (optarg == (char *)NULL)
				{
					usage(argv[0]);
					return 1;
				}
				
				// this is an optional option to set user defined environment variables
				val = strdup(optarg);
				// we need to strsep the string at the "=" character
				// we expect the user to pass in the -e argument as envVar=val
				if ((env = strsep(&val, "=")) == NULL)
				{
					//error
					fprintf(stderr, "strsep failed");
					return 1;
				}
				// ensure the user didn't pass us something stupid i.e. non-conforming
				if ((*env == '\0') || (*val == '\0'))
				{
					// they passed us something stupid
					fprintf(stderr, "Unrecognized env argument.\n");
					usage(argv[0]);
					return 1;
				}
				
				// set the actual environment variable
				if (setenv(env, val, 1) < 0)
				{
					// failure
					fprintf(stderr, "setenv failed");
					return 1;
				}
				
				// free the strdup'ed string - this will subsequently also get rid of the val
				free(env);
				
				break;
				
			case 'h':
				usage(argv[0]);
				return 1;
			default:
				usage(argv[0]);
				return 1;
		}
	}
	
	// get the apid from the toolhelper path from argv[0]
	if ((sscanf(argv[0], "/var/spool/alps/%*d/toolhelper%llu/%*s", (long long unsigned int *)&apid)) == 0)
	{
		// fix for CLE 5.0 changes
		if ((sscanf(argv[0], "/var/opt/cray/alps/spool/%*d/toolhelper%llu/%*s", (long long unsigned int *)&apid)) == 0)
		{
			// failure
			fprintf(stderr, "sscanf apid failed");
			return 1;
		}
	}
	// write the apid to the apid_str
	snprintf(apid_str, APID_STR_BUF_LEN, "%llu", (long long unsigned int)apid);
	
	// if debug mode is turned on, redirect stdout/stderr to a log file
	if (debug_flag)
	{
		// read the nid from the system location
		// open up the file defined in the alps header containing our node id (nid)
		if ((alps_fd = fopen(ALPS_XT_NID, "r")) == NULL)
		{
			return 1;
		}
		
		// we expect this file to have a numeric value giving our current nid
		if (fgets(file_buf, BUFSIZ, alps_fd) == NULL)
		{
			return 1;
		}
		// convert this to an integer value
		nid = atoi(file_buf);
		
		// close the file stream
		fclose(alps_fd);
		
		// write the apid into the file_buf
		snprintf(file_buf, BUFSIZ, "%llu", (long long unsigned int)apid);
		
		log = create_log(nid, file_buf);
		hook_stdoe(log);
	}
	
	// set the APID_ENV_VAR environment variable to the apid
	if (setenv(APID_ENV_VAR, apid_str, 1) < 0)
	{
		// failure
		fprintf(stderr, "setenv failed");
		return 1;
	}
	
	// create the toolhelper path from argv[0]
	// begin by locating the final '/' in the string
	end = strrchr(argv[0], '/');
	
	// determine the string length of the toolhelper directory location
	// this is the string length of argv[0] subtracted by the length of
	// the substring from the final '/' character.
	len = strlen(argv[0]) - strlen(end);
	
	// malloc space for the string. Note that we need one extra byte for the
	// null terminator
	if ((tool_path = malloc(len+1)) == (void *)0)
	{
		fprintf(stderr, "malloc failed");
		return 1;
	}
	
	// strncpy the substring
	if (strncpy(tool_path, argv[0], len) == (char *)NULL)
	{
		fprintf(stderr, "strncpy failed");
		return 1;
	}
	// set the final null terminator
	tool_path[len] = '\0';
	
	fprintf(stderr, "Toolhelper path: %s\n", tool_path);
	
	// set the SCRATCH_ENV_VAR environment variable to the toolhelper directory.
	// ALPS will enforce cleanup here and the tool is guaranteed to be able to write
	// to it.
	if (setenv(SCRATCH_ENV_VAR, tool_path, 1) < 0)
	{
		// failure
		fprintf(stderr, "setenv failed");
		return 1;
	}
	
	// set the SHELL environment variable to the shell included on the compute
	// node. Note that other shells other than /bin/sh are not currently supported
	// in CNL.
	if (setenv(SHELL_ENV_VAR, SHELL_VAR, 1) < 0)
	{
		// failure
		fprintf(stderr, "setenv failed");
		return 1;
	}
	
	// call adjustPaths so that we chdir to where we shipped stuff over to and setup PATH/LD_LIBRARY_PATH
	if (adjustPaths(tool_path))
	{
		fprintf(stderr, "Could not adjust paths.\n");
		free(tool_path);
		return 1;
	}
	
	// anything after the final "--" in the options string will be passed directly to the exec'ed binary
	
	// create the full path to the binary we are going to exec
	len = strlen(tool_path) + strlen("/") + strlen(binary) + 1;
	if ((binary_path = malloc(len)) == (void *)0)
	{
		fprintf(stderr, "malloc failed");
		return 1;
	}
	snprintf(binary_path, len, "%s/%s", tool_path, binary);
	
	fprintf(stderr, "Binary path: %s\n", binary_path);
	
	// setup the new argv array
	// Note that argv[optind] is the first argument that appears after the "--" terminator
	// We need to modify what argv[optind - 1] points to so that it follows the standard argv[0] 
	// nomenclature.
	argv[optind - 1] = binary_path;
	
	// now we can exec our program
	execv(binary_path, &argv[optind - 1]);
	
	fprintf(stderr, "Return from exec!\n");
	
	return 1;
}
