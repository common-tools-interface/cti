/******************************************************************************\
 * Inferior.cpp
 *
 * Copyright 2018-2020 Hewlett Packard Enterprise Development LP.
 * SPDX-License-Identifier: Linux-OpenIB
 ******************************************************************************/

// This pulls in config.h
#include "cti_defs.h"

#include <sys/types.h>
#include <sys/wait.h>

#include "Inferior.hpp"

#include "useful/cti_wrappers.hpp"
#include "useful/cti_argv.hpp"
#include "useful/cti_execvp.hpp"

static inline bool debug_enabled()
{
    static const auto _enabled = []() {
        return (::getenv("CTI_DEBUG") != nullptr);
    }();
    return _enabled;
}

static inline void log(const char* format, ...)
{
    if (debug_enabled()) {
        va_list argptr;
        va_start(argptr, format);
        vfprintf(stderr, format, argptr);
        va_end(argptr);
    }
}

/* process management helpers */

static Dyninst::ProcControlAPI::FollowFork::follow_t disableGlobalFollowFork() {
    using FollowFork = Dyninst::ProcControlAPI::FollowFork;

    FollowFork::setDefaultFollowFork(FollowFork::DisableBreakpointsDetach);

    return FollowFork::getDefaultFollowFork();
}

/* symtab helpers */

static Dyninst::SymtabAPI::Symtab* make_Symtab(std::string const& binary) {
    using Symtab = Dyninst::SymtabAPI::Symtab;

    Symtab *symtab_ptr;
    if (!Symtab::openFile(symtab_ptr, binary)) {
        throw std::runtime_error("Symtab failed to open file: '" + binary + "'");
    }
    return symtab_ptr;
}

static auto find_module_base(Dyninst::ProcControlAPI::Process const& proc)
{
    // Use Dyninst's library list to find the LOAD address of the launcher binary.
    // * Assume that the first executable is the target launcher.
    // * Can't rely on the executable name, as launchers may parse arguments in one
    //   binary, then exec another.
    // * When the base address is not explicitly provided by the binary, Dyninst
    //   does not adjust its symbol table for this base address and it must be
    //   determined at runtime.
    // * Previously used `readelf` to determine if the launcher binary provided
    //   an explicit base address, and if not, to read the process' memory map.
    // * However, Dyninst provides a function `getLoadAddress` to get the binary
    //   load address. This can be used when looking up a symbol name to adjust
    //   to the proper address.
    // * In the case where the base address is provided explicitly, `getLoadAddress`
    //   returns address 0x0. As the symbol table has already been fixed using the
    //   proper base address in this case, a 0x0 base address is correct.
    for (auto&& lib : proc.libraries()) {
        if (lib == nullptr) {
            log("Dyninst returned a null library pointer\n");
            continue;
        }

        log("Reading library %p\n", lib);
        if (!lib->isSharedLib()) {
            return lib->getLoadAddress();
        }
    }

    // No executable found in process
    return Dyninst::Address{0x0};
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
    : m_followForkMode{disableGlobalFollowFork()}
    , m_symtab{make_Symtab(launcher), Symtab::closeSymtab}
    , m_symbols{}
    , m_proc{}
    , m_module_base{}
{
    log("Starting %s\n", launcher.c_str());
    m_proc = Process::createProcess(launcher, launcherArgv, envVars, remapFds);
    if (!m_proc) {
        throw std::runtime_error("failed to start launcher");
    }

    m_module_base = find_module_base(*m_proc);

    if (m_followForkMode != FollowFork::DisableBreakpointsDetach) {
        throw std::runtime_error("failed to disable ProcessControl follow-fork mode");
    }

    /* prepare breakpoint callback */
    log("Setting event breakpoint handler\n");
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
    : m_followForkMode{disableGlobalFollowFork()}
    , m_symtab{make_Symtab(launcher), Symtab::closeSymtab}
    , m_symbols{}
    , m_proc{}
    , m_module_base{}
{
    log("Attaching to pid %d\n", pid);
    m_proc = Process::attachProcess(pid, {});
    if (!m_proc) {
        throw std::runtime_error("Failed to attach to PID " + std::to_string(pid));
    }

    m_module_base = find_module_base(*m_proc);

    if (m_followForkMode != FollowFork::DisableBreakpointsDetach) {
        throw std::runtime_error("failed to disable ProcessControl follow-fork mode");
    }

    /* prepare breakpoint callback */
    log("Setting event breakpoint handler\n");
    Process::registerEventCallback(Dyninst::ProcControlAPI::EventType::Breakpoint, stop_on_breakpoint);
}

Inferior::~Inferior() {
    Process::removeEventCallback(Dyninst::ProcControlAPI::EventType::Breakpoint, stop_on_breakpoint);

    if (!isTerminated()) {
        m_proc->detach();
    }
}

pid_t Inferior::getPid() {
    return m_proc->getPid();
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
        cti::waitpid(pid, nullptr, 0);
    }
}

/* memory read / write base implementations */
void Inferior::writeFromBuf(Address destAddr, const char* buf, size_t len) {
    Dyninst::ProcControlAPI::clearLastError();
    if (!m_proc->writeMemory(destAddr, buf, len)) {
        throw std::runtime_error("write of " + std::to_string(len) + " bytes failed: "
            + std::to_string(Dyninst::ProcControlAPI::getLastError()));
    }
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

    auto const symbol = m_symbols.at(symName);
    auto const address = m_module_base + symbol->getOffset();

    log("symbol %s: start addr %p + symbol offset %p = %p\n",
        symName.c_str(), m_module_base, symbol->getOffset(), address);

    return address;
}

/* default handler: stop on breakpoint */

void Inferior::setBreakpoint(std::string const& fnName) {
    Breakpoint::ptr breakpoint = Breakpoint::newBreakpoint();
    m_proc->addBreakpoint(getAddress(fnName), breakpoint);
}
