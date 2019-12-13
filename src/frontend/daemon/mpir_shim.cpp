
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
	auto outputFd = int{-1};
	auto launcherPath = std::string{};
	std::vector<std::string> launcherArgv;

	if (auto const rawOutputFd     = ::getenv("CTI_MPIR_SHIM_OUTPUT_FD")) {
		outputFd = std::stoi(rawOutputFd);
	}
	if (auto const rawLauncherPath = ::getenv("CTI_MPIR_LAUNCHER_PATH")) {
		launcherPath = std::string{rawLauncherPath};
	}

	launcherArgv = {launcherPath};
	for (int i = 0; i < argc; i++) {
		launcherArgv.push_back(argv[i]);
	}

	return std::make_tuple(outputFd, launcherPath, launcherArgv);
}

int main(int argc, char const* argv[])
{
	// Parse and verify arguments
	auto const [outputFd, launcherPath, launcherArgv] = parse_from_env(argc, argv);
	if (launcherPath.empty() || launcherArgv.empty()) {
		exit(1);
	}

	// Redirect output
	if (STDOUT_FILENO != outputFd) {
		::dup2(STDOUT_FILENO, outputFd);
	}

	// Create MPIR launch instance based on arguments
	auto mpirInstance = MPIRInstance{launcherPath, launcherArgv};

	// send the MPIR data
	auto const mpirProctable = mpirInstance.getProctable();
	rawWriteLoop(outputFd, FE_daemon::MPIRResp
		{ .type     = FE_daemon::RespType::MPIR
		, .mpir_id  = -1
		, .launcher_pid = mpirInstance.getLauncherPid()
		, .job_id   = 0
		, .step_id  = 0
		, .num_pids = static_cast<int>(mpirProctable.size())
	});
	for (auto&& [pid, hostname] : mpirProctable) {
		rawWriteLoop(outputFd, pid);
		writeLoop(outputFd, hostname.c_str(), hostname.length() + 1);
	}

	// STOP self, continue from MPIR_Breakpoint by sending SIGCONT
	signal(SIGINT, SIG_IGN);
	raise(SIGSTOP);

	return 0;
}
