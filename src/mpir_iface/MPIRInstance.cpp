#include "MPIRInstance.hpp"

using Symbol  = MPIRInferior::Symbol;
using Address = MPIRInferior::Address;

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
	inferior.setBreakpoint("MPIR_Breakpoint", &MPIRInferior::stop_on_breakpoint);

	/* set MPIR_being_debugged = 1 */
	inferior.writeMemory(inferior.getSymbol("MPIR_being_debugged")->getOffset(), 1);
}


/* memory access helpers */

template <typename T>
static void readVariable(MPIRInferior& inf, T* buf, std::string const& symName) {
	const Symbol *sym = inf.getSymbol(symName);
	assert(sizeof(T) == sym->getSize());
	inf.readMemory(buf, sym->getOffset(), sym->getSize());
}

template <typename T>
static void readArrayElem(MPIRInferior& inf, T* buf, std::string const& symName, size_t idx) {
	Address elem_addr;
	{ Address array_start;
		readVariable(inf, &array_start, symName);
		elem_addr = array_start + idx * sizeof(void*);
	}

	inf.readMemory(buf, elem_addr, sizeof(T));
}

/* instance implementations */

void MPIRInstance::runToMPIRBreakpoint() {
	DEBUG(std::cerr, "running inferior til MPIR_Breakpoint" << std::endl);
	MPIRDebugState debugState = MPIRDebugState::Unknown;

	while (debugState != MPIRDebugState::DebugSpawned) {
		inferior.continueRun();
		/* inferior now in stopped state. read MPIR_debug_state */
		readVariable(inferior, &debugState, "MPIR_debug_state");
	}

}

static const size_t BUFSIZE = 64;
std::vector<MPIRInstance::MPIR_ProcTableElem> MPIRInstance::getProcTable() {
	int num_pids = 0;
	readVariable(inferior, &num_pids, "MPIR_proctable_size");
	DEBUG(std::cerr, "procTable has size " << std::to_string(num_pids) << std::endl);

	std::vector<MPIR_ProcTableElem> procTable;

	/* copy elements */
	for (int i = 0; i < num_pids; i++) {
		MPIR_ProcDescElem procDesc;
		readArrayElem<MPIR_ProcDescElem>(inferior, &procDesc, "MPIR_proctable", i);

		/* read hostname */
		char buf[BUFSIZE + 1];
		inferior.readMemory(buf, procDesc.host_name, BUFSIZE);
		buf[BUFSIZE] = '\0';

		/* copy hostname */
		{ std::stringstream ss;
			ss << buf;
			procTable.emplace_back(MPIR_ProcTableElem{procDesc.pid, ss.str()});
		}
	}

	return procTable;
}

std::string MPIRInstance::readStringAt(std::string const& symName) {
	/* get address */
	Address strAddress;
	readVariable(inferior, &strAddress, symName);

	/* read string */
	std::string result;
	{ char c = ' ';
		while (c) {
			inferior.readMemory(&c, strAddress++, 1);
			if (c) { result.push_back(c); }
		}
	}

	return result;
}
