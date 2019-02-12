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

#include "useful/handle.hpp"

/* Types used here */

#include "slurm_util/slurm_util.h"
namespace slurm_util {
	struct NodeLayout {
		std::string hostname;
		size_t numPEs; // number of PEs running on node
		size_t firstPE; // first PE number on this node
	};

	struct StepLayout {
		size_t numPEs; // number of PEs associated with job step
		std::vector<NodeLayout> nodes; // array of hosts

		// fetch job step layout information from slurm_util helper
		StepLayout(uint32_t jobid, uint32_t stepid);
	};
}

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


struct CraySLURMApp : public App {
private: // variables
	SrunInfo               srunInfo;    // Job and Step IDs
	slurm_util::StepLayout stepLayout;  // SLURM Layout of job step
	handle::MPIR           barrier;     // MPIR handle to release startup barrier
	bool                   dlaunchSent; // Have we already shipped over the dlaunch utility?

	std::string toolPath;    // Backend path where files are unpacked
	std::string attribsPath; // Backend Cray-specific directory
	std::string stagePath;   // Local directory where files are staged before transfer to BE
	std::vector<std::string> extraFiles; // List of extra support files to transfer to BE

protected: // delegated constructor
	CraySLURMApp(uint32_t jobid, uint32_t stepid, mpir_id_t mpir_id);

public: // constructor / destructor interface
	// register case
	CraySLURMApp(uint32_t jobid, uint32_t stepid);
	// attach case
	CraySLURMApp(mpir_id_t mpir_id);
	// launch case
	CraySLURMApp(const char * const launcher_argv[], int stdout_fd, int stderr_fd,
		const char *inputFile, const char *chdirPath, const char * const env_list[]);

	CraySLURMApp(CraySLURMApp&& moved);
	~CraySLURMApp();

public: // app interaction interface
	std::string getJobId()            const;
	std::string getLauncherHostname() const;
	std::string getToolPath()         const { return toolPath;    }
	std::string getAttribsPath()      const { return attribsPath; }

	std::vector<std::string> getExtraFiles() const { return extraFiles; }

	size_t getNumPEs()       const { return stepLayout.numPEs;       }
	size_t getNumHosts()     const { return stepLayout.nodes.size(); }
	std::vector<std::string> getHostnameList() const;
	std::vector<CTIHost>     getHostsPlacement() const;

	void releaseBarrier();
	void kill(int signal);
	void shipPackage(std::string const& tarPath) const;
	void startDaemon(const char* const args[]);

public: // slurm specific interface
	uint64_t getApid() const { return CRAY_SLURM_APID(srunInfo.jobid, srunInfo.stepid); }
	SrunInfo getSrunInfo() const { return srunInfo; }
};


class CraySLURMFrontend : public Frontend {

public: // inherited interface
	cti_wlm_type getWLMType() const { return CTI_WLM_CRAY_SLURM; }

	AppId
	launchBarrier(CArgArray launcher_argv, int stdout_fd, int stderr,
	              CStr inputFile, CStr chdirPath, CArgArray env_list);

	std::string getHostname() const;

public: // slurm specific interface
	AppId registerJobStep(uint32_t jobid, uint32_t stepid);
	SrunInfo getSrunInfo(pid_t srunPid);      // attach and read srun info
};
