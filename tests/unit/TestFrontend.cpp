/******************************************************************************\
 * TestFrontend.cpp - A mock frontend implementation
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

// getpid
#include <sys/types.h>
#include <unistd.h>

#include "TestFrontend.hpp"

cti_wlm_type
TestFrontend::getWLMType() const
{
	return CTI_WLM_NONE;
}

std::unique_ptr<App>
TestFrontend::launchBarrier(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
	CStr inputFile, CStr chdirPath, CArgArray env_list)
{
	return std::make_unique<TestApp>(getpid());
}

std::unique_ptr<App>
TestFrontend::registerJob(size_t numIds, ...)
{
	if (numIds != 1) {
		throw std::logic_error("expecting single pid argument to register app");
	}

	va_list idArgs;
	va_start(idArgs, numIds);

	pid_t launcherPid = va_arg(idArgs, pid_t);

	va_end(idArgs);

	return std::make_unique<TestApp>(launcherPid);
}

std::string
TestFrontend::getHostname() const
{
	return "hostname";
}

/* TestApp implementation */

TestApp::TestApp(pid_t launcherPid)
	: m_launcherPid{launcherPid}
	, m_atBarrier{true}
{}

std::string
TestApp::getJobId() const
{
	return std::to_string(m_launcherPid);
}

std::string
TestApp::getLauncherHostname() const
{
	return "hostname";
}

std::string
TestApp::getToolPath() const
{
	return "toolpath";
}

std::string
TestApp::getAttribsPath() const
{
	return "attrpath";
}

std::vector<std::string>
TestApp::getExtraFiles() const
{
	return {};
}

size_t
TestApp::getNumPEs() const
{
	return 1;
}

size_t
TestApp::getNumHosts() const
{
	return 1;
}

std::vector<std::string>
TestApp::getHostnameList() const
{
	return {};
}

std::vector<CTIHost>
TestApp::getHostsPlacement() const
{
	return {};
}

void
TestApp::releaseBarrier()
{
	m_atBarrier = false;
}

void
TestApp::kill(int signal)
{
	return;
}

void TestApp::shipPackage(std::string const& tarPath) const
{
	return;
}

void TestApp::startDaemon(const char* const args[])
{
	return;
}
