/******************************************************************************\
 * Inferior.cpp
 *
 * Copyright 2018-2020 Hewlett Packard Enterprise Development LP.
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
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
        return bool{::getenv("CTI_DEBUG")};
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

static auto find_module_base(std::string const& launcher, pid_t pid)
{
    // Determine if the launcher loads in at a fixed address
    { auto bashReadelfArgv = cti::ManagedArgv{"bash", "-c",
        "readelf -l --wide " + launcher + " | grep 'LOAD' | grep 'R E' | tr -s ' ' | cut -d ' ' -f5"};
        auto bashReadelfOutput = cti::Execvp{"bash", bashReadelfArgv.get(), cti::Execvp::stderr::Ignore};

        auto rawModuleBase = std::string{};
        if (!std::getline(bashReadelfOutput.stream(), rawModuleBase, '\n')) {
            log("readelf failed, returning 0x0 (%s)\n", bashReadelfArgv.get()[2]);
            return Inferior::Address{0x0};
        }
        log("LOAD module base: %s\n", rawModuleBase.c_str());

        // If module base is explicitly specified, Dyninst can correctly determine addresses
        if (auto const module_base = std::stoul(rawModuleBase, nullptr, 16)) {
            log("module base specified, returning 0x0\n");
            return Inferior::Address{0x0};
        }
    }

    // If module base is not specified, have to read the process memory map information to
    // determine where the loader placed the launcher binary
    { auto const launcherBasename = cti::cstr::basename(launcher);
        auto bashMapArgv = cti::ManagedArgv{"bash", "-c",
            "cat /proc/" + std::to_string(pid) + "/maps | grep '/" + launcherBasename + "' | grep 'r-xp' | cut -d '-' -f1"};
        auto bashMapOutput = cti::Execvp{"bash", bashMapArgv.get(), cti::Execvp::stderr::Ignore};

        auto rawModuleBase = std::string{};
        if (!std::getline(bashMapOutput.stream(), rawModuleBase, '\n')) {
            throw std::runtime_error("failed to parse maps: " + std::string{bashMapArgv.get()[2]});
        }

        log("map module base: %p\n", rawModuleBase.c_str());

        return Inferior::Address{std::stoul(rawModuleBase, nullptr, 16)};
    }
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
    , m_proc{Process::createProcess(launcher, launcherArgv, envVars, remapFds)}
    , m_module_base{find_module_base(launcher, m_proc->getPid())}
{
    if (m_followForkMode != FollowFork::DisableBreakpointsDetach) {
        throw std::runtime_error("failed to disable ProcessControl follow-fork mode");
    }

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
    : m_followForkMode{disableGlobalFollowFork()}
    , m_symtab{make_Symtab(launcher), Symtab::closeSymtab}
    , m_symbols{}
    , m_proc(Process::attachProcess(pid, {}))
    , m_module_base{find_module_base(launcher, pid)}
{
    if (m_followForkMode != FollowFork::DisableBreakpointsDetach) {
        throw std::runtime_error("failed to disable ProcessControl follow-fork mode");
    }

    if (!m_proc) {
        throw std::runtime_error("failed to attach to PID " + std::to_string(pid));
    }

    /* prepare breakpoint callback */
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
