/******************************************************************************\
 * cti_overwatch.c - library routines to interface with the cti overwatch 
 *                   process.
 *
 * Copyright 2014 Cray Inc.  All Rights Reserved.
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
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/prctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <sys/stat.h>

#include "cti_args.h"
#include "cti_overwatch.h"

static void
_cti_free_overwatch(cti_overwatch_t *this)
{
	// sanity
	if (this == NULL)
		return;
		
	// tell the overwatch to exit gracefully
	kill(this->o_pid, SIGUSR2);
	
	if (this->pipe_r != NULL)
		fclose(this->pipe_r);

	if (this->pipe_w != NULL)
		fclose(this->pipe_w);
		
	free(this);
}

// Signals should be blocked before calling this function.
// Requires path to overwatch binary to be passed in
cti_overwatch_t *
_cti_create_overwatch(const char *path)
{
	cti_overwatch_t *	rtn;
	int 				pipeR[2];	// parent read pipe
	int 				pipeW[2];	// parent write pipe
	cti_args_t *		my_args;	// args for overwatch process
	
	// sanity check - ensure we can access the binary
	if (path == NULL || access(path, R_OK | X_OK))
	{
		// invalid path
		return NULL;
	}
	
	// allocate return object
	if ((rtn = malloc(sizeof(cti_overwatch_t))) == NULL)
	{
		// malloc failed
		return NULL;
	}
	memset(rtn, 0, sizeof(cti_overwatch_t));
	
	// create the pipes
	if (pipe(pipeR) < 0)
	{
		// pipe failed
		free(rtn);
		return NULL;
	}
	if (pipe(pipeW) < 0)
	{
		// pipe failed
		free(rtn);
		close(pipeR[0]);
		close(pipeR[1]);
		return NULL;
	}
	
	// create a new args obj
	if ((my_args = _cti_newArgs()) == NULL)
	{
		// failure
		free(rtn);
		close(pipeR[0]);
		close(pipeR[1]);
		close(pipeW[0]);
		close(pipeW[1]);
		return NULL;
	}
	
	// add name of the overwatch binary
	if (_cti_addArg(my_args, "%s", path))
	{
		free(rtn);
		close(pipeR[0]);
		close(pipeR[1]);
		close(pipeW[0]);
		close(pipeW[1]);
		_cti_freeArgs(my_args);
		return NULL;
	}
	
	// add the -r and -w args
	if (_cti_addArg(my_args, "-r"))
	{
		free(rtn);
		close(pipeR[0]);
		close(pipeR[1]);
		close(pipeW[0]);
		close(pipeW[1]);
		_cti_freeArgs(my_args);
		return NULL;
	}
	if (_cti_addArg(my_args, "%d", pipeW[0]))
	{
		free(rtn);
		close(pipeR[0]);
		close(pipeR[1]);
		close(pipeW[0]);
		close(pipeW[1]);
		_cti_freeArgs(my_args);
		return NULL;
	}
	if (_cti_addArg(my_args, "-w"))
	{
		free(rtn);
		close(pipeR[0]);
		close(pipeR[1]);
		close(pipeW[0]);
		close(pipeW[1]);
		_cti_freeArgs(my_args);
		return NULL;
	}
	if (_cti_addArg(my_args, "%d", pipeR[1]))
	{
		free(rtn);
		close(pipeR[0]);
		close(pipeR[1]);
		close(pipeW[0]);
		close(pipeW[1]);
		_cti_freeArgs(my_args);
		return NULL;
	}
	
	// setup rtn
	rtn->pipe_r = fdopen(pipeR[0], "r");
	if (rtn->pipe_r == NULL)
	{
		free(rtn);
		close(pipeR[0]);
		close(pipeR[1]);
		close(pipeW[0]);
		close(pipeW[1]);
		_cti_freeArgs(my_args);
		return NULL;
	}
	rtn->pipe_w = fdopen(pipeW[1], "w");
	if (rtn->pipe_w == NULL)
	{
		free(rtn);
		close(pipeR[0]);
		close(pipeR[1]);
		close(pipeW[0]);
		close(pipeW[1]);
		_cti_freeArgs(my_args);
		return NULL;
	}
	
	// fork off a process
	rtn->o_pid = fork();
	
	// error case
	if (rtn->o_pid < 0)
	{
		// fork failed
		free(rtn);
		close(pipeR[0]);
		close(pipeR[1]);
		close(pipeW[0]);
		close(pipeW[1]);
		_cti_freeArgs(my_args);
		return NULL;
	}
	
	// child case
	if (rtn->o_pid == 0)
	{
		struct rlimit		rl;
		int					i;
		int					fd0;
		int					fd1;
		struct sigaction 	sig_action;
		sigset_t			mask;
		
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
		
		// Ensure every file descriptor is closed besides our read/write pipes
		for (i=3; i < rl.rlim_max; ++i)
		{
			if (i != pipeR[1] && i != pipeW[0])
			{
				close(i);
			}
		}
		
		// setup stdin/stdout/stderr
		fd0 = open("/dev/null", O_RDONLY);
		fd1 = open("/dev/null", O_WRONLY);
		dup2(fd0, STDIN_FILENO);
		dup2(fd1, STDOUT_FILENO);
		dup2(fd1, STDERR_FILENO);
		
		// put this overwatch in its own process group
		setpgid(rtn->o_pid, rtn->o_pid);
		
		// reset SIGUSR1 and SIGUSR2 signal handlers
		memset(&sig_action, 0, sizeof(sig_action));
		sig_action.sa_handler = SIG_DFL;
		sig_action.sa_flags = 0;
		if (sigemptyset(&sig_action.sa_mask))
		{
			perror("sigemptyset");
			exit(1);
		}
		if (sigaction(SIGUSR1, &sig_action, NULL))
		{
			perror("sigaction");
			exit(1);
		}
		if (sigaction(SIGUSR2, &sig_action, NULL))
		{
			perror("sigaction");
			exit(1);
		}
		
		// ensure SIGUSR1 and SIGUSR2 are unblocked
		if (sigemptyset(&mask))
		{
			perror("sigemptyset");
			exit(1);
		}
		if (sigaddset(&mask, SIGUSR1))
		{
			perror("sigaddset");
			exit(1);
		}
		if (sigaddset(&mask, SIGUSR2))
		{
			perror("sigaddset");
			exit(1);
		}
		if (sigprocmask(SIG_UNBLOCK, &mask, NULL))
		{
			perror("sigprocmask");
			exit(1);
		}
		
		// set parent death signal to send SIGUSR1
		if (prctl(PR_SET_PDEATHSIG, SIGUSR1))
		{
			perror("prctl");
			exit(1);
		}
		
		// ensure parent isn't already dead
		if (getppid() == 1)
		{
			// exit since parent is already dead
			exit(0);
		}
		
		// exec the overwatch process
		execv(path, my_args->argv);
		
		// exec shouldn't return
		perror("execv");
		exit(1);
	}
	
	// parent case
	
	// ensure the overwatch is placed in its own process group
	setpgid(rtn->o_pid, rtn->o_pid);
	
	// close unused ends of pipe
	close(pipeR[1]);
	close(pipeW[0]);
	
	// cleanup args
	_cti_freeArgs(my_args);
	
	// done
	return rtn;
}

int
_cti_assign_overwatch(cti_overwatch_t *this, pid_t chld_pid)
{
	char	sync;

	// sanity
	if (this == NULL)
		return 1;
		
	// sanity
	if (kill(chld_pid, 0))
	{
		// chld_pid doesn't exist
		_cti_free_overwatch(this);
		return 1;
	}
	
	// write the pid
	if (fwrite(&chld_pid, sizeof(pid_t), 1, this->pipe_w) != 1)
	{
		// fwrite failed
		_cti_free_overwatch(this);
		return 1;
	}
	
	fflush(this->pipe_w);
	
	// read a byte of data, this is the overwatch acknowledge of our pid
	if (fread(&sync, sizeof(char), 1, this->pipe_r) != 1)
	{
		// read failed
		_cti_free_overwatch(this);
		return 1;
	}
	
	// cleanup the pipes in the overwatch obj - we are done with them
	fclose(this->pipe_w);
	this->pipe_w = NULL;
	fclose(this->pipe_r);
	this->pipe_r = NULL;
	
	return 0;
}

void
_cti_exit_overwatch(cti_overwatch_t *this)
{
	return _cti_free_overwatch(this);
}

