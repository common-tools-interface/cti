/******************************************************************************\
 * Inferior.hpp
 *
 * Copyright 2018-2020 Hewlett Packard Enterprise Development LP.
 * SPDX-License-Identifier: Linux-OpenIB
 ******************************************************************************/
#pragma once

#include <vector>
#include <string>
#include <map>
#include <memory>
#include <type_traits>

#include <signal.h>

// dyninst symtab
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wclass-memaccess"
#include <Symtab.h>
#pragma GCC diagnostic pop
// dyninst processcontrol
#include <PCProcess.h>
#include <Event.h>
#include <PlatFeatures.h>

/* inferior: manages dyninst process info, symbols, breakpoints */

class Inferior {

public: // types
    using Symbol = Dyninst::SymtabAPI::Symbol;
    using Address = Dyninst::Address;

private: // types
    using Process    = Dyninst::ProcControlAPI::Process;
    using Breakpoint = Dyninst::ProcControlAPI::Breakpoint;
    using Symtab     = Dyninst::SymtabAPI::Symtab;
    using FollowFork = Dyninst::ProcControlAPI::FollowFork;

    using SymbolMap = std::map<std::string, Symbol*>;

private: // variables
    /* dyninst symbol / proc members */
    FollowFork::follow_t m_followForkMode;
    std::unique_ptr<Symtab, decltype(&Symtab::closeSymtab)> m_symtab;
    SymbolMap m_symbols;
    Process::ptr m_proc;
    Address m_module_base;

public: // interface

    /* process interaction */
    pid_t getPid();
    void continueRun();
    bool isTerminated() { return !m_proc || m_proc->isTerminated(); }
    void terminate();

    void writeFromBuf(std::string const& destName, const char* buf, size_t len);
    void writeFromBuf(Address destAddr,            const char* buf, size_t len);

    void readToBuf(char* buf, std::string const& sourceName, size_t len);
    void readToBuf(char* buf, Address sourceAddr,            size_t len);

    /* templated over char buf source / dest functions */
    template <typename T>
    void writeMemory(Address sourceAddr, T const& data) {
        static_assert(std::is_trivially_copyable<T>::value);
        writeFromBuf(sourceAddr, reinterpret_cast<const char*>(&data), sizeof(T));
    }
    template <typename T>
    void writeVariable(std::string const& destName, T const& data) {
        static_assert(std::is_trivially_copyable<T>::value);
        writeFromBuf(destName, reinterpret_cast<const char*>(&data), sizeof(T));
    }
    template <typename T>
    T readMemory(Address sourceAddr) {
        static_assert(std::is_trivially_copyable<T>::value);
        T result;
        readToBuf(reinterpret_cast<char*>(&result), sourceAddr, sizeof(T));
        return result;
    }
    template <typename T>
    T readVariable(std::string const& sourceName) {
        static_assert(std::is_trivially_copyable<T>::value);
        T result;
        readToBuf(reinterpret_cast<char*>(&result), sourceName, sizeof(T));
        return result;
    }

    /* breakpoint management */
    void setBreakpoint(std::string const& fnName);

    /* symbol management */
    void addSymbol(std::string const& symName);
    Address getAddress(std::string const& symName);

    /* create a new process with arguments */
    Inferior(std::string const& launcher, std::vector<std::string> const& launcherArgv,
        std::vector<std::string> const& envVars = {}, std::map<int, int> const& remapFds = {});
    Inferior(char const* launcher, char const* const launcherArgv[],
        std::vector<std::string> const& envVars = {}, std::map<int, int> const& remapFds = {});

    /* attach to existing process */
    Inferior(std::string const& launcher, pid_t pid);
    ~Inferior();
};
