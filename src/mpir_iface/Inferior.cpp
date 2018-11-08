#include "Inferior.hpp"

/* symtab helpers */

static Dyninst::SymtabAPI::Symtab* make_Symtab(std::string const& binary) {
	using Symtab = Dyninst::SymtabAPI::Symtab;

	Symtab *symtab_ptr;
	if (!Symtab::openFile(symtab_ptr, binary)) {
		throw std::runtime_error("Symtab failed to open file");
	}
	return symtab_ptr;
}

/* breakpoint helpers */

Dyninst::ProcControlAPI::Process::cb_ret_t
stop_on_breakpoint(Dyninst::ProcControlAPI::Event::const_ptr genericEv) {
	return Dyninst::ProcControlAPI::Process::cbProcStop;
}

/* inferior implementations */

Inferior::Inferior(std::string const& launcher,
	std::vector<std::string> const& launcherArgv, 
	std::vector<std::string> const& envVars,
	std::map<int, int> const& remapFds) :
	symtab(make_Symtab(launcher), Symtab::closeSymtab),
	proc(Process::createProcess(launcher, launcherArgv, envVars, remapFds)) {

	if (!proc) {
		throw std::runtime_error("failed to launch " + launcher);
	}

	/* prepare breakpoint callback */
	Process::registerEventCallback(Dyninst::ProcControlAPI::EventType::Breakpoint, stop_on_breakpoint);
}

Inferior::Inferior(std::string const& launcher, pid_t pid) :
	symtab(make_Symtab(launcher), Symtab::closeSymtab),
	proc(Process::attachProcess(pid, {})) {

	/* prepare breakpoint callback */
	Process::registerEventCallback(Dyninst::ProcControlAPI::EventType::Breakpoint, stop_on_breakpoint);
}

Inferior::~Inferior() {
	Process::removeEventCallback(Dyninst::ProcControlAPI::EventType::Breakpoint, stop_on_breakpoint);

	proc->detach();
	DEBUG(std::cerr, "~Inferior: detached from " << std::to_string(proc->getPid()) << std::endl);
}

pid_t Inferior::getPid() {
	return proc->getPid();
}

/* memory read / write base implementations */
void Inferior::writeFromBuf(Address destAddr, const char* buf, size_t len) {
	proc->writeMemory(destAddr, buf, len);
}
void Inferior::writeFromBuf(std::string const& destName, const char* buf, size_t len) {
	writeFromBuf(getAddress(destName), buf, len);
}
void Inferior::readToBuf(char* buf, Address sourceAddr, size_t len) {
	proc->readMemory(buf, sourceAddr, len);
}
void Inferior::readToBuf(char* buf, std::string const& sourceName, size_t len) {
	readToBuf(buf, getAddress(sourceName), len);
}

/* symbol / breakpoint manipulation */
void Inferior::continueRun() {
	/* note that can only read on stopped thread */
	do {
		proc->continueProc();
		Process::handleEvents(true); // blocks til event received
	} while (!proc->hasStoppedThread());
}

void Inferior::addSymbol(std::string const& symName) {
	std::vector<Symbol*> foundSyms;
	symtab->findSymbol(foundSyms, symName);
	if (!foundSyms.empty()) {
		symbols[symName] = foundSyms[0];
	} else {
		throw std::runtime_error(std::string("error: ") + symName + " not found");
	}
}

Inferior::Address Inferior::getAddress(std::string const& symName) {
	// if symbol address not found yet, find it
	if (symbols.find(symName) == symbols.end()) {
		addSymbol(symName);
	}

	return symbols.at(symName)->getOffset();
}

/* default handler: stop on breakpoint */

void Inferior::setBreakpoint(std::string const& fnName) {
	Breakpoint::ptr breakpoint = Breakpoint::newBreakpoint();
	proc->addBreakpoint(getAddress(fnName), breakpoint);
}