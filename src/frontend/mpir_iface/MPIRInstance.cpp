/******************************************************************************\
 * MPIRInstance.cpp
 *
 * Copyright 2018-2019 Cray Inc.  All Rights Reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
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

#include <sstream>
// POSIX extensions enabled by autoconf
#include <limits.h>

#include "MPIRInstance.hpp"

using Symbol  = Inferior::Symbol;

/* create new process instance */
MPIRInstance::MPIRInstance(std::string const& launcher,
	std::vector<std::string> const& launcherArgv,
	std::vector<std::string> envVars, std::map<int, int> remapFds) :
	m_inferior{launcher, launcherArgv, envVars, remapFds} {

	/* read symbols, set breakpoints, etc. */
	setupMPIRStandard();
	/* wait until MPIR data is ready and object can handle its data */
	runToMPIRBreakpoint();
}

/* attach to process given pid */
MPIRInstance::MPIRInstance(std::string const& launcher, pid_t pid) :
	m_inferior{launcher, pid} {

	setupMPIRStandard();
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
	DEBUG(std::cerr, "running inferior til MPIR_Breakpoint" << std::endl);
	MPIRDebugState debugState = MPIRDebugState::Unknown;

	do {
		DEBUG(std::cerr, "MPIR_debug_state: " << debugState << std::endl);
		DEBUG(std::cerr, "MPIR_being_debugged: " << m_inferior.readVariable<int>("MPIR_being_debugged") << std::endl);
		m_inferior.continueRun();

		if (m_inferior.isTerminated()) {
			throw std::runtime_error("MPIR target terminated before MPIR_Breakpoint");
		}

		/* inferior now in stopped state. read MPIR_debug_state */
		debugState = m_inferior.readVariable<MPIRDebugState>("MPIR_debug_state");
	} while (debugState != MPIRDebugState::DebugSpawned);
}

template <typename T>
static T readArrayElem(Inferior& inf, std::string const& symName, size_t idx) {
	Inferior::Address elem_addr;
	{ auto array_start = inf.readVariable<Inferior::Address>(symName);
		elem_addr = array_start + idx * sizeof(T);
	}
	return inf.readMemory<T>(elem_addr);
}

MPIRProctable MPIRInstance::getProctable() {
	auto num_pids = m_inferior.readVariable<int>("MPIR_proctable_size");
	DEBUG(std::cerr, "procTable has size " << std::to_string(num_pids) << std::endl);

	MPIRProctable proctable;

	/* copy elements */
	for (int i = 0; i < num_pids; i++) {
		auto procDesc = readArrayElem<MPIR_ProcDescElem>(m_inferior, "MPIR_proctable", i);

		/* read hostname */
		auto buf = m_inferior.readMemory<std::array<char, HOST_NAME_MAX+1>>(procDesc.host_name);
		buf[HOST_NAME_MAX] = '\0';

		/* copy hostname */
		{ std::stringstream ss;
			ss << buf.data();
			DEBUG(std::cerr, "procTable[" << i << "]: " << procDesc.pid << ", " << ss.str() << std::endl);
			proctable.emplace_back(MPIRProctableElem{procDesc.pid, ss.str()});
		}
	}

	return proctable;
}

std::string MPIRInstance::readStringAt(std::string const& symName) {
	/* get address */
	auto strAddress = m_inferior.readVariable<Address>(symName);

	/* read string */
	std::string result;
	while (char c = m_inferior.readMemory<char>(strAddress++)) {
		result.push_back(c);
	}

	return result;
}
