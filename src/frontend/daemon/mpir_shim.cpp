/******************************************************************************\
 * mpir_shim.cpp - cti fe_daemon utility to extract MPIR proctable information
 *
 * Copyright 2019 Cray Inc. All Rights Reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
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

#include "frontend/mpir_iface/MPIRInstance.hpp"
#include "cti_fe_daemon_iface.hpp"

/*
 * Client scripts can read this data in until a newline is encountered. At
   this point, this program will have raised SIGSTOP.
 * To continue the job launch from MPIR_Breakpoint (e.g. after the proper
   backend files are created from the MPIR proctable), send a SIGINT.
 * After continuing, the target program's output will be sent to standard out / standard error.
*/

static auto parse_from_env(int argc, char const* argv[])
{
	auto inputFd  = int{-1};
	auto outputFd = int{-1};
	auto launcherPath = std::string{};
	auto originalPath = std::string{};
	std::vector<std::string> launcherArgv;

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

	launcherArgv = {launcherPath};
	for (int i = 0; i < argc; i++) {
		launcherArgv.push_back(argv[i]);
	}

	return std::make_tuple(inputFd, outputFd, launcherPath, originalPath, launcherArgv);
}

int main(int argc, char const* argv[])
{
	// Parse and verify arguments
	auto const [inputFd, outputFd, launcherPath, originalPath, launcherArgv] = parse_from_env(argc, argv);
	if (launcherPath.empty() || launcherArgv.empty()) {
		exit(1);
	}

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

	// Create MPIR launch instance based on arguments
	auto mpirInstance = MPIRInstance{launcherPath, launcherArgv};

	// send PID
	rawWriteLoop(outputFd, getpid());

	// send the MPIR data
	auto const mpirProctable = mpirInstance.getProctable();
	rawWriteLoop(outputFd, FE_daemon::MPIRResp
		{ .type     = FE_daemon::RespType::MPIR
		, .mpir_id  = 0
		, .launcher_pid = mpirInstance.getLauncherPid()
		, .num_pids = static_cast<int>(mpirProctable.size())
	});
	for (auto&& [pid, hostname] : mpirProctable) {
		rawWriteLoop(outputFd, pid);
		writeLoop(outputFd, hostname.c_str(), hostname.length() + 1);
	}

	// Close pipe
	::close(outputFd);

	// STOP self, continue from MPIR_Breakpoint by sending SIGCONT
	signal(SIGINT, SIG_IGN);
	raise(SIGSTOP);

	return 0;
}
