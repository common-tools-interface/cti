#include "MPIRInstance.hpp"

/* create new process instance */
MPIRInstance::MPIRInstance(std::string const& launcher,
	std::vector<std::string> const& launcherArgv, std::map<int, int> remapFds) :
	inferior(launcher, launcherArgv, remapFds) {

	/* read symbols, set breakpoints, etc. */
	setupMPIRStandard();
	/* wait until MPIR data is ready and object can handle its data */
	runToMPIRBreakpoint();
}

/* attach to process given pid */
MPIRInstance::MPIRInstance(std::string const& launcher, Dyninst::PID pid) :
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
	{ Symbol *sym = inferior.getSymbol("MPIR_being_debugged");
		int trueByte = 1;
		inferior.proc->writeMemory(sym->getOffset(), reinterpret_cast<const void*>(&trueByte), 1);
	}
}

void MPIRInstance::runToMPIRBreakpoint() {
	DEBUG(std::cerr, "running inferior til MPIR_Breakpoint" << std::endl);
	MPIRDebugState debugState = MPIRDebugState::Unknown;

	while (debugState != MPIRDebugState::DebugSpawned) {
		inferior.proc->continueProc();
		Process::handleEvents(true); // blocks til event received

		/* read MPIR_debug_state. note that can only read on stopped thread */
		if (inferior.proc->hasStoppedThread()) {
			MPIRInstance::readVariable(&debugState, "MPIR_debug_state");
		}
	}

	/* inferior now in stopped state */
}

/* memory access functions */

template <typename T>
void MPIRInstance::readVariable(T* buf, std::string const& symName) {
	Symbol *sym = inferior.getSymbol(symName);
	assert(sizeof(T) == sym->getSize());
	readAddress(buf, sym->getOffset(), sym->getSize());
}

template <typename T>
void MPIRInstance::readArrayElem(T* buf, std::string const& symName, size_t idx) {
	Dyninst::Address elem_addr;
	{ Dyninst::Address array_start;
		readVariable(&array_start, symName);
		elem_addr = array_start + idx * sizeof(void*);
	}

	readAddress(buf, elem_addr, sizeof(T));
}

static const size_t BUFSIZE = 64;
std::vector<MPIRInstance::MPIR_ProcTableElem> MPIRInstance::getProcTable() {
	int num_pids = 0;
	readVariable(&num_pids, "MPIR_proctable_size");
	DEBUG(std::cerr, "procTable has size " << std::to_string(num_pids) << std::endl);

	std::vector<MPIR_ProcTableElem> procTable;

	/* copy elements */
	for (int i = 0; i < num_pids; i++) {
		MPIR_ProcDescElem procDesc;
		readArrayElem<MPIR_ProcDescElem>(&procDesc, "MPIR_proctable", i);

		/* read hostname */
		char buf[BUFSIZE + 1];
		readAddress(buf, procDesc.host_name, BUFSIZE);
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
	MPIRInstance::Address strAddress;
	readVariable(&strAddress, symName);

	/* read string */
	std::string result;
	{ char c = ' ';
		while (c) {
			readAddress(&c, strAddress++, 1);
			if (c) { result.push_back(c); }
		}
	}

	return result;
}

template <typename T>
void MPIRInstance::writeVariable(std::string const& symName, T const& data, size_t len) {
	Symbol *sym = inferior.getSymbol(symName);
	inferior.proc->writeMemory(sym->getOffset(), reinterpret_cast<const void*>(&data), len);
}

