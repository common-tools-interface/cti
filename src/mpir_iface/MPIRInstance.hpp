#pragma once

#include <string>

#include "MPIRInferior.hpp"

/* instance: implements mpir standard */

class MPIRInstance {
private: // variables
	MPIRInferior inferior;

public: // interface

	/* constructors */
	MPIRInstance(std::string const& launcher, std::vector<std::string> const& launcherArgv,
		std::vector<std::string> envVars = {}, std::map<int, int> remapFds = {});
	MPIRInstance(std::string const& attacher, pid_t pid);

	/* MPIR standard data structures */
	typedef struct {
		MPIRInferior::Address host_name;
		MPIRInferior::Address executable_name;
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

	pid_t getLauncherPid() { return inferior.getPid(); }

	typedef struct {
		pid_t pid;
		std::string hostname;
	} MPIR_ProcTableElem;
	std::vector<MPIR_ProcTableElem> getProcTable();

	/* memory access */

	/* read c-string pointed to by symbol */
	std::string readStringAt(std::string const& symName);
};
