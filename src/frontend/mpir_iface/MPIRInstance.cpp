/******************************************************************************\
 * MPIRInstance.cpp
 *
 * Copyright 2018-2020 Hewlett Packard Enterprise Development LP.
 * SPDX-License-Identifier: Linux-OpenIB
 ******************************************************************************/

// This pulls in config.h
#include "cti_defs.h"

#include <sstream>
// POSIX extensions enabled by autoconf
#include <limits.h>

#include "MPIRInstance.hpp"

#include "useful/cti_wrappers.hpp"

using Symbol  = Inferior::Symbol;

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

/* create new process instance */
MPIRInstance::MPIRInstance(std::string const& launcher,
    std::vector<std::string> const& launcherArgv,
    std::vector<std::string> envVars, std::map<int, int> remapFds) :
    m_inferior{launcher, launcherArgv, envVars, remapFds} {

    /* read symbols, set breakpoints, etc. */
    setupMPIRStandard();
}

/* attach to process given pid */
MPIRInstance::MPIRInstance(std::string const& launcher, pid_t pid) :
    m_inferior{launcher, pid} {

    setupMPIRStandard();

    /* wait until proctable has been filled */
    while (m_inferior.readVariable<int>("MPIR_proctable_size") == 0) {
        m_inferior.continueRun();

        /* ensure execution wasn't stopped due to termination */
        if (m_inferior.isTerminated()) {
            throw std::runtime_error("MPIR attach target terminated before proctable filled");
        }
    }
}

void MPIRInstance::setupMPIRStandard() {
    /* read in required MPIR symbols */
    m_inferior.addSymbol("MPIR_being_debugged");
    m_inferior.addSymbol("MPIR_Breakpoint");
    m_inferior.addSymbol("MPIR_debug_state");
    m_inferior.addSymbol("MPIR_i_am_starter");
    m_inferior.addSymbol("MPIR_proctable");
    m_inferior.addSymbol("MPIR_proctable_size");

    /* set up breakpoints */
    m_inferior.setBreakpoint("MPIR_Breakpoint");

    /* set MPIR_being_debugged = 1 */
    m_inferior.writeVariable("MPIR_being_debugged", 1);
}

/* instance implementations */

void MPIRInstance::runToMPIRBreakpoint() {
    log("running inferior til MPIR_Breakpoint\n");

    while (true) {
        m_inferior.continueRun();

        if (m_inferior.isTerminated()) {
            throw std::runtime_error("MPIR launch target terminated before MPIR_Breakpoint");
        }

        /* inferior now in stopped state. read MPIR_debug_state */
        auto const debugState = m_inferior.readVariable<MPIRDebugState>("MPIR_debug_state");
        auto const proctable_size = m_inferior.readVariable<int>("MPIR_proctable_size");

        log("MPIR_debug_state: %d MPIR_proctable_size: %d\n", debugState, proctable_size);

        if ((debugState == MPIRDebugState::DebugSpawned) && (proctable_size > 0)) {
            break;
        }

        // Allow launcher to run for a bit before checking conditions
        ::sleep(1);
    };

    log("MPIR_debug_state: exited loop\n");
}

int MPIRInstance::waitExit() {
    log("running inferior til exit\n");

    while (true) {
        m_inferior.continueRun();

        // Exited normally
        if (m_inferior.isExited()) {
            return m_inferior.getExitCode();

        // Exited due to signal
        } else if (m_inferior.isCrashed()) {
            log("inferior terminated due to crash\n");
            return -1;

        // According to ProcControlAPI documentation, if process didn't exit
        // normally or due to crash, it will be considered "terminated",
        // but actually be detached, not necessarily exited.
        // Consider it successfully exited in this case.
        } else if (m_inferior.isTerminated()) {
            log("inferior considered terminated due to detach\n");
            return 0;
        }
    }
}

template <typename T>
static T readArrayElem(Inferior& inf, std::string const& symName, size_t idx) {
    Inferior::Address elem_addr;
    { auto array_start = inf.readVariable<Inferior::Address>(symName);
        elem_addr = array_start + idx * sizeof(T);
    }
    return inf.readMemory<T>(elem_addr);
}

void MPIRInstance::readAt(std::string const& symName, char* buf, size_t len)
{
    m_inferior.readToBuf(buf, symName, len);
}

std::string MPIRInstance::readStringAt(MPIRInstance::Address strAddress) {
    /* read string */
    std::string result;
    while (char c = m_inferior.readMemory<char>(strAddress++)) {
        result.push_back(c);
    }

    return result;
}

std::string MPIRInstance::readStringAt(std::string const& symName) {
    /* get address */
    auto strAddress = m_inferior.readVariable<Address>(symName);

    /* delegate read string */
    return readStringAt(strAddress);
}

std::string MPIRInstance::readCharArrayAt(std::string const& symName)
{
    auto result = std::string{};

    auto arrayAddress = m_inferior.getAddress(symName);
    while (char c = m_inferior.readMemory<char>(arrayAddress++)) {
        result.push_back(c);
    }

    return result;
}

MPIRProctable MPIRInstance::getProctable() {
    auto num_pids = m_inferior.readVariable<int>("MPIR_proctable_size");
    log("procTable has size %d\n", num_pids);

    if (num_pids == 0) {
        throw std::runtime_error("launcher MPIR_proctable_size is 0");
    }

    MPIRProctable proctable;

    /* copy elements */
    for (int i = 0; i < num_pids; i++) {
        auto procDesc = readArrayElem<MPIR_ProcDescElem>(m_inferior, "MPIR_proctable", i);

        /* read hostname and executable */
        auto hostname = readStringAt(procDesc.host_name);
        auto executable = readStringAt(procDesc.executable_name);

        log("procTable[%d]: %d, %s, %s\n", i, procDesc.pid, hostname.c_str(), executable.c_str());

        proctable.emplace_back(MPIRProctableElem{procDesc.pid, std::move(hostname), std::move(executable)});
    }

    return proctable;
}
