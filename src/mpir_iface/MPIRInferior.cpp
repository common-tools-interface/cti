#include "MPIRInferior.hpp"

/* inferior implementations */
MPIRInferior::MPIRInferior(std::string const& launcher,
	std::vector<std::string> const& launcherArgv, 
	std::vector<std::string> envVars, std::map<int, int> remapFds) :
	symtab(launcher),
	proc(Process::createProcess(launcher, launcherArgv, envVars, remapFds)) {

	/* prepare breakpoint callback */
	Process::registerEventCallback(Dyninst::ProcControlAPI::EventType::Breakpoint, on_breakpoint);
}

MPIRInferior::MPIRInferior(std::string const& launcher, Dyninst::PID pid) :
	symtab(launcher),
	proc(Process::attachProcess(pid, {})) {

	/* prepare breakpoint callback */
	Process::registerEventCallback(Dyninst::ProcControlAPI::EventType::Breakpoint, on_breakpoint);
}

void MPIRInferior::continueRun() {
	/* note that can only read on stopped thread */
	do {
		proc->continueProc();
		Process::handleEvents(true); // blocks til event received
	} while (!proc->hasStoppedThread());
}

void MPIRInferior::setBreakpoint(std::string const& fnName, HandlerFnType* handlerPtr) {
	Dyninst::Address address = getSymbol(fnName)->getOffset();
	Breakpoint::ptr breakpoint = Breakpoint::newBreakpoint();
	breakpoint->setData(reinterpret_cast<void*>(handlerPtr));
	proc->addBreakpoint(address, breakpoint);
}

void MPIRInferior::addSymbol(std::string const& symName) {
	auto foundSyms = symtab.findSymbol(symName);
	if (!foundSyms.empty()) {
		symbols[symName] = foundSyms[0];
	} else {
		throw std::runtime_error(std::string("error: ") + symName + " not found");
	}
}

MPIRInferior::~MPIRInferior() {
	Process::removeEventCallback(Dyninst::ProcControlAPI::EventType::Breakpoint, on_breakpoint);

	proc->detach();
	DEBUG(std::cerr, "~MPIRInferior: detached from " << std::to_string(proc->getPid()) << std::endl);
}