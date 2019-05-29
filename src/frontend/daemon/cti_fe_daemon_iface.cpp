/******************************************************************************\
 * cti_fe_daemon_iface.cpp - command interface for frontend daemon
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

#include <sys/types.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/time.h>

#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <stdexcept>

#include "useful/cti_execvp.hpp"

#include "cti_fe_daemon_iface.hpp"
using DaemonAppId = FE_daemon::DaemonAppId;

/* protocol helpers */

// write FD remap control message, binary path, arguments, environment to domain socket
static void writeLaunchData(int const reqFd, char const* file, char const* const argv[],
	int stdin_fd, int stdout_fd, int stderr_fd, char const* const env[])
{
	// verify that reqFd is a domain socket
	{ struct sockaddr sa;
		auto len = socklen_t{sizeof(sa)};
		if (::getsockname(reqFd, &sa, &len) < 0) {
			throw std::runtime_error("getsockname failed: " + std::string{strerror(errno)});
		}
		if (sa.sa_family != AF_UNIX) {
			throw std::runtime_error("daemon request file descriptor must be a domain socket");
		}
	}

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

	// write filepath string
	writeLoop(reqFd, file, strlen(file) + 1);
	// write null-terminated argument array
	for (auto arg = argv; *arg != nullptr; arg++) {
		writeLoop(reqFd, *arg, strlen(*arg) + 1);
	}
	rawWriteLoop(reqFd, '\0');
	// write null-terminated environment array
	if (env) {
		for (auto var = env; *var != nullptr; var++) {
			writeLoop(reqFd, *var, strlen(*var) + 1);
		}
	}
	rawWriteLoop(reqFd, '\0');
}

// throw if boolean response is not true, indicating failure
static void verifyOKResp(int const reqFd)
{
	auto const okResp = rawReadLoop<FE_daemon::OKResp>(reqFd);
	if ((okResp.type != FE_daemon::RespType::OK) || !okResp.success) {
		throw std::runtime_error("failed to verify OK response");
	}
}

// return ID response content, throw if id < 0, indicating failure
static DaemonAppId readIDResp(int const reqFd)
{
	auto const idResp = rawReadLoop<FE_daemon::IDResp>(reqFd);
	if ((idResp.type != FE_daemon::RespType::ID) || (idResp.id < 0)) {
		throw std::runtime_error("failed to read DaemonAppID response");
	}
	return idResp.id;
}

// return MPIR launch / attach data, throw if MPIR ID < 0, indicating failure
static FE_daemon::MPIRResult readMPIRResp(int const reqFd)
{
	// read basic table information
	auto const mpirResp = rawReadLoop<FE_daemon::MPIRResp>(reqFd);
	if ((mpirResp.type != FE_daemon::RespType::MPIR) || (mpirResp.mpir_id < 0)) {
		throw std::runtime_error("failed to read proctable response");
	}

	// fill in MPIR data excluding proctable
	FE_daemon::MPIRResult result
		{ mpirResp.mpir_id
		, mpirResp.launcher_pid
		, mpirResp.job_id
		, mpirResp.step_id
		, {} // proctable
	};
	result.proctable.reserve(mpirResp.num_pids);

	// set up pipe stream
	cti::FdBuf respBuf{dup(reqFd)};
	std::istream respStream{&respBuf};

	// fill in pid and hostname of proctable elements
	for (int i = 0; i < mpirResp.num_pids; i++) {
		MPIRProctableElem elem;
		// read pid
		elem.pid = rawReadLoop<pid_t>(reqFd);
		// read hostname
		if (!std::getline(respStream, elem.hostname, '\0')) {
			throw std::runtime_error("failed to read string");
		}
		result.proctable.emplace_back(std::move(elem));
	}

	return result;
}

/* interface implementation */

FE_daemon::~FE_daemon()
{
	// Send shutdown request if we have initialized the daemon
	if (m_init) {
		m_init = false;
		// This should be the only way to call ReqType::Shutdown
		rawWriteLoop(m_req_sock.getWriteFd(), ReqType::Shutdown);
		verifyOKResp(m_resp_sock.getReadFd());
	}
	// FIXME: Shouldn't this do a waitpid???
}

void
FE_daemon::initialize(std::string const& fe_daemon_bin)
{
	// Only fork once!
	if (m_init) {
		return;
	}

	// Start the frontend daemon
    if (auto const forkedPid = fork()) {
        // parent case

        // set child in own process gorup
        if (setpgid(forkedPid, forkedPid) < 0) {
            perror("setpgid");
            exit(1);
        }

        // set up fe_daemon req / resp pipe
        m_req_sock.closeRead();
        m_resp_sock.closeWrite();

        // wait until fe_daemon set up
        if (rawReadLoop<pid_t>(m_resp_sock.getReadFd()) != forkedPid) {
            throw std::runtime_error("fe_daemon launch failed");
        }
    }
    else {
    	// child case

		// set in own process gorup
		if (setpgid(0, 0) < 0) {
			perror("setpgid");
			exit(1);
		}

		// set up death signal
		prctl(PR_SET_PDEATHSIG, SIGHUP);

		// set up fe_daemon req /resp pipe
		m_req_sock.closeWrite();
		m_resp_sock.closeRead();

		// remap standard FDs
		dup2(open("/dev/null", O_RDONLY), STDIN_FILENO);
		dup2(open("/dev/null", O_WRONLY), STDOUT_FILENO);
		dup2(open("/dev/null", O_WRONLY), STDERR_FILENO);

		// close FDs above pipe FDs
		auto max_fd = size_t{};
		{ struct rlimit rl;
			if (getrlimit(RLIMIT_NOFILE, &rl) < 0) {
				throw std::runtime_error("getrlimit failed.");
			} else {
				max_fd = (rl.rlim_max == RLIM_INFINITY) ? 1024 : rl.rlim_max;
			}
		}
		int const min_fd = std::max(m_req_sock.getReadFd(), m_resp_sock.getWriteFd()) + 1;
		for (size_t i = min_fd; i < max_fd; ++i) {
			close(i);
		}

		// setup args
		using FEDA = CTIFEDaemonArgv;
		cti::OutgoingArgv<FEDA> fe_daemonArgv{fe_daemon_bin};
		fe_daemonArgv.add(FEDA::ReadFD,  std::to_string(m_req_sock.getReadFd()));
		fe_daemonArgv.add(FEDA::WriteFD, std::to_string(m_resp_sock.getWriteFd()));

		// exec
		execvp(fe_daemon_bin.c_str(), fe_daemonArgv.get());
		throw std::runtime_error("returned from execvp: " + std::string{strerror(errno)});
    }
    // Setup in parent was sucessful
    m_init = true;
}

DaemonAppId
FE_daemon::request_ForkExecvpApp(char const* file,
	char const* const argv[], int stdin_fd, int stdout_fd, int stderr_fd, char const* const env[])
{
	rawWriteLoop(m_req_sock.getWriteFd(), ReqType::ForkExecvpApp);
	writeLaunchData(m_req_sock.getWriteFd(), file, argv, stdin_fd, stdout_fd, stderr_fd, env);
	return readIDResp(m_resp_sock.getReadFd());
}

void
FE_daemon::request_ForkExecvpUtil(DaemonAppId app_id, RunMode runMode,
	char const* file, char const* const argv[], int stdin_fd, int stdout_fd, int stderr_fd,
	char const* const env[])
{
	rawWriteLoop(m_req_sock.getWriteFd(), ReqType::ForkExecvpUtil);
	rawWriteLoop(m_req_sock.getWriteFd(), app_id);
	rawWriteLoop(m_req_sock.getWriteFd(), runMode);
	writeLaunchData(m_req_sock.getWriteFd(), file, argv, stdin_fd, stdout_fd, stderr_fd, env);
	verifyOKResp(m_resp_sock.getReadFd());
}

FE_daemon::MPIRResult
FE_daemon::request_LaunchMPIR(char const* file,
	char const* const argv[], int stdin_fd, int stdout_fd, int stderr_fd, char const* const env[])
{
	rawWriteLoop(m_req_sock.getWriteFd(), ReqType::LaunchMPIR);
	writeLaunchData(m_req_sock.getWriteFd(), file, argv, stdin_fd, stdout_fd, stderr_fd, env);
	return readMPIRResp(m_resp_sock.getReadFd());
}

FE_daemon::MPIRResult
FE_daemon::request_AttachMPIR(char const* launcher_path, pid_t launcher_pid)
{
	rawWriteLoop(m_req_sock.getWriteFd(), ReqType::AttachMPIR);
	writeLoop(m_req_sock.getWriteFd(), launcher_path, strlen(launcher_path) + 1);
	rawWriteLoop(m_req_sock.getWriteFd(), launcher_pid);
	return readMPIRResp(m_resp_sock.getReadFd());
}

void
FE_daemon::request_ReleaseMPIR(DaemonAppId mpir_id)
{
	rawWriteLoop(m_req_sock.getWriteFd(), ReqType::ReleaseMPIR);
	rawWriteLoop(m_req_sock.getWriteFd(), mpir_id);
	verifyOKResp(m_resp_sock.getReadFd());
}

void
FE_daemon::request_TerminateMPIR(DaemonAppId mpir_id)
{
	rawWriteLoop(m_req_sock.getWriteFd(), ReqType::TerminateMPIR);
	rawWriteLoop(m_req_sock.getWriteFd(), mpir_id);
	verifyOKResp(m_resp_sock.getReadFd());
}

DaemonAppId
FE_daemon::request_RegisterApp(pid_t app_pid)
{
	rawWriteLoop(m_req_sock.getWriteFd(), ReqType::RegisterApp);
	rawWriteLoop(m_req_sock.getWriteFd(), app_pid);
	return readIDResp(m_resp_sock.getReadFd());
}

void
FE_daemon::request_RegisterUtil(DaemonAppId app_id, pid_t util_pid)
{
	rawWriteLoop(m_req_sock.getWriteFd(), ReqType::RegisterUtil);
	rawWriteLoop(m_req_sock.getWriteFd(), app_id);
	rawWriteLoop(m_req_sock.getWriteFd(), util_pid);
	verifyOKResp(m_resp_sock.getReadFd());
}

void
FE_daemon::request_DeregisterApp(DaemonAppId app_id)
{
	rawWriteLoop(m_req_sock.getWriteFd(), ReqType::DeregisterApp);
	rawWriteLoop(m_req_sock.getWriteFd(), app_id);
	verifyOKResp(m_resp_sock.getReadFd());
}
