/******************************************************************************\
 * MockFrontend.cpp - A mock frontend implementation
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

#include "Frontend.hpp"

cti_wlm_type
MockFrontend::getWLMType() const
{
	return CTI_WLM_MOCK;
}

std::unique_ptr<App>
MockFrontend::launchBarrier(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
	CStr inputFile, CStr chdirPath, CArgArray env_list)
{
	return std::make_unique<MockApp>(getpid());
}

std::unique_ptr<App>
MockFrontend::registerJob(size_t numIds, ...)
{
	if (numIds != 1) {
		throw std::logic_error("expecting single pid argument to register app");
	}

	va_list idArgs;
	va_start(idArgs, numIds);

	pid_t launcherPid = va_arg(idArgs, pid_t);

	va_end(idArgs);

	return std::make_unique<MockApp>(launcherPid);
}

std::string
MockFrontend::getHostname() const
{
	return "hostname";
}

/* MockApp implementation */

MockApp::MockApp(pid_t launcherPid)
	: m_launcherPid{launcherPid}
	, m_atBarrier{true}
{}

std::string
MockApp::getJobId() const
{
	return std::to_string(m_launcherPid);
}

std::string
MockApp::getLauncherHostname() const
{
	return "hostname";
}

std::string
MockApp::getToolPath() const
{
	return "toolpath";
}

std::string
MockApp::getAttribsPath() const
{
	return "attrpath";
}

std::vector<std::string>
MockApp::getExtraFiles() const
{
	return {};
}

size_t
MockApp::getNumPEs() const
{
	return 1;
}

size_t
MockApp::getNumHosts() const
{
	return 1;
}

std::vector<std::string>
MockApp::getHostnameList() const
{
	return {};
}

std::vector<CTIHost>
MockApp::getHostsPlacement() const
{
	return {};
}

void
MockApp::releaseBarrier()
{
	if (!m_atBarrier) {
		throw std::runtime_error("app not at startup barrier");
	}
	m_atBarrier = false;
}

void
MockApp::kill(int signal)
{
	return;
}

void MockApp::shipPackage(std::string const& tarPath) const
{
	return;
}

void MockApp::startDaemon(const char* const args[])
{
	return;
}
