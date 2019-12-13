
#include <unistd.h>
#include <limits.h>

#include <signal.h>

#include <getopt.h>

#include <iostream>
#include <memory>
#include <vector>

#include "MPIRInstance.hpp"

#include "useful/cti_wrappers.hpp"

/* use launcher MPIR interface to output the following:
<number of job process elements>
<pid of process 0>
<hostname where process 0 resides>
...
<pid of process N>
<hostname where process N resides>
<NEWLINE>
*/

/*
 * Client scripts can read this data in until a newline is encountered. At
   this point, this program will have raised SIGSTOP.
 * To continue the job launch from MPIR_Breakpoint (e.g. after the proper
   backend files are created from the MPIR proctable), send a SIGINT.
 * After continuing, the target program's output will be sent to standard out / standard error.
*/

void usage(char const* argv[], int return_code) {
	std::cerr << "usage: " << argv[0] << " --launcher_path=<launcher path> <launcher args>" << std::endl;
	std::cerr << "       " << argv[0] << " --attach_pid=<target pid>" << std::endl;
	exit(return_code);
}

static auto parse_argv(int argc, char const* argv[])
{
	std::string launcherPath;
	std::vector<std::string> launcherArgv;
	pid_t attachPid = 0;

	// parse arguments with getopt
	optind = 0;
	static struct option long_options[] = {
		{"launcher_path", required_argument, 0, 'l'},
		{"attach_pid",    required_argument, 0, 'a'},
		{"help", no_argument, 0, 'h'},
		{nullptr, 0, nullptr, 0}
	};

	// parse arguments with getopt
	int ch; int option_index;
	while ((ch = getopt_long(argc, (char* const*)argv, "l:a:h", long_options, &option_index)) != -1) {
		switch (ch) {
		case 'l': // launch
			launcherPath = std::string(optarg);
			break;
		case 'a': // attach
			attachPid = std::stoi(optarg);
			break;
		case 'h':
			usage(argv, 0);
		case '?':
		default:
			usage(argv, 1);
		}
	}

	// leftover is launcher arguments
	launcherArgv = {launcherPath};
	for (int i = optind; i < argc; i++) {
		launcherArgv.push_back(argv[i]);
	}

	return std::make_tuple(launcherPath, launcherArgv, attachPid);
}

int main(int argc, char const* argv[])
{
	// Parse and verify arguments
	auto const [launcherPath, launcherArgv, attachPid] = parse_argv(argc, argv);
	auto const launchMode = (!launcherPath.empty() && !launcherArgv.empty() && !attachPid);
	auto const attachMode = ( launcherPath.empty() &&  launcherArgv.empty() &&  attachPid);
	if (!launchMode && !attachMode) {
		usage(argv, 1);
	}

	// Create MPIR launch or attach instance based on arguments
	auto mpirInstance = launchMode
		? MPIRInstance{launcherPath, launcherArgv}
		: MPIRInstance{cti::cstr::readlink("/proc/" + std::to_string(attachPid) + "/exe"), attachPid};

	// read and output num_pids, proctable
	auto const mpirProctable = mpirInstance.getProctable();
	// num_pids followed by proctable elements: proc pid newline hostname newline
	fprintf(stdout, "%lu\n", mpirProctable.size());
	for (auto&& [pid, hostname] : mpirProctable) {
		fprintf(stdout, "%d\n%s\n", pid, hostname.c_str());
	}
	fprintf(stdout, "\n");

	// STOP self, continue from MPIR_Breakpoint by sending SIGCONT
	signal(SIGINT, SIG_IGN);
	raise(SIGSTOP);

	return 0;
}
