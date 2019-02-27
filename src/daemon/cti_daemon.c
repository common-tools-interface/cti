/*********************************************************************************\
 * cti_daemon.c - A wrapper program used to launch tool daemons on the
 *		     compute nodes. This will ensure that the path and
 *		     ld_library_path env variables point to the proper location
 *		     and allows users to specify environment variable settings
 *		     that a tool daemon should inherit.
 *
 * Copyright 2011-2019 Cray Inc.  All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
 *
 *********************************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <dirent.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/resource.h>

#include <archive.h>
#include <archive_entry.h>

#include "cti_defs.h"
#include "cti_daemon.h"
#include "cti_useful.h"

static int		debug_flag = 0;

const struct option long_opts[] = {
			{"apid",		required_argument,	0, 'a'},
			{"binary",		required_argument,	0, 'b'},
			{"clean",		no_argument,		0, 'c'},
			{"directory",	required_argument,	0, 'd'},
			{"env",			required_argument,	0, 'e'},
			{"inst",		required_argument,	0, 'i'},
			{"manifest",	required_argument,	0, 'm'},
			{"path",		required_argument,	0, 'p'},
			{"apath",		required_argument,	0, 't'},
			{"ldlibpath",	required_argument,	0, 't'},
			{"wlm",			required_argument,	0, 'w'},
			{"help",		no_argument,		0, 'h'},
			{"debug",		no_argument,		&debug_flag, 1},
			{0, 0, 0, 0}
			};
			
struct cti_pids
{
	pid_t				pids[32];
	unsigned int		idx;
	struct cti_pids *	next;
};
			
/* wlm specific proto objects defined elsewhere */
extern cti_wlm_proto_t	_cti_cray_slurm_wlmProto;
extern cti_wlm_proto_t	_cti_slurm_wlmProto;

/* noneness wlm proto object */
static cti_wlm_proto_t	_cti_nonenessProto =
{
	CTI_WLM_NONE,					// wlm_type
	_cti_wlm_init_none,				// wlm_init
	_cti_wlm_getNodeID_none			// wlm_getNodeID
};

/* global wlm proto object - this is initialized to noneness by default */
cti_wlm_proto_t *	_cti_wlmProto 	= &_cti_nonenessProto;

static void
usage(void)
{
	fprintf(stdout, "Usage: %s [OPTIONS]...\n", CTI_LAUNCHER);
	fprintf(stdout, "Launch a program on a compute node. Chdir's to the toolhelper\n");
	fprintf(stdout, "directory and add it to PATH and LD_LIBRARY_PATH. Sets optional\n");
	fprintf(stdout, "specified variables in the environment of the process.\n\n");
	
	fprintf(stdout, "\t-a, --apid      Application id\n");
	fprintf(stdout, "\t-b, --binary	   Binary file to execute\n");
	fprintf(stdout, "\t-c, --clean     Terminate existing tool daemons and cleanup session\n");
	fprintf(stdout, "\t-d, --directory Use named directory for CWD\n");
	fprintf(stdout, "\t-e, --env       Specify an environment variable to set\n");
	fprintf(stdout, "\t                The argument provided to this option must be issued\n");
	fprintf(stdout, "\t                with var=val, for example: -e myVar=myVal\n");
	fprintf(stdout, "\t-i, --inst      Instance of tool daemon. Used in conjunction with sessions\n");
	fprintf(stdout, "\t-m, --manifest  Manifest tarball to extract/set as CWD if -d omitted\n");
	fprintf(stdout, "\t-p, --path      PWD path where tool daemon should be started\n");
	fprintf(stdout, "\t-t, --apath     Path where the pmi_attribs file can be found\n");
	fprintf(stdout, "\t-l, --ldlibpath What to set as LD_LIBRARY_PATH\n");
	fprintf(stdout, "\t-w, --wlm       Workload Manager in use\n");
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
			fprintf(stderr, "%s: archive_write_data_block(): %s\n", CTI_LAUNCHER, archive_error_string(ar));
			return (r);
		}
	}
}

int
do_lock(int fd, int cmd, int type, off_t offset, int whence, off_t len)
{
	struct flock	lock;
	
	lock.l_type = type;			// F_RDLCK, F_WRLCK, F_UNLCK
	lock.l_start = offset;		// byte offset relative to l_whence
	lock.l_whence = whence;		// SEEK_SET, SEEK_CUR, SEEK_END
	lock.l_len = len;			// number of bytes (0 means EOF)

	return (fcntl(fd, cmd, &lock));
}

#define FLOCK(fd)		do_lock((fd), F_SETLK, F_WRLCK, 0, SEEK_SET, 0)
#define UNFLOCK(fd)	do_lock((fd), F_SETLK, F_UNLCK, 0, SEEK_SET, 0)

void
remove_dir(char *path)
{
	DIR *				dir;
	struct dirent *		d;
	char *				file = NULL;
	
	if ((dir = opendir(path)) == NULL)
	{
		return;
	}
	
	while ((d = readdir(dir)) != NULL)
	{
		// ensure this isn't the . or .. file
		switch (strlen(d->d_name))
		{
			case 1:
				if (d->d_name[0] == '.')
				{
					// continue to the outer while loop
					continue;
				}
				break;
				
			case 2:
				if (strcmp(d->d_name, "..") == 0)
				{
					// continue to the outer while loop
					continue;
				}
				break;
			
			default:
				break;
		}
		
		if (asprintf(&file, "%s/%s", path, d->d_name) <= 0)
		{
			fprintf(stderr, "%s: asprintf failed\n", CTI_LAUNCHER);
			continue;
		}
		
		if (d->d_type == DT_DIR)
		{
			remove_dir(file);
		}
		
		remove(file);
		free(file);
		file = NULL;
	}
	
	closedir(dir);
	remove(path);
}

int
main(int argc, char **argv)
{
	struct rlimit	rl;
	int				opt_ind = 0;
	int				c, i, sCnt;
	bool			cleanup = false;
	int				wlm_arg = CTI_WLM_NONE;
	char *			wlm_str;
	char *			apid_str = NULL;
	char *			tool_path = NULL;
	char *			attribs_path = NULL;
	char *			ld_lib_path = NULL;
	FILE *			log;
	struct stat 	statbuf;
	char *			binary = NULL; 
	char *			binary_path;
	char *			directory = NULL;
	char *			manifest = NULL;
	int				inst = 1;	// default to 1 if no instance argument is provided
	char *			manifest_path = NULL;
	int				o_fd;
	char *			lock_path = NULL;
	FILE *			lock_file;
	pid_t			mypid = -1;
	char *			pid_path = NULL;
	char *			bin_path = NULL;
	char *			lib_path = NULL;
	char *			file_path = NULL;
	cti_stack_t *	env_args = NULL;
	char			*env, *val, *t;
	char *			old_env_path = NULL;
	char *			env_path = NULL;
	int						r, flags = 0;
	struct archive *		a;
	struct archive *		ext;
	struct archive_entry *	entry;
	char *					cwd;

	// we require at least 1 argument beyond argv[0]
	if (argc < 2)
	{
		usage();
		return 1;
	}

	chmod(argv[0], S_IRUSR|S_IWUSR|S_IXUSR|S_IRGRP|S_IWGRP|S_IXGRP|S_IROTH|S_IWOTH|S_IXOTH);
	
	// We want to do as little as possible while parsing the opts. This is because
	// we do not create a log file until after the opts are parsed, and there will
	// be no valid output until after the log is created on most systems.
	while ((c = getopt_long(argc, argv, "a:b:cd:e:i:m:p:t:l:w:h", long_opts, &opt_ind)) != -1)
	{
		switch (c)
		{
			case 0:
				// if this is a flag, do nothing
				break;
			
			case 'a': 
				if (optarg == NULL)
				{
					usage();
					return 1;
				}
				
				// strip leading whitespace
				while (*optarg == ' ')
				{
					++optarg;
				}
				
				// this is the application id that is WLM specific
				apid_str = strdup(optarg);
				
				break;
			
			case 'b':
				if (optarg == NULL)
				{
					usage();
					return 1;
				}
				
				// strip leading whitespace
				while (*optarg == ' ')
				{
					++optarg;
				}
				
				// this is the name of the binary we will exec
				binary = strdup(optarg);
				
				break;
				
			case 'c':
				// set cleanup to true
				cleanup = true;
				
				break;
				
			case 'd':
				if (optarg == NULL)
				{
					usage();
					return 1;
				}
				
				// strip leading whitespace
				while (*optarg == ' ')
				{
					++optarg;
				}
				
				// this is the name of the existing directory we should cd to
				directory = strdup(optarg);
				
				break;
				
			case 'e':
				if (optarg == NULL)
				{
					usage();
					return 1;
				}
				
				// strip leading whitespace
				while (*optarg == ' ')
				{
					++optarg;
				}
				
				// this is an optional option to set user defined environment variables
				if (env_args == NULL)
				{
					// create the string list
					if ((env_args = _cti_newStack()) == NULL)
					{
						// failed to create stack - shouldn't happen
						fprintf(stderr, "%s: _cti_newStack() failed.\n", CTI_LAUNCHER);
						return 1;
					}
				}
				
				if (_cti_push(env_args, strdup(optarg)))
				{
					// failed to push the string - shouldn't happen
					fprintf(stderr, "%s: _cti_push() failed.\n", CTI_LAUNCHER);
					return 1;
				}
				
				break;
				
			case 'i':
				if (optarg == NULL)
				{
					usage();
					return 1;
				}
				
				// This is our instance number. We need to wait for all those
				// before us to finish their work before proceeding.
				inst = atoi(optarg);
				
				break;
				
			case 'm':
				if (optarg == NULL)
				{
					usage();
					return 1;
				}
				
				// strip leading whitespace
				while (*optarg == ' ')
				{
					++optarg;
				}
				
				// this is the name of the manifest tarball we will extract
				manifest = strdup(optarg);
				
				break;
				
			case 'p':
				if (optarg == NULL)
				{
					usage();
					return 1;
				}
				
				// strip leading whitespace
				while (*optarg == ' ')
				{
					++optarg;
				}
				
				// this is the tool path argument where we should cd to
				tool_path = strdup(optarg);
				
				break;
				
			case 't':
				if (optarg == NULL)
				{
					usage();
					return 1;
				}
				
				// strip leading whitespace
				while (*optarg == ' ')
				{
					++optarg;
				}
				
				// this is the pmi_attribs path argument to find the attribs file
				// This is optional, some implementations might not use it.
				attribs_path = strdup(optarg);
				
				break;

			case 'l':
				if (optarg == NULL)
				{
					usage();
					return 1;
				}
				
				// strip leading whitespace
				while (*optarg == ' ')
				{
					++optarg;
				}
				
				// this is the argument to set LD_LIBRARY_PATH envvar
				// This is optional, some implementations might not use it.
				ld_lib_path = strdup(optarg);
				
				break;
				
			case 'w':
				if (optarg == NULL)
				{
					usage();
					return 1;
				}
				
				// this is the wlm value that we should use
				wlm_arg = atoi(optarg);
				
				break;
				
			case 'h':
				usage();
				return 1;
			default:
				usage();
				return 1;
		}
	}
	
	// Start becoming a daemon
	
	// clear file creation mask
	umask(0);
	
	// get max number of file descriptors
	if (getrlimit(RLIMIT_NOFILE, &rl) < 0)
	{
		// guess the value
		rl.rlim_max = 1024;
	}
	if (rl.rlim_max == RLIM_INFINITY)
	{
		// guess the value
		rl.rlim_max = 1024;
	}
	
	// Ensure every file descriptor is closed
	for (i=0; i < rl.rlim_max; ++i)
	{
		close(i);
	}
	
	/*
	* We close channels 0-2 to keep things "clean".
	* Unfortuantely, that means that any file opens that I do will
	* have them available. If I open my log file, for instance, it
	* will get channel 0 and that just doesn't seem safe. So, the lines
	* below open (and waste) three channels such that I am guaranteed
	* that future opens will not get them 0-2. Note that the opens may
	* or may not get all/any of 0-2, but should they be available, they
	* will be gotten.
	* This, of course, must happen "early" before any other opens.
	*
	*/
	open("/dev/null", O_RDONLY);
	open("/dev/null", O_WRONLY);
	open("/dev/null", O_WRONLY);
	
	// Setup the wlm arg without error checking so that we can create a debug
	// log if asked to. We will error check below.
	switch (wlm_arg)
	{
		case CTI_WLM_CRAY_SLURM:
			_cti_wlmProto = &_cti_cray_slurm_wlmProto;
			break;
		
		case CTI_WLM_SSH:
		case CTI_WLM_SLURM:	
			_cti_wlmProto = &_cti_slurm_wlmProto;
			break;

		case CTI_WLM_NONE:
		default:
			// the wlmProto defaults to noneness, so break
			break;
	}
	
	// if debug mode is turned on, redirect stdout/stderr to a log file
	if (debug_flag)
	{
		int null_apid = 0;
		
		// sanity so that we can write something if we are missing the apid string
		if (apid_str == NULL)
		{
			++null_apid;
			apid_str = strdup("NOAPID");
		}
		
		// setup the log
		log = _cti_create_log(_cti_wlmProto->wlm_getNodeID(), apid_str);
		_cti_hook_stdoe(log);
		
		// cleanup the apid string if it was missing
		if (null_apid)
		{
			free(apid_str);
			apid_str = NULL;
		}
	}

	/* It is NOW safe to write to stdout/stderr, the log file has been setup */
	
	// print out argv array if debug is turned on
	if (debug_flag)
	{
		for (i=0; i < argc; ++i)
		{
			fprintf(stderr, "%s: argv[%d] = \"%s\"\n", CTI_LAUNCHER, i, argv[i]);
		}
	}
	
	// Now ensure the user provided a valid wlm argument. 
	switch (wlm_arg)
	{
		case CTI_WLM_CRAY_SLURM:
		case CTI_WLM_SLURM:
		case CTI_WLM_SSH:
			// These wlm are valid
			break;
		
		case CTI_WLM_NONE:
			// These wlm are not supported
			fprintf(stderr, "%s: WLM provided by wlm argument is not yet supported!\n", CTI_LAUNCHER);
			return 1;
		
		default:
			fprintf(stderr, "%s: Invalid wlm argument.\n", CTI_LAUNCHER);
			return 1;
	}
	
	// Ensure the user provided an apid argument
	if (apid_str == NULL)
	{
		// failure
		fprintf(stderr, "%s: Missing apid argument!\n", CTI_LAUNCHER);
		return 1;
	}
	
	// Ensure the user provided a directory or manifest option
	if (directory == NULL && manifest == NULL)
	{
		// failure
		fprintf(stderr, "%s: Missing either directory or manifest argument!\n", CTI_LAUNCHER);
		return 1;
	}
	
	// Ensure the user provided a toolpath option
	if (tool_path == NULL)
	{
		// failure
		fprintf(stderr, "%s: Missing path argument!\n", CTI_LAUNCHER);
		return 1;
	}
	
	// call the wlm specific init function
	if (_cti_wlmProto->wlm_init())
	{
		// failure
		fprintf(stderr, "%s: wlm_init() failed.\n", CTI_LAUNCHER);
		return 1;
	}
	
	// save the old cwd value into the environment
	if ((cwd = getcwd(NULL, 0)) != NULL)
	{
		if (setenv(OLD_CWD_ENV_VAR, cwd, 1) < 0)
		{
			// failure
			fprintf(stderr, "%s: setenv failed\n", CTI_LAUNCHER);
			return 1;
		}
		free(cwd);
	}
	
	// process the env args
	if (env_args != NULL)
	{
		// pop the top element
		val = (char *)_cti_pop(env_args);
	
		while (val != NULL)
		{
			// we need to strsep the string at the "=" character
			// we expect the user to pass in the -e argument as envVar=val
			// Note that env will now point at the start and val will point at
			// the value argument
			if ((env = strsep(&val, "=")) == NULL)
			{
				//error
				fprintf(stderr, "%s: strsep failed\n", CTI_LAUNCHER);
				return 1;
			}
			
			// ensure the user didn't pass us something stupid i.e. non-conforming
			if ((*env == '\0') || (*val == '\0'))
			{
				// they passed us something stupid
				fprintf(stderr, "%s: Unrecognized env argument.\n", CTI_LAUNCHER);
				return 1;
			}
			
			// set the actual environment variable
			if (setenv(env, val, 1) < 0)
			{
				// failure
				fprintf(stderr, "%s: setenv failed\n", CTI_LAUNCHER);
				return 1;
			}
			
			// free this element
			free(env);
			
			// pop the next element
			val = (char *)_cti_pop(env_args);
		}
		
		// Done with the env_args
		_cti_consumeStack(env_args);
	}
	
	// set the APID_ENV_VAR environment variable to the apid
	if (setenv(APID_ENV_VAR, apid_str, 1) < 0)
	{
		// failure
		fprintf(stderr, "%s: setenv failed\n", CTI_LAUNCHER);
		return 1;
	}
	
	// set the WLM_ENV_VAR environment variable to the wlm
	if (asprintf(&wlm_str, "%d", _cti_wlmProto->wlm_type) <= 0)
	{
		fprintf(stderr, "%s: asprintf failed\n", CTI_LAUNCHER);
		return 1;
	}
	if (setenv(WLM_ENV_VAR, wlm_str, 1) < 0)
	{
		// failure
		fprintf(stderr, "%s: setenv failed\n", CTI_LAUNCHER);
		return 1;
	}
	free(wlm_str);
	
	// cd to the tool_path and relax the permissions
	if (debug_flag)
	{
		fprintf(stderr, "%s: inst %d: Toolhelper path: %s\n", CTI_LAUNCHER, inst, tool_path);
	}
	
	if (stat(tool_path, &statbuf) == -1)
	{
		fprintf(stderr, "%s: Could not stat %s\n", CTI_LAUNCHER, tool_path);
		return 1;
	}
	
	// Ensure we can write into this directory, otherwise try to relax the permissions
	if (getuid() == statbuf.st_uid)
	{
		// we are the owner - check for RWX
		if ((statbuf.st_mode & S_IRWXU) ^ S_IRWXU)
		{
			// missing perms, so chmod since we are owner
			if (chmod(tool_path, statbuf.st_mode | S_IRWXU) != 0)
			{
				fprintf(stderr, "%s: Could not chmod %s\n", CTI_LAUNCHER, tool_path);
				return 1;
			}
		}
	} else
	{
		// here we cannot chmod, so we need to check for perms on the directory
		// and fail if we don't have the setup properly for us. In that case, the
		// system is misconfigured.
		int 	ngrps;
		gid_t *	grps;
		int		i,chk=0;
		
		if ((ngrps = getgroups(0, NULL)) < 0)
		{
			fprintf(stderr, "%s: ", CTI_LAUNCHER);
			perror("getgroups");
			return 1;
		}
		if ((grps = malloc(ngrps * sizeof(gid_t))) == NULL)
		{
			fprintf(stderr, "%s: malloc failed.", CTI_LAUNCHER);
			return 1;
		}
		if ((ngrps = getgroups(ngrps, grps)) < 0)
		{
			fprintf(stderr, "%s: ", CTI_LAUNCHER);
			perror("getgroups");
			return 1;
		}
		
		// check to see if we are in the group
		for (i=0; i < ngrps; ++i)
		{
			if (grps[i] == statbuf.st_gid)
			{
				// we are in the group - check for RWX
				if ((statbuf.st_mode & S_IRWXG) ^ S_IRWXG)
				{
					// missing perms, no way to recover
					fprintf(stderr, "%s: Missing group perms in %s\n", CTI_LAUNCHER, tool_path);
					return 1;
				}
				// break from the for loop
				++chk;
				break;
			}
		}
		
		// if chk is true, we are in the group and have proper perms, otherwise check other
		if (!chk)
		{
			if ((statbuf.st_mode & S_IRWXO) ^ S_IRWXO)
			{
				// missing perms, no way to recover
				fprintf(stderr, "%s: Missing others perms in %s\n", CTI_LAUNCHER, tool_path);
				return 1;
			}
		}
	}
	
	// change the working directory to path
	if (chdir(tool_path) != 0)
	{
		fprintf(stderr, "%s: Could not chdir to %s\n", CTI_LAUNCHER, tool_path);
		return 1;
	}
	
	// Now unpack the manifest
	if (manifest != NULL)
	{
		if (debug_flag)
		{
			fprintf(stderr, "%s: inst %d: Manifest provided: %s\n", CTI_LAUNCHER, inst, manifest);
		}
	
		// create the manifest path string
		if (asprintf(&manifest_path, "%s/%s", tool_path, manifest) <= 0)
		{
			fprintf(stderr, "%s: asprintf failed\n", CTI_LAUNCHER);
			return 1;
		}
		
		// ensure the manifest tarball exists
		if (stat(manifest_path, &statbuf) == -1)
		{
			fprintf(stderr, "%s: Could not stat manifest tarball %s\n", CTI_LAUNCHER, manifest_path);
			return 1;
		}
		
		// ensure it is a regular file
		if (!S_ISREG(statbuf.st_mode))
		{
			fprintf(stderr, "%s: %s is not a regular file!\n", CTI_LAUNCHER, manifest_path);
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
			fprintf(stderr, "%s: archive_read_open_filename(): %s\n", CTI_LAUNCHER, archive_error_string(a));
			return r;
		}
	
		for (;;) 
		{
			r = archive_read_next_header(a, &entry);
			if (r == ARCHIVE_EOF)
				break;
			if (r != ARCHIVE_OK)
			{
				fprintf(stderr, "%s: archive_read_next_header(): %s\n", CTI_LAUNCHER, archive_error_string(a));
				return 1;
			}
		
			r = archive_write_header(ext, entry);
			if (r != ARCHIVE_OK)
			{
				fprintf(stderr, "%s: archive_write_header(): %s\n", CTI_LAUNCHER, archive_error_string(ext));
				return 1;
			}
		
			copy_data(a, ext);
		
			r = archive_write_finish_entry(ext);
			if (r != ARCHIVE_OK)
			{
				fprintf(stderr, "%s: archive_write_finish_entry(): %s\n", CTI_LAUNCHER, archive_error_string(ext));
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
		if (debug_flag)
		{
			fprintf(stderr, "%s: inst %d: Directory provided: %s\n", CTI_LAUNCHER, inst, directory);
		}
		
		// create the manifest path string
		if (manifest_path != NULL)
		{
			free(manifest_path);
			manifest_path = NULL;
		}
		if (asprintf(&manifest_path, "%s/%s", tool_path, directory) <= 0)
		{
			fprintf(stderr, "%s: asprintf failed\n", CTI_LAUNCHER);
			return 1;
		}
	}
	
	// ensure the manifest directory exists
	if (stat(manifest_path, &statbuf) == -1)
	{
		fprintf(stderr, "%s: Could not stat root directory %s\n", CTI_LAUNCHER, manifest_path);
		return 1;
	}
	
	// ensure it is a directory
	if (!S_ISDIR(statbuf.st_mode))
	{
		fprintf(stderr, "%s: %s is not a directory!\n", CTI_LAUNCHER, manifest_path);
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
	
	// create the path to the pid file
	if (asprintf(&pid_path, "%s/%s", manifest_path, PID_FILE) <= 0)
	{
		fprintf(stderr, "%s: asprintf failed\n", CTI_LAUNCHER);
		return 1;
	}
	
	// TODO: Terminate here
	if (cleanup)
	{
		struct cti_pids tool_pid = {{0}};
		struct cti_pids * pid_ptr = &tool_pid;
		ssize_t nr;
		
		if ((o_fd = open(pid_path, O_RDONLY)) < 0)
		{
			fprintf(stderr, "%s: open failed\n", CTI_LAUNCHER);
			perror("open");
			return 1;
		}
		
		// lock the file
		FLOCK(o_fd);
		
		do {
			nr = read(o_fd, &pid_ptr->pids[pid_ptr->idx], sizeof(pid_ptr->pids) - ((sizeof(pid_ptr->pids)/sizeof(pid_ptr->pids[0])) * pid_ptr->idx));
			if (nr < 0)
			{
				fprintf(stderr, "%s: read failed\n", CTI_LAUNCHER);
				perror("read");
				return 1;
			}
			if (nr > 0)
			{
				pid_ptr->idx = nr / sizeof(pid_ptr->pids[0]);
				if (pid_ptr->idx >= sizeof(pid_ptr->pids)/sizeof(pid_ptr->pids[0]))
				{
					// need to grow
					if ((pid_ptr->next = malloc(sizeof(struct cti_pids))) == NULL)
					{
						fprintf(stderr, "%s: malloc failed\n", CTI_LAUNCHER);
						return 1;
					}
					memset(pid_ptr->next, 0, sizeof(struct cti_pids));
					pid_ptr = pid_ptr->next;
				}
			}
		} while (nr != 0);
		
		// send a SIGTERM to each pid
		pid_ptr = &tool_pid;
		while (pid_ptr != NULL)
		{
			for (i=0; i < pid_ptr->idx; ++i)
			{
				if (kill(pid_ptr->pids[i], 0))
					continue;
				fprintf(stderr, "%s: inst %d: Sending SIGTERM to %d\n", CTI_LAUNCHER, inst, pid_ptr->pids[i]);
				kill(pid_ptr->pids[i], SIGTERM);
			}
			pid_ptr = pid_ptr->next;
		}
		
		// sleep for 10 seconds
		fprintf(stderr, "%s: inst %d: Sleeping for 10 seconds...\n", CTI_LAUNCHER, inst);
		sleep(10);
		
		// send a SIGKILL to each pid
		pid_ptr = &tool_pid;
		while (pid_ptr != NULL)
		{
			for (i=0; i < pid_ptr->idx; ++i)
			{
				if (kill(pid_ptr->pids[i], 0))
					continue;
				fprintf(stderr, "%s: inst %d: Sending SIGKILL to %d\n", CTI_LAUNCHER, inst, pid_ptr->pids[i]);
				kill(pid_ptr->pids[i], SIGKILL);
			}
			pid_ptr = pid_ptr->next;
		}
		
		// remove the manifest directory
		fprintf(stderr, "%s: inst %d: Removing directory %s.\n", CTI_LAUNCHER, inst, manifest_path);
		remove_dir(manifest_path);
		
		// remove the lock files
		for (i = inst-1; i > 0; --i)
		{
			// reset sCnt
			sCnt = 0;
		
			// create the path to this instances lock file
			if (asprintf(&lock_path, "%s/.lock_%s_%d", tool_path, directory, i) <= 0)
			{
				fprintf(stderr, "%s: asprintf failed\n", CTI_LAUNCHER);
				return 1;
			}
			
			unlink(lock_path);
			free(lock_path);
		}
		
		fprintf(stderr, "%s: inst %d: Cleanup complete.\n", CTI_LAUNCHER, inst);
		
		return 0;
	}
	
	// We want to write the pid of this process into the pid file for cleanup
	// later. Note that if the destroy session function is called, it will need
	// to wait for this instances lock file to get created, which happens below.
	// That way we are guaranteed to not miss any tool daemons that were started.
	
	// Write this pid into the pid file for cleanup
	if ((o_fd = open(pid_path, O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR)) < 0)
	{
		fprintf(stderr, "%s: open failed\n", CTI_LAUNCHER);
		perror("open");
		return 1;
	}
	
	// lock the file
	FLOCK(o_fd);
	
	// get our pid and write it
	mypid = getpid();
	if (write(o_fd, &mypid, sizeof(mypid)) < sizeof(mypid))
	{
		fprintf(stderr, "%s: write failed\n", CTI_LAUNCHER);
		perror("write");
		return 1;
	}
	
	// done
	UNFLOCK(o_fd);
	close(o_fd);
	
	// create the path to the lock file
	if (asprintf(&lock_path, "%s/.lock_%s_%d", tool_path, directory, inst) <= 0)
	{
		fprintf(stderr, "%s: asprintf failed\n", CTI_LAUNCHER);
		return 1;
	}
	
	// try to fopen the lock file
	if ((lock_file = fopen(lock_path, "w")) == NULL)
	{
		fprintf(stderr, "%s: fopen on %s failed\n", CTI_LAUNCHER, lock_path);
		// don't exit here, this will break future tool daemons though so its pretty
		// bad to have a failure at this point. But it shouldn't screw this instance
		// up at the very least.
	} else
	{
		// close the file, it has been created
		fclose(lock_file);
	}
	
	// Set the BE_GUARD_ENV_VAR environment variable to signal that we are running
	// on the backend
	if (setenv(BE_GUARD_ENV_VAR, "1", 1) < 0)
	{
		// failure
		fprintf(stderr, "%s: setenv failed\n", CTI_LAUNCHER);
		return 1;
	}
	
	// Set the ROOT_DIR_VAR environment variable to the manifest directory.
	if (setenv(ROOT_DIR_VAR, manifest_path, 1) < 0)
	{
		// failure
		fprintf(stderr, "%s: setenv failed\n", CTI_LAUNCHER);
		return 1;
	}
	
	// Get the current value of the SCRATCH_ENV_VAR environment variable and set
	// it to OLD_SCRATCH_ENV_VAR in case the tool daemon needs access to it.
	if ((old_env_path = getenv(SCRATCH_ENV_VAR)) != NULL)
	{
		if (setenv(OLD_SCRATCH_ENV_VAR, old_env_path, 1) < 0)
		{
			// failure
			fprintf(stderr, "%s: setenv failed\n", CTI_LAUNCHER);
			return 1;
		}
	}

	// set the SCRATCH_ENV_VAR environment variable to the toolhelper directory.
	// ALPS will enforce cleanup here and the tool is guaranteed to be able to write
	// to it.
	if (asprintf(&env_path, "%s/tmp", manifest_path) <= 0)
	{
		fprintf(stderr, "%s: asprintf failed\n", CTI_LAUNCHER);
		return 1;
	}
	if (setenv(SCRATCH_ENV_VAR, env_path, 1) < 0)
	{
		// failure
		fprintf(stderr, "%s: setenv failed\n", CTI_LAUNCHER);
		return 1;
	}
	
	// set the BIN_DIR_VAR environment variable to the toolhelper directory.
	if (asprintf(&bin_path, "%s/bin", manifest_path) <= 0)
	{
		fprintf(stderr, "%s: asprintf failed\n", CTI_LAUNCHER);
		return 1;
	}
	if (setenv(BIN_DIR_VAR, bin_path, 1) < 0)
	{
		// failure
		fprintf(stderr, "%s: setenv failed\n", CTI_LAUNCHER);
		return 1;
	}
	
	// set the LIB_DIR_VAR environment variable to the toolhelper directory.
	if (asprintf(&lib_path, "%s/lib", manifest_path) <= 0)
	{
		fprintf(stderr, "%s: asprintf failed\n", CTI_LAUNCHER);
		return 1;
	}
	if (setenv(LIB_DIR_VAR, lib_path, 1) < 0)
	{
		// failure
		fprintf(stderr, "%s: setenv failed\n", CTI_LAUNCHER);
		return 1;
	}
	
	// set FILE_DIR_VAR environment variable to the toolhelper directory
	if (asprintf(&file_path, "%s", manifest_path) <= 0)
	{
		fprintf(stderr, "%s: asprintf failed\n", CTI_LAUNCHER);
		return 1;
	}
	if (setenv(FILE_DIR_VAR, file_path, 1) < 0)
	{
		// failure
		fprintf(stderr, "%s: setenv failed\n", CTI_LAUNCHER);
		return 1;
	}
	
	// set TOOL_DIR_VAR to tool_path - This should remain hidden from the user and only used by internal library calls
	if (setenv(TOOL_DIR_VAR, tool_path, 1) < 0)
	{
		// failure
		fprintf(stderr, "%s: setenv failed\n", CTI_LAUNCHER);
		return 1;
	}
	
	// set PMI_ATTRIBS_DIR_VAR to attribs_path if there is one. This is optional.
	if (attribs_path != NULL)
	{
		if (setenv(PMI_ATTRIBS_DIR_VAR, attribs_path, 1) < 0)
		{
			// failure
			fprintf(stderr, "%s: setenv failed\n", CTI_LAUNCHER);
			return 1;
		}
	}
	
	// call _cti_adjustPaths so that we chdir to where we shipped stuff over to and setup PATH/LD_LIBRARY_PATH
	if (_cti_adjustPaths(manifest_path, ld_lib_path))
	{
		fprintf(stderr, "%s: Could not adjust paths.\n", CTI_LAUNCHER);
		free(tool_path);
		return 1;
	}
	
	// ensure that binary was provided, otherwise just exit - the caller wanted to stage stuff.
	if (binary == NULL)
	{
		fprintf(stderr, "%s: inst %d: No binary provided. Stage to %s complete.\n", CTI_LAUNCHER, inst, manifest_path);
		return 0;
	}
	
	// anything after the final "--" in the options string will be passed directly to the exec'ed binary
	
	// create the full path to the binary we are going to exec
	if (asprintf(&binary_path, "%s/bin/%s", manifest_path, binary) <= 0)
	{
		fprintf(stderr, "%s: asprintf failed\n", CTI_LAUNCHER);
		return 1;
	}
	
	if (debug_flag)
	{
		fprintf(stderr, "%s: inst %d: Binary path: %s\n", CTI_LAUNCHER, inst, binary_path);
	}
	
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
			fprintf(stderr, "%s: asprintf failed\n", CTI_LAUNCHER);
			return 1;
		}
		
		// loop until we can stat this lock file
		while (stat(lock_path, &statbuf))
		{
			// print out a message if sCnt is divisible by 100
			if (debug_flag)
			{
				if (sCnt++%100 == 0)
				{
					fprintf(stderr, "%s: inst %d: Lock file %s not found. Sleeping...\n", CTI_LAUNCHER, inst, lock_path);
				}
			}
			
			// sleep until the file is created
			usleep(10000);
		}
	}
	
	if (debug_flag)
	{
		fprintf(stderr, "%s: inst %d: All dependency locks acquired. Ready to exec.\n", CTI_LAUNCHER, inst);
	}
	
	// At this point it is safe to assume we have all our dependencies.
	
	// ensure the binary exists
	if (stat(binary_path, &statbuf) == -1)
	{
		fprintf(stderr, "%s: Could not stat %s\n", CTI_LAUNCHER, binary_path);
		return 1;
	}
	
	// ensure it is a regular file
	if (!S_ISREG(statbuf.st_mode))
	{
		fprintf(stderr, "%s: %s is not a regular file!\n", CTI_LAUNCHER, binary_path);
		return 1;
	}
	
	// setup the new argv array
	// Note that argv[optind] is the first argument that appears after the "--" terminator
	// We need to modify what argv[optind - 1] points to so that it follows the standard argv[0] 
	// nomenclature.
	argv[optind - 1] = binary_path;
	
	// now we can exec our program
	execv(binary_path, &argv[optind - 1]);
	
	fprintf(stderr, "%s: inst %d: Return from exec!\n", CTI_LAUNCHER, inst);
	
	perror("execv");
	
	return 1;
}

/* Noneness functions for wlm proto */

int
_cti_wlm_init_none(void)
{
	fprintf(stderr, "%s: wlm_init() not supported.", CTI_LAUNCHER);
	return 1;
}

int
_cti_wlm_getNodeID_none(void)
{
	fprintf(stderr, "%s: wlm_getNodeID() not supported.", CTI_LAUNCHER);
	return -1;
}

