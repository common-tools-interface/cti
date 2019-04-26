/******************************************************************************\
 * cti_overwatch_iface.hpp - command interface for frontend daemon
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
#pragma once

#include <sys/socket.h>

#include "frontend/Frontend.hpp"
#include "frontend/mpir_iface/MPIRProctable.hpp"
#include "cti_fe_daemon.hpp"

/* fd read / write helpers */

// read num_bytes from fd into buf
static inline void readLoop(char* buf, int const fd, int num_bytes)
{
	while (num_bytes > 0) {
		errno = 0;
		int bytes_read = read(fd, buf, num_bytes);
		if (bytes_read < 0) {
			if (errno == EINTR) {
				continue;
			} else {
				throw std::runtime_error("read failed: " + std::string{strerror(errno)});
			}
		} else {
			num_bytes -= bytes_read;
		}
	}
}

// read and return an object T from fd (useful for reading message structs from pipes)
template <typename T>
static inline T rawReadLoop(int const fd)
{
	static_assert(std::is_trivially_copyable<T>::value);
	T result;
	readLoop(reinterpret_cast<char*>(&result), fd, sizeof(T));
	return result;
}

// write num_bytes from buf to fd
static void writeLoop(int const fd, char const* buf, int num_bytes)
{
	while (num_bytes > 0) {
		errno = 0;
		int written = write(fd, buf, num_bytes);
		if (written < 0) {
			if (errno == EINTR) {
				continue;
			} else {
				throw std::runtime_error("write failed: " + std::string{strerror(errno)});
			}
		} else {
			num_bytes -= written;
		}
	}
}

// write an object T to fd (useful for writing message structs to pipes)
template <typename T>
static void rawWriteLoop(int const fd, T const& obj)
{
	static_assert(std::is_trivially_copyable<T>::value);
	writeLoop(fd, reinterpret_cast<char const*>(&obj), sizeof(T));
}

/* protocol helpers for cti_fe_iface */

namespace cti {
namespace fe_daemon {

struct MPIRResult
{
	MPIRId mpir_id;
	pid_t launcher_pid;
	uint32_t job_id;
	uint32_t step_id;
	MPIRProctable proctable;
};

// write an app launch request and parameters to pipe, return launched PID
pid_t request_ForkExecvpApp(int const reqFd, int const respFd, char const* file,
	char const* const argv[], int stdin_fd, int stdout_fd, int stderr_fd, char const* const env[]);

// write a utility launch request and parameters to pipe, return launched PID
pid_t request_ForkExecvpUtil(int const reqFd, int const respFd, pid_t app_pid, RunMode runMode,
	char const* file, char const* const argv[], int stdin_fd, int stdout_fd, int stderr_fd,
	char const* const env[]);

// write an mpir launch request and parameters to pipe, return MPIR data including proctable
MPIRResult request_LaunchMPIR(int const reqFd, int const respFd, char const* file,
	char const* const argv[], int stdin_fd, int stdout_fd, int stderr_fd, char const* const env[]);

// write an MPIR attach request to pipe, return MPIR data
MPIRResult request_AttachMPIR(int const reqFd, int const respFd, pid_t app_pid);

// write an mpir release request to pipe, verify response
void request_ReleaseMPIR(int const reqFd, int const respFd, MPIRId mpir_id);

// write an app register request to pipe, verify response, return pid
pid_t request_RegisterApp(int const reqFd, int const respFd, pid_t app_pid);

// write utility register request to pipe, verify response, return pid
pid_t request_RegisterUtil(int const reqFd, int const respFd, pid_t app_pid, pid_t util_pid);

// write an app deregister request to pipe, verify response
void request_DeregisterApp(int const reqFd, int const respFd, pid_t app_pid);

// write daemon shutdown request to pipe, verify response
void request_Shutdown(int const reqFd, int const respFd);

}; // fe_daemon
}; // cti

/* internal overwatch interface implemented in cti_fe_iface */

// overwatch will fork and execvp a binary and register it as an app
pid_t _cti_forkExecvpApp(char const* file, char const* const argv[], int stdin_fd,
	int stdout_fd, int stderr_fd, char const* const env[]);

// overwatch will fork and execvp a binary and register it as a utility belonging to app_pid
pid_t _cti_forkExecvpAsyncUtil(pid_t app_pid, char const* file, char const* const argv[],
	int stdin_fd, int stdout_fd, int stderr_fd, char const* const env[]);

// overwatch will fork and execvp a binary, register it, and wait for its completion
pid_t _cti_forkExecvpSyncUtil(pid_t app_pid, char const* file, char const* const argv[],
	int stdin_fd, int stdout_fd, int stderr_fd, char const* const env[]);

// overwatch will launch a binary under MPIR control and extract its proctable
cti::fe_daemon::MPIRResult _cti_launchMPIR(char const* file, char const* const argv[],
	int stdin_fd, int stdout_fd, int stderr_fd, char const* const env[]);

// overwatch will attach to a binary and extract its proctable
cti::fe_daemon::MPIRResult _cti_attachMPIR(pid_t app_pid);

// overwatch will release a binary under mpir control from its breakpoint
void _cti_releaseMPIR(cti::fe_daemon::MPIRId mpir_id);

// overwatch will register an already-forked process as an app. make sure this is paired with a
// _cti_deregisterApp for timely cleanup
pid_t _cti_registerApp(pid_t app_pid);

// overwatch will register an already-forked process as a utility belonging to app_pid
pid_t _cti_registerUtil(pid_t app_pid, pid_t util_pid);

// overwatch will terminate all utilities belonging to app_pid and deregister app_pid
void _cti_deregisterApp(pid_t app_pid);

// overwatch will terminate all registered apps and utilities
void _cti_shutdownOverwatch();

