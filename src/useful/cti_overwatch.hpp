#pragma once

#ifdef MPIR
#include "frontend/mpir_iface/MPIRInstance.hpp"
#endif

/* internal overwatch interface implemented in cti_fe_iface */

// overwatch will fork and execvp a binary and register it as an app
pid_t _cti_forkExecvpApp(char const* file, char* const argv[], int stdout_fd, int stderr_fd);

// overwatch will fork and execvp a binary and register it as a utility belonging to app_pid
pid_t _cti_forkExecvpUtil(pid_t app_pid, char const* file, char* const argv[], int stdout_fd, int stderr_fd);

#ifdef MPIR

// overwatch will launch a binary under MPIR control and extract its proctable
MPIR::ProcTable _cti_launchMPIR(char const* file, char const* argv[], int stdout_fd, int stderr_fd);

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
	size_t file_and_argv_len;
	// after sending this struct, send a list of null-terminated strings:
	// - file path string
	// - each argument string
	// - empty string to end the list
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
	size_t num_hosts;
	// after sending this struct, send:
	// - list of `num_hosts` pids
	// - list of `num_hosts` null-terminated hostnames
};
#endif
