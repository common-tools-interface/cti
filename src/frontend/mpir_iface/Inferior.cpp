/******************************************************************************\
 * Inferior.cpp
 *
 * Copyright 2018-2019 Cray Inc.  All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
 *
 ******************************************************************************/

// This pulls in config.h
#include "cti_defs.h"

#include <sys/types.h>
#include <sys/wait.h>

#include "Inferior.hpp"

/* symtab helpers */

static Dyninst::SymtabAPI::Symtab* make_Symtab(std::string const& binary) {
	using Symtab = Dyninst::SymtabAPI::Symtab;

	Symtab *symtab_ptr;
	if (!Symtab::openFile(symtab_ptr, binary)) {
		throw std::runtime_error("Symtab failed to open file: '" + binary + "'");
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
	std::map<int, int> const& remapFds)
	: m_symtab{make_Symtab(launcher), Symtab::closeSymtab}
	, m_symbols{}
	, m_proc{Process::createProcess(launcher, launcherArgv, envVars, remapFds)}
{
	if (!m_proc) {
		throw std::runtime_error("failed to launch " + launcher);
	}

	/* prepare breakpoint callback */
	Process::registerEventCallback(Dyninst::ProcControlAPI::EventType::Breakpoint, stop_on_breakpoint);
}

static size_t numArgv(char const* const argv[])
{
	size_t count = 0;
	for (char const* const* arg = argv; *arg != nullptr; arg++) { count++; }
	return count;
}

Inferior::Inferior(char const* launcher, char const* const launcherArgv[],
	std::vector<std::string> const& envVars, std::map<int, int> const& remapFds)
	: Inferior
		{ launcher
		, std::vector<std::string>{ launcherArgv, launcherArgv + numArgv(launcherArgv) }
		, envVars
		, remapFds
	}
{}

Inferior::Inferior(std::string const& launcher, pid_t pid)
	: m_symtab{make_Symtab(launcher), Symtab::closeSymtab}
	, m_symbols{}
	, m_proc(Process::attachProcess(pid, {}))
{
	/* prepare breakpoint callback */
	Process::registerEventCallback(Dyninst::ProcControlAPI::EventType::Breakpoint, stop_on_breakpoint);
}

Inferior::~Inferior() {
	Process::removeEventCallback(Dyninst::ProcControlAPI::EventType::Breakpoint, stop_on_breakpoint);

	if (!isTerminated()) {
		m_proc->detach();
		DEBUG(std::cerr, "~Inferior: detached from " << std::to_string(m_proc->getPid()) << std::endl);
	}
}

pid_t Inferior::getPid() {
	return m_proc->getPid();
}

/* memory read / write base implementations */
void Inferior::writeFromBuf(Address destAddr, const char* buf, size_t len) {
	m_proc->writeMemory(destAddr, buf, len);
}
void Inferior::writeFromBuf(std::string const& destName, const char* buf, size_t len) {
	writeFromBuf(getAddress(destName), buf, len);
}
void Inferior::readToBuf(char* buf, Address sourceAddr, size_t len) {
	m_proc->readMemory(buf, sourceAddr, len);
}
void Inferior::readToBuf(char* buf, std::string const& sourceName, size_t len) {
	readToBuf(buf, getAddress(sourceName), len);
}

/* symbol / breakpoint manipulation */
void Inferior::continueRun() {
	/* note that can only read on stopped thread */
	do {
		m_proc->continueProc();
		Process::handleEvents(true); // blocks til event received
	} while (!isTerminated() && !m_proc->hasStoppedThread());
}

void Inferior::terminate() {
	if (!isTerminated()) {
		auto const pid = m_proc->getPid();
		m_proc->detach();
		::kill(pid, SIGTERM);
		::waitpid(pid, nullptr, 0);
	}
}

void Inferior::addSymbol(std::string const& symName) {
	std::vector<Symbol*> foundSyms;
	m_symtab->findSymbol(foundSyms, symName);
	if (!foundSyms.empty()) {
		m_symbols[symName] = foundSyms[0];
	} else {
		throw std::runtime_error(std::string("error: ") + symName + " not found");
	}
}

Inferior::Address Inferior::getAddress(std::string const& symName) {
	// if symbol address not found yet, find it
	if (m_symbols.find(symName) == m_symbols.end()) {
		addSymbol(symName);
	}

	return m_symbols.at(symName)->getOffset();
}

/* default handler: stop on breakpoint */

void Inferior::setBreakpoint(std::string const& fnName) {
	Breakpoint::ptr breakpoint = Breakpoint::newBreakpoint();
	m_proc->addBreakpoint(getAddress(fnName), breakpoint);
}
