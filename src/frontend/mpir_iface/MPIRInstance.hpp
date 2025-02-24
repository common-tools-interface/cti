/******************************************************************************\
 * MPIRInstance.hpp
 *
 * Copyright 2018-2020 Hewlett Packard Enterprise Development LP.
 * SPDX-License-Identifier: Linux-OpenIB
 ******************************************************************************/
#pragma once

#include <string>

#include "Inferior.hpp"
#include "MPIRProctable.hpp"

/* instance: implements mpir standard */

class MPIRInstance {
private: // types
    using Address = Inferior::Address;

    /* MPIR standard data structures (present in Inferior's memory) */
    struct MPIR_ProcDescElem {
        Address host_name;
        Address executable_name;
        pid_t pid;
    };

    enum MPIRDebugState : int {
        Unknown = -1,
        Null = 0,
        DebugSpawned = 1,
        DebugAborting = 2,
    };

private: // variables
    Inferior m_inferior;

private: // helpers
    void setupMPIRStandard();

public: // interface

    /* constructors */
    MPIRInstance(std::string const& launcher, std::vector<std::string> const& launcherArgv,
        std::vector<std::string> envVars = {}, std::map<int, int> remapFds = {});
    MPIRInstance(std::string const& attacher, pid_t pid);
    ~MPIRInstance() = default;

    /* MPIR standard functions */
    void runToMPIRBreakpoint();
    MPIRProctable getProctable();

    /* inferior access functions */
    pid_t getLauncherPid() { return m_inferior.getPid(); }
    void terminate() { m_inferior.terminate(); }
    int waitExit();

    /* memory access */

    // read bytes starting at symbol
    void readAt(std::string const& symName, char* buf, size_t len);

    // read c-string at address
    std::string readStringAt(Address strAddress);

    // read c-string pointed to by symbol
    std::string readStringAt(std::string const& symName);

    // read c-string starting at symbol
    std::string readCharArrayAt(std::string const& symName);
};
