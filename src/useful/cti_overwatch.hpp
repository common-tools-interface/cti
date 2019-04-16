#pragma once

#ifdef MPIR
#include "frontend/mpir_iface/MPIRInstance.hpp"
#endif

// implemented in cti_fe_iface
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

// fd read / write helpers
inline static void readLoop(char* buf, int const fd, size_t num_bytes)
{
	while (true) {
		errno = 0;
		int ret = read(fd, buf, num_bytes);
		if (ret < 0) {
			if (errno == EINTR) {
				continue;
			} else {
				throw std::runtime_error("read failed: " + std::string{strerror(errno)});
			}
		} else {
			return;
		}
	}
}

template <typename T>
inline static T rawReadLoop(int const fd)
{
	static_assert(std::is_trivially_copyable<T>::value);
	T result;
	readLoop(reinterpret_cast<char*>(&result), fd, sizeof(T));
	return result;
}

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
