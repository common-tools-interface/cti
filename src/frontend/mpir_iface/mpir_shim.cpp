#include <signal.h>

#include <iostream>
#include <memory>
#include <vector>

#include "MPIRInstance.hpp"

/* use launcher MPIR interface to output the following:
<contents of optional string argument>
...
<contents of optional string argument>
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

void usage(char* const argv[], int return_code) {
	std::cerr << "usage: " << argv[0] << " --launch=<launcher path> <launcher args>" << std::endl;
	std::cerr << "       " << argv[0] << " --attach=<target pid>" << std::endl;
	exit(return_code);
}

struct MPIRHandle {
	mpir_id_t mid;
	MPIRHandle(mpir_id_t mid_) : mid(mid_) {}
	~MPIRHandle() { if (mid >= 0) { _cti_mpir_releaseInstance(mid); } }
	operator bool() const { return (mid >= 0); }
};

using MPIRProcTable = std::unique_ptr<cti_mpir_procTable_t, decltype(&_cti_mpir_deleteProcTable)>;
using CStr = std::unique_ptr<char, decltype(&::free)>;

#include <unistd.h>
#include <limits.h>

std::string readLink(std::string const& path) {
	char buf[PATH_MAX];
	ssize_t len = ::readlink(path.c_str(), buf, PATH_MAX - 1);
	if (len >= 0) {
		buf[len] = '\0';
		return std::string(buf);
	}
	throw std::runtime_error("readlink failed");
}

#include <getopt.h>

int main(int argc, char* const argv[]) {

	// create MPIR launch or attach instance based on arguments
	auto make_MPIRHandle = [](int const argc, char* const argv[]) {

		std::string launcherPath;
		const char* const *launcherArgv = nullptr;
		pid_t attachPid = 0;

		// parse arguments with getopt
		{ static struct option long_options[] = {
				{"launch", required_argument, 0, 'l'},
				{"attach", required_argument, 0, 'a'},
				{"help", no_argument, 0, 'h'},
				{nullptr, 0, nullptr, 0}
			};

			// parse arguments with getopt
			int ch; int option_index;
			while ((ch = getopt_long(argc, argv, "l:a:h", long_options, &option_index)) != -1) {
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
			launcherArgv = argv + optind;
		}

		// launch MPIR inferior
		if (!launcherPath.empty() && !attachPid) {
			return MPIRHandle(_cti_mpir_newLaunchInstance(launcherPath.c_str(), launcherArgv, nullptr, 0, -1, -1));
		} else if (launcherPath.empty() && attachPid) {
			// readlink attach path from /proc/<pid>/exe
			auto const attachPath = readLink("/proc/" + std::to_string(attachPid) + "/exe");

			return MPIRHandle(_cti_mpir_newAttachInstance(attachPath.c_str(), attachPid));
		} else {
			usage(argv, 1);
			return MPIRHandle(0);
		}
	};

	if (auto inst = make_MPIRHandle(argc, argv)) {
		// read and output totalview_jobid
		if (auto jobIdStr = CStr(_cti_mpir_getStringAt(inst.mid, "totalview_jobid"), ::free)) {

			// jobid followed by newline
			std::cout << jobIdStr.get() << std::endl;

		} else { // jobid read was not valid
			std::cerr << "totalview_jobid read error" << std::endl;
			return 1;
		}

		// read and output num_pids, proctable
		if (auto procTable = MPIRProcTable(_cti_mpir_newProcTable(inst.mid), _cti_mpir_deleteProcTable)) {

			// num_pids followed by proctable elements: proc pid newline hostname newline
			std::cout << procTable->num_pids << std::endl;
			for (size_t i = 0; i < procTable->num_pids; i++) {
				std::cout << procTable->pids[i] << std::endl;
				std::cout << procTable->hostnames[i] << std::endl;
			}

			// end with newline
			std::cout << std::endl;

		} else { // proctable read was not valid
			std::cerr << "proctable read error" << std::endl;
			return 1;
		}

		// STOP self, continue from MPIR_Breakpoint by sending SIGCONT
		signal(SIGINT, SIG_IGN);
		raise(SIGSTOP);

	} else { // mpir launch failed
		std::cerr << "mpir launch error: " << argv[1] << "(";
		for (const char* const* arg = argv + 2; *arg != nullptr; arg++) {
			std::cerr << *arg << ", ";
		}
		std::cerr << ")" << std::endl;
		return 1;
	}

	return 0;
}
