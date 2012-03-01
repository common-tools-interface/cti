/******************************************************************************\
 * alps_run.c - A interface to launch and interact with aprun sessions. This
 *	      provides the tool developer with an easy to use interface to
 *	      start new instances of an aprun program and get the pid_t of
 *	      the associated aprun session.
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
 ******************************************************************************/
 
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */
 
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>

#include "alps_run.h"
#include "alps_application.h"

/* static prototypes */
static void reapAprunInv(pid_t);
static void consumeAprunInv(aprunInv_t *);
static aprunInv_t * findAprunInv(pid_t);

// global aprunInv_t list
aprunInv_t *	head = (aprunInv_t *)NULL;

static void
reapAprunInv(pid_t aprunPid)
{
	aprunInv_t * appPtr;
	aprunInv_t * prePtr;
	
	// sanity check
	if (((appPtr = head) == (aprunInv_t *)NULL) || (aprunPid <= 0))
		return;
	
	prePtr = head;
	
	// find the aprunInv_t entry in the global list
	while (appPtr->aprunPid != aprunPid)
	{
		prePtr = appPtr;
		if ((appPtr = appPtr->next) == (aprunInv_t *)NULL)
		{
			// on last entry and aprunPid not found
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
findAprunInv(pid_t aprunPid)
{
	aprunInv_t * appPtr;

	// find the aprunInv_t entry in the global list
	if (((appPtr = head) == (aprunInv_t *)NULL) || (aprunPid <= 0))
		return (aprunInv_t *)NULL;
		
	while (appPtr->aprunPid != aprunPid)
	{
		if ((appPtr = appPtr->next) == (aprunInv_t *)NULL)
		{
			// entry not found
			return (aprunInv_t *)NULL;
		}
	}

	return appPtr;
}

pid_t
launchAprun_barrier(char **aprun_argv, int redirectOutput, int redirectInput, 
			int stdout_fd, int stderr_fd, char *inputFile)
{
	aprunInv_t * myapp;
	aprunInv_t * newapp;
	pid_t        mypid;
	char **      tmp;
	int          aprun_argc = 0;
	int          fd_len = 0;
	int          i, j, fd;
	char *       pipefd_buf;
	char **      my_argv;
	// pipes for aprun
	int aprunPipeR[2];
	int aprunPipeW[2];

	// create a new aprunInv_t object
	if ((myapp = malloc(sizeof(aprunInv_t))) == (void *)0)
	{
		// Malloc failed
		return 0;
	}
	memset(myapp, 0, sizeof(aprunInv_t));     // clear it to NULL
	
	// make the pipes for aprun (tells aprun to hold the program at the initial barrier)
	if (pipe(aprunPipeR) < 0)
	{
		fprintf(stderr, "Pipe creation failure on aprunPipeR.\n");
		free(myapp);
		return 0;
	}
	if (pipe(aprunPipeW) < 0)
	{
		fprintf(stderr, "Pipe creation failure on aprunPipeW.\n");
		free(myapp);
		return 0;
	}
	
	// set my ends of the pipes in the aprunInv_t structure
	myapp->pipeCtl.pipe_r = aprunPipeR[1];
	myapp->pipeCtl.pipe_w = aprunPipeW[0];
	
	// create the argv array for the actual aprun exec
	// figure out the length of the argv array
	// this is the number of args in the aprun_argv array passed to us plus 2 for the -P w,r argument and 2 for aprun and null term
		
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
		return 0;
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
		return 0;
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
		return 0;
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
			}
		}
		
		// dup2 the null fd onto STDIN_FILENO
		if (dup2(fd, STDIN_FILENO) < 0)
		{
			fprintf(stderr, "Unable to redirect aprun stdin.\n");
		}
		close(fd);
		
		// redirect stdout/stderr if directed
		if (redirectOutput)
		{
			// dup2 stdout
			if (dup2(stdout_fd, STDOUT_FILENO) < 0)
			{
				fprintf(stderr, "Unable to redirect aprun stdout.\n");
			}
			
			// dup2 stderr
			if (dup2(stderr_fd, STDERR_FILENO) < 0)
			{
				fprintf(stderr, "Unable to redirect aprun stderr.\n");
			}
		}

		// exec aprun
		execvp(APRUN, my_argv);
		
		// exec shouldn't return
		fprintf(stderr, "Return from exec.\n");
		exit(1);
	}
	
	// parent case
	
	// set aprunPid in aprunInv_t structure
	myapp->aprunPid = mypid;
	
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
	
	// Wait on pipe read for app to start and get to barrier
	if (read(myapp->pipeCtl.pipe_w, &myapp->pipeCtl.sync_int, sizeof(myapp->pipeCtl.sync_int)) <= 0)
	{
		fprintf(stderr, "Aprun launch failed.\n");
		free(myapp);
		return 0;
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
	
	// register this process with the alps_transfer interface
	if (registerAprunPid(myapp->aprunPid))
	{
		// we failed to register our app
		fprintf(stderr, "Could not register application!\n");
	}
	
	// return the pid of the aprun process we forked
	return myapp->aprunPid;      
}

int
releaseAprun_barrier(pid_t aprunPid)
{
	aprunInv_t * appPtr;
	
	// sanity check
	if (aprunPid <= 0)
		return 1;
	
	// find the aprunInv_t entry in the global list
	if ((appPtr = findAprunInv(aprunPid)) == (aprunInv_t *)NULL)
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
killAprun(pid_t aprunPid, int signum)
{
	aprunInv_t * appPtr;
	int          mypid;
	uint64_t     apid, i;
	int          sig, j;
	size_t       len;
	char *       sigStr;
	char *       apidStr;
	char **      my_argv;
	char **      tmp;
	
	
	// sanity check
	if (aprunPid <= 0)
		return 1;
		
	// ensure the user called us with a signal number that makes sense, otherwise default to 9
	if ((sig = signum) <= 0)
	{
		sig = DEFAULT_SIG;
	}
		
	// find the aprunInv_t entry in the global list
	if ((appPtr = findAprunInv(aprunPid)) == (aprunInv_t *)NULL)
		return 1;
		
	// get the apid associated with this aprunPid
	if ((apid = getApid(aprunPid)) == 0)
	{
		// failed to find an entry in the apls_transfer interface for the associated aprun pid
		return 1;
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
	
	// remove the aprunInv_t object for this aprunPid from the global list
	reapAprunInv(aprunPid);
	// deregister this aprunPid from the alps_transfer interface
	deregisterAprunPid(aprunPid);
	
	return 0;
}

