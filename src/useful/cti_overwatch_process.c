/******************************************************************************\
 * cti_overwatch_process.c - cti overwatch process used to ensure child
 *                           processes will be cleaned up on unexpected exit.
 *
 * Copyright 2014-2019 Cray Inc.  All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
 *
 ******************************************************************************/

// This pulls in config.h
#include "cti_defs.h"

#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>

// global vars
volatile pid_t	pid = 0;

const struct option long_opts[] = {
			{"read",		required_argument,	0, 'r'},
			{"write",		required_argument,	0, 'w'},
			{"help",		no_argument,		0, 'h'},
			{0, 0, 0, 0}
			};

void
usage(char *name)
{
	fprintf(stdout, "Usage: %s [OPTIONS]...\n", name);
	fprintf(stdout, "Create an overwatch process to ensure children are cleaned up on parent exit\n");
	fprintf(stdout, "This should not be called directly.\n\n");

	fprintf(stdout, "\t-r, --read      fd of read control pipe         (required)\n");
	fprintf(stdout, "\t-w, --write     fd of write control pipe        (required)\n");
	fprintf(stdout, "\t-h, --help      Display this text and exit\n\n");
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

	// we require at least 3 args
	if (argc < 3)
	{
		usage(argv[0]);
		return 1;
	}

	// parse the provide args
	while ((c = getopt_long(argc, argv, "r:w:h", long_opts, &opt_ind)) != -1)
	{
		switch (c)
		{
			case 0:
				// if this is a flag, do nothing
				break;

			case 'r':
				if (optarg == NULL)
				{
					usage(argv[0]);
					return 1;
				}

				// convert the string into the actual fd
				errno = 0;
				end_p = NULL;
				val = strtol(optarg, &end_p, 10);

				// check for errors
				if ((errno == ERANGE && (val == LONG_MAX || val == LONG_MIN)) || (errno != 0 && val == 0))
				{
					perror("strtol");
					return 1;
				}
				if (end_p == NULL || *end_p != '\0')
				{
					perror("strtol");
					return 1;
				}
				if (val > INT_MAX || val < INT_MIN)
				{
					fprintf(stderr, "Invalid read fd argument.\n");
					return 1;
				}

				rfp = fdopen((int)val, "r");
				if (rfp == NULL)
				{
					fprintf(stderr, "Invalid read fd argument.\n");
					return 1;
				}

				break;

			case 'w':
				if (optarg == NULL)
				{
					usage(argv[0]);
					return 1;
				}

				// convert the string into the actual fd
				errno = 0;
				end_p = NULL;
				val = strtol(optarg, &end_p, 10);

				// check for errors
				if ((errno == ERANGE && (val == LONG_MAX || val == LONG_MIN)) || (errno != 0 && val == 0))
				{
					perror("strtol");
					return 1;
				}
				if (end_p == NULL || *end_p != '\0')
				{
					perror("strtol");
					return 1;
				}
				if (val > INT_MAX || val < INT_MIN)
				{
					fprintf(stderr, "Invalid write fd argument.\n");
					return 1;
				}

				wfp = fdopen((int)val, "w");

				if (wfp == NULL)
				{
					fprintf(stderr, "Invalid write fd argument.\n");
					return 1;
				}

				break;

			case 'h':
				usage(argv[0]);
				return 0;

			default:
				usage(argv[0]);
				return 1;
		}
	}

	// post-process required args to make sure we have everything we need
	if (rfp == NULL || wfp == NULL)
	{
		usage(argv[0]);
		return 1;
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

