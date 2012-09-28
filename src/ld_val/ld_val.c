/*********************************************************************************\
 * ld_val.c - A library that takes advantage of the rtld audit interface library
 *	    to recieve the locations of the shared libraries that are required
 *	    by the runtime dynamic linker for a specified program. This is the
 *	    static portion of the code to link into a program wishing to use
 *	    this interface.
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/prctl.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "ld_val_defs.h"
#include "ld_val.h"

/* Internal prototypes */
static void overwatch_handler(int);
static int creat_shm_segs(void);
static int attach_shm_segs(void);
static int destroy_shm_segs(void);
static int save_str(char *);
static char ** make_rtn_array(void);
static char * ld_get_lib(int);
static char * ld_verify(char *);
static int ld_load(char *, char *, char *);

/* list of valid linkers */
// We should check the 64 bit linker first since most
// apps are built using x86-64 nowadays.
// Check the lsb linker last. (do we even use lsb code?)
// lsb = linux standard base
static const char *linkers[] = {
	"/lib64/ld-linux-x86-64.so.2",
	"/lib/ld-linux.so.2",
	"/lib64/ld-lsb-x86-64.so.2",
	"/lib/ld-lsb.so.2",
	"/lib64/ld-lsb-x86-64.so.3",
	"/lib/ld-lsb.so.3",
	NULL
};

/* global variables */
static key_t	key_a;
static key_t	key_b;
static pid_t	overwatch_pid;
static int		shmid;
static int		shm_ctlid;
static char *	shm;
static char *	shm_ctl;
static int		num_ptrs;
static int		num_alloc;
static char **	tmp_array = (char **)NULL;

static void
overwatch_handler(int sig)
{
	// remove the shared memory segments	
	if (shmctl(shmid, IPC_RMID, NULL) < 0)
	{
		perror("IPC error: shmctl");
	}
	
	if (shmctl(shm_ctlid, IPC_RMID, NULL) < 0)
	{
		perror("IPC error: shmctl");
	}
	
	// exit
	exit(0);
}

static int
creat_shm_segs()
{
	/*
	*  Create the shm segments - Note that these will behave as a semaphore in the
	*  event that multiple programs are trying to access this interface at once.
	*
	*  This function is now able to avoid a deadlock if the caller causes an
	*  error or is interrupted by a signal. This is achieved by using an
	*  "overwatch" process which does the actual creation of the shm segment
	*  id. The overwatch process is forked off and will destroy the id if it
	*  detects that it has become orphaned or if the parent sends it a SIGUSR1.
	*
	*  Note this is still prone to a denial of service if the caller never goes
	*  away. Perhaps a watchdog timer in the overwatch could fix this issue.
	*
	*/
	
	// pipe to signal that the shm segment was created
	int 	fds[2];
	// this is never used, but good form I guess.
	char	tmp[1];
	
	// start out by creating the keys from a well known file location and a character id
	if ((key_a = ftok(KEYFILE, ID_A)) == (key_t)-1)
	{
		perror("IPC error: ftok");
		return 1;
	}
	if ((key_b = ftok(KEYFILE, ID_B)) == (key_t)-1)
	{
		perror("IPC error: ftok");
		return 1;
	}

	// create the pipe
	if (pipe(fds) < 0)
	{
		perror("pipe");
		return 1;
	}

	// fork off the overwatch process
	overwatch_pid = fork();
	
	// error case
	if (overwatch_pid < 0)
	{
		perror("fork");
		return 1;
	}
	
	// child case
	if (overwatch_pid == 0)
	{
		struct sigaction new_handler;
		sigset_t mask;
	
		// close the read end of the pipe
		close(fds[0]);
		
		// setup the signal handler so this child can exit
		memset(&new_handler, 0, sizeof(new_handler));
		sigemptyset(&new_handler.sa_mask);
		new_handler.sa_flags = 0;
		new_handler.sa_handler = overwatch_handler;
		sigaction(SIGUSR1, &new_handler, NULL);
		
		// set the parent death signal to send us SIGUSR1
		if (prctl(PR_SET_PDEATHSIG, SIGUSR1) < 0)
		{
			perror("prctl");
			// close my end of the pipe to let the parent figure out we failed
			close(fds[1]);
			exit(1);
		}
		
		// create the shared memory segments
		while ((shmid = shmget(key_a, PATH_MAX, IPC_CREAT | IPC_EXCL | SHM_R | SHM_W)) < 0) 
		{
			if (errno == EEXIST)
				continue;
		
			perror("IPC error: shmget");
			// close my end of the pipe to let the parent figure out we failed
			close(fds[1]);
			exit(1);
		}
	
		while ((shm_ctlid = shmget(key_b, CTL_CHANNEL_SIZE, IPC_CREAT | IPC_EXCL | SHM_R | SHM_W)) < 0)
		{
			if (errno == EEXIST)
				continue;
			
			perror("IPC error: shmget");
			// delete the segment we just created since this one failed
			if (shmctl(shmid, IPC_RMID, NULL) < 0)
			{
				perror("IPC error: shmctl");
			}
			// close my end of the pipe to let the parent figure out we failed
			close(fds[1]);
			exit(1);
		}
		
		// init the signal mask to empty
		sigemptyset(&mask);
		
		// I am done. Close my end of the pipe so the parent knows I am finished.
		close(fds[1]);
		
		// wait for a signal to arrive
		sigsuspend(&mask);
		
		// never reached
		exit(0);
	}
	
	// parent case
	// close write end of pipe
	close(fds[1]);
	
	// block in a read until we get an EOF, this signals the child is finished
	// doing its work
	if (read(fds[0], tmp, 1) < 0)
	{
		perror("read");
	}
	
	// close the read end of the pipe
	close(fds[0]);
	
	// get the id of the memory segments - they have been created for us
	if ((shmid = shmget(key_a, PATH_MAX, SHM_R | SHM_W)) < 0) 
	{
		perror("IPC error: shmget");
		return 1;
	}
	
	if ((shm_ctlid = shmget(key_b, CTL_CHANNEL_SIZE, SHM_R | SHM_W)) < 0)
	{		
		perror("IPC error: shmget");
		return 1;
	}
	
	return 0;
}

static int
attach_shm_segs()
{
	/*
	*  Attach the shm segments to our data space.
	*/
	if ((shm = shmat(shmid, NULL, 0)) == (char *)-1) 
	{
		perror("IPC error: shmat");
		return 1;
	}
	if ((shm_ctl = shmat(shm_ctlid, NULL, 0)) == (char *)-1)
	{
		perror("IPC error: shmat");
		return 1;
	}
	
	return 0;
}

static int
destroy_shm_segs()
{
	int ret = 0;
	
	if (shmdt((void *)shm) == -1)
	{
		perror("IPC error: shmdt");
		ret = 1;
	}
	
	if (shmdt((void *)shm_ctl) == -1)
	{
		perror("IPC error: shmdt");
		ret = 1;
	}
	
	// send the overwatch child a kill of SIGUSR1
	if (kill(overwatch_pid, SIGUSR1) < 0)
	{
		perror("kill");
		ret = 1;
	}
	
	// wait for child to return
	waitpid(overwatch_pid, NULL, 0);
	
	return ret;
}

static int
save_str(char *str)
{
	if (str == (char *)NULL)
		return -1;
	
	if (num_ptrs >= num_alloc)
	{
		num_alloc += BLOCK_SIZE;
		if ((tmp_array = realloc((void *)tmp_array, num_alloc * sizeof(char *))) == (char **)NULL)
		{
			perror("realloc");
			return -1;
		}
	}
	
	tmp_array[num_ptrs++] = str;
	
	return num_ptrs;
}

static char **
make_rtn_array()
{
	char **rtn;
	int i;
	
	if (tmp_array == (char **)NULL)
		return (char **)NULL;
	
	// create the return array
	if ((rtn = calloc(num_ptrs+1, sizeof(char *))) == (char **)NULL)
	{
		perror("calloc");
		return (char **)NULL;
	}
	
	// assign each element of the return array
	for (i=0; i<num_ptrs; i++)
	{
		rtn[i] = tmp_array[i];
	}
	
	// set the final element to null
	rtn[i] = (char *)NULL;
	
	// free the temp array
	free(tmp_array);
	
	return rtn;
}

static char *
ld_verify(char *executable)
{
	const char *linker = NULL;
	int pid, status, fc, i=1;
	
	if (executable == (char *)NULL)
		return (char *)NULL;

	// Verify that the linker is able to perform relocations on our binary
	// This should be able to handle both 32 and 64 bit executables
	// We will simply choose the first one that works for us.
	for (linker = linkers[0]; linker != NULL; linker = linkers[i++])
	{
		pid = fork();
		
		// error case
		if (pid < 0)
		{
			perror("fork");
			return (char *)NULL;
		}
		
		// child case
		if (pid == 0)
		{
			// redirect our stdout/stderr to /dev/null
			fc = open("/dev/null", O_WRONLY);
			dup2(fc, STDERR_FILENO);
			dup2(fc, STDOUT_FILENO);
			
			// exec the linker to verify it is able to load our program
			execl(linker, linker, "--verify", executable, (char *)NULL);
			perror("execl");
		}
		
		// parent case
		// wait for child to return
		waitpid(pid, &status, 0);
		if (WIFEXITED(status))
		{
			// if we recieved an exit status of 0, the verify was successful
			if (WEXITSTATUS(status) == 0)
				break;
		}
	}
	
	return (char *)linker;
}

static int
ld_load(char *linker, char *executable, char *lib)
{
	int pid, fc;
	
	if (linker == (char *)NULL || executable == (char *)NULL)
		return -1;
	
	// invoke the rtld interface.
	pid = fork();
	
	// error case
	if (pid < 0)
	{
		perror("fork");
		return pid;
	}
	
	// child case
	if (pid == 0)
	{
		// set the LD_AUDIT environment variable for this process
		if (setenv(LD_AUDIT, lib, 1) < 0)
		{
			perror("setenv");
			fprintf(stderr, "Failed to set LD_AUDIT environment variable.\n");
			exit(1);
		}
		
		// redirect our stdout/stderr to /dev/null
		fc = open("/dev/null", O_WRONLY);
		dup2(fc, STDERR_FILENO);
		dup2(fc, STDOUT_FILENO);
		
		// exec the linker with --list to get a list of our dso's
		execl(linker, linker, "--list", executable, (char *)NULL);
		perror("execl");
	}
	
	// parent case
	return pid;
}

static char *
ld_get_lib(int pid)
{
	char *libstr;
	
	if (pid <= 0)
		return (char *)NULL;
	
	// wait for the child to signal us on the shm_ctl channel
	// as long as the child is alive
	while ((*shm_ctl != '1') && !waitpid(pid, NULL, WNOHANG));
	
	// only read if signaled
	if (*shm_ctl == '1')
	{
		// copy the library location string
		libstr = strdup(shm);
		
		// reset the shm segment
		memset(shm, '\0', PATH_MAX);
		
		// reset the control channel
		*shm_ctl = '0';
		
		// return the string
		return libstr;
	}
	
	// if we get here, we are done so return null
	return (char *)NULL;
}

char **
ld_val(char *executable)
{
	char *linker;
	int pid;
	int rec = 0;
	char *tmp_audit;
	char *audit_location;
	char *libstr;
	char **rtn;
	
	// reset global vars
	key_a = 0;
	key_b = 0;
	shmid = 0;
	shm_ctlid = 0;
	shm = NULL;
	shm_ctl = NULL;
	num_ptrs = 0;
	
	if (executable == (char *)NULL)
		return (char **)NULL;
	
	// ensure that we found a valid linker that was verified
	if ((linker = ld_verify(executable)) == (char *)NULL)
	{
		fprintf(stderr, "FATAL: Failed to locate a working dynamic linker for the specified binary.\n");
		return (char **)NULL;
	}
	
	// We now have a valid linker to use, so lets set up our shm segments
	
	// Create our shm segments
	// This will spin if another caller is using this interface
	if (creat_shm_segs())
	{
		fprintf(stderr, "Failed to create shm segments.\n");
		return (char **)NULL;
	}

	// Attach the segments to our data space.
	if (attach_shm_segs())
	{
		fprintf(stderr, "Failed to attach shm segments.\n");
		destroy_shm_segs();
		return (char **)NULL;
	}
	
	// create space for the tmp_array
	if ((tmp_array = calloc(BLOCK_SIZE, sizeof(char *))) == (void *)0)
	{
		perror("calloc");
		destroy_shm_segs();
		return (char **)NULL;
	}
	num_alloc = BLOCK_SIZE;
	
	// get the location of the audit library
	if ((tmp_audit = getenv(LIBAUDIT_ENV)) != (char *)NULL)
	{
		audit_location = strdup(tmp_audit);
	} else
	{
		fprintf(stderr, "Could not read LD_VAL_LIBRARY to get location of libaudit.so.\n");
		destroy_shm_segs();
		return (char **)NULL;
	}
	
	// Now we load our program using the list command to get its dso's
	if ((pid = ld_load(linker, executable, audit_location)) <= 0)
	{
		fprintf(stderr, "Failed to load the program using the linker.\n");
		destroy_shm_segs();
		return (char **)NULL;
	}
	
	// Read from the shm segment while the child process is still alive
	do {
		libstr = ld_get_lib(pid);
		
		// we want to ignore the first library we recieve
		// as it will always be the ld.so we are using to
		// get the shared libraries.
		if (++rec == 1)
		{
			if (libstr != (char *)NULL)
				free(libstr);
			continue;
		}
		
		// if we recieved a null, we might be done.
		if (libstr == (char *)NULL)
			continue;
			
		if ((save_str(libstr)) <= 0)
		{
			fprintf(stderr, "Unable to save temp string.\n");
			destroy_shm_segs();
			return (char **)NULL;
		}
	} while (!waitpid(pid, NULL, WNOHANG));
	
	rtn = make_rtn_array();
	
	// destroy the shm segments
	destroy_shm_segs();
	
	// cleanup memory
	free((void *)audit_location);

	return rtn;
}

