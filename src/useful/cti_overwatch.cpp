/******************************************************************************\
 * cti_overwatch_process.c - cti overwatch process used to ensure child
 *                           processes will be cleaned up on unexpected exit.
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

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <csignal>

#include <algorithm>
#include <tuple>
#include <set>
#include <unordered_map>
#include <future>
#include <vector>

#include "cti_defs.h"
#include "useful/cti_argv.hpp"

#include "cti_overwatch.hpp"

void
usage(char const *argv0)
{
	fprintf(stdout, "Usage: %s [OPTIONS]...\n", argv0);
	fprintf(stdout, "Create an overwatch process to ensure children are cleaned up on parent exit\n");
	fprintf(stdout, "This should not be called directly.\n\n");

	fprintf(stdout, "\t-%c, --%s  pid of original client application (required)\n",
		CTIOverwatchArgv::ClientPID.val, CTIOverwatchArgv::ClientPID.name);
	fprintf(stdout, "\t-%c, --%s  control msgqueue key (required)\n",
		CTIOverwatchArgv::QueueKey.val, CTIOverwatchArgv::QueueKey.name);
	fprintf(stdout, "\t-%c, --%s  Display this text and exit\n\n",
		CTIOverwatchArgv::Help.val, CTIOverwatchArgv::Help.name);
}

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

// messaging
pid_t clientPid = pid_t{-1};
auto msgQueue = MsgQueue<OverwatchMsgType, OverwatchData>{};

// running apps / utils
auto appList = ProcSet{};
auto utilMap = std::unordered_map<pid_t, ProcSet>{};
volatile bool exiting = false;

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
	// tell signal handling loop to terminate
	exiting = true;

	// terminate all running utilities
	start_thread([&](){ utilMap.clear(); });

	// terminate all running apps
	start_thread([&](){ appList.clear(); });

	// clean up msgQueue
	msgQueue.deregister();

	// wait for all threads
	finish_threads();

	exit(rc);
}

// normal signal handler
void
sig_relay_handler(int sig)
{
	// if client alive
	int status;
	if (waitpid(clientPid, &status, WNOHANG) && !WIFEXITED(status)) {
		// relay signal
		fprintf(stderr, "relay signal: %d\n", sig);
		::kill(clientPid, sig);
	} else {
		// send shutdown message to main thread
		fprintf(stderr, "client died, exiting\n");
		msgQueue.send(OverwatchMsgType::Shutdown, OverwatchData{});
		return;
	}
}

// sigchld handler
void
sigchld_handler(pid_t const exitedPid)
{
	fprintf(stderr, "exitedpid %d\n", exitedPid);

	// abnormal cti termination
	if (exitedPid == clientPid) {
		// send shutdown message to main thread
		msgQueue.send(OverwatchMsgType::Shutdown, OverwatchData{});
		return;

	} else {
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
}

void
sig_wait_loop()
{
	// read all signals
	sigset_t  all_sigs;
	sigfillset(&all_sigs);
	if (sigprocmask(SIG_SETMASK, &all_sigs, nullptr) < 0) {
		// send shutdown message to main thread
		msgQueue.send(OverwatchMsgType::Shutdown, OverwatchData{});
		return;
	}

	while (!exiting) {
		// get active signal
		siginfo_t sig_info;
		errno = 0;
		int sig = sigwaitinfo(&all_sigs, &sig_info);
		if ((errno == EINTR) && (sig < 0)) {
			fprintf(stderr, "interrupted\n");
			continue;
		} else if (sig < 0) {
			perror("sigwaitinfo");
			// send shutdown message to main thread
			msgQueue.send(OverwatchMsgType::Shutdown, OverwatchData{});
			return;
		}

		fprintf(stderr, "got signal: %d\n", sig);

		if (exiting) {
			return;
		}

		// dispatch to handler
		if ((sig == SIGCHLD) && (sig_info.si_code == CLD_EXITED)) {
			if (sig_info.si_pid > 1) {
				sigchld_handler(sig_info.si_pid);
			} else {
				sigchld_handler(clientPid);
			}
		} else {
			sig_relay_handler(sig);
		}
	}
}

int 
main(int argc, char *argv[])
{
	// parse incoming argv for main client PID and message queue key
	{ auto incomingArgv = cti_argv::IncomingArgv<CTIOverwatchArgv>{argc, argv};
		int c; std::string optarg;
		while (true) {
			std::tie(c, optarg) = incomingArgv.get_next();
			if (c < 0) {
				break;
			}

			switch (c) {

			case CTIOverwatchArgv::ClientPID.val:
				clientPid = std::stoll(optarg);
				fprintf(stderr, "client pid %d\n", clientPid);
				break;

			case CTIOverwatchArgv::QueueKey.val:
				msgQueue = MsgQueue<OverwatchMsgType, OverwatchData>{std::stoi(optarg)};
				fprintf(stderr, "msgqueue key %d\n", std::stoi(optarg));
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

	// verify client pid and existence of message queue
	if ((clientPid < 0) || !msgQueue) {
		usage(argv[0]);
		exit(1);
	}

	// block all signals
	sigset_t  all_sigs;
	sigfillset(&all_sigs);
	if (sigprocmask(SIG_SETMASK, &all_sigs, nullptr) < 0) {
		perror("sigprocmask");
		exit(1);
	}

	// start signal handling thread
	std::thread{sig_wait_loop}.detach();

	// wait for msgQueue command
	while (true) {
		OverwatchMsgType msgType; OverwatchData msgData;
		std::tie(msgType, msgData) = msgQueue.recv();

		fprintf(stderr, "msg type %ld data %d %d\n", msgType, msgData.appPid, msgData.utilPid);

		switch (msgType) {

		case OverwatchMsgType::AppRegister:
			if (msgData.appPid > 0) {

				// register app pid
				appList.insert(msgData.appPid);

			} else {
				throw std::runtime_error("invalid app pid: " + std::to_string(msgData.appPid));
			}
			break;

		case OverwatchMsgType::UtilityRegister:
			if (msgData.appPid >= 0) {
				if (msgData.utilPid > 0) {

					// register app pid if valid and not tracked
					if ((msgData.appPid > 0) && !appList.contains(msgData.appPid)) {
						appList.insert(msgData.appPid);
					}

					// register utility pid to app
					utilMap[msgData.appPid].insert(msgData.utilPid);

				} else {
					throw std::runtime_error("invalid util pid: " + std::to_string(msgData.utilPid));
				}
			} else {
				throw std::runtime_error("invalid app pid: " + std::to_string(msgData.appPid));
			}
			break;

		case OverwatchMsgType::AppDeregister:
			if (msgData.appPid > 0) {

				// terminate all of app's utilities
				start_thread([&](){ utilMap.erase(msgData.appPid); });

				// ensure app is terminated
				if (appList.contains(msgData.appPid)) {
					start_thread([&](){ tryTerm(msgData.appPid); });
					start_thread([&](){ appList.erase(msgData.appPid); });
				}

			} else {
				throw std::runtime_error("invalid app pid: " + std::to_string(msgData.appPid));
			}
			break;

		case OverwatchMsgType::Shutdown:
			shutdown_and_exit(0);

		default:
			fprintf(stderr, "unknown msg type %ld data %d %d\n", msgType, msgData.appPid, msgData.utilPid);
			break;

		}
	}
}

