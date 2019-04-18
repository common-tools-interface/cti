/******************************************************************************\
 * cti_overwatch.hpp - command interface for overwatch daemon
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

#ifdef MPIR
#include "frontend/mpir_iface/MPIRInstance.hpp"
#endif

/* internal overwatch interface implemented in cti_fe_iface */

// overwatch will fork and execvp a binary and register it as an app
pid_t _cti_forkExecvpApp(char const* file, char const* const argv[], int stdout_fd, int stderr_fd,
	char const* const env[]);

// overwatch will fork and execvp a binary and register it as a utility belonging to app_pid
pid_t _cti_forkExecvpUtil(pid_t app_pid, char const* file, char const* const argv[], int stdout_fd,
	int stderr_fd, char const* const env[]);

#ifdef MPIR

// overwatch will launch a binary under MPIR control and extract its proctable
MPIR::ProcTable _cti_launchMPIR(char const* file, char const* const argv[], int stdout_fd, int stderr_fd,
	char const* const env[]);

// overwatch will release a binary under mpir control from its breakpoint
void _cti_releaseMPIRBreakpoint(int mpir_id);
#else

// overwatch will register an already-forked process as an app. make sure this is paired with a
// _cti_deregisterApp for timely cleanup
pid_t _cti_registerApp(pid_t app_pid);

// overwatch will register an already-forked process as a utility belonging to app_pid
pid_t _cti_registerUtil(pid_t app_pid, pid_t util_pid);
#endif

// overwatch will terminate all utilities belonging to app_pid and deregister app_pid
void _cti_deregisterApp(pid_t app_pid);

// overwatch will terminate all registered apps and utilities
void _cti_shutdownOverwatch();

/* fd read / write helpers */

// read num_bytes from fd into buf
inline static void readLoop(char* buf, int const fd, int num_bytes)
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
inline static T rawReadLoop(int const fd)
{
	static_assert(std::is_trivially_copyable<T>::value);
	T result;
	readLoop(reinterpret_cast<char*>(&result), fd, sizeof(T));
	return result;
}

// write num_bytes from buf to fd
inline static void writeLoop(int const fd, char const* buf, int num_bytes)
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
inline static void rawWriteLoop(int const fd, T const& obj)
{
	static_assert(std::is_trivially_copyable<T>::value);
	writeLoop(fd, reinterpret_cast<char const*>(&obj), sizeof(T));
}

// request types

enum OverwatchReqType : long {
	ForkExecvpApp,
	ForkExecvpUtil,

	#ifdef MPIR

	LaunchMPIR,
	ReleaseMPIR

	#else

	RegisterApp,
	RegisterUtil,

	#endif

	DeregisterApp,

	Shutdown
};

// ForkExecvpApp, ForkExecvpUtil, LaunchMPIR
struct LaunchReq
{
	pid_t app_pid; // unused for ForkExecvpApp, LaunchMPIR
	int stdout_fd;
	int stderr_fd;
	// after sending this struct, send a list of null-terminated strings:
	// - file path string
	// - each argument string
	// - EMPTY STRING
	// - each environment variable string (format VAR=VAL)
	// - EMPTY STRING
	// sum of lengths of these strings including null-terminators should equal `file_and_argv_len`
};

#ifdef MPIR

struct ReleaseMPIRReq
{
	int mpir_id;
};

#else

// RegisterApp, DeregisterApp
struct AppReq
{
	pid_t app_pid;
};

// RegisterUtil
struct UtilReq
{
	pid_t app_pid;
	pid_t util_pid;
};

#endif

// Response types

enum OverwatchRespType : long {
	// Shutdown, RegisterApp, RegisterUtil, ReleaseMPIR
	OK,

	// ForkExecvpApp, ForkExecvpUtil
	PID,

	#ifdef MPIR
	// LaunchMPIR
	MPIRProcTable,
	#endif
};

struct OKResp
{
	OverwatchRespType type;
	bool success;
};

struct PIDResp
{
	OverwatchRespType type;
	pid_t pid;
};

#ifdef MPIR
struct MPIRProcTableResp
{
	OverwatchRespType type;
	int mpir_id;
	size_t num_pids;
	// after sending this struct, send:
	// - list of `num_pids` pids
	// - list of null-terminated hostnames
	// - EMPTY STRING
};
#endif
