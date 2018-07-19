#include "MPIRInferior.hpp"

/* inferior implementations */
MPIRInferior::MPIRInferior(std::string const& launcher,
	std::vector<std::string> const& launcherArgv, std::map<int, int> remapFds) :
	symtab(launcher),
	proc(Process::createProcess(launcher, launcherArgv, {}, remapFds)) {

	/* prepare breakpoint callback */
	Process::registerEventCallback(Dyninst::ProcControlAPI::EventType::Breakpoint, on_breakpoint);
}

MPIRInferior::MPIRInferior(std::string const& launcher, Dyninst::PID pid) :
	symtab(launcher),
	proc(Process::attachProcess(pid, {})) {

	/* prepare breakpoint callback */
	Process::registerEventCallback(Dyninst::ProcControlAPI::EventType::Breakpoint, on_breakpoint);
}

void MPIRInferior::setBreakpoint(std::string const& fnName, HandlerFnType* handlerPtr) {
	Dyninst::Address address = getSymbol(fnName)->getOffset();
	Breakpoint::ptr breakpoint = Breakpoint::newBreakpoint();
	breakpoint->setData(reinterpret_cast<void*>(handlerPtr));
	proc->addBreakpoint(address, breakpoint);
}

void MPIRInferior::addSymbol(std::string const& symName) {
	std::vector<Symbol *> foundSyms;
	if (symtab.get()->findSymbol(foundSyms, symName)) {
		symbols[symName] = foundSyms[0];
	} else {
		throw std::runtime_error(std::string("error: ") + symName + " not found");
	}
}

MPIRInferior::~MPIRInferior() {
	Process::removeEventCallback(Dyninst::ProcControlAPI::EventType::Breakpoint, on_breakpoint);

	proc->detach();
}