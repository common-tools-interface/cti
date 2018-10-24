#include "MPIRInstance.hpp"

using Symbol  = Inferior::Symbol;

/* create new process instance */
MPIRInstance::MPIRInstance(std::string const& launcher,
	std::vector<std::string> const& launcherArgv,
	std::vector<std::string> envVars, std::map<int, int> remapFds) :
	inferior(launcher, launcherArgv, envVars, remapFds) {

	/* read symbols, set breakpoints, etc. */
	setupMPIRStandard();
	/* wait until MPIR data is ready and object can handle its data */
	runToMPIRBreakpoint();
}

/* attach to process given pid */
MPIRInstance::MPIRInstance(std::string const& launcher, pid_t pid) :
	inferior(launcher, pid) {

	setupMPIRStandard();
}

void MPIRInstance::setupMPIRStandard() {
	/* read in required MPIR symbols */
	inferior.addSymbol("MPIR_being_debugged");
	inferior.addSymbol("MPIR_Breakpoint");
	inferior.addSymbol("MPIR_debug_state");
	inferior.addSymbol("MPIR_i_am_starter");
	inferior.addSymbol("MPIR_partial_attach_ok");
	inferior.addSymbol("MPIR_proctable");
	inferior.addSymbol("MPIR_proctable_size");

	/* set up breakpoints */
	inferior.setBreakpoint("MPIR_Breakpoint");

	/* set MPIR_being_debugged = 1 */
	inferior.writeVariable("MPIR_being_debugged", 1);
}


/* instance implementations */

void MPIRInstance::runToMPIRBreakpoint() {
	DEBUG(std::cerr, "running inferior til MPIR_Breakpoint" << std::endl);
	MPIRDebugState debugState = MPIRDebugState::Unknown;

	do {
		DEBUG(std::cerr, "MPIR_debug_state: " << debugState << std::endl);
		DEBUG(std::cerr, "MPIR_being_debugged: " << inferior.readVariable<int>("MPIR_being_debugged") << std::endl);
		inferior.continueRun();
		/* inferior now in stopped state. read MPIR_debug_state */
		debugState = inferior.readVariable<MPIRDebugState>("MPIR_debug_state");
	} while (debugState != MPIRDebugState::DebugSpawned);
}

template <typename T>
static T readArrayElem(Inferior& inf, std::string const& symName, size_t idx) {
	Inferior::Address elem_addr;
	{ auto array_start = inf.readVariable<Inferior::Address>(symName);
		elem_addr = array_start + idx * sizeof(T);
	}
	return inf.readMemory<T>(elem_addr);
}

#include <sstream>
static const size_t BUFSIZE = 64;
std::vector<MPIRInstance::MPIR_ProcTableElem> MPIRInstance::getProcTable() {
	auto num_pids = inferior.readVariable<int>("MPIR_proctable_size");
	DEBUG(std::cerr, "procTable has size " << std::to_string(num_pids) << std::endl);

	std::vector<MPIR_ProcTableElem> procTable;

	/* copy elements */
	for (int i = 0; i < num_pids; i++) {
		auto procDesc = readArrayElem<MPIR_ProcDescElem>(inferior, "MPIR_proctable", i);

		/* read hostname */
		auto buf = inferior.readMemory<std::array<char, BUFSIZE+1>>(procDesc.host_name);
		buf[BUFSIZE] = '\0';

		/* copy hostname */
		{ std::stringstream ss;
			ss << buf.data();
			DEBUG(std::cerr, "procTable[" << i << "]: " << procDesc.pid << ", " << ss.str() << std::endl);
			procTable.emplace_back(MPIR_ProcTableElem{procDesc.pid, ss.str()});
		}
	}

	return procTable;
}

std::string MPIRInstance::readStringAt(std::string const& symName) {
	/* get address */
	auto strAddress = inferior.readVariable<Address>(symName);

	/* read string */
	std::string result;
	while (char c = inferior.readMemory<char>(strAddress++)) {
		result.push_back(c);
	}

	return result;
}
