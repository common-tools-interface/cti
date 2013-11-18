/*********************************************************************************\
 * ld_val.c - A library that takes advantage of the rtld audit interface library
 *	    to recieve the locations of the shared libraries that are required
 *	    by the runtime dynamic linker for a specified program. This is the
 *	    static portion of the code to link into a program wishing to use
 *	    this interface.
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
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "ld_val_defs.h"
#include "ld_val.h"

/* Internal prototypes */
static void		_cti_overwatch_handler(int);
static int		_cti_creat_shm_segs(void);
static int		_cti_attach_shm_segs(void);
static int		_cti_destroy_shm_segs(void);
static int		_cti_save_str(char *);
static char **	_cti_make_rtn_array(void);
static char *	_cti_ld_get_lib(void);
static char *	_cti_ld_verify(char *);
static int		_cti_ld_load(char *, char *, char *);

/* list of valid linkers */
// We should check the 64 bit linker first since most
// apps are built using x86-64 nowadays.
// Check the lsb linker last. (do we even use lsb code?)
// lsb = linux standard base
static const char *_cti_linkers[] = {
	"/lib64/ld-linux-x86-64.so.2",
	"/lib/ld-linux.so.2",
	"/lib64/ld-lsb-x86-64.so.2",
	"/lib/ld-lsb.so.2",
	"/lib64/ld-lsb-x86-64.so.3",
	"/lib/ld-lsb.so.3",
	NULL
};

/* global variables */
static char *	_cti_key_file = NULL;
static key_t	_cti_key_a;
static key_t	_cti_key_b;
static pid_t	_cti_overwatch_pid;
static int		_cti_shmid;
static char *	_cti_shm = NULL;
static int		_cti_sem_ctrlid;
static int		_cti_num_ptrs;
static int		_cti_num_alloc;
static char **	_cti_tmp_array = NULL;

static void
_cti_overwatch_handler(int sig)
{
	// remove the shared memory segment	
	if (shmctl(_cti_shmid, IPC_RMID, NULL) < 0)
	{
		perror("IPC error: shmctl");
	}
	
	// remove the semaphore
	if (semctl(_cti_sem_ctrlid, 0, IPC_RMID) < 0)
	{
		perror("IPC error: semctl");
	}
	
	// exit
	exit(0);
}

static int
_cti_creat_shm_segs()
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
	// used for calls to semctl
	union semun {
			int					val;
			struct semid_ds *	buf;
			unsigned short *	array;
			struct seminfo *	__buf;
	} sem_arg;
	
	// start out by creating the keys from a well known file location and a character id
	if ((_cti_key_a = ftok(_cti_key_file, ID_A)) == (key_t)-1)
	{
		perror("IPC error: ftok");
		return 1;
	}
	if ((_cti_key_b = ftok(_cti_key_file, ID_B)) == (key_t)-1)
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
	_cti_overwatch_pid = fork();
	
	// error case
	if (_cti_overwatch_pid < 0)
	{
		perror("fork");
		return 1;
	}
	
	// child case
	if (_cti_overwatch_pid == 0)
	{
		struct sigaction new_handler;
		sigset_t mask;
	
		// close the read end of the pipe
		close(fds[0]);
		
		// setup the signal handler so this child can exit
		memset(&new_handler, 0, sizeof(new_handler));
		sigemptyset(&new_handler.sa_mask);
		new_handler.sa_flags = 0;
		new_handler.sa_handler = _cti_overwatch_handler;
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
		while ((_cti_shmid = shmget(_cti_key_a, PATH_MAX, IPC_CREAT | IPC_EXCL | SHM_R | SHM_W)) < 0) 
		{
			if (errno == EEXIST)
				continue;
		
			perror("IPC error: shmget");
			// close my end of the pipe to let the parent figure out we failed
			close(fds[1]);
			exit(1);
		}
	
		// create the semaphore
		while ((_cti_sem_ctrlid = semget(_cti_key_b, 2, IPC_CREAT | IPC_EXCL | 0600)) < 0)
		{
			if (errno == EEXIST)
				continue;
			
			perror("IPC error: semget");
			// delete the segment we just created since this one failed
			if (shmctl(_cti_shmid, IPC_RMID, NULL) < 0)
			{
				perror("IPC error: shmctl");
			}
			// close my end of the pipe to let the parent figure out we failed
			close(fds[1]);
			exit(1);
		}
		
		// init the semaphore values to 0
		sem_arg.val = 0;
		semctl(_cti_sem_ctrlid, LDVAL_SEM, SETVAL, sem_arg);
		semctl(_cti_sem_ctrlid, AUDIT_SEM, SETVAL, sem_arg);
		
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
	
	// get the id of the memory segment
	if ((_cti_shmid = shmget(_cti_key_a, PATH_MAX, SHM_R | SHM_W)) < 0) 
	{
		perror("IPC error: shmget");
		return 1;
	}
	
	// get the id of the semaphore
	if ((_cti_sem_ctrlid = semget(_cti_key_b, 0, 0)) < 0)
	{
		perror("IPC error: semget");
		return 1;
	}
	
	return 0;
}

static int
_cti_attach_shm_segs()
{
	/*
	*  Attach the shm segment to our data space.
	*/
	if ((_cti_shm = shmat(_cti_shmid, NULL, 0)) == (char *)-1) 
	{
		perror("IPC error: shmat");
		return 1;
	}
	
	return 0;
}

static int
_cti_destroy_shm_segs()
{
	int ret = 0;
	
	if (shmdt(_cti_shm) == -1)
	{
		perror("IPC error: shmdt");
		ret = 1;
	}
	
	// send the overwatch child a kill of SIGUSR1
	if (kill(_cti_overwatch_pid, SIGUSR1) < 0)
	{
		perror("kill");
		ret = 1;
	}
	
	// wait for child to return
	waitpid(_cti_overwatch_pid, NULL, 0);
	
	return ret;
}

static int
_cti_save_str(char *str)
{
	if (str == NULL)
		return -1;
	
	if (_cti_num_ptrs >= _cti_num_alloc)
	{
		_cti_num_alloc += BLOCK_SIZE;
		if ((_cti_tmp_array = realloc(_cti_tmp_array, _cti_num_alloc * sizeof(char *))) == (void *)0)
		{
			perror("realloc");
			return -1;
		}
	}
	
	_cti_tmp_array[_cti_num_ptrs++] = str;
	
	return _cti_num_ptrs;
}

static char **
_cti_make_rtn_array()
{
	char **rtn;
	int i;
	
	if (_cti_tmp_array == NULL)
		return NULL;
	
	// create the return array
	if ((rtn = calloc(_cti_num_ptrs+1, sizeof(char *))) == (void *)0)
	{
		perror("calloc");
		return NULL;
	}
	
	// assign each element of the return array
	for (i=0; i<_cti_num_ptrs; i++)
	{
		rtn[i] = _cti_tmp_array[i];
	}
	
	// set the final element to null
	rtn[i] = NULL;
	
	// free the temp array
	free(_cti_tmp_array);
	
	return rtn;
}

static char *
_cti_ld_verify(char *executable)
{
	const char *linker = NULL;
	int pid, status, fc, i=1;
	
	if (executable == NULL)
		return NULL;

	// Verify that the linker is able to perform relocations on our binary
	// This should be able to handle both 32 and 64 bit executables
	// We will simply choose the first one that works for us.
	for (linker = _cti_linkers[0]; linker != NULL; linker = _cti_linkers[i++])
	{
		pid = fork();
		
		// error case
		if (pid < 0)
		{
			perror("fork");
			return NULL;
		}
		
		// child case
		if (pid == 0)
		{
			// redirect our stdout/stderr to /dev/null
			fc = open("/dev/null", O_WRONLY);
			dup2(fc, STDERR_FILENO);
			dup2(fc, STDOUT_FILENO);
			
			// exec the linker to verify it is able to load our program
			execl(linker, linker, "--verify", executable, NULL);
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
_cti_ld_load(char *linker, char *executable, char *lib)
{
	int pid, fc;
	
	if (linker == NULL || executable == NULL)
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
		execl(linker, linker, "--list", executable, NULL);
		perror("execl");
	}
	
	// parent case
	return pid;
}

static char *
_cti_ld_get_lib()
{
	char *				libstr;
	struct sembuf		sops[1];
	struct timespec		timeout;
	
	// setup the semop command
	// give one resource on our sema
	sops[0].sem_num = LDVAL_SEM;	// operate on our sema
	sops[0].sem_op = 1;				// give 1 resource
	sops[0].sem_flg = SEM_UNDO;
	
	// execute the semop cmd
	if (semop(_cti_sem_ctrlid, sops, 1) == -1)
	{
		perror("semop");
		return NULL;
	}
	
	// grab one resource from the audit sema
	sops[0].sem_num = AUDIT_SEM;	// operate on audit sema
	sops[0].sem_op = -1;			// grab 1 resource
	sops[0].sem_flg = SEM_UNDO;
	
	// This operation is allowed to timeout in the event the AUDIT process goes
	// away
	
	// setup the timeout to sleep for 1 microsecond
	timeout.tv_sec = 0;
	timeout.tv_nsec = 1000000;
	
	// execute the semtimedop cmd
	if (semtimedop(_cti_sem_ctrlid, sops, 1, &timeout) == -1)
	{
		// audit process is likely dead, remove the resource on our sema
		sops[0].sem_num = LDVAL_SEM;	// operate on our sema
		sops[0].sem_op = -1;			// grab 1 resource
		sops[0].sem_flg = SEM_UNDO | IPC_NOWAIT;
		
		// execute the semop cmd
		if (semop(_cti_sem_ctrlid, sops, 1) == -1)
		{
			// Here we have an interesting situation. We tried to remove the resource
			// from our sema, but failed to do so. Which means the audit process must
			// have grabbed it. I suppose this might be possible if our first timedop
			// fails and the clock cycle after that the audit process grabs our resource.
			//
			// Try to grab the resource once more, this time with a longer timeout.
			// This is for good measure, because in this situation something went
			// horibly wrong.
			
			// grab one resource from the audit sema
			sops[0].sem_num = AUDIT_SEM;	// operate on audit sema
			sops[0].sem_op = -1;			// grab 1 resource
			sops[0].sem_flg = SEM_UNDO;
			
			// setup the timeout to sleep for 1 second
			timeout.tv_sec = 1;
			timeout.tv_nsec = 0;
			
			// execute the semtimedop cmd
			if (semtimedop(_cti_sem_ctrlid, sops, 1, &timeout) == -1)
			{
				fprintf(stderr, "Encountered deadlock scenario.\n");
				perror("semop");
				return NULL;
			}
			
			// We grabbed the audit resource, so continue on as normal
		} else
		{
			// safe to return, the resource on our sema has been removed.
			return NULL;
		}
	}

	// copy the library location string
	libstr = strdup(_cti_shm);
	
	// reset the _cti_shm segment
	memset(_cti_shm, '\0', PATH_MAX);
		
	// return the string
	return libstr;
}

char **
_cti_ld_val(char *executable)
{
	char *			linker;
	int				pid;
	int				rec = 0;
	char *			tmp_audit;
	char *			tmp_keyfile;
	struct stat		stat_buf;
	FILE *			tmp_file;
	char *			audit_location;
	char *			libstr;
	char **			rtn;
	
	// reset global vars
	_cti_key_a = 0;
	_cti_key_b = 0;
	_cti_shmid = 0;
	_cti_sem_ctrlid = 0;
	_cti_shm = NULL;
	_cti_num_ptrs = 0;
	
	if (executable == NULL)
		return NULL;
		
	// get the location of the keyfile or else set it to the default value
	if ((tmp_keyfile = getenv(LIBAUDIT_KEYFILE_ENV_VAR)) != NULL)
	{
		_cti_key_file = strdup(tmp_keyfile);
		// stat the user defined _cti_key_file to make sure it exists
		if (stat(_cti_key_file, &stat_buf) < 0)
		{
			// _cti_key_file doesn't exist so try to do an fopen on it. This will
			// will ensure all our future calls to ftok will work.
			if ((tmp_file = fopen(_cti_key_file, "w")) == (FILE *)NULL)
			{
				fprintf(stderr, "FATAL: The keyfile environment variable %s file doesn't exist and\n", LIBAUDIT_KEYFILE_ENV_VAR);
				fprintf(stderr, "       its value is not a writable location.\n");
				return NULL;
			}
		}
	} else
	{
		_cti_key_file = strdup(DEFAULT_KEYFILE);
		// make sure our default choice works, otherwise things will break.
		if (stat(_cti_key_file, &stat_buf) < 0)
		{
			fprintf(stderr, "FATAL: The keyfile environment variable %s file isn't set and\n", LIBAUDIT_KEYFILE_ENV_VAR);
			fprintf(stderr, "       the default keyfile doesn't exist.\n");
			return NULL;
		}
	}
	
	// ensure that we found a valid linker that was verified
	if ((linker = _cti_ld_verify(executable)) == NULL)
	{
		fprintf(stderr, "FATAL: Failed to locate a working dynamic linker for the specified binary.\n");
		return NULL;
	}
	
	// We now have a valid linker to use, so lets set up our shm segments
	
	// Create our shm segments
	// This will spin if another caller is using this interface
	if (_cti_creat_shm_segs())
	{
		fprintf(stderr, "Failed to create shm segment.\n");
		return NULL;
	}

	// Attach the segment to our data space.
	if (_cti_attach_shm_segs())
	{
		fprintf(stderr, "Failed to attach shm segment.\n");
		_cti_destroy_shm_segs();
		return NULL;
	}
	
	// create space for the _cti_tmp_array
	if ((_cti_tmp_array = calloc(BLOCK_SIZE, sizeof(char *))) == (void *)0)
	{
		perror("calloc");
		_cti_destroy_shm_segs();
		return NULL;
	}
	_cti_num_alloc = BLOCK_SIZE;
	
	// get the location of the audit library
	if ((tmp_audit = getenv(LIBAUDIT_ENV_VAR)) != NULL)
	{
		audit_location = strdup(tmp_audit);
	} else
	{
		fprintf(stderr, "Could not read CRAY_LD_VAL_LIBRARY to get location of libaudit.so.\n");
		_cti_destroy_shm_segs();
		return NULL;
	}
	
	// Now we load our program using the list command to get its dso's
	if ((pid = _cti_ld_load(linker, executable, audit_location)) <= 0)
	{
		fprintf(stderr, "Failed to load the program using the linker.\n");
		_cti_destroy_shm_segs();
		return NULL;
	}
	
	// Read from the shm segment while the child process is still alive
	do {
		libstr = _cti_ld_get_lib();
		
		// we want to ignore the first library we recieve
		// as it will always be the ld.so we are using to
		// get the shared libraries.
		if (++rec == 1)
		{
			if (libstr != NULL)
				free(libstr);
			continue;
		}
		
		// if we recieved a null, we might be done.
		if (libstr == NULL)
			continue;
			
		if ((_cti_save_str(libstr)) <= 0)
		{
			fprintf(stderr, "Unable to save temp string.\n");
			_cti_destroy_shm_segs();
			return NULL;
		}
	} while (!waitpid(pid, NULL, WNOHANG));
	
	rtn = _cti_make_rtn_array();
	
	// destroy the shm segments
	_cti_destroy_shm_segs();
	
	// cleanup memory
	free(audit_location);
	free(_cti_key_file);

	return rtn;
}

