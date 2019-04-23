/******************************************************************************\
 * cti_fe_daemon.cpp - cti fe_daemon process used to ensure child
 *                     processes will be cleaned up on unexpected exit.
 *
 * Copyright 2019 Cray Inc.  All Rights Reserved.
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
#include <future>
#include <vector>
#include <unordered_set>

#include "cti_defs.h"
#include "useful/cti_argv.hpp"
#include "useful/cti_execvp.hpp"

#include "cti_fe_daemon_iface.hpp"

using ReqType   = cti::fe_daemon::ReqType;
using LaunchReq = cti::fe_daemon::LaunchReq;
using AppReq    = cti::fe_daemon::AppReq;
using UtilReq   = cti::fe_daemon::UtilReq;

using RespType  = cti::fe_daemon::RespType;
using OKResp    = cti::fe_daemon::OKResp;
using PIDResp   = cti::fe_daemon::PIDResp;

static void
tryTerm(pid_t const pid)
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
	std::unordered_set<pid_t> m_pids;

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
static void start_thread(Func&& func) {
	runningThreads.emplace_back(std::async(std::launch::async, func));
}
static void finish_threads() {
	for (auto&& future : runningThreads) {
		future.wait();
	}
}

/* runtime helpers */

static void
usage(char *name)
{
	fprintf(stdout, "Usage: %s [OPTIONS]...\n", name);
	fprintf(stdout, "Create fe_daemon process to ensure children are cleaned up on parent exit\n");
	fprintf(stdout, "This should not be called directly.\n\n");

	fprintf(stdout, "\t-%c, --%s  fd of read control pipe         (required)\n",
		CTIOverwatchArgv::ReadFD.val, CTIOverwatchArgv::ReadFD.name);
	fprintf(stdout, "\t-%c, --%s  fd of write control pipe        (required)\n",
		CTIOverwatchArgv::WriteFD.val, CTIOverwatchArgv::WriteFD.name);
	fprintf(stdout, "\t-%c, --%s  Display this text and exit\n\n",
		CTIOverwatchArgv::Help.val, CTIOverwatchArgv::Help.name);
}

static void
shutdown_and_exit(int const rc)
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
	auto utilTermFuture = std::async(std::launch::async, [&](){ utilMap.clear(); });

	// terminate all running apps
	auto appTermFuture = std::async(std::launch::async, [&](){ appList.clear(); });

	// wait for all threads
	utilTermFuture.wait();
	appTermFuture.wait();
	finish_threads();

	// close pipes
	close(reqFd);
	close(respFd);

	exit(rc);
}

/* signal handlers */

static void
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
static void
cti_fe_daemon_handler(int sig, siginfo_t *sig_info, void *secret)
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

/* registration helpers */

static void
registerAppPID(pid_t const app_pid)
{
	if ((app_pid > 0) && !appList.contains(app_pid)) {
		// register app pid
		appList.insert(app_pid);
	} else {
		throw std::runtime_error("invalid app pid: " + std::to_string(app_pid));
	}
}

static void
registerUtilPID(pid_t const app_pid, pid_t const util_pid)
{
	// verify app pid
	if (app_pid <= 0) {
		throw std::runtime_error("invalid app pid: " + std::to_string(app_pid));
	}

	// register app pid if valid and not tracked
	if ((app_pid > 0) && !appList.contains(app_pid)) {
		registerAppPID(app_pid);
	}

	// register utility pid to app
	if ((app_pid > 0) && (util_pid > 0)) {
		utilMap[app_pid].insert(util_pid);
	} else {
		throw std::runtime_error("invalid util pid: " + std::to_string(util_pid));
	}
}

/* pipe command helpers */

// write a boolean response to pipe
static void
writeOKResp(int const respFd, bool  const success)
{
	rawWriteLoop(respFd, OKResp
		{ .type = RespType::OK
		, .success = success
	});
}

// write a pid response to pipe
static void
writePIDResp(int const respFd, pid_t const pid)
{
	rawWriteLoop(respFd, PIDResp
		{ .type = RespType::PID
		, .pid  = pid
	});
}

// read filename, argv, environment map appended to an app / util / mpir launch request
static std::tuple<std::string, cti::ManagedArgv, std::unordered_map<std::string, std::string>>
readLaunchReq(int const reqFd)
{
	// receive a single null-terminated string from stream
	auto receiveString = [](std::istream& reqStream) {
		std::string result;
		if (!std::getline(reqStream, result, '\0')) {
			throw std::runtime_error("failed to read string");
		}
		return result;
	};

	// set up pipe stream
	cti::FdBuf reqBuf{dup(reqFd)};
	std::istream reqStream{&reqBuf};

	// read filename
	auto const filename = receiveString(reqStream);
	fprintf(stderr, "got file: %s\n", filename.c_str());

	// read arguments
	cti::ManagedArgv argv;
	while (true) {
		auto const arg = receiveString(reqStream);
		if (arg.empty()) {
			break;
		} else {
			argv.add(arg);
			fprintf(stderr, "got arg: %s\n", arg.c_str());
		}
	}

	// read env
	std::unordered_map<std::string, std::string> envMap;
	std::string envVarVal;
	while (true) {
		auto const envVarVal = receiveString(reqStream);
		if (envVarVal.empty()) {
			break;
		} else {
			auto const equalsAt = envVarVal.find("=");
			if (equalsAt == std::string::npos) {
				throw std::runtime_error("failed to parse env var: " + envVarVal);
			} else {
				fprintf(stderr, "got envvar: %s\n", envVarVal.c_str());
				envMap.emplace(envVarVal.substr(0, equalsAt), envVarVal.substr(equalsAt + 1));
			}
		}
	}

	return std::make_tuple(filename, std::move(argv), envMap);
}

// if running the pid-producing function succeeds, write a PID response to pipe
template <typename Func>
static void
tryWritePIDResp(int const respFd, Func&& func)
{
	try {
		// run the pid-producing function
		auto const pid = func();

		// send PID response
		writePIDResp(respFd, pid);

	} catch (std::exception const& ex) {
		fprintf(stderr, "%s\n", ex.what());

		// send failure response
		writePIDResp(respFd, pid_t{-1});
	}
}

// if running the function succeeds, write an OK response to pipe
template <typename Func>
static void
tryWriteOKResp(int const respFd, Func&& func)
{
	try {
		// run the function
		func();

		// send OK response
		writeOKResp(respFd, true);

	} catch (std::exception const& ex) {
		fprintf(stderr, "%s\n", ex.what());

		// send failure response
		writeOKResp(respFd, false);
	}
}

/* request handlers */

static pid_t
handle_launchReq(LaunchReq const& launchReq)
{
	// read filename, argv, env array from pipe
	std::string filename;
	cti::ManagedArgv argv;
	std::unordered_map<std::string, std::string> envMap;
	std::tie(filename, argv, envMap) = readLaunchReq(reqFd);

	// fork exec
	if (auto const forkedPid = fork()) {
		if (forkedPid < 0) {
			throw std::runtime_error("fork error: " + std::string{strerror(errno)});
		}

		// parent case

		return forkedPid;
	} else {
		// child case

		// close communication pipes
		close(reqFd);
		close(respFd);

		// dup2 all stdin/out/err to provided FDs
		dup2(open("/dev/null", O_RDONLY), STDIN_FILENO);
		dup2((launchReq.stdout_fd < 0) ? open("/dev/null", O_WRONLY) : launchReq.stdout_fd, STDOUT_FILENO);
		dup2((launchReq.stderr_fd < 0) ? open("/dev/null", O_WRONLY) : launchReq.stderr_fd, STDERR_FILENO);

		// set environment variables with overwrite
		for (auto const& envVarVal : envMap) {
			if (!envVarVal.second.empty()) {
				setenv(envVarVal.first.c_str(), envVarVal.second.c_str(), true);
			} else {
				unsetenv(envVarVal.first.c_str());
			}
		}

		// exec srun
		execvp(filename.c_str(), argv.get());
		perror("execvp");
		exit(1);
	}
}

static void
handle_deregisterAppReq(AppReq const& deregisterReq)
{
	if (deregisterReq.app_pid > 0) {
		// terminate all of app's utilities
		auto utilTermFuture = std::async(std::launch::async,
			[&](){ utilMap.erase(deregisterReq.app_pid); });

		// ensure app is terminated
		if (appList.contains(deregisterReq.app_pid)) {
			auto appTermFuture = std::async(std::launch::async, [&](){ tryTerm(deregisterReq.app_pid); });
			appList.erase(deregisterReq.app_pid);
			appTermFuture.wait();
		}

		// finish util termination
		utilTermFuture.wait();
	} else {
		throw std::runtime_error("invalid app pid: " + std::to_string(deregisterReq.app_pid));
	}
}

int
main(int argc, char *argv[])
{
	// parse incoming argv for request and response FDs
	{ auto incomingArgv = cti::IncomingArgv<CTIOverwatchArgv>{argc, argv};
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
	sig_action.sa_sigaction = cti_fe_daemon_handler;
	if (sigaction(SIGTERM, &sig_action, nullptr) ||
	    sigaction(SIGCHLD, &sig_action, nullptr) ||
	    sigaction(SIGPIPE, &sig_action, nullptr) ||
	    sigaction(SIGHUP,  &sig_action, nullptr)) {
		perror("sigaction");
		return 1;
	}

	// write our PID to signal to the parent we are all set up
	fprintf(stderr, "%d sending initial ok\n", getpid());
	writePIDResp(respFd, getpid());

	// wait for pipe commands
	while (true) {
		auto const reqType = rawReadLoop<ReqType>(reqFd);
		fprintf(stderr, "req type %ld\n", reqType);

		switch (reqType) {

			case ReqType::ForkExecvpApp:
				tryWritePIDResp(respFd, [&]() {
					auto const launchReq = rawReadLoop<LaunchReq>(reqFd);

					// fork exec the app based on launch request
					auto const forkedPid = handle_launchReq(launchReq);
					// register the new app
					registerAppPID(forkedPid);

					return forkedPid;
				});
				break;

			case ReqType::ForkExecvpUtil:
				tryWritePIDResp(respFd, [&]() {
					auto const launchReq = rawReadLoop<LaunchReq>(reqFd);

					// fork exec the util based on launch request
					auto const forkedPid = handle_launchReq(launchReq);
					// register the new utility
					registerUtilPID(launchReq.app_pid, forkedPid);

					return forkedPid;
				});
				break;

#ifdef MPIR
			case ReqType::LaunchMPIR:
				tryWritePIDResp(respFd, [&]() {
					auto const launchReq = rawReadLoop<LaunchReq>(reqFd);

					fprintf(stderr, "not implemented: LaunchMPIR\n");
					shutdown_and_exit(1);
					return 0;
				});
				break;

			case ReqType::ReleaseMPIR:
				tryWriteOKResp(respFd, [&]() {
					auto const releaseMPIRReq = rawReadLoop<ReleaseMPIRReq>(reqFd);

					fprintf(stderr, "not implemented: ReleaseMPIR\n");
					shutdown_and_exit(1);
				});
				break;
#else

			case ReqType::RegisterApp:
				tryWriteOKResp(respFd, [&]() {
					auto const registerReq = rawReadLoop<AppReq>(reqFd);

					// register the new app
					registerAppPID(registerReq.app_pid);
				});
				break;

			case ReqType::RegisterUtil:
				fprintf(stderr, "start register\n");
				tryWriteOKResp(respFd, [&]() {
					fprintf(stderr, "read register\n");
					auto const registerReq = rawReadLoop<UtilReq>(reqFd);

					// register the new utility
					fprintf(stderr, "run register\n");
					registerUtilPID(registerReq.app_pid, registerReq.util_pid);
				});
				fprintf(stderr, "ok sent\n");
				break;
#endif

			case ReqType::DeregisterApp:
				tryWriteOKResp(respFd, [&]() {
					handle_deregisterAppReq(rawReadLoop<AppReq>(reqFd));
				});
				break;

			case ReqType::Shutdown:
				// send OK response and run shutdown
				writeOKResp(respFd, true);
				shutdown_and_exit(0);
				break;

			default:
				fprintf(stderr, "unknown req type %ld\n", reqType);
				break;

		}
	}

	// we should not get here
	shutdown_and_exit(1);
}
