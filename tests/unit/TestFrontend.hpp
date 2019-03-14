/******************************************************************************\
 * TestFrontend.hpp - A mock frontend implementation
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


#include "frontend/Frontend.hpp"

class TestFrontend : public Frontend
{
public: // inherited interface
	cti_wlm_type getWLMType() const override;

	std::unique_ptr<App> launchBarrier(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
		CStr inputFile, CStr chdirPath, CArgArray env_list) override;

	std::unique_ptr<App> registerJob(size_t numIds, ...) override;

	std::string getHostname() const override;
};

/* Types used here */

class TestApp : public App
{
private: // variables
	pid_t      m_launcherPid; // job launcher PID
	bool       m_atBarrier; // Are we at MPIR barrier?

public: // constructor / destructor interface
	// register case
	TestApp(pid_t launcherPid);

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
