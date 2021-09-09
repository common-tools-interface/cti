/******************************************************************************\
 * mpir_shim.cpp - cti fe_daemon utility to extract MPIR proctable information
 *
 * Copyright 2019-2021 Hewlett Packard Enterprise Development LP.
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 ******************************************************************************/

#include <unistd.h>
#include <limits.h>

#include <signal.h>

#include <getopt.h>

#include <iostream>
#include <memory>
#include <vector>

#include "frontend/mpir_iface/Inferior.hpp"
#include "cti_fe_daemon_iface.hpp"

static const auto shimToken = std::string{CTI_SHIM_TOKEN};

/*
 * MPIR shim is used by bootstrap/wrapper scripts that aren't MPIR compliant.
 * CTI will insert the shim into PATH with the same name as the normal launcher,
 * and the wrapper will launch the shim instead of the actual launcher. The shim
 * then starts the real launcher in a stopped state, sends the pid to CTI, which
 * then attaches and reads the MPIR info. The shim only activates if the
 * CTI_SHIM_TOKEN is the last argument on the command line.
*/

static auto parse_from_env(int argc, char const* argv[])
{
	auto inputFd  = int{-1};
	auto outputFd = int{-1};
	auto launcherPath = std::string{};
	auto originalPath = std::string{};
	std::vector<std::string> launcherArgv;
	auto hasShimToken = false;

	if (auto const rawInputFd      = ::getenv("CTI_MPIR_SHIM_INPUT_FD")) {
		inputFd = std::stoi(rawInputFd);
	}
	if (auto const rawOutputFd     = ::getenv("CTI_MPIR_SHIM_OUTPUT_FD")) {
		outputFd = std::stoi(rawOutputFd);
	}
	if (auto const rawLauncherPath = ::getenv("CTI_MPIR_LAUNCHER_PATH")) {
		launcherPath = std::string{rawLauncherPath};
	}
	if (auto const rawOriginalPath = ::getenv("CTI_MPIR_ORIGINAL_PATH")) {
		originalPath = std::string{rawOriginalPath};
	}

	// Remap stdin / out /err
	if (auto const rawStdinFd = ::getenv("CTI_MPIR_STDIN_FD")) {
		::dup2(std::stoi(rawStdinFd), STDIN_FILENO);
	}
	if (auto const rawStdoutFd = ::getenv("CTI_MPIR_STDOUT_FD")) {
		::dup2(std::stoi(rawStdoutFd), STDOUT_FILENO);
	}
	if (auto const rawStderrFd = ::getenv("CTI_MPIR_STDERR_FD")) {
		::dup2(std::stoi(rawStderrFd), STDERR_FILENO);
	}

	// Check for the shim activation token; some wrappers make their own calls
	// to srun and we should only activate the shim on the true job launch.
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
		fprintf(stderr, "has token\n");

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
			fprintf(stderr, "failed to restore PATH: %s\n", strerror(errno));
			exit(-1);
		}

		fprintf(stderr, "launching\n");

		// Create MPIR launch instance based on arguments
		fprintf(stderr, "launcher: %s\n", launcherPath.c_str());
		fprintf(stderr, "argv:\n");
		for (const auto arg : launcherArgv) {
			fprintf(stderr, "\t%s\n", arg.c_str());
		}

		pid_t pid;
		{
			// Start job launcher in stopped state
			auto launcherProcess = Inferior{launcherPath, launcherArgv, {}, {}};

			pid = launcherProcess.getPid();
			fprintf(stderr, "launcher pid reports %d\n", pid);

			// Ensure inferior stays stopped on detach
			::kill(pid, SIGSTOP);

			// Inferior will detach on scope exit
		}

		// Send attach info to CTI
		fprintf(stderr, "detached\n");
		fprintf(stderr, "sending data\n");

		// send PID
		rawWriteLoop(outputFd, pid);

		// Close pipe
		if (outputFd != 1) {
			::close(outputFd);
		}

		// It is now the job of CTI to read MPIR data and send SIGCONT to the
		// inferior launcher when it is done reading.

		// stay alive until launcher is done
		fprintf(stderr, "waiting for child\n");
		waitpid(pid, nullptr, 0);
	} else {
		fprintf(stderr, "no token, forwarding to %s\n", launcherPath.c_str());
		argv[0] = launcherPath.c_str();
		execvpe(launcherPath.c_str(), const_cast<char* const*>(argv), const_cast<char* const*>(env));
		return -1;
	}

	fprintf(stderr, "done\n");

	return 0;
}
