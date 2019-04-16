/******************************************************************************\
 * cti_overwatch.cpp - cti overwatch process used to ensure child
 *                     processes will be cleaned up on unexpected exit.
 *
 * Copyright 2014 Cray Inc.  All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
 *
 ******************************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>

#include <tuple>

#include "cti_defs.h"
#include "useful/cti_argv.hpp"

#include "cti_overwatch.hpp"

/* global vars */
volatile pid_t	pid = 0;

void
usage(char *name)
{
	fprintf(stdout, "Usage: %s [OPTIONS]...\n", name);
	fprintf(stdout, "Create an overwatch process to ensure children are cleaned up on parent exit\n");
	fprintf(stdout, "This should not be called directly.\n\n");

	fprintf(stdout, "\t-%c, --%s  fd of read control pipe         (required)\n",
		CTIOverwatchArgv::ReadFD.val, CTIOverwatchArgv::ReadFD.name);
	fprintf(stdout, "\t-%c, --%s  fd of write control pipe        (required)\n",
		CTIOverwatchArgv::WriteFD.val, CTIOverwatchArgv::WriteFD.name);
	fprintf(stdout, "\t-%c, --%s  Display this text and exit\n\n",
		CTIOverwatchArgv::Help.val, CTIOverwatchArgv::Help.name);
}

// signal handler to kill the child
void
cti_overwatch_handler(int sig)
{
	if (pid != 0)
	{
		// send sigterm
		if (kill(pid, SIGTERM))
		{
			// process doesn't exist, so simply exit
			exit(0);
		}
		// sleep five seconds
		sleep(5);
		// send sigkill
		kill(pid, SIGKILL);
		// exit
		exit(0);
	}
	
	// no pid, so exit
	exit(1);
}

// signal handler that causes us to exit
void
cti_exit_handler(int sig)
{
	// simply exit
	exit(0);
}

int 
main(int argc, char *argv[])
{
	int					opt_ind = 0;
	int					c;
	long int			val;
	char *				end_p;
	FILE *				rfp = NULL;
	FILE *				wfp = NULL;
	pid_t				my_pid;
	sigset_t			mask;
	struct sigaction	sig_action;
	char				done = 1;

	int reqFd  = -1;
	int respFd = -1;

	// parse incoming argv for request and response FDs
	{ auto incomingArgv = cti_argv::IncomingArgv<CTIOverwatchArgv>{argc, argv};
		int c; std::string optarg;
		while (true) {
			std::tie(c, optarg) = incomingArgv.get_next();
			if (c < 0) {
				break;
			}

			switch (c) {

			case CTIOverwatchArgv::ReadFD.val:
				reqFd = std::stoi(optarg);
				break;

			case CTIOverwatchArgv::WriteFD.val:
				respFd = std::stoi(optarg);
				break;

			case CTIOverwatchArgv::Help.val:
				usage(argv[0]);
				exit(0);

			case '?':
			default:
				usage(argv[0]);
				exit(1);

			}
		}
	}

	// post-process required args to make sure we have everything we need
	if ((reqFd < 0) || (respFd < 0)) {
		usage(argv[0]);
		exit(1);
	}

	// read the pid from the pipe
	if (fread(&my_pid, sizeof(pid_t), 1, rfp) != 1)
	{
		// read failed
		perror("fread");
		return 1;
	}
	pid = my_pid;
	
	// ensure all signals except SIGUSR1 and SIGUSR2 are blocked
	if (sigfillset(&mask))
	{
		perror("sigfillset");
		return 1;
	}
	if (sigdelset(&mask, SIGUSR1))
	{
		perror("sigdelset");
		return 1;
	}
	if (sigdelset(&mask, SIGUSR2))
	{
		perror("sigdelset");
		return 1;
	}
	if (sigprocmask(SIG_SETMASK, &mask, NULL))
	{
		perror("sigprocmask");
		return 1;
	}
	
	// setup the signal handler
	memset(&sig_action, 0, sizeof(sig_action));
	if (sigfillset(&sig_action.sa_mask))
	{
		perror("sigfillset");
		return 1;
	}
	
	// set handler for SIGUSR1
	sig_action.sa_handler = cti_overwatch_handler;
	if (sigaction(SIGUSR1, &sig_action, NULL))
	{
		perror("sigaction");
		return 1;
	}
	
	// set handler for SIGUSR2
	sig_action.sa_handler = cti_exit_handler;
	if (sigaction(SIGUSR2, &sig_action, NULL))
	{
		perror("sigaction");
		return 1;
	}
	
	// write the done byte to signal to the parent we are all set up
	if (fwrite(&done, sizeof(char), 1, wfp) != 1)
	{
		// fwrite failed
		perror("fwrite");
		return 1;
	}
	
	// close our pipes
	fclose(rfp);
	fclose(wfp);
	
	// sleep until we get a signal
	pause();
	
	// we should not get here
	fprintf(stderr, "Exec past pause!\n");
	return 1;
}
