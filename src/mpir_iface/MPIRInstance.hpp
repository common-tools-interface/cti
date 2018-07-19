#pragma once

#include <string>

#include "MPIRInferior.hpp"

/* instance: implements mpir standard */

class MPIRInstance {
	using Symbol = Dyninst::SymtabAPI::Symbol;
	using Process = Dyninst::ProcControlAPI::Process;
	using Breakpoint = Dyninst::ProcControlAPI::Breakpoint;

	MPIRInferior inferior;

public:
	using Address = Dyninst::Address;

	/* constructors */
	MPIRInstance(std::string const& launcher, std::vector<std::string> const& launcherArgv,
		std::map<int, int> remapFds = {});
	MPIRInstance(std::string const& attacher, pid_t pid);

	/* MPIR standard data structures */
	typedef struct {
		Address host_name;
		Address executable_name;
		pid_t pid;
	} MPIR_ProcDescElem;

	enum MPIRDebugState {
		Unknown = -1,
		Null = 0,
		DebugSpawned = 1,
		DebugAborting = 2,
	};

	/* MPIR standard functions */
	void setupMPIRStandard();
	void runToMPIRBreakpoint();

	/* inferior access functions */

	pid_t getLauncherPid() { return inferior.proc->getPid(); }

	typedef struct {
		pid_t pid;
		std::string hostname;
	} MPIR_ProcTableElem;
	std::vector<MPIR_ProcTableElem> getProcTable();

	/* memory access */

	template <typename T>
	void readAddress(T* buf, Address address, size_t len) {
		inferior.proc->readMemory(reinterpret_cast<void*>(buf), address, len);
	}

	/* read c-string pointed to by symbol */
	std::string readStringAt(std::string const& symName);

	/* read variable into given buffer */
	template <typename T>
	void readVariable(T* buf, std::string const& symName);

	template <typename T>
	void readArrayElem(T* buf, std::string const& symName, size_t idx);

	template <typename T>
	void writeVariable(std::string const& symName, T const& data, size_t len = sizeof(T));
};
