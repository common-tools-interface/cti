#pragma once

// std library
#include <vector>
#include <string>
#include <map>
#include <memory>

// dyninst symtab
#include <Symtab.h>
// dyninst processcontrol
#include <PCProcess.h>
#include <Event.h>

#define DEBUG(str, x) do { str << x; } while (0)
// #define DEBUG(str, x)

/* RAII for signal blocking */
#include <signal.h>
class SignalGuard {
	const int IGNORED_SIGNALS[13] {
		64, 63, 39, 33, 32, SIGUSR1, SIGUSR2, SIGCONT, SIGTSTP,
		SIGCHLD, SIGPROF, SIGALRM, SIGVTALRM
	};

public:
	SignalGuard() {
		struct sigaction ignore_action { SIG_IGN };

		for (auto sig : IGNORED_SIGNALS) {
			if (sigaction(sig, &ignore_action, NULL) == -1) {
				DEBUG(std::cerr, "failed to block signal " << sig << std::endl);
			}
		}
	}

	~SignalGuard() {
		struct sigaction default_action { SIG_DFL };

		for (auto sig : IGNORED_SIGNALS) {
			if (sigaction(sig, &default_action, NULL) == -1) {
				DEBUG(std::cerr, "failed to unblock signal " << sig << std::endl);
			}
		}
	}
};

/* inferior: manages dyninst process info, symbols, breakpoints */

template <typename T>
using UniquePtrDestr = std::unique_ptr<T, std::function<void(T*)>>;

class Inferior {

public: // types
	using Symbol = Dyninst::SymtabAPI::Symbol;
	using Address = Dyninst::Address;

private: // types
	using Process    = Dyninst::ProcControlAPI::Process;
	using Breakpoint = Dyninst::ProcControlAPI::Breakpoint;
	using Symtab     = Dyninst::SymtabAPI::Symtab;

	using SymbolMap = std::map<std::string, Symbol*>;

private: // variables
	/* block signals during MPIR control of process */
	SignalGuard signalGuard;

	/* dyninst symbol / proc members */
	UniquePtrDestr<Symtab> symtab;
	SymbolMap symbols;
	Process::ptr proc;

	void writeFromBuf(std::string const& destName, const char* buf, size_t len);
	void writeFromBuf(Address destAddr,            const char* buf, size_t len);

	void readToBuf(char* buf, std::string const& sourceName, size_t len);
	void readToBuf(char* buf, Address sourceAddr,            size_t len);

public: // interface

	/* process interaction */
	pid_t getPid();
	void continueRun();

	/* templated over char buf source / dest functions */
	template <typename T>
	void writeMemory(Address sourceAddr, T const& data) {
		writeFromBuf(sourceAddr, reinterpret_cast<const char*>(&data), sizeof(T));
	}
	template <typename T>
	void writeVariable(std::string const& destName, T const& data) {
		writeFromBuf(destName, reinterpret_cast<const char*>(&data), sizeof(T));
	}
	template <typename T>
	T readMemory(Address sourceAddr) {
		T result;
		readToBuf(reinterpret_cast<char*>(&result), sourceAddr, sizeof(T));
		return result;
	}
	template <typename T>
	T readVariable(std::string const& sourceName) {
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
	/* attach to existing process */
	Inferior(std::string const& launcher, pid_t pid);
	~Inferior();
};
