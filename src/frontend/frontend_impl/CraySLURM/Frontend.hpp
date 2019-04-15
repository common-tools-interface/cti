/******************************************************************************\
 * cray_slurm_fe.h - A header file for the Cray slurm specific frontend
 *                   interface.
 *
 * Copyright 2014-2019 Cray Inc.	All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
 *
 ******************************************************************************/

#pragma once

#include <stdint.h>
#include <sys/types.h>

#include <stdexcept>

#include "frontend/Frontend.hpp"
#include "mpir_iface/MPIRInstance.hpp"

#include "useful/cti_wrappers.hpp"

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

class CraySLURMFrontend final : public Frontend
{
public: // inherited interface
	cti_wlm_type getWLMType() const override { return CTI_WLM_CRAY_SLURM; }

	std::unique_ptr<App> launchBarrier(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
		CStr inputFile, CStr chdirPath, CArgArray env_list) override;

	std::unique_ptr<App> registerJob(size_t numIds, ...) override;

	std::string getHostname() const override;

public: // slurm specific types
	struct NodeLayout {
		std::string hostname;
		size_t numPEs; // number of PEs running on node
		size_t firstPE; // first PE number on this node
	};

	struct StepLayout {
		size_t numPEs; // number of PEs associated with job step
		std::vector<NodeLayout> nodes; // array of hosts
	};

	// objects that are created during an App creation. ownership will pass to the App
	struct SrunInstance {
		std::unique_ptr<MPIRInstance> stoppedSrun; // SRUN inferior for barrier release
		cti::temp_file_handle outputPath; // handle to output fifo file
		cti::temp_file_handle errorPath;  // handle to error fifo file
		overwatch_handle redirectUtility; // running output redirection utility
	};

public: // slurm specific interface
	// Get the default launcher binary name, or, if provided, from the environment.
	static std::string getLauncherName();

	// use sattach to retrieve node / host information about a SLURM job
	/* sattach layout format:
	Job step layout:
	  {numPEs} tasks, {numNodes} nodes ({hostname}...)
	  <newline>
	  Node {nodeNum} ({hostname}), {numPEs} task(s): PE_0 {PE_i }...
	*/
	static StepLayout fetchStepLayout(uint32_t job_id, uint32_t step_id);

	// Use a Slurm Step Layout to create the SLURM Node Layout file inside the staging directory, return the new path.
	static std::string createNodeLayoutFile(StepLayout const& stepLayout, std::string const& stagePath);

	// Use an MPIR ProcTable to create the SLURM PID List file inside the staging directory, return the new path.
	static std::string createPIDListFile(std::vector<MPIRInstance::MPIR_ProcTableElem> const& procTable, std::string const& stagePath);

	// Launch a SLURM app under MPIR control and hold at SRUN barrier.
	static SrunInstance launchApp(const char * const launcher_argv[],
		const char *inputFile, int stdoutFd, int stderrFd, const char *chdirPath,
		const char * const env_list[]);

	// Extract the SLURM Job ID from launcher memory using an existing MPIR control session.
	static uint32_t fetchJobId(MPIRInstance& srunInstance);

	// Extract the SLURM Step ID from launcher memory using an existing MPIR control session. Optional, returns 0 on failure.
	static uint32_t fetchStepId(MPIRInstance& srunInstance);

	// attach and read srun info
	static SrunInfo getSrunInfo(pid_t srunPid);
};

class CraySLURMApp final : public App
{
private: // type aliases
	using SrunInstance = CraySLURMFrontend::SrunInstance;

private: // variables
	SrunInfo               m_srunInfo;    // Job and Step IDs
	CraySLURMFrontend::StepLayout m_stepLayout; // SLURM Layout of job step
	int                    m_queuedOutFd; // Where to redirect stdout after barrier release
	int                    m_queuedErrFd; // Where to redirect stderr after barrier release
	bool                   m_dlaunchSent; // Have we already shipped over the dlaunch utility?
	std::vector<overwatch_handle> m_watchedUtilities; // active utility redirect / sattach / srun instances

	std::unique_ptr<MPIRInstance> m_stoppedSrun; // MPIR instance handle to release startup barrier
	cti::temp_file_handle m_outputPath;
	cti::temp_file_handle m_errorPath;

	std::string m_toolPath;    // Backend path where files are unpacked
	std::string m_attribsPath; // Backend Cray-specific directory
	std::string m_stagePath;   // Local directory where files are staged before transfer to BE
	std::vector<std::string> m_extraFiles; // List of extra support files to transfer to BE

private: // member helpers
	void redirectOutput(int stdoutFd, int stderrFd);
	// delegated constructor
	CraySLURMApp(uint32_t jobid, uint32_t stepid, SrunInstance&& srunInstance);

public: // constructor / destructor interface
	// register case
	CraySLURMApp(uint32_t jobid, uint32_t stepid);
	// attach case
	CraySLURMApp(SrunInstance&& srunInstance);
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
