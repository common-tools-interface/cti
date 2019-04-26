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

/* protocol helpers */

static void writeLaunchData(int const reqFd, char const* file, char const* const argv[],
	int stdin_fd, int stdout_fd, int stderr_fd, char const* const env[])
{
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
}

static void verifyOKResp(int const respFd)
{
	auto const okResp = rawReadLoop<cti::fe_daemon::OKResp>(respFd);
	if ((okResp.type != cti::fe_daemon::RespType::OK) || !okResp.success) {
		throw std::runtime_error("failed to verify OK response");
	}
}

static pid_t readPIDResp(int const respFd)
{
	auto const pidResp = rawReadLoop<cti::fe_daemon::PIDResp>(respFd);
	if ((pidResp.type != cti::fe_daemon::RespType::PID) || (pidResp.pid < 0)) {
		throw std::runtime_error("failed to read PID response");
	}
	return pidResp.pid;
}

static cti::fe_daemon::MPIRResult readMPIRResp(int const respFd)
{
	// read basic table information
	auto const mpirResp = rawReadLoop<cti::fe_daemon::MPIRResp>(respFd);
	if ((mpirResp.type != cti::fe_daemon::RespType::MPIR) || !mpirResp.mpir_id) {
		throw std::runtime_error("failed to read proctable response");
	}

	cti::fe_daemon::MPIRResult result
		{ mpirResp.mpir_id
		, mpirResp.launcher_pid
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

/* interface implementation */

pid_t
cti::fe_daemon::request_ForkExecvpApp(int const reqFd, int const respFd, char const* file,
	char const* const argv[], int stdin_fd, int stdout_fd, int stderr_fd, char const* const env[])
{
	rawWriteLoop(reqFd, ReqType::ForkExecvpApp);
	writeLaunchData(reqFd, file, argv, stdin_fd, stdout_fd, stderr_fd, env);
	return readPIDResp(respFd);
}

pid_t
cti::fe_daemon::request_ForkExecvpUtil(int const reqFd, int const respFd, pid_t app_pid, RunMode runMode,
	char const* file, char const* const argv[], int stdin_fd, int stdout_fd, int stderr_fd,
	char const* const env[])
{
	rawWriteLoop(reqFd, ReqType::ForkExecvpUtil);
	rawWriteLoop(reqFd, app_pid);
	rawWriteLoop(reqFd, runMode);
	writeLaunchData(reqFd, file, argv, stdin_fd, stdout_fd, stderr_fd, env);
	return readPIDResp(respFd);
}

cti::fe_daemon::MPIRResult
cti::fe_daemon::request_LaunchMPIR(int const reqFd, int const respFd, char const* file,
	char const* const argv[], int stdin_fd, int stdout_fd, int stderr_fd, char const* const env[])
{
	rawWriteLoop(reqFd, ReqType::LaunchMPIR);
	writeLaunchData(reqFd, file, argv, stdin_fd, stdout_fd, stderr_fd, env);
	return readMPIRResp(respFd);
}

cti::fe_daemon::MPIRResult
cti::fe_daemon::request_AttachMPIR(int const reqFd, int const respFd, pid_t app_pid)
{
	rawWriteLoop(reqFd, ReqType::AttachMPIR);
	rawWriteLoop(reqFd, app_pid);
	return readMPIRResp(respFd);
}

void
cti::fe_daemon::request_ReleaseMPIR(int const reqFd, int const respFd, MPIRId mpir_id)
{
	rawWriteLoop(reqFd, ReqType::ReleaseMPIR);
	rawWriteLoop(reqFd, mpir_id);
	verifyOKResp(respFd);
}

pid_t
cti::fe_daemon::request_RegisterApp(int const reqFd, int const respFd, pid_t app_pid)
{
	rawWriteLoop(reqFd, ReqType::RegisterApp);
	rawWriteLoop(reqFd, app_pid);
	verifyOKResp(respFd);
	return app_pid;
}

pid_t
cti::fe_daemon::request_RegisterUtil(int const reqFd, int const respFd, pid_t app_pid, pid_t util_pid)
{
	rawWriteLoop(reqFd, ReqType::RegisterUtil);
	rawWriteLoop(reqFd, app_pid);
	rawWriteLoop(reqFd, util_pid);
	verifyOKResp(respFd);
	return util_pid;
}

void
cti::fe_daemon::request_DeregisterApp(int const reqFd, int const respFd, pid_t app_pid)
{
	rawWriteLoop(reqFd, ReqType::DeregisterApp);
	rawWriteLoop(reqFd, app_pid);
	verifyOKResp(respFd);
}

void
cti::fe_daemon::request_Shutdown(int const reqFd, int const respFd)
{
	rawWriteLoop(reqFd, ReqType::Shutdown);
	verifyOKResp(respFd);
}
