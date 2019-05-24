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

// This pulls in config.h
#include "cti_defs.h"
#include "cti_argv_defs.hpp"

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
#include <unordered_map>
#include <unordered_set>
#include <memory>

#include "cti_defs.h"
#include "useful/cti_argv.hpp"
#include "useful/cti_execvp.hpp"

#include "frontend/mpir_iface/MPIRInstance.hpp"
#include "cti_fe_daemon_iface.hpp"

using ReqType   = FE_daemon::ReqType;
using RespType  = FE_daemon::RespType;
using OKResp    = FE_daemon::OKResp;
using PIDResp   = FE_daemon::PIDResp;
using MPIRResp  = FE_daemon::MPIRResp;

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

auto mpirMap = std::unordered_map<FE_daemon::MPIRId, std::unique_ptr<MPIRInstance>>{};
static FE_daemon::MPIRId newMPIRId() {
	static auto id = FE_daemon::MPIRId{0};
	return ++id;
}

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
		CTIFEDaemonArgv::ReadFD.val, CTIFEDaemonArgv::ReadFD.name);
	fprintf(stdout, "\t-%c, --%s  fd of write control pipe        (required)\n",
		CTIFEDaemonArgv::WriteFD.val, CTIFEDaemonArgv::WriteFD.name);
	fprintf(stdout, "\t-%c, --%s  Display this text and exit\n\n",
		CTIFEDaemonArgv::Help.val, CTIFEDaemonArgv::Help.name);
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

static void registerAppPID(pid_t const app_pid)
{
	if ((app_pid > 0) && !appList.contains(app_pid)) {
		// register app pid
		appList.insert(app_pid);
	} else {
		throw std::runtime_error("invalid app pid: " + std::to_string(app_pid));
	}
}

static void registerUtilPID(pid_t const app_pid, pid_t const util_pid)
{
	// verify app pid
	if (app_pid <= 0) {
		throw std::runtime_error("invalid app pid: " + std::to_string(app_pid));
	}

	// register app pid if valid and not tracked
	if (!appList.contains(app_pid)) {
		registerAppPID(app_pid);
	}

	// register utility pid to app
	if (util_pid > 0) {
		utilMap[app_pid].insert(util_pid);
	} else {
		throw std::runtime_error("invalid util pid: " + std::to_string(util_pid));
	}
}


static void deregisterAppPID(pid_t const app_pid)
{
	if (app_pid > 0) {
		// terminate all of app's utilities
		auto utilTermFuture = std::async(std::launch::async,
			[&](){ utilMap.erase(app_pid); });

		// ensure app is terminated
		if (appList.contains(app_pid)) {
			auto appTermFuture = std::async(std::launch::async, [&](){ tryTerm(app_pid); });
			appList.erase(app_pid);
			appTermFuture.wait();
		}

		// finish util termination
		utilTermFuture.wait();
	} else {
		throw std::runtime_error("invalid app pid: " + std::to_string(app_pid));
	}
}

/* protocol helpers */

struct LaunchData {
	int stdin_fd, stdout_fd, stderr_fd;
	std::string filepath;
	std::vector<std::string> argvList;
	std::vector<std::string> envList;
};

// read stdin/out/err fds, filepath, argv, environment map appended to an app / util / mpir launch request
static LaunchData readLaunchData(int const reqFd)
{
	LaunchData result;

	// receive a single null-terminated string from stream
	auto receiveString = [](std::istream& reqStream) {
		std::string result;
		if (!std::getline(reqStream, result, '\0')) {
			throw std::runtime_error("failed to read string");
		}
		return result;
	};

	// read and remap stdin/out/err
	auto const N_FDS = 3;
	struct {
		int fd_data[N_FDS];
		struct cmsghdr cmd_hdr;
	} buffer;

	// create message buffer for one character
	struct iovec empty_iovec;
	char c;
	empty_iovec.iov_base = &c;
	empty_iovec.iov_len  = 1;

	// create empty message header with enough space for fds
	struct msghdr msg_hdr = {};
	msg_hdr.msg_iov     = &empty_iovec;
	msg_hdr.msg_iovlen  = 1;
	msg_hdr.msg_control = &buffer;
	msg_hdr.msg_controllen = CMSG_SPACE(sizeof(int) * N_FDS);

	// fill in the message header type
	struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg_hdr);
	cmsg->cmsg_len   = CMSG_LEN(sizeof(int) * N_FDS);
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type  = SCM_RIGHTS;

	// receive remap FD message
	if (::recvmsg(reqFd, &msg_hdr, 0) < 0) {
		throw std::runtime_error("failed to receive fds: " + std::string{strerror(errno)});
	}
	auto const fds = (int *)CMSG_DATA(cmsg);
	result.stdin_fd  = fds[0];
	result.stdout_fd = fds[1];
	result.stderr_fd = fds[2];

	// set up pipe stream
	cti::FdBuf reqBuf{dup(reqFd)};
	std::istream reqStream{&reqBuf};

	// read filepath
	fprintf(stderr, "recv filename\n");
	result.filepath = receiveString(reqStream);
	fprintf(stderr, "got file: %s\n", result.filepath.c_str());

	// read arguments
	while (true) {
		auto const arg = receiveString(reqStream);
		if (arg.empty()) {
			break;
		} else {
			fprintf(stderr, "got arg: %s\n", arg.c_str());
			result.argvList.emplace_back(std::move(arg));
		}
	}

	// read env
	while (true) {
		auto const envVarVal = receiveString(reqStream);
		if (envVarVal.empty()) {
			break;
		} else {
			fprintf(stderr, "got envvar: %s\n", envVarVal.c_str());
			result.envList.emplace_back(std::move(envVarVal));
		}
	}

	return result;
}

// if running the function succeeds, write an OK response to pipe
template <typename Func>
static void tryWriteOKResp(int const respFd, Func&& func)
{
	try {
		// run the function
		func();

		// send OK response
		rawWriteLoop(respFd, OKResp
			{ .type = RespType::OK
			, .success = true
		});

	} catch (std::exception const& ex) {
		fprintf(stderr, "%s\n", ex.what());

		// send failure response
		rawWriteLoop(respFd, OKResp
			{ .type = RespType::OK
			, .success = false
		});
	}
}

// if running the pid-producing function succeeds, write a PID response to pipe
template <typename Func>
static void tryWritePIDResp(int const respFd, Func&& func)
{
	try {
		// run the pid-producing function
		auto const pid = func();

		// send PID response
		rawWriteLoop(respFd, PIDResp
			{ .type = RespType::PID
			, .pid  = pid
		});

	} catch (std::exception const& ex) {
		fprintf(stderr, "%s\n", ex.what());

		// send failure response
		rawWriteLoop(respFd, PIDResp
			{ .type = RespType::PID
			, .pid  = pid_t{-1}
		});
	}
}

// if running the function succeeds, write an MPIR response to pipe
template <typename Func>
static void tryWriteMPIRResp(int const respFd, Func&& func)
{
	try {
		// run the mpir-producing function
		auto const mpirData = func();

		// send the MPIR data
		rawWriteLoop(respFd, MPIRResp
			{ .type     = RespType::MPIR
			, .mpir_id  = mpirData.mpir_id
			, .launcher_pid = mpirData.launcher_pid
			, .job_id   = mpirData.job_id
			, .step_id  = mpirData.step_id
			, .num_pids = static_cast<int>(mpirData.proctable.size())
		});
		for (auto&& elem : mpirData.proctable) {
			rawWriteLoop(respFd, elem.pid);
			writeLoop(respFd, elem.hostname.c_str(), elem.hostname.length() + 1);
		}

	} catch (std::exception const& ex) {
		fprintf(stderr, "%s\n", ex.what());

		// send failure response
		rawWriteLoop(respFd, MPIRResp
			{ .type     = RespType::MPIR
			, .mpir_id  = FE_daemon::MPIRId{0}
		});
	}
}

/* process helpers */

static pid_t forkExec(LaunchData const& launchData)
{
	// construct argv
	cti::ManagedArgv argv;
	for (auto&& arg : launchData.argvList) {
		argv.add(arg);
	}

	// parse env
	std::unordered_map<std::string, std::string> envMap;
	for (auto&& envVarVal : launchData.envList) {
		auto const equalsAt = envVarVal.find("=");
		if (equalsAt == std::string::npos) {
			throw std::runtime_error("failed to parse env var: " + envVarVal);
		} else {
			fprintf(stderr, "got envvar: %s\n", envVarVal.c_str());
			envMap.emplace(envVarVal.substr(0, equalsAt), envVarVal.substr(equalsAt + 1));
		}
	}

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
		dup2(launchData.stdin_fd,  STDIN_FILENO);
		dup2(launchData.stdout_fd, STDOUT_FILENO);
		dup2(launchData.stderr_fd, STDERR_FILENO);

		// set environment variables with overwrite
		for (auto const& envVarVal : envMap) {
			if (!envVarVal.second.empty()) {
				setenv(envVarVal.first.c_str(), envVarVal.second.c_str(), true);
			} else {
				unsetenv(envVarVal.first.c_str());
			}
		}

		// exec srun
		execvp(launchData.filepath.c_str(), argv.get());
		perror("execvp");
		exit(1);
	}
}

static FE_daemon::MPIRResult extractMPIRResult(FE_daemon::MPIRId const mpirId,
	MPIRInstance& mpirInst)
{
	auto const rawJobId  = mpirInst.readStringAt("totalview_jobid");
	auto const rawStepId = mpirInst.readStringAt("totalview_stepid");
	fprintf(stderr, "new mpir id %d\n", mpirId);

	return FE_daemon::MPIRResult
		{ mpirId // mpir_id
		, mpirInst.getLauncherPid() // launcher_pid
		, static_cast<uint32_t>(std::stoul(rawJobId)) // job_id
		, rawStepId.empty() ? 0 : static_cast<uint32_t>(std::stoul(rawStepId)) // step_id
		, mpirInst.getProctable() // proctable
	};
}

static FE_daemon::MPIRResult launchMPIR(LaunchData const& launchData)
{
	std::map<int, int> const remapFds
		{ { launchData.stdin_fd,  STDIN_FILENO  }
		, { launchData.stdout_fd, STDOUT_FILENO }
		, { launchData.stderr_fd, STDERR_FILENO }
	};

	auto idInstPair = mpirMap.emplace(std::make_pair(newMPIRId(),
		std::make_unique<MPIRInstance>(
			launchData.filepath, launchData.argvList, launchData.envList, std::map<int, int>{}
		)
	)).first;

	return extractMPIRResult(idInstPair->first, *idInstPair->second);
}

static FE_daemon::MPIRResult attachMPIR(std::string const& launcherPath, pid_t const launcherPid)
{
	auto idInstPair = mpirMap.emplace(std::make_pair(newMPIRId(),
		std::make_unique<MPIRInstance>(launcherPath.c_str(), launcherPid)
	)).first;
	return extractMPIRResult(idInstPair->first, *idInstPair->second);
}

static void releaseMPIR(FE_daemon::MPIRId const mpir_id)
{
	auto const idInstPair = mpirMap.find(mpir_id);
	if (idInstPair != mpirMap.end()) {
		mpirMap.erase(idInstPair);
	} else {
		throw std::runtime_error("mpir id not found: " + std::to_string(mpir_id));
	}
}

static void terminateMPIR(FE_daemon::MPIRId const mpir_id)
{
	auto const idInstPair = mpirMap.find(mpir_id);
	if (idInstPair != mpirMap.end()) {
		idInstPair->second->terminate();
		mpirMap.erase(idInstPair);
	} else {
		throw std::runtime_error("mpir id not found: " + std::to_string(mpir_id));
	}
}

/* handler implementations */

static void handle_ForkExecvpApp(int const reqFd, int const respFd)
{
	tryWritePIDResp(respFd, [&]() {
		auto const launchData = readLaunchData(reqFd);

		auto const appPid = forkExec(launchData);

		registerAppPID(appPid);

		return appPid;
	});
}

static void handle_ForkExecvpUtil(int const reqFd, int const respFd)
{
	tryWritePIDResp(respFd, [&]() {
		auto const appPid  = rawReadLoop<pid_t>(reqFd);
		auto const runMode = rawReadLoop<FE_daemon::RunMode>(reqFd);
		auto const launchData = readLaunchData(reqFd);

		auto const utilPid = forkExec(launchData);

		registerUtilPID(appPid, utilPid);
		if (runMode == FE_daemon::Synchronous) {
			::waitpid(utilPid, nullptr, 0);
		}

		return utilPid;
	});
}

static void handle_LaunchMPIR(int const reqFd, int const respFd)
{
	tryWriteMPIRResp(respFd, [&]() {
		auto const launchData = readLaunchData(reqFd);

		auto const mpirData = launchMPIR(launchData);

		registerAppPID(mpirData.launcher_pid);

		return mpirData;
	});
}

static void handle_AttachMPIR(int const reqFd, int const respFd)
{
	tryWriteMPIRResp(respFd, [&]() {
		// set up pipe stream
		cti::FdBuf reqBuf{dup(reqFd)};
		std::istream reqStream{&reqBuf};

		// read launcher path and pid
		std::string launcherPath;
		if (!std::getline(reqStream, launcherPath, '\0')) {
			throw std::runtime_error("failed to read launcher path");
		}
		auto const launcherPid = rawReadLoop<pid_t>(reqFd);

		auto const mpirData = attachMPIR(launcherPath, launcherPid);

		// extract MPIR data
		registerAppPID(mpirData.launcher_pid);

		return mpirData;
	});
}

static void handle_ReleaseMPIR(int const reqFd, int const respFd)
{
	tryWriteOKResp(respFd, [&]() {
		auto const mpirId = rawReadLoop<FE_daemon::MPIRId>(reqFd);

		releaseMPIR(mpirId);
	});
}

static void handle_TerminateMPIR(int const reqFd, int const respFd)
{
	tryWriteOKResp(respFd, [&]() {
		auto const mpirId = rawReadLoop<FE_daemon::MPIRId>(reqFd);

		terminateMPIR(mpirId);
	});
}

static void handle_RegisterApp(int const reqFd, int const respFd)
{
	tryWriteOKResp(respFd, [&]() {
		auto const appPid = rawReadLoop<pid_t>(reqFd);

		registerAppPID(appPid);
	});
}

static void handle_RegisterUtil(int const reqFd, int const respFd)
{
	tryWriteOKResp(respFd, [&]() {
		auto const appPid  = rawReadLoop<pid_t>(reqFd);
		auto const utilPid = rawReadLoop<pid_t>(reqFd);

		registerUtilPID(appPid, utilPid);
	});
}

static void handle_DeregisterApp(int const reqFd, int const respFd)
{
	tryWriteOKResp(respFd, [&]() {
		auto const appPid = rawReadLoop<pid_t>(reqFd);

		deregisterAppPID(appPid);
	});
}

static void handle_Shutdown(int const reqFd, int const respFd)
{
	tryWriteOKResp(respFd, [&]() {
		// send OK response
		rawWriteLoop(respFd, OKResp
			{ .type = RespType::OK
			, .success = true
		});

		shutdown_and_exit(0);
	});
}



int
main(int argc, char *argv[])
{
	// parse incoming argv for request and response FDs
	{ auto incomingArgv = cti::IncomingArgv<CTIFEDaemonArgv>{argc, argv};
		int c; std::string optarg;
		while (true) {
			std::tie(c, optarg) = incomingArgv.get_next();
			if (c < 0) {
				break;
			}

			switch (c) {

			case CTIFEDaemonArgv::ReadFD.val:
				reqFd = std::stoi(optarg);
				break;

			case CTIFEDaemonArgv::WriteFD.val:
				respFd = std::stoi(optarg);
				break;

			case CTIFEDaemonArgv::Help.val:
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

	// block all signals except SIGTERM, SIGCHLD, SIGPIPE, SIGTRAP, SIGHUP
	sigset_t block_set;
	memset(&block_set, 0, sizeof(block_set));
	if (sigfillset(&block_set)) {
		perror("sigfillset");
		return 1;
	}
	if (sigdelset(&block_set, SIGTERM) ||
	    sigdelset(&block_set, SIGCHLD) ||
	    sigdelset(&block_set, SIGPIPE) ||
	    sigdelset(&block_set, SIGTRAP) ||
	    sigdelset(&block_set, SIGHUP)) {
		perror("sigdelset");
		return 1;
	}
	if (sigprocmask(SIG_SETMASK, &block_set, nullptr)) {
		perror("sigprocmask");
		return 1;
	}


	// set handler for SIGTERM, SIGCHLD, SIGPIPE, SIGHUP
	// SIGTRAP is used by Dyninst for breakpoint events
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
	rawWriteLoop(respFd, getpid());

	// wait for pipe commands
	while (true) {
		auto const reqType = rawReadLoop<ReqType>(reqFd);
		fprintf(stderr, "req type %ld\n", reqType);

		switch (reqType) {

			case ReqType::ForkExecvpApp:
				handle_ForkExecvpApp(reqFd, respFd);
				break;

			case ReqType::ForkExecvpUtil:
				handle_ForkExecvpUtil(reqFd, respFd);
				break;

			case ReqType::LaunchMPIR:
				handle_LaunchMPIR(reqFd, respFd);
				break;

			case ReqType::AttachMPIR:
				handle_AttachMPIR(reqFd, respFd);
				break;

			case ReqType::ReleaseMPIR:
				handle_ReleaseMPIR(reqFd, respFd);
				break;

			case ReqType::TerminateMPIR:
				handle_TerminateMPIR(reqFd, respFd);
				break;

			case ReqType::RegisterApp:
				handle_RegisterApp(reqFd, respFd);
				break;

			case ReqType::RegisterUtil:
				handle_RegisterUtil(reqFd, respFd);
				break;

			case ReqType::DeregisterApp:
				handle_DeregisterApp(reqFd, respFd);
				break;

			case ReqType::Shutdown:
				handle_Shutdown(reqFd, respFd);
				break;

			default:
				fprintf(stderr, "unknown req type %ld\n", reqType);
				break;

		}
	}

	// we should not get here
	shutdown_and_exit(1);
}
