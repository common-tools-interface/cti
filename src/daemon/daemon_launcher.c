/*********************************************************************************\
 * daemon_launcher.c - A wrapper program used to launch tool daemons on the
 *		     compute nodes. This will ensure that the path and
 *		     ld_library_path env variables point to the proper location
 *		     and allows users to specify environment variable settings
 *		     that a tool daemon should inherit.
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

#include <sys/types.h>
#include <sys/stat.h>

#include <archive.h>
#include <archive_entry.h>

#include "cti_defs.h"
#include "useful.h"

static int debug_flag = 0;

const struct option long_opts[] = {
			{"binary",		required_argument,	0, 'b'},
			{"directory",	required_argument,	0, 'd'},
			{"env",			required_argument,	0, 'e'},
			{"inst",		required_argument,	0, 'i'},
			{"manifest",	required_argument,	0, 'm'},
			{"help",		no_argument,		0, 'h'},
			{"debug",		no_argument,		&debug_flag, 1},
			{0, 0, 0, 0}
			};
				
static void
usage(char *name)
{
	fprintf(stdout, "Usage: %s [OPTIONS]...\n", name);
	fprintf(stdout, "Launch a program on a compute node. Chdir's to the toolhelper\n");
	fprintf(stdout, "directory and add it to PATH and LD_LIBRARY_PATH. Sets optional\n");
	fprintf(stdout, "specified variables in the environment of the process.\n\n");
	
	fprintf(stdout, "\t-b, --binary	   Binary file to execute\n");
	fprintf(stdout, "\t-d, --directory Use named directory for CWD\n");
	fprintf(stdout, "\t-e, --env       Specify an environment variable to set\n");
	fprintf(stdout, "\t                The argument provided to this option must be issued\n");
	fprintf(stdout, "\t                with var=val, for example: -e myVar=myVal\n");
	fprintf(stdout, "\t-i, --instance  Instance of tool daemon. Used in conjunction with sessions\n");
	fprintf(stdout, "\t-m, --manifest  Manifest tarball to extract/set as CWD if -d omitted\n");
	fprintf(stdout, "\t    --debug     Turn on debug logging to a file. (STDERR/STDOUT to file)\n");
	fprintf(stdout, "\t-h, --help      Display this text and exit\n");
}

static int
copy_data(struct archive *ar, struct archive *aw)
{
	int r;
	const void *buff;
	size_t size;
	off_t offset;

	for (;;)
	{
		r = archive_read_data_block(ar, &buff, &size, &offset);
		if (r == ARCHIVE_EOF) 
		{
			return (ARCHIVE_OK);
		}
		if (r != ARCHIVE_OK)
			return (r);
		r = archive_write_data_block(aw, buff, size, offset);
		if (r != ARCHIVE_OK) 
		{
			fprintf(stderr, "archive_write_data_block(): %s\n", archive_error_string(ar));
			return (r);
		}
	}
}

int
main(int argc, char **argv)
{
	int			opt_ind = 0;
	int			c, nid, i, sCnt;
	FILE *		alps_fd;	// ALPS NID file stream
	FILE *		log;
	char		file_buf[BUFSIZ];	// file read buffer
	uint64_t	apid = 0;
	char		apid_str[APID_STR_BUF_LEN];
	size_t		len;
	char		*end, *tool_path, *launch_path;
	struct stat statbuf;
	char *		binary = NULL; 
	char *		binary_path;
	char *		directory = NULL;
	char *		manifest = NULL;
	int			inst = 1;	// default to 1 if no instance argument is provided
	char *		manifest_path = NULL;
	char *		lock_path = NULL;
	FILE *		lock_file;
	char *		bin_path = NULL;
	char *		lib_path = NULL;
	char		*env, *val, *t;
	char *		old_env_path = NULL;
	char *		env_path = NULL;
	int						r, flags = 0;
	struct archive *		a;
	struct archive *		ext;
	struct archive_entry *	entry;

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
	
	while ((c = getopt_long(argc, argv, "b:d:e:i:m:h", long_opts, &opt_ind)) != -1)
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
				
			case 'd':
				if (optarg == (char *)NULL)
				{
					usage(argv[0]);
					return 1;
				}
				
				// this is the name of the existing directory we should cd to
				directory = strdup(optarg);
				
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
					fprintf(stderr, "strsep failed\n");
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
					fprintf(stderr, "setenv failed\n");
					return 1;
				}
				
				// free the strdup'ed string - this will subsequently also get rid of the val
				free(env);
				
				break;
				
			case 'i':
				if (optarg == (char *)NULL)
				{
					usage(argv[0]);
					return 1;
				}
				
				// This is our instance number. We need to wait for all those
				// before us to finish their work before proceeding.
				inst = atoi(optarg);
				
				break;
				
			case 'm':
				if (optarg == (char *)NULL)
				{
					usage(argv[0]);
					return 1;
				}
				
				// this is the name of the manifest tarball we will extract
				manifest = strdup(optarg);
				
				break;
				
			case 'h':
				usage(argv[0]);
				return 1;
			default:
				usage(argv[0]);
				return 1;
		}
	}
	
	// call realpath on argv[0] to resolve any extra slashes
	if ((launch_path = realpath(argv[0], NULL)) == NULL)
	{
		// failure
		fprintf(stderr, "realpath failed\n");
		return 1;
	}
	
	// get the apid from the toolhelper path from argv[0]
	if ((sscanf(launch_path, "/var/spool/alps/%*d/toolhelper%llu/%*s", (long long unsigned int *)&apid)) == 0)
	{
		// fix for CLE 5.0 changes
		if ((sscanf(launch_path, "/var/opt/cray/alps/spool/%*d/toolhelper%llu/%*s", (long long unsigned int *)&apid)) == 0)
		{
			// failure
			fprintf(stderr, "sscanf apid failed\n");
			return 1;
		}
	}
	
	// cleanup
	free(launch_path);
	
	// write the apid to the apid_str
	snprintf(apid_str, APID_STR_BUF_LEN, "%llu", (long long unsigned int)apid);
	
	// if debug mode is turned on, redirect stdout/stderr to a log file
	if (debug_flag)
	{
		// read the nid from the system location
		// open up the file defined in the alps header containing our node id (nid)
		if ((alps_fd = fopen(ALPS_XT_NID, "r")) == NULL)
		{
			fprintf(stderr, "%s not found.\n", ALPS_XT_NID);
			return 1;
		}
		
		// we expect this file to have a numeric value giving our current nid
		if (fgets(file_buf, BUFSIZ, alps_fd) == NULL)
		{
			fprintf(stderr, "fgets failed.\n");
			return 1;
		}
		// convert this to an integer value
		nid = atoi(file_buf);
		
		// close the file stream
		fclose(alps_fd);
		
		// write the apid into the file_buf
		snprintf(file_buf, BUFSIZ, "%llu", (long long unsigned int)apid);
		
		log = _cti_create_log(nid, file_buf);
		_cti_hook_stdoe(log);
	}
	
	// Ensure the user provided a directory or manifest option
	if (directory == NULL && manifest == NULL)
	{
		// failure
		fprintf(stderr, "Missing either directory or manifest argument!\n");
		return 1;
	}
	
	// set the APID_ENV_VAR environment variable to the apid
	if (setenv(APID_ENV_VAR, apid_str, 1) < 0)
	{
		// failure
		fprintf(stderr, "setenv failed\n");
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
		fprintf(stderr, "malloc failed\n");
		return 1;
	}
	
	// strncpy the substring
	if (strncpy(tool_path, argv[0], len) == (char *)NULL)
	{
		fprintf(stderr, "strncpy failed\n");
		return 1;
	}
	// set the final null terminator
	tool_path[len] = '\0';
	
	fprintf(stderr, "inst %d: Toolhelper path: %s\n", inst, tool_path);
	
	// cd to the tool_path and relax the permissions
	if (stat(tool_path, &statbuf) == -1)
	{
		fprintf(stderr, "Could not stat %s\n", tool_path);
		return 1;
	}
	
	// Relax permissions to ensure we can write to this directory
	// use the existing perms for group and global settings
	if (chmod(tool_path, statbuf.st_mode | S_IRWXU) != 0)
	{
		fprintf(stderr, "Could not chmod %s\n", tool_path);
		return 1;
	}
	
	// change the working directory to path
	if (chdir(tool_path) != 0)
	{
		fprintf(stderr, "Could not chdir to %s\n", tool_path);
		return 1;
	}
	
	// Now unpack the manifest
	if (manifest != NULL)
	{
		fprintf(stderr, "inst %d: Manifest provided: %s\n", inst, manifest);
	
		// create the manifest path string
		if (asprintf(&manifest_path, "%s/%s", tool_path, manifest) <= 0)
		{
			fprintf(stderr, "asprintf failed\n");
			return 1;
		}
		
		// ensure the manifest tarball exists
		if (stat(manifest_path, &statbuf) == -1)
		{
			fprintf(stderr, "Could not stat manifest tarball %s\n", manifest_path);
			return 1;
		}
		
		// ensure it is a regular file
		if (!S_ISREG(statbuf.st_mode))
		{
			fprintf(stderr, "%s is not a regular file!\n", manifest_path);
			return 1;
		}
	
		// set the flags
		flags |= ARCHIVE_EXTRACT_PERM;
		flags |= ARCHIVE_EXTRACT_ACL;
		flags |= ARCHIVE_EXTRACT_FFLAGS;
	
		a = archive_read_new();
		ext = archive_write_disk_new();
		archive_write_disk_set_options(ext, flags);
		archive_read_support_format_tar(a);
	
		if ((r = archive_read_open_filename(a, manifest_path, 10240)))
		{
			fprintf(stderr, "archive_read_open_filename(): %s\n", archive_error_string(a));
			return r;
		}
	
		for (;;) 
		{
			r = archive_read_next_header(a, &entry);
			if (r == ARCHIVE_EOF)
				break;
			if (r != ARCHIVE_OK)
			{
				fprintf(stderr, "archive_read_next_header(): %s\n", archive_error_string(a));
				return 1;
			}
		
			r = archive_write_header(ext, entry);
			if (r != ARCHIVE_OK)
			{
				fprintf(stderr, "archive_write_header(): %s\n", archive_error_string(ext));
				return 1;
			}
		
			copy_data(a, ext);
		
			r = archive_write_finish_entry(ext);
			if (r != ARCHIVE_OK)
			{
				fprintf(stderr, "archive_write_finish_entry(): %s\n", archive_error_string(ext));
				return 1;
			}
		}
	
		archive_read_close(a);
		archive_read_free(a);
	
		// The manifest should be extracted at this point.
	
		// We are done with the tarball, so remove it - if this fails just ignore it since it won't
		// break things
		remove(manifest_path);
	
		// Modify manifest_path to point at the directory, not the ".tar" bits
		if ((t = strstr(manifest_path, ".tar")) != NULL)
		{
			// set it to a null terminator
			*t = '\0';
		}
	}
	
	// handle the directory option
	if (directory != NULL)
	{
		fprintf(stderr, "inst %d: Directory provided: %s\n", inst, directory);
		
		// create the manifest path string
		if (manifest_path != NULL)
		{
			free(manifest_path);
			manifest_path = NULL;
		}
		if (asprintf(&manifest_path, "%s/%s", tool_path, directory) <= 0)
		{
			fprintf(stderr, "asprintf failed\n");
			return 1;
		}
	}
	
	// ensure the manifest directory exists
	if (stat(manifest_path, &statbuf) == -1)
	{
		fprintf(stderr, "Could not stat root directory %s\n", manifest_path);
		return 1;
	}
		
	// ensure it is a directory
	if (!S_ISDIR(statbuf.st_mode))
	{
		fprintf(stderr, "%s is not a directory!\n", manifest_path);
		return 1;
	}
	
	// At this point we are done untar'ing stuff into the directory. We need
	// to create our hidden file corresponding to our instance so that other
	// tool daemons will know that their dependencies included with our manifest
	// are ready for them to use. This prevents race conditions where a later
	// tool daemon depends on files included in an earlier tool daemons manifest
	// but the later tool daemon executes first.
	
	// first ensure the directory string exists, otherwise create it
	if (directory == NULL)
	{
		// grab the directory part based on the manifest string
		directory = strdup(manifest);
		
		// Modify t1 to point at the directory, not the ".tar" bits
		if ((t = strstr(directory, ".tar")) != NULL)
		{
			// set it to a null terminator
			*t = '\0';
		}
	}
	
	// create the path to the lock file
	if (asprintf(&lock_path, "%s/.lock_%s_%d", tool_path, directory, inst) <= 0)
	{
		fprintf(stderr, "asprintf failed\n");
		return 1;
	}
	
	// try to fopen the lock file
	if ((lock_file = fopen(lock_path, "w")) == NULL)
	{
		fprintf(stderr, "fopen on %s failed\n", lock_path);
		// don't exit here, this will break future tool daemons though so its pretty
		// bad to have a failure at this point. But it shouldn't screw this instance
		// up at the very least.
	} else
	{
		// close the file, it has been created
		fclose(lock_file);
	}
	
	// Set the ALPS_DIR_VAR environment variable to the toolhelper directory.
	if (setenv(ALPS_DIR_VAR, manifest_path, 1) < 0)
	{
		// failure
		fprintf(stderr, "setenv failed\n");
		return 1;
	}
	
	// Set the ROOT_DIR_VAR environment variable to the manifest directory.
	if (setenv(ROOT_DIR_VAR, manifest_path, 1) < 0)
	{
		// failure
		fprintf(stderr, "setenv failed\n");
		return 1;
	}
	
	// Get the current value of the SCRATCH_ENV_VAR environment variable and set
	// it to OLD_SCRATCH_ENV_VAR in case the tool daemon needs access to it.
	if ((old_env_path = getenv(SCRATCH_ENV_VAR)) != NULL)
	{
		if (setenv(OLD_SCRATCH_ENV_VAR, old_env_path, 1) < 0)
		{
			// failure
			fprintf(stderr, "setenv failed\n");
			return 1;
		}
	}

	// set the SCRATCH_ENV_VAR environment variable to the toolhelper directory.
	// ALPS will enforce cleanup here and the tool is guaranteed to be able to write
	// to it.
	if (asprintf(&env_path, "%s/tmp", manifest_path) <= 0)
	{
		fprintf(stderr, "asprintf failed\n");
		return 1;
	}
	if (setenv(SCRATCH_ENV_VAR, env_path, 1) < 0)
	{
		// failure
		fprintf(stderr, "setenv failed\n");
		return 1;
	}
	
	// set the BIN_DIR_VAR environment variable to the toolhelper directory.
	if (asprintf(&bin_path, "%s/bin", manifest_path) <= 0)
	{
		fprintf(stderr, "asprintf failed\n");
		return 1;
	}
	if (setenv(BIN_DIR_VAR, bin_path, 1) < 0)
	{
		// failure
		fprintf(stderr, "setenv failed\n");
		return 1;
	}
	
	// set the LIB_DIR_VAR environment variable to the toolhelper directory.
	if (asprintf(&lib_path, "%s/lib", manifest_path) <= 0)
	{
		fprintf(stderr, "asprintf failed\n");
		return 1;
	}
	if (setenv(LIB_DIR_VAR, lib_path, 1) < 0)
	{
		// failure
		fprintf(stderr, "setenv failed\n");
		return 1;
	}
	
	// set the SHELL environment variable to the shell included on the compute
	// node. Note that other shells other than /bin/sh are not currently supported
	// in CNL.
	if (setenv(SHELL_ENV_VAR, SHELL_PATH, 1) < 0)
	{
		// failure
		fprintf(stderr, "setenv failed\n");
		return 1;
	}
	
	// call _cti_adjustPaths so that we chdir to where we shipped stuff over to and setup PATH/LD_LIBRARY_PATH
	if (_cti_adjustPaths(manifest_path))
	{
		fprintf(stderr, "Could not adjust paths.\n");
		free(tool_path);
		return 1;
	}
	
	// ensure that binary was provided, otherwise just exit - the caller wanted to stage stuff.
	if (binary == NULL)
	{
		fprintf(stderr, "inst %d: No binary provided. Stage to %s complete.\n", inst, manifest_path);
		return 0;
	}
	
	// anything after the final "--" in the options string will be passed directly to the exec'ed binary
	
	// create the full path to the binary we are going to exec
	if (asprintf(&binary_path, "%s/bin/%s", manifest_path, binary) <= 0)
	{
		fprintf(stderr, "asprintf failed\n");
		return 1;
	}
	
	fprintf(stderr, "inst %d: Binary path: %s\n", inst, binary_path);
	
	// At this point we need to wait on any other previous tool daemons that may
	// or may not contain depedencies this instance needs. We will try to make
	// sure each previous instance has a lock file, otherwise we spin until it
	// gets created. We have no way of knowing for sure if the previous manifest
	// contains dependencies that we need. This information is not tracked.
	for (i = inst-1; i > 0; --i)
	{
		// reset sCnt
		sCnt = 0;
		
		// free the existing lock path
		free(lock_path);
		
		// create the path to this instances lock file
		if (asprintf(&lock_path, "%s/.lock_%s_%d", tool_path, directory, i) <= 0)
		{
			fprintf(stderr, "asprintf failed\n");
			return 1;
		}
		
		// loop until we can stat this lock file
		while (stat(lock_path, &statbuf))
		{
			// print out a message if sCnt is divisible by 100
			if (sCnt++%100 == 0)
			{
				fprintf(stderr, "inst %d: Lock file %s not found. Sleeping...\n", inst, lock_path);
			}
			
			// sleep until the file is created
			usleep(10000);
		}
	}
	
	fprintf(stderr, "inst %d: All dependency locks acquired. Ready to exec.\n", inst);
	
	// At this point it is safe to assume we have all our dependencies.
	
	// ensure the binary exists
	if (stat(binary_path, &statbuf) == -1)
	{
		fprintf(stderr, "Could not stat %s\n", binary_path);
		return 1;
	}
	
	// ensure it is a regular file
	if (!S_ISREG(statbuf.st_mode))
	{
		fprintf(stderr, "%s is not a regular file!\n", binary_path);
		return 1;
	}
	
	// setup the new argv array
	// Note that argv[optind] is the first argument that appears after the "--" terminator
	// We need to modify what argv[optind - 1] points to so that it follows the standard argv[0] 
	// nomenclature.
	argv[optind - 1] = binary_path;
	
	// now we can exec our program
	execv(binary_path, &argv[optind - 1]);
	
	fprintf(stderr, "inst %d: Return from exec!\n", inst);
	
	perror("execv");
	
	return 1;
}

