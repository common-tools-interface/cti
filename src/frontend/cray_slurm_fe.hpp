/******************************************************************************\
 * cray_slurm_fe.h - A header file for the Cray slurm specific frontend 
 *                   interface.
 *
 * Copyright 2019 Cray Inc.	All Rights Reserved.
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
#include "slurm_util/slurm_util.h"
#include "mpir_iface/mpir_iface.h"

// managed MPIR session
class MPIRHandle {
private: // variables
	mpir_id_t m_data;

public: // interface
	operator bool() const {
		return (m_data >= 0);
	}

	void reset()
	{
		if (*this) {
			_cti_mpir_releaseInstance(m_data);
			m_data = mpir_id_t{-1};
		}
	}

	mpir_id_t get() const {
		return m_data;
	}

	MPIRHandle()
		: m_data{-1}
	{}

	MPIRHandle(mpir_id_t m_data_)
		: m_data{m_data_}
	{}

	MPIRHandle(MPIRHandle&& moved)
		: m_data{std::move(moved.m_data)}
	{
		moved.m_data = mpir_id_t{-1};
	}

	~MPIRHandle()
	{
		reset();
	}
};

/* Types used here */

namespace slurm_util {
	struct NodeLayout {
		std::string hostname;
		size_t numPEs; // number of PEs running on node
		size_t firstPE; // first PE number on this node
	};

	struct StepLayout {
		size_t numPEs; // number of PEs associated with job step
		std::vector<NodeLayout> nodes; // array of hosts
	};
}

// cti_srunProc_t extended to performs sanity checking upon construction
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
private: // variables
	SrunInfo               m_srunInfo;    // Job and Step IDs
	slurm_util::StepLayout m_stepLayout;  // SLURM Layout of job step
	MPIRHandle             m_barrier;     // MPIR handle to release startup barrier
	int                    m_queuedOutFd; // Where to redirect stdout after barrier release
	int                    m_queuedErrFd; // Where to redirect stderr after barrier release
	bool                   m_dlaunchSent; // Have we already shipped over the dlaunch utility?
	std::vector<pid_t>     m_sattachPids; // active sattaches for stdout/err redirection

	std::string m_toolPath;    // Backend path where files are unpacked
	std::string m_attribsPath; // Backend Cray-specific directory
	std::string m_stagePath;   // Local directory where files are staged before transfer to BE
	std::vector<std::string> m_extraFiles; // List of extra support files to transfer to BE

private: // helpers
	void redirectOutput(int stdoutFd, int stderrFd);

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
	std::string getJobId()            const override;
	std::string getLauncherHostname() const override;
	std::string getToolPath()         const override { return m_toolPath;    }
	std::string getAttribsPath()      const override { return m_attribsPath; }

	std::vector<std::string> getExtraFiles() const override { return m_extraFiles; }

	size_t getNumPEs()       const override { return m_stepLayout.numPEs;       }
	size_t getNumHosts()     const override { return m_stepLayout.nodes.size(); }
	std::vector<std::string> getHostnameList()   const override;
	std::vector<CTIHost>     getHostsPlacement() const override;

	void releaseBarrier() override;
	void kill(int signal) override;
	void shipPackage(std::string const& tarPath) const override;
	void startDaemon(const char* const args[]) override;

public: // slurm specific interface
	uint64_t getApid() const { return CRAY_SLURM_APID(m_srunInfo.jobid, m_srunInfo.stepid); }
	SrunInfo getSrunInfo() const { return m_srunInfo; }
};


class CraySLURMFrontend : public Frontend {

public: // inherited interface
	cti_wlm_type getWLMType() const override { return CTI_WLM_CRAY_SLURM; }

	std::unique_ptr<App> launchBarrier(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
		CStr inputFile, CStr chdirPath, CArgArray env_list) override;

	std::unique_ptr<App> registerJob(size_t numIds, ...) override;

	std::string getHostname() const override;

public: // slurm specific interface

	SrunInfo getSrunInfo(pid_t srunPid); // attach and read srun info
};
