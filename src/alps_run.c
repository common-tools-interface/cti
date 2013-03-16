/******************************************************************************\
 * alps_run.c - A interface to launch and interact with aprun sessions. This
 *	      provides the tool developer with an easy to use interface to
 *	      start new instances of an aprun program and get the pid_t of
 *	      the associated aprun session.
 *
 * © 2011-2013 Cray Inc.  All Rights Reserved.
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
 
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */
 
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include <linux/limits.h>

#include "alps_run.h"
#include "alps_application.h"

/* static prototypes */
static void consumeAprunInv(aprunInv_t *);
static aprunInv_t * findAprunInv(uint64_t);
static int	checkPathForWrappedAprun(char *);

// global aprunInv_t list
static aprunInv_t *	head = (aprunInv_t *)NULL;

void
reapAprunInv(uint64_t apid)
{
	aprunInv_t * appPtr;
	aprunInv_t * prePtr;
	
	// sanity check
	if (((appPtr = head) == (aprunInv_t *)NULL) || (apid <= 0))
		return;
	
	prePtr = head;
	
	// find the aprunInv_t entry in the global list
	while (appPtr->apid != apid)
	{
		prePtr = appPtr;
		if ((appPtr = appPtr->next) == (aprunInv_t *)NULL)
		{
			// on last entry and apid not found
			return;
		}
	}
	
	// check to see if we are the first entry in the global list
	if (appPtr == prePtr)
	{
		// first entry in list is a match
		head = appPtr->next;
		// consume the actual entry
		consumeAprunInv(appPtr);
	} else
	{
		// we are in the middle of the list
		
		// jump over the appPtr
		prePtr->next = appPtr->next;
		// consume the appPtr
		consumeAprunInv(appPtr);
	}
}

static void
consumeAprunInv(aprunInv_t *app)
{
	// close the open pipe fds
	close(app->pipeCtl.pipe_r);
	close(app->pipeCtl.pipe_w);
	
	// free the object from memory
	free(app);
}

static aprunInv_t *
findAprunInv(uint64_t apid)
{
	aprunInv_t * appPtr;

	// find the aprunInv_t entry in the global list
	if (((appPtr = head) == (aprunInv_t *)NULL) || (apid <= 0))
		return (aprunInv_t *)NULL;
		
	while (appPtr->apid != apid)
	{
		if ((appPtr = appPtr->next) == (aprunInv_t *)NULL)
		{
			// entry not found
			return (aprunInv_t *)NULL;
		}
	}

	return appPtr;
}

static int	
checkPathForWrappedAprun(char *aprun_path)
{
	char *			usr_aprun_path;
	struct stat		buf;
	
	// The following is used when a user sets the CRAY_APRUN_PATH environment
	// variable to the absolute location of aprun. It overrides the default
	// behavior.
	if ((usr_aprun_path = getenv(USER_DEF_APRUN_LOCATION)) != NULL)
	{
		// There is a path to aprun set, lets try to stat it to make sure it
		// exists
		if (stat(usr_aprun_path, &buf) == 0)
		{
			// We were able to stat it! Lets check aprun_path against it
			if (strncmp(aprun_path, usr_aprun_path, strlen(usr_aprun_path)))
			{
				// This is a wrapper. Return 1.
				return 1;
			}
			
			// This is a real aprun. Return 0.
			return 0;
		} else
		{
			// We were unable to stat the file pointed to by usr_aprun_path, lets
			// print a warning and fall back to using the default method.
			fprintf(stderr, "Warning: CRAY_APRUN_PATH is set but cannot stat its value.\n");
		}
	}
	
	// check to see if the path points at the old aprun location
	if (strncmp(aprun_path, OLD_APRUN_LOCATION, strlen(OLD_APRUN_LOCATION)))
	{
		// it doesn't point to the old aprun location, so check the new OBS
		// location
		if (strncmp(aprun_path, OBS_APRUN_LOCATION, strlen(OBS_APRUN_LOCATION)))
		{
			// This is a wrapper. Return 1.
			return 1;
		}
	}
	
	// This is a real aprun, return 0
	return 0;
}

static int
filter_pid_entries(const struct dirent *a)
{
	unsigned long int pid;
	
	// We only want to get files that are of the format /proc/<pid>/
	// if the assignment succeeds then the file matches this type.
	return sscanf(a->d_name, "%lu", &pid);
}

aprunProc_t	*
launchAprun_barrier(	char **aprun_argv, int redirectOutput, int redirectInput, 
						int stdout_fd, int stderr_fd, char *inputFile, char *chdirPath,
						char **env_list	)
{
	aprunInv_t *	myapp;
	aprunInv_t *	newapp;
	pid_t			mypid;
	char **			tmp;
	int				aprun_argc = 0;
	int				fd_len = 0;
	int				i, j, fd;
	char *			pipefd_buf;
	char **			my_argv;
	// pipes for aprun
	int				aprunPipeR[2];
	int				aprunPipeW[2];
	// used for determining if the aprun binary is a wrapper script
	char *			aprun_proc_path = NULL;
	char *			aprun_exe_path;
	struct dirent 	**file_list;
	int				file_list_len;
	char *			proc_stat_path = NULL;
	FILE *			proc_stat = NULL;
	int				proc_ppid;
	// return object
	aprunProc_t *	rtn;

	// create a new aprunInv_t object
	if ((myapp = malloc(sizeof(aprunInv_t))) == (void *)0)
	{
		// Malloc failed
		return NULL;
	}
	memset(myapp, 0, sizeof(aprunInv_t));     // clear it to NULL
	
	// make the pipes for aprun (tells aprun to hold the program at the initial 
	// barrier)
	if (pipe(aprunPipeR) < 0)
	{
		fprintf(stderr, "Pipe creation failure on aprunPipeR.\n");
		free(myapp);
		return NULL;
	}
	if (pipe(aprunPipeW) < 0)
	{
		fprintf(stderr, "Pipe creation failure on aprunPipeW.\n");
		free(myapp);
		return NULL;
	}
	
	// set my ends of the pipes in the aprunInv_t structure
	myapp->pipeCtl.pipe_r = aprunPipeR[1];
	myapp->pipeCtl.pipe_w = aprunPipeW[0];
	
	// create the argv array for the actual aprun exec
	// figure out the length of the argv array
	// this is the number of args in the aprun_argv array passed to us plus 2 
	// for the -P w,r argument and 2 for aprun and null term
		
	// iterate through the aprun_argv array
	tmp = aprun_argv;
	while (*tmp++ != (char *)NULL)
	{
		++aprun_argc;
	}
		
	// allocate the new argv array. Need additional entry for null terminator
	if ((my_argv = calloc(aprun_argc+4, sizeof(char *))) == (void *)0)
	{
		// calloc failed
		free(myapp);
		return NULL;
	}
		
	// add the initial aprun argv
	my_argv[0] = strdup(APRUN);
	
	// Add the -P r,w args
	my_argv[1] = strdup("-P");
	
	// determine length of the fds
	j = aprunPipeR[0];
	do{
		++fd_len;
	} while (j/=10);
		
	j = aprunPipeW[1];
	do{
		++fd_len;
	} while (j/=10);
	
	// need a final char for comma and null terminator
	fd_len += 2;
	
	// allocate space for the buffer including the terminating zero
	if ((pipefd_buf = malloc(sizeof(char)*fd_len)) == (void *)0)
	{
		// malloc failed
		free(myapp);
		// cleanup my_argv array
		tmp = my_argv;
		while (*tmp != (char *)NULL)
		{
			free(*tmp++);
		}
		free(my_argv);
		return NULL;
	}
	
	// write the buffer
	snprintf(pipefd_buf, fd_len, "%d,%d", aprunPipeW[1], aprunPipeR[0]);
	
	my_argv[2] = pipefd_buf;
	
	// set the argv array for aprun
	// here we expect the final argument to be the program we wish to start
	// and we need to add our -P r,w argument before this happens
	for (i=3; i < aprun_argc+3; i++)
	{
		my_argv[i] = strdup(aprun_argv[i-3]);
	}
	
	// add the null terminator
	my_argv[i++] = (char *)NULL;
	
	// fork off a process to launch aprun
	mypid = fork();
	
	// error case
	if (mypid < 0)
	{
		fprintf(stderr, "Fatal fork error.\n");
		free(myapp);
		// cleanup my_argv array
		tmp = my_argv;
		while (*tmp != (char *)NULL)
		{
			free(*tmp++);
		}
		free(my_argv);
		return NULL;
	}
	
	// child case
	if (mypid == 0)
	{
		// close unused ends of pipe
		close(aprunPipeR[1]);
		close(aprunPipeW[0]);
		
		if (redirectInput)
		{
			// open the provided input file if non-null and redirect it to
			// stdin
			if (inputFile == (char *)NULL)
			{
				fprintf(stderr, "Provided inputFile argument is null.\n");
				exit(1);
			}
			if ((fd = open(inputFile, O_RDONLY)) < 0)
			{
				fprintf(stderr, "Unable to open %s for reading.\n", inputFile);
				exit(1);
			}
		} else
		{
			// we don't want this aprun to suck up stdin of the tool program
			if ((fd = open("/dev/null", O_RDONLY)) < 0)
			{
				fprintf(stderr, "Unable to open /dev/null for reading.\n");
				exit(1);
			}
		}
		
		// dup2 the fd onto STDIN_FILENO
		if (dup2(fd, STDIN_FILENO) < 0)
		{
			fprintf(stderr, "Unable to redirect aprun stdin.\n");
			exit(1);
		}
		close(fd);
		
		// redirect stdout/stderr if directed
		if (redirectOutput)
		{
			// dup2 stdout
			if (dup2(stdout_fd, STDOUT_FILENO) < 0)
			{
				fprintf(stderr, "Unable to redirect aprun stdout.\n");
				exit(1);
			}
			
			// dup2 stderr
			if (dup2(stderr_fd, STDERR_FILENO) < 0)
			{
				fprintf(stderr, "Unable to redirect aprun stderr.\n");
				exit(1);
			}
		}
		
		// chdir if directed
		if (chdirPath != (char *)NULL)
		{
			if (chdir(chdirPath))
			{
				fprintf(stderr, "Unable to chdir to provided path.\n");
				exit(1);
			}
		}
		
		// if env_list is not null, call putenv for each entry in the list
		if (env_list != (char **)NULL)
		{
			i = 0;
			while(env_list[i] != (char *)NULL)
			{
				// putenv returns non-zero on error
				if (putenv(env_list[i++]))
				{
					fprintf(stderr, "Unable to putenv provided env_list.\n");
					exit(1);
				}
			}
		}

		// exec aprun
		execvp(APRUN, my_argv);
		
		// exec shouldn't return
		fprintf(stderr, "Return from exec.\n");
		exit(1);
	}
	
	// parent case
	
	// close unused ends of pipe
	close(aprunPipeR[0]);
	close(aprunPipeW[1]);
	
	// cleanup my_argv array
	tmp = my_argv;
	while (*tmp != (char *)NULL)
	{
		free(*tmp++);
	}
	free(my_argv);
	
	// Wait on pipe read for app to start and get to barrier - once this happens
	// we know the real aprun is up and running
	if (read(myapp->pipeCtl.pipe_w, &myapp->pipeCtl.sync_int, sizeof(myapp->pipeCtl.sync_int)) <= 0)
	{
		fprintf(stderr, "Aprun launch failed.\n");
		// attempt to kill aprun since the caller will not recieve the aprun pid
		// just in case the aprun process is still hanging around.
		kill(mypid, DEFAULT_SIG);
		free(myapp);
		return NULL;
	}
	
	// The following code was added to detect if a site is using a wrapper script
	// around aprun. Some sites use these as prologue/epilogue. I know this
	// functionality has been added to alps, but sites are still using the
	// wrapper. If this is no longer true in the future, rip this stuff out.
	
	// FIXME: This doesn't handle multiple layers of depth.
	
	// first read the link of the exe in /proc for the aprun pid.
	
	// create the path to the /proc/<pid>/exe location
	if (asprintf(&aprun_proc_path, "/proc/%lu/exe", (unsigned long)mypid) < 0)
	{
		fprintf(stderr, "asprintf failed.\n");
		goto continue_on_error;
	}
	
	// alloc size for the path buffer, base this on PATH_MAX. Note that /proc
	// is not posix compliant so trying to do the right thing by calling lstat
	// won't work.
	if ((aprun_exe_path = malloc(PATH_MAX)) == (void *)0)
	{
		// Malloc failed
		fprintf(stderr, "malloc failed.\n");
		free(aprun_proc_path);
		goto continue_on_error;
	}
	// set it to null, this also guarantees that we will have a null terminator.
	memset(aprun_exe_path, 0, PATH_MAX);
	
	// read the link
	if (readlink(aprun_proc_path, aprun_exe_path, PATH_MAX-1) < 0)
	{
		fprintf(stderr, "Warning: readlink failed on aprun /proc/<pid>/exe.\n");
		free(aprun_proc_path);
		free(aprun_exe_path);
		goto continue_on_error;
	}
	
	// check the link path to see if its the real aprun binary
	if (checkPathForWrappedAprun(aprun_exe_path))
	{
		// aprun is wrapped, we need to start harvesting stuff out from /proc.
		
		// start by getting all the /proc/<pid>/ files
		if ((file_list_len = scandir("/proc", &file_list, filter_pid_entries, NULL)) < 0)
		{
			perror("scandir");
			fprintf(stderr, "Failure: Could not enumerate /proc for real aprun process.\n");
			free(aprun_proc_path);
			free(aprun_exe_path);
			// attempt to kill aprun since the caller will not recieve the aprun pid
			// just in case the aprun process is still hanging around.
			kill(mypid, DEFAULT_SIG);
			free(myapp);
			return NULL;
		}
		
		// loop over each entry reading in its ppid from its stat file
		for (i=0; i <= file_list_len; ++i)
		{
			// if i is equal to file_list_len, then we are at an error condition
			// we did not find the child aprun process. We should error out at
			// this point since we will error out later in an alps call anyways.
			if (i == file_list_len)
			{
				fprintf(stderr, "Failure: Could not find child aprun process of wrapped aprun command.\n");
				free(aprun_proc_path);
				free(aprun_exe_path);
				// attempt to kill aprun since the caller will not recieve the aprun pid
				// just in case the aprun process is still hanging around.
				kill(mypid, DEFAULT_SIG);
				free(myapp);
				// free the file_list
				for (i=0; i < file_list_len; ++i)
				{
					free(file_list[i]);
				}
				free(file_list);
				return NULL;
			}
		
			// create the path to the /proc/<pid>/stat for this entry
			if (asprintf(&proc_stat_path, "/proc/%s/stat", (file_list[i])->d_name) < 0)
			{
				fprintf(stderr, "asprintf failed.\n");
				fprintf(stderr, "Failure: Could not enumerate /proc/<pid>/stat for real aprun process.\n");
				free(aprun_proc_path);
				free(aprun_exe_path);
				// attempt to kill aprun since the caller will not recieve the aprun pid
				// just in case the aprun process is still hanging around.
				kill(mypid, DEFAULT_SIG);
				free(myapp);
				// free the file_list
				for (i=0; i < file_list_len; ++i)
				{
					free(file_list[i]);
				}
				free(file_list);
				return NULL;
			}
			
			// open the stat file for reading
			if ((proc_stat = fopen(proc_stat_path, "r")) == NULL)
			{
				// ignore this entry and go onto the next
				free(proc_stat_path);
				proc_stat_path = NULL;
				continue;
			}
			
			// free the proc_stat_path
			free(proc_stat_path);
			proc_stat_path = NULL;
			
			// parse the stat file for the ppid
			if (fscanf(proc_stat, "%*d %*s %*c %d", &proc_ppid) != 1)
			{
				// could not get the ppid?? continue to the next entry
				fclose(proc_stat);
				proc_stat = NULL;
				continue;
			}
			
			// close the stat file
			fclose(proc_stat);
			proc_stat = NULL;
			
			// check to see if the ppid matches the pid of our child
			if (proc_ppid == mypid)
			{
				// it matches, check to see if this is the real aprun
				
				// free the existing aprun_proc_path
				free(aprun_proc_path);
				aprun_proc_path = NULL;
				
				// allocate the new aprun_proc_path
				if (asprintf(&aprun_proc_path, "/proc/%s/exe", (file_list[i])->d_name) < 0)
				{
					fprintf(stderr, "asprintf failed.\n");
					fprintf(stderr, "Failure: Could not enumerate /proc/<pid>/exe for real aprun process.\n");
					free(aprun_proc_path);
					free(aprun_exe_path);
					// attempt to kill aprun since the caller will not recieve the aprun pid
					// just in case the aprun process is still hanging around.
					kill(mypid, DEFAULT_SIG);
					free(myapp);
					// free the file_list
					for (i=0; i < file_list_len; ++i)
					{
						free(file_list[i]);
					}
					free(file_list);
					return NULL;
				}
				
				// reset aprun_exe_path to null.
				memset(aprun_exe_path, 0, PATH_MAX);
				
				// read the exe link to get what its pointing at
				if (readlink(aprun_proc_path, aprun_exe_path, PATH_MAX-1) < 0)
				{
					// if the readlink failed, ignore the error and continue to
					// the next entry. Its possible that this could fail under
					// certain scenarios like the process is running as root.
					continue;
				}
				
				// check if this is the real aprun
				if (!checkPathForWrappedAprun(aprun_exe_path))
				{
					// success! This is the real aprun
					// set the aprunPid of the real aprun in the aprunInv_t structure
					myapp->aprunPid = (pid_t)strtoul((file_list[i])->d_name, NULL, 10);
					
					// cleanup memory
					free(aprun_proc_path);
					free(aprun_exe_path);
					
					// free the file_list
					for (i=0; i < file_list_len; ++i)
					{
						free(file_list[i]);
					}
					free(file_list);
					// done
					break;
				}
			}
		}
	} else
	{
		// cleanup memory
		free(aprun_proc_path);
		free(aprun_exe_path);
	
continue_on_error:
		// set aprunPid in aprunInv_t structure
		myapp->aprunPid = mypid;
	}
	
	// insert myapp into the global list
	if (head == (aprunInv_t *)NULL)
	{
		head = myapp;
	} else
	{
		// iterate through until we find an empty next entry
		newapp = head;
		while (newapp->next != (aprunInv_t *)NULL)
		{
			newapp = newapp->next;
		}
		// set the next entry to myapp
		newapp->next = myapp;
	}
	
	// set the apid associated with the pid of aprun
	if ((myapp->apid = getApid(myapp->aprunPid)) == 0)
	{
		fprintf(stderr, "Could not obtain apid associated with pid of aprun.\n");
		// attempt to kill aprun since the caller will not recieve the aprun pid
		// just in case the aprun process is still hanging around.
		kill(myapp->aprunPid, DEFAULT_SIG);
		free(myapp);
		return NULL;
	}
	
	// register this process with the alps_transfer interface
	if (registerApid(myapp->apid))
	{
		// we failed to register our app
		fprintf(stderr, "Could not register application!\n");
	}
	
	// create a new aprunProc_t return object
	if ((rtn = malloc(sizeof(aprunProc_t))) == (void *)0)
	{
		// Malloc failed
		return NULL;
	}
	
	// set the return object members
	rtn->apid = myapp->apid;
	rtn->aprunPid = myapp->aprunPid;
	
	// return the apid and the pid of the aprun process we forked
	return rtn;
}

int
releaseAprun_barrier(uint64_t apid)
{
	aprunInv_t * appPtr;
	
	// sanity check
	if (apid <= 0)
		return 1;
	
	// find the aprunInv_t entry in the global list
	if ((appPtr = findAprunInv(apid)) == (aprunInv_t *)NULL)
		return 1;
	
	// Conduct a pipe write for alps to release app from the startup barrier.
	// Just write back what we read earlier.
	if (write(appPtr->pipeCtl.pipe_r, &appPtr->pipeCtl.sync_int, sizeof(appPtr->pipeCtl.sync_int)) <= 0)
	{
		fprintf(stderr, "Aprun barrier release operation failed.\n");
		return 1;
	}
	
	// done
	return 0;
}

int
killAprun(uint64_t apid, int signum)
{
	int          mypid;
	uint64_t     i;
	int          sig, j;
	size_t       len;
	char *       sigStr;
	char *       apidStr;
	char **      my_argv;
	char **      tmp;
	
	
	// sanity check
	if (apid <= 0)
		return 1;
		
	// ensure the user called us with a signal number that makes sense, otherwise default to 9
	if ((sig = signum) <= 0)
	{
		sig = DEFAULT_SIG;
	}
	
	// create the string to pass to exec
	
	// allocate the argv array. Need additional entry for null terminator
	if ((my_argv = calloc(4, sizeof(char *))) == (void *)0)
	{
		// calloc failed
		return 1;
	}
	
	// first argument should be "apkill"
	my_argv[0] = strdup(APKILL);
	
	// second argument is -signum
	// determine length of signum
	len = 0;
	j = sig;
	do {
		++len;
	} while (j/=10);
	
	// add 1 additional char for the '-' and another for the null terminator
	len += 2;
	
	// alloc space for sigStr
	if ((sigStr = malloc(len*sizeof(char))) == (void *)0)
	{
		// malloc failed
		
		// cleanup my_argv array
		tmp = my_argv;
		while (*tmp != (char *)NULL)
		{
			free(*tmp++);
		}
		free(my_argv);
		
		return 1;
	}
	
	// write the signal string
	snprintf(sigStr, len, "-%d", sig);
	my_argv[1] = sigStr;
	
	// third argument is apid
	// determine length of apid
	len = 0;
	i = apid;
	do {
		++len;
	} while (i/=10);
	
	// add 1 additional char for the null terminator
	++len;
	
	// alloc space for apidStr
	if ((apidStr = malloc(len*sizeof(char))) == (void *)0)
	{
		// malloc failed
		
		// cleanup my_argv array
		tmp = my_argv;
		while (*tmp != (char *)NULL)
		{
			free(*tmp++);
		}
		free(my_argv);
		
		return 1;
	}
	
	// write the apid string
	snprintf(apidStr, len, "%llu", (long long unsigned int)apid);
	my_argv[2] = apidStr;
	
	// set the final null terminator
	my_argv[3] = (char *)NULL;
	
	// fork off a process to launch apkill
	mypid = fork();
	
	// error case
	if (mypid < 0)
	{
		fprintf(stderr, "Fatal fork error.\n");
		// cleanup my_argv array
		tmp = my_argv;
		while (*tmp != (char *)NULL)
		{
			free(*tmp++);
		}
		free(my_argv);
		return 1;
	}
	
	// child case
	if (mypid == 0)
	{
		// exec apkill
		execvp(APKILL, my_argv);
		
		// exec shouldn't return
		fprintf(stderr, "Return from exec.\n");
		exit(1);
	}
	
	// parent case
	// cleanup my_argv array
	tmp = my_argv;
	while (*tmp != (char *)NULL)
	{
		free(*tmp++);
	}
	free(my_argv);
	
	// deregister this apid from the interface
	deregisterApid(apid);
	
	// wait until the apkill finishes
	waitpid(mypid, NULL, 0);
	
	return 0;
}

