/******************************************************************************\
 * mpir_shim.cpp - cti fe_daemon utility to extract MPIR proctable information
 *
 * Copyright 2019-2021 Hewlett Packard Enterprise Development LP.
 * SPDX-License-Identifier: Linux-OpenIB
 ******************************************************************************/

#include <unistd.h>
#include <limits.h>
#include <stdarg.h>

#include <signal.h>

#include <getopt.h>

#include <iostream>
#include <memory>
#include <vector>

#include "frontend/mpir_iface/Inferior.hpp"
#include "cti_fe_daemon_iface.hpp"
#include "useful/cti_wrappers.hpp"

static bool debug_enabled() {
	static const auto enabled = (::getenv("CTI_DEBUG") != nullptr);
	return enabled;
}

namespace globals
{
	static auto logfile = std::unique_ptr<FILE, decltype(&::fclose)>{nullptr, ::fclose};
}

static void log(const char* format, ...)
{
	if (globals::logfile || debug_enabled()) {
		va_list argptr;
		va_start(argptr, format);
		vfprintf((globals::logfile) ? globals::logfile.get() : stderr, format, argptr);
		va_end(argptr);
	}
}

/*
 * MPIR shim is used by bootstrap/wrapper scripts that aren't MPIR compliant.
 * CTI will insert the shim into PATH with the same name as the normal launcher,
 * and the wrapper will launch the shim instead of the actual launcher. The shim
 * then starts the real launcher in a stopped state, sends the pid to CTI, which
 * then attaches and reads the MPIR info. The shim only activates if the
 * CTI_SHIM_TOKEN is the last argument on the command line, and the token is not
 * passed to the actual job launch.
*/

static auto parse_from_env(int argc, char const* argv[])
{
	auto inputFd  = int{-1};
	auto outputFd = int{-1};
	auto launcherPath = std::string{};
	auto originalPath = std::string{};
	std::vector<std::string> launcherArgv;
	auto shimToken = std::string{};
	auto hasShimToken = false;

	if (auto const rawInputFd = ::getenv("CTI_MPIR_SHIM_INPUT_FD")) {
		inputFd = cti::stoi(rawInputFd, "CTI_MPIR_SHIM_INPUT_FD");
	}
	if (auto const rawOutputFd = ::getenv("CTI_MPIR_SHIM_OUTPUT_FD")) {
		outputFd = cti::stoi(rawOutputFd, "CTI_MPIR_SHIM_OUTPUT_FD");
	}
	if (auto const rawLauncherPath = ::getenv("CTI_MPIR_LAUNCHER_PATH")) {
		launcherPath = std::string{rawLauncherPath};
	}
	if (auto const rawOriginalPath = ::getenv("CTI_MPIR_ORIGINAL_PATH")) {
		originalPath = std::string{rawOriginalPath};
	}
	if (auto const rawShimToken = ::getenv("CTI_MPIR_SHIM_TOKEN")) {
		shimToken = std::string{rawShimToken};
	}
	if (auto const logPath = ::getenv("CTI_MPIR_SHIM_LOG_PATH")) {
		globals::logfile = {::fopen(logPath, "a"), ::fclose};
	}

	// Remap stdin / out / err
	if (auto const rawStdinFd = ::getenv("CTI_MPIR_STDIN_FD")) {
		::dup2(cti::stoi(rawStdinFd, "CTI_MPIR_STDIN_FD"), STDIN_FILENO);
	}
	if (auto const rawStdoutFd = ::getenv("CTI_MPIR_STDOUT_FD")) {
		::dup2(cti::stoi(rawStdoutFd, "CTI_MPIR_STDOUT_FD"), STDOUT_FILENO);
	}
	if (auto const rawStderrFd = ::getenv("CTI_MPIR_STDERR_FD")) {
		::dup2(cti::stoi(rawStderrFd, "CTI_MPIR_STDERR_FD"), STDERR_FILENO);
	}

	log("started shim, pid is %d\n", getpid());

	// Check for the shim activation token; some wrappers make their own calls
	// to the job launcher and we should only activate the shim on the true job launch.
	// The token will always be the last argument if it is present.
	if (argv[argc - 1] == shimToken) {
		hasShimToken = true;
		argc--; // don't include the token in actual app launch
	}

	launcherArgv = {launcherPath};
	for (int i = 1; i < argc; i++) {
		launcherArgv.push_back(argv[i]);
	}

	return std::make_tuple(inputFd, outputFd, launcherPath, originalPath, launcherArgv, hasShimToken);
}

int main(int argc, char const* argv[], char const* env[])
{
	// Parse and verify arguments
	auto const [inputFd, outputFd, launcherPath, originalPath, launcherArgv, hasShimToken] = parse_from_env(argc, argv);
	if (launcherPath.empty() || launcherArgv.empty()) {
		exit(1);
	}

	if (hasShimToken) {
		log("cti shim token detected\n");

		// Close unused pipe end
		::close(inputFd);

		// Restore original PATH
		for (auto&& env_var :
			{ "CTI_MPIR_SHIM_INPUT_FD"
			, "CTI_MPIR_SHIM_OUTPUT_FD"
			, "CTI_MPIR_LAUNCHER_PATH"
			, "CTI_MPIR_ORIGINAL_PATH" }) {
			::unsetenv(env_var);
		}
		if (::setenv("PATH", originalPath.c_str(), 1)) {
			log("failed to restore PATH: %s\n", strerror(errno));
			exit(-1);
		}

		// Create MPIR launch instance based on arguments
		log("launcher: %s\n", launcherPath.c_str());
		log("argv:\n");
		for (auto&& arg : launcherArgv) {
			log("\t%s\n", arg.c_str());
		}

		// Start job launcher in stopped state and get the pid
		auto pid = [](const std::string &launcherPath, const std::vector<std::string> &launcherArgv){
			auto launcherProcess = Inferior{launcherPath, launcherArgv, {}, {}};

			auto pid = launcherProcess.getPid();
			log("launcher pid is %d\n", pid);

			// Ensure inferior stays stopped on detach
			::kill(pid, SIGSTOP);

			// Inferior will detach on scope exit
			return pid;
		}(launcherPath, launcherArgv);

		// Send PID to CTI
		log("detached, sending pid\n");
		fdWriteLoop(outputFd, pid);

		// Close pipe
		::close(outputFd);

		// It is now the job of CTI to read MPIR data and send SIGCONT to the
		// inferior launcher when it is done reading.

		// Stay alive until launcher is done so wrapper scripts that monitor
		// the lifetime of their "launcher" don't exit immediately
		log("waiting for child\n");
		waitpid(pid, nullptr, 0);
		log("child exited\n");
	} else {
		log("no token, forwarding to %s\n", launcherPath.c_str());
		argv[0] = launcherPath.c_str();

		log("\t");
		for (int i = 0; i < argc; i++) {
			log("%s ", argv[i]);
		}
		log("\n");

		execvpe(launcherPath.c_str(), const_cast<char* const*>(argv), const_cast<char* const*>(env));
		return -1;
	}

	return 0;
}
