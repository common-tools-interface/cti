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
	// block all signals
	sigset_t block_set;
	memset(&block_set, 0, sizeof(block_set));
	if (sigfillset(&block_set)) {
		perror("sigfillset");
		exit(1);
	}
	if (sigprocmask(SIG_SETMASK, &block_set, nullptr)) {
		perror("sigprocmask");
		exit(1);
	}

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

/* signal handlers */

void
sigchld_handler(pid_t const exitedPid)
{
	// regular app termination
	if (appList.contains(exitedPid)) {
		// app already terminated
		appList.erase(exitedPid);
	}
	if (utilMap.find(exitedPid) != utilMap.end()) {
		// terminate all of app's utilities
		start_thread([&](){ utilMap.erase(exitedPid); });
	}
}

// dispatch to sigchld / term handler
void
cti_overwatch_handler(int sig, siginfo_t *sig_info, void *secret)
{
	if (sig == SIGCHLD) {
		if ((sig_info->si_code == CLD_EXITED) && (sig_info->si_pid > 1)) {
			sigchld_handler(sig_info->si_pid);
		}
	} else if ((sig == SIGTERM) || (sig == SIGHUP)) {
		shutdown_and_exit(0);
	} else {
		// TODO: determine which signals should be relayed to child
	}
}

// pipe command handlers

static void handle_forkExecvpAppReq(LaunchReq const& launchReq)
{
	fprintf(stderr, "not implemented: ForkExecvpApp\n");
	shutdown_and_exit(1);
}

static void handle_forkExecvpUtilReq(LaunchReq const& launchReq)
{
	fprintf(stderr, "not implemented: ForkExecvpUtil\n");
	shutdown_and_exit(1);
}

#ifdef MPIR

static void handle_launchMPIRReq(LaunchReq const& launchReq)
{
	fprintf(stderr, "not implemented: LaunchMPIR\n");
	shutdown_and_exit(1);
}

static void handle_releaseMPIRReq(ReleaseMPIRReq const& releaseMPIRReq)
{
	fprintf(stderr, "not implemented: ReleaseMPIR\n");
	shutdown_and_exit(1);
}

#else

static void handle_registerAppReq(AppReq const& registerReq)
{
	if (registerReq.app_pid > 0) {
		// register app pid
		appList.insert(registerReq.app_pid);

		// send OK response
		rawWriteLoop(respFd, OKResp
			{ .type = OverwatchRespType::OK
			, .success = true
		});
	} else {
		throw std::runtime_error("invalid app pid: " + std::to_string(registerReq.app_pid));
	}
}

static void handle_registerUtilReq(UtilReq const& registerReq)
{
	// register app pid if valid and not tracked
	if ((registerReq.app_pid > 0) && !appList.contains(registerReq.app_pid)) {
		appList.insert(registerReq.app_pid);
	}

	// register utility pid to app
	if ((registerReq.app_pid > 0) && (registerReq.util_pid > 0)) {
		utilMap[registerReq.app_pid].insert(registerReq.util_pid);
	} else {
		throw std::runtime_error("invalid util pid: " + std::to_string(registerReq.util_pid));
	}

	// send OK response
	rawWriteLoop(respFd, OKResp
		{ .type = OverwatchRespType::OK
		, .success = true
	});
}

#endif

static void handle_deregisterAppReq(AppReq const& deregisterReq)
{
	if (deregisterReq.app_pid > 0) {
		// terminate all of app's utilities
		start_thread([&](){ utilMap.erase(deregisterReq.app_pid); });

		// ensure app is terminated
		if (appList.contains(deregisterReq.app_pid)) {
			start_thread([&](){ tryTerm(deregisterReq.app_pid); });
			start_thread([&](){ appList.erase(deregisterReq.app_pid); });
		}

		// send OK response
		rawWriteLoop(respFd, OKResp
			{ .type = OverwatchRespType::OK
			, .success = true
		});
	} else {
		throw std::runtime_error("invalid app pid: " + std::to_string(deregisterReq.app_pid));
	}
}

static void handle_shutdownReq()
{
	// send OK response
	rawWriteLoop(respFd, OKResp
		{ .type = OverwatchRespType::OK
		, .success = true
	});

	// run shutdown
	shutdown_and_exit(0);
}

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

int
main(int argc, char *argv[])
{
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

	// block all signals except SIGTERM, SIGCHLD, SIGPIPE, SIGHUP
	sigset_t block_set;
	memset(&block_set, 0, sizeof(block_set));
	if (sigfillset(&block_set)) {
		perror("sigfillset");
		return 1;
	}
	if (sigdelset(&block_set, SIGTERM) ||
	    sigdelset(&block_set, SIGCHLD) ||
	    sigdelset(&block_set, SIGPIPE) ||
	    sigdelset(&block_set, SIGHUP)) {
		perror("sigdelset");
		return 1;
	}
	if (sigprocmask(SIG_SETMASK, &block_set, nullptr)) {
		perror("sigprocmask");
		return 1;
	}


	// set handler for SIGTERM, SIGCHLD, SIGPIPE, SIGHUP
	struct sigaction sig_action;
	sig_action.sa_flags = SA_RESTART | SA_SIGINFO;
	sig_action.sa_sigaction = cti_overwatch_handler;
	if (sigaction(SIGTERM, &sig_action, nullptr) ||
	    sigaction(SIGCHLD, &sig_action, nullptr) ||
	    sigaction(SIGPIPE, &sig_action, nullptr) ||
	    sigaction(SIGHUP,  &sig_action, nullptr)) {
		perror("sigaction");
		return 1;
	}

	// write our PID to signal to the parent we are all set up
	fprintf(stderr, "%d sending initial ok\n", getpid());
	rawWriteLoop(respFd, PIDResp 
		{ .type = OverwatchRespType::PID
		, .pid  = getpid()
	});

	// wait for pipe commands
	while (true) {
		auto const reqType = rawReadLoop<OverwatchReqType>(reqFd);
		fprintf(stderr, "req type %ld\n", reqType);

		switch (reqType) {

			case OverwatchReqType::ForkExecvpApp:
				handle_forkExecvpAppReq(rawReadLoop<LaunchReq>(reqFd));
				break;

			case OverwatchReqType::ForkExecvpUtil:
				handle_forkExecvpUtilReq(rawReadLoop<LaunchReq>(reqFd));
				break;

#ifdef MPIR
			case OverwatchReqType::LaunchMPIR:
				handle_launchMPIRReq(rawReadLoop<LaunchReq>(reqFd));
				break;

			case OverwatchReqType::ReleaseMPIR:
				handle_releaseMPIRReq(rawReadLoop<ReleaseMPIRReq>(reqFd));
				break;
#else

			case OverwatchReqType::RegisterApp:
				handle_registerAppReq(rawReadLoop<AppReq>(reqFd));
				break;

			case OverwatchReqType::RegisterUtil:
				handle_registerUtilReq(rawReadLoop<UtilReq>(reqFd));
				break;
#endif

			case OverwatchReqType::DeregisterApp:
				handle_deregisterAppReq(rawReadLoop<AppReq>(reqFd));
				break;

			case OverwatchReqType::Shutdown:
				handle_shutdownReq();
				break;

			default:
				fprintf(stderr, "unknown req type %ld\n", reqType);
				break;

		}
	}

	// we should not get here
	shutdown_and_exit(1);
}
