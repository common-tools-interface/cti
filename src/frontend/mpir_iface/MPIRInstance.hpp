/******************************************************************************\
 * MPIRInstance.hpp
 *
 * Copyright 2018-2019 Cray Inc. All Rights Reserved.
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

	/* memory access */

	// read c-string pointed to by symbol
	std::string readStringAt(std::string const& symName);
};
