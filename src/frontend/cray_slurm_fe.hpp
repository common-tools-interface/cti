/******************************************************************************\
 * cray_slurm_fe.h - A header file for the Cray slurm specific frontend 
 *                   interface.
 *
 * Copyright 2014 Cray Inc.	All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
 *
 * $HeadURL$
 * $Date$
 * $Rev$
 * $Author$
 *
 ******************************************************************************/

#pragma once

#include <stdint.h>
#include <sys/types.h>

#include <stdexcept>

#include "frontend/Frontend.hpp"

struct SrunInfo : public cti_srunProc_t {
	SrunInfo(uint32_t jobid, uint32_t stepid)
		: cti_srunProc_t{jobid, stepid}
	{
		// sanity check - Note that 0 is a valid step id.
		if (jobid == 0) {
			throw std::runtime_error("Invalid jobid " + std::to_string(jobid));
		}
	}
};

class CraySLURMApp : public App {

public: // inherited interface
	std::string const getJobId() const;
	void releaseBarrier();
	void kill(int signal);
	std::vector<std::string> const getExtraFiles() const;
	void shipPackage(std::string const& tarPath) const;
	void startDaemon(CArgArray argv) const;
	size_t getNumPEs() const;
	size_t getNumHosts() const;
	std::vector<std::string> const getHostnameList() const;
	std::vector<CTIHost> const getHostsPlacement() const;
	std::string const getLauncherHostname() const;
	std::string const getToolPath() const;
	std::string const getAttribsPath() const;

public: // slurm interface
	~CraySLURMApp();
	SrunInfo getSrunInfo() const; // get srun info from existing CTI app
};


class CraySLURMFrontend : public Frontend {

public: // inherited interface
	cti_wlm_type getWLMType() const;
	AppUPtr launch(CArgArray launcher_argv, int stdout_fd, int stderr,
	             CStr inputFile, CStr chdirPath, CArgArray env_list);
	AppUPtr launchBarrier(CArgArray launcher_argv, int stdout_fd, int stderr,
	                    CStr inputFile, CStr chdirPath, CArgArray env_list);
	std::string const getHostname(void) const;

public: // slurm interface
	AppUPtr registerJobStep(uint32_t jobid, uint32_t stepid);
	SrunInfo getSrunInfo(pid_t srunPid);      // attach and read srun info
};
