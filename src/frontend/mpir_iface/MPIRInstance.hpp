/******************************************************************************\
 * MPIRInstance.hpp
 *
 * Copyright 2018 Cray Inc.  All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
 *
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

	/* memory access */

	// read c-string pointed to by symbol
	std::string readStringAt(std::string const& symName);
};
