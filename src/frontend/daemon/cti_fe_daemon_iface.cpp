/******************************************************************************\
 * cti_overwatch_iface.cpp - command interface for overwatch daemon
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

#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>

#include <stdexcept>

#include "useful/cti_execvp.hpp"

#include "cti_fe_daemon_iface.hpp"

/* fe_iface helpers */

void
cti::fe_daemon::writeReqType(int const reqFd, cti::fe_daemon::ReqType const type)
{
	rawWriteLoop(reqFd, type);
}

pid_t
cti::fe_daemon::writeLaunchReq(int const reqFd, int const respFd, pid_t app_pid, char const* file,
	char const* const argv[], int stdin_fd, int stdout_fd, int stderr_fd, char const* const env[])
{
	// construct and write fork/exec message
	auto const forkExecReq = LaunchReq
		{ .app_pid = app_pid
	};
	rawWriteLoop(reqFd, forkExecReq);

	// share stdin/out/err fds
	auto const N_FDS = 3;
	int const stdfds[] =
		{ (stdin_fd  >= 0) ? stdin_fd  : STDIN_FILENO
		, (stdout_fd >= 0) ? stdout_fd : STDOUT_FILENO
		, (stderr_fd >= 0) ? stderr_fd : STDERR_FILENO
	};
	struct {
		int fd_data[N_FDS];
		struct cmsghdr cmd_hdr;
	} buffer;

	// create message buffer for one character
	struct iovec empty_iovec;
	char c = ' ';
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
	for(int i = 0; i < N_FDS; i++) {
		((int *)CMSG_DATA(cmsg))[i] = stdfds[i];
	}

	// send remap FD message
	if (::sendmsg(reqFd, &msg_hdr, 0) < 0) {
		throw std::runtime_error("failed to receive fds: " + std::string{strerror(errno)});
	}

	// write flat file/argv/env strings
	writeLoop(reqFd, file, strlen(file) + 1);
	for (auto arg = argv; *arg != nullptr; arg++) {
		writeLoop(reqFd, *arg, strlen(*arg) + 1);
	}
	rawWriteLoop(reqFd, '\0');
	if (env) {
		for (auto var = env; *var != nullptr; var++) {
			writeLoop(reqFd, *var, strlen(*var) + 1);
		}
	}
	rawWriteLoop(reqFd, '\0');

	// read response
	auto const forkExecResp = rawReadLoop<PIDResp>(respFd);

	// verify response
	if ((forkExecResp.type != RespType::PID) || (forkExecResp.pid < 0)) {
		fprintf(stderr, "forkExecResp type %ld pid %d\n", forkExecResp.type, forkExecResp.pid);
		throw std::runtime_error("overwatch fork exec failed");
	}

	return forkExecResp.pid;
}

void
cti::fe_daemon::writeReleaseMPIRReq(int const reqFd, int const respFd, cti::fe_daemon::MPIRId mpir_id)
{
	// construct and write release
	auto const releaseMPIRReq = ReleaseMPIRReq
		{ .mpir_id = mpir_id
	};
	rawWriteLoop(reqFd, releaseMPIRReq);

	// read response
	auto const releaseResp = rawReadLoop<OKResp>(respFd);

	// verify response
	if ((releaseResp.type != RespType::OK) || !releaseResp.success) {
		throw std::runtime_error("overwatch release mpir barrier failed");
	}
}

pid_t
cti::fe_daemon::writeAppReq(int const reqFd, int const respFd, pid_t app_pid)
{
	// construct and write register message
	auto const registerAppReq = AppReq
		{ .app_pid = app_pid
	};
	rawWriteLoop(reqFd, registerAppReq);

	// read response
	auto const registerResp = rawReadLoop<OKResp>(respFd);

	// verify response
	if ((registerResp.type != RespType::OK) || !registerResp.success) {
		throw std::runtime_error("overwatch register app failed");
	}

	return app_pid;
}

pid_t
cti::fe_daemon::writeUtilReq(int const reqFd, int const respFd, pid_t app_pid, pid_t util_pid)
{
	// construct and write register message
	auto const registerUtilReq = UtilReq
		{ .app_pid = app_pid
		, .util_pid = util_pid
	};
	rawWriteLoop(reqFd, registerUtilReq);

	// read response
	auto const registerResp = rawReadLoop<OKResp>(respFd);

	// verify response
	if ((registerResp.type != RespType::OK) || !registerResp.success) {
		throw std::runtime_error("overwatch register util failed");
	}

	return util_pid;
}

void
cti::fe_daemon::writeShutdownReq(int const reqFd, int const respFd)
{
	// read response
	auto const shutdownResp = rawReadLoop<OKResp>(respFd);

	// verify response
	if ((shutdownResp.type != RespType::OK) || !shutdownResp.success) {
		throw std::runtime_error("overwatch shutdown failed");
	}
}

// read a pid response from pipe
pid_t
cti::fe_daemon::readPIDResp(int const respFd)
{
	auto const pidResp = rawReadLoop<PIDResp>(respFd);
	if ((pidResp.type != RespType::PID) || (pidResp.pid < 0)) {
		throw std::runtime_error("failed to read PID response");
	}
	return pidResp.pid;
}

cti::fe_daemon::MPIRResult
cti::fe_daemon::readMPIRResp(int const respFd, pid_t const launcherPid)
{
	// read basic table information
	auto const mpirResp = rawReadLoop<MPIRResp>(respFd);
	if ((mpirResp.type != RespType::MPIR) || !mpirResp.mpir_id) {
		throw std::runtime_error("failed to read proctable response");
	}

	MPIRResult result
		{ mpirResp.mpir_id
		, launcherPid
		, mpirResp.job_id
		, mpirResp.step_id
		, {} // proctable
	};
	result.proctable.reserve(mpirResp.num_pids);

	// set up pipe stream
	cti::FdBuf respBuf{dup(respFd)};
	std::istream respStream{&respBuf};

	// fill in pid and hostname of next element
	for (int i = 0; i < mpirResp.num_pids; i++) {
		MPIRProctableElem elem;
		// read pid
		elem.pid = rawReadLoop<pid_t>(respFd);
		// read hostname
		if (!std::getline(respStream, elem.hostname, '\0')) {
			throw std::runtime_error("failed to read string");
		}
		result.proctable.emplace_back(std::move(elem));
	}

	return result;
}