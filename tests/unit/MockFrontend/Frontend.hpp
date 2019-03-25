/******************************************************************************\
 * MockFrontend.hpp - A mock frontend implementation
 *
 * Copyright 2017 Cray Inc.	All Rights Reserved.
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

#include "gmock/gmock.h"

#include "frontend/Frontend.hpp"

class MockFrontend : public Frontend
{
public: // inherited interface
	MOCK_CONST_METHOD0(getWLMType, cti_wlm_type(void));

	MOCK_METHOD6(launchBarrier, std::unique_ptr<App>(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
		CStr inputFile, CStr chdirPath, CArgArray env_list));

	std::unique_ptr<App> registerJob(size_t numIds, ...) override { return nullptr; }

	MOCK_CONST_METHOD0(getHostname, std::string(void));
};

/* Types used here */

class MockApp : public App
{
private: // variables
	pid_t      m_launcherPid; // job launcher PID
	std::string const m_jobId; // unique job identifier
	bool       m_atBarrier; // Are we at MPIR barrier?

public: // constructor / destructor interface
	// register case
	MockApp(pid_t launcherPid);
	~MockApp() = default;

public: // app interaction interface
	std::string getJobId()            const override;
	std::string getLauncherHostname() const override;
	std::string getToolPath()         const override;
	std::string getAttribsPath()      const override;

	std::vector<std::string> getExtraFiles() const override;

	size_t getNumPEs()       const override;
	size_t getNumHosts()     const override;
	std::vector<std::string> getHostnameList()   const override;
	std::vector<CTIHost>     getHostsPlacement() const override;

	void releaseBarrier() override;
	void kill(int signal) override;
	void shipPackage(std::string const& tarPath) const override;
	void startDaemon(const char* const args[]) override;
};
