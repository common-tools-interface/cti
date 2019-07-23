/******************************************************************************\
 * Frontend.hpp - A header file for the SSH based workload manager
 *
 * Copyright 2017-2019 Cray Inc.	All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
 *
 ******************************************************************************/

#pragma once

#include <vector>

#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>

#include "frontend/Frontend.hpp"
#include "mpir_iface/MPIRInstance.hpp"

class GenericSSHFrontend final : public Frontend
{
private: // Global state
	struct passwd		m_pwd;
	std::vector<char> 	m_pwd_buf;

public: // inherited interface
	static char const* getName()        { return "generic"; }
	static char const* getDescription() { return "MPIR/SSH based workload manager"; }
	static bool isSupported();

	cti_wlm_type_t getWLMType() const override { return CTI_WLM_SSH; }

	std::weak_ptr<App> launchBarrier(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
		CStr inputFile, CStr chdirPath, CArgArray env_list) override;

	std::weak_ptr<App> registerJob(size_t numIds, ...) override;

	std::string getHostname() const override;

public: // ssh specific types
	struct NodeLayout {
		std::string	hostname;
		std::vector<pid_t> pids; // Pids of the PEs running on this node
		size_t firstPE; // first PE number on this node
	};

	struct StepLayout {
		size_t numPEs; // number of PEs associated with job step
		std::vector<NodeLayout> nodes; // array of hosts
	};

public: // ssh specific interface
	// Get the default launcher binary name, or, if provided, from the environment.
	static std::string getLauncherName();

	// use MPIR proctable to retrieve node / host information about a job
	StepLayout fetchStepLayout(MPIRProctable const& procTable);

	// Use a SSH Step Layout to create the SSH Node Layout file inside the staging directory, return the new path.
	std::string createNodeLayoutFile(StepLayout const& stepLayout, std::string const& stagePath);

	// Use an MPIR ProcTable to create the SSH PID List file inside the staging directory, return the new path.
	std::string createPIDListFile(MPIRProctable const& procTable, std::string const& stagePath);

	// Launch an app under MPIR control and hold at barrier.
	std::unique_ptr<MPIRInstance> launchApp(const char * const launcher_argv[],
		int stdout_fd, int stderr_fd, const char *inputFile, const char *chdirPath, const char * const env_list[]);

public: // constructor / destructor interface
	GenericSSHFrontend();
	~GenericSSHFrontend();
    GenericSSHFrontend(const GenericSSHFrontend&) = delete;
    GenericSSHFrontend& operator=(const GenericSSHFrontend&) = delete;
    GenericSSHFrontend(GenericSSHFrontend&&) = delete;
    GenericSSHFrontend& operator=(GenericSSHFrontend&&) = delete;
};


/* Types used here */

class GenericSSHApp final : public App
{
private: // variables
	pid_t      m_launcherPid; // job launcher PID
	std::unique_ptr<MPIRInstance> m_launcherInstance; // MPIR instance handle to release startup barrier
	GenericSSHFrontend::StepLayout m_stepLayout; // SSH Layout of job step
	bool       m_beDaemonSent; // Have we already shipped over the backend daemon?

	std::string m_toolPath;    // Backend path where files are unpacked
	std::string m_attribsPath; // Backend Cray-specific directory
	std::string m_stagePath;   // Local directory where files are staged before transfer to BE
	std::vector<std::string> m_extraFiles; // List of extra support files to transfer to BE

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

public: // ssh specific interface
	/* none */

private: // delegated constructor
	GenericSSHApp(GenericSSHFrontend& fe, pid_t launcherPid, std::unique_ptr<MPIRInstance>&& launcherInstance);
public: // constructor / destructor interface
	// register case
	GenericSSHApp(GenericSSHFrontend& fe, pid_t launcherPid);
	// attach case
	GenericSSHApp(GenericSSHFrontend& fe, std::unique_ptr<MPIRInstance>&& launcherInstance);
	// launch case
	GenericSSHApp(GenericSSHFrontend& fe, const char * const launcher_argv[], int stdout_fd, int stderr_fd,
		const char *inputFile, const char *chdirPath, const char * const env_list[]);
	~GenericSSHApp();
	GenericSSHApp(const GenericSSHApp&) = delete;
    GenericSSHApp& operator=(const GenericSSHApp&) = delete;
    GenericSSHApp(GenericSSHApp&&) = delete;
    GenericSSHApp& operator=(GenericSSHApp&&) = delete;
};
