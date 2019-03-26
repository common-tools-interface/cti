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

using ::testing::_;
using ::testing::Invoke;
using ::testing::WithoutArgs;

/* MockFrontend implementation */

MockFrontend::MockFrontend()
{
	// describe behavior of mock launchBarrier
	ON_CALL(*this, launchBarrier(_, _, _, _, _, _))
		.WillByDefault(WithoutArgs(Invoke([]() {
			return std::make_unique<MockApp::Nice>(getpid());
		})));
}

/* MockApp implementation */

static size_t appCount = 0;

MockApp::MockApp(pid_t launcherPid)
	: m_launcherPid{launcherPid}
	, m_jobId{std::to_string(m_launcherPid) + std::to_string(appCount++)}
	, m_atBarrier{true}
{}

void
MockApp::releaseBarrier()
{
	if (!m_atBarrier) {
		throw std::runtime_error("app not at startup barrier");
	}
	m_atBarrier = false;
}
