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

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <tuple>
#include <set>
#include <unordered_map>
#include <future>
#include <vector>
#include <tuple>

#include "cti_defs.h"
#include "useful/cti_argv.hpp"

#include "cti_overwatch.hpp"


static void tryTerm(pid_t const pid)
{
	fprintf(stderr, "tryterm %d\n", pid);
	if (::kill(pid, SIGTERM)) {
		return;
	}
	::sleep(3);
	::kill(pid, SIGKILL);
	::waitpid(pid, nullptr, 0);
}

/* types */

struct ProcSet
{
	std::set<pid_t> m_pids;

	ProcSet() {}

	ProcSet(ProcSet&& moved)
		: m_pids{std::move(moved.m_pids)}
	{
		moved.m_pids.clear();
	}

	void clear()
	{
		// copy and clear member
		auto const pids = m_pids;
		m_pids.clear();

		// create futures
		std::vector<std::future<void>> termFutures;
		termFutures.reserve(m_pids.size());

		// terminate in parallel
		for (auto&& pid : pids) {
			fprintf(stderr, "terminating pid %d\n", pid);
			termFutures.emplace_back(std::async(std::launch::async, tryTerm, pid));
		}

		// collect
		for (auto&& future : termFutures) {
			future.wait();
		}
	}

	~ProcSet()
	{
		if (!m_pids.empty()) {
			clear();
		}
	}

	void insert(pid_t const pid)   { m_pids.insert(pid); }
	void erase(pid_t const pid)    { m_pids.erase(pid);  }
	bool contains(pid_t const pid) { return (m_pids.find(pid) != m_pids.end()); }
};

/* global variables */

// running apps / utils
auto appList = ProcSet{};
auto utilMap = std::unordered_map<pid_t, ProcSet>{};

// communication
int reqFd  = -1; // incoming request pipe
int respFd = -1; // outgoing response pipe

// threading helpers
std::vector<std::future<void>> runningThreads;
template <typename Func>
void start_thread(Func&& func) {
	runningThreads.emplace_back(std::async(std::launch::async, func));
}
void finish_threads() {
	for (auto&& future : runningThreads) {
		future.wait();
	}
}

void shutdown_and_exit(int const rc)
{
	// terminate all running utilities
	start_thread([&](){ utilMap.clear(); });

	// terminate all running apps
	start_thread([&](){ appList.clear(); });

	// wait for all threads
	finish_threads();

	// close pipes
	close(reqFd);
	close(respFd);

	exit(rc);
}

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
		if (::kill(pid, SIGTERM))
		{
			// process doesn't exist, so simply exit
			exit(0);
		}
		// sleep five seconds
		::sleep(5);
		// send sigkill
		::kill(pid, SIGKILL);
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
	char				done = 1;

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

	// setup the signal mask
	struct sigaction sig_action;
	memset(&sig_action, 0, sizeof(sig_action));
	if (sigfillset(&sig_action.sa_mask)) {
		perror("sigfillset");
		return 1;
	}

	// set handler for all signals
	sig_action.sa_handler = cti_overwatch_handler;
	if (sigaction(SIGUSR1, &sig_action, nullptr)) {
		perror("sigaction");
		return 1;
	}

	// write our PID to signal to the parent we are all set up
	rawWriteLoop(respFd, PIDResp 
		{ .type = OverwatchRespType::PID
		, .pid  = getpid()
	});

	// sleep until we get a signal
	pause();

	// we should not get here
	shutdown_and_exit(1);
}
