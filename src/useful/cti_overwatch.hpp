#pragma once

#ifdef MPIR
#include "frontend/mpir_iface/MPIRInstance.hpp"
#endif

pid_t _cti_forkExecvpApp(char const* file, char* const argv[], int stdout_fd, int stderr_fd);
pid_t _cti_forkExecvpUtil(pid_t app_pid, char const* file, char* const argv[], int stdout_fd, int stderr_fd);
#ifdef MPIR
MPIR::ProcTable _cti_launchMPIR(char const* file, char const* argv[], int stdout_fd, int stderr_fd);
void _cti_releaseMPIRBreakpoint(int mpir_id);
#else
pid_t _cti_registerApp(pid_t app_pid);
pid_t _cti_registerUtil(pid_t app_pid, pid_t util_pid);
#endif
void _cti_shutdownOverwatch();

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

	Shutdown
};

// ForkExecvpApp, ForkExecvpUtil, LaunchMPIR
struct LaunchReq
{
	pid_t app_pid; // unused for ForkExecvpApp, LaunchMPIR
	int stdout_fd;
	int stderr_fd;
	size_t file_and_argv_len;
	// include list of null-terminated strings
};

#ifdef MPIR

struct ReleaseMPIRReq
{
	int mpir_id;
};

#else

// RegisterApp, RegisterUtil,
struct RegisterReq
{
	pid_t app_pid;
	pid_t util_pid; // unused for RegisterApp
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
	size_t num_hosts;
	// include list of pids
	// include list of null-terminated hostnames
};
#endif
