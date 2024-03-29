/******************************************************************************\
 * Frontend.hpp - A mock frontend implementation
 *
 * Copyright 2019-2020 Hewlett Packard Enterprise Development LP.
 * SPDX-License-Identifier: Linux-OpenIB
 ******************************************************************************/

#pragma once

#include "gmock/gmock.h"

#include "frontend/Frontend.hpp"

class MockFrontend : public Frontend
{
public: // types
    using Nice = ::testing::NiceMock<MockFrontend>;

    enum class LaunchBarrierMode { Disabled, Enabled };

public: // mock constructor
    MockFrontend();
    virtual ~MockFrontend() = default;

public: // inherited interface
    MOCK_CONST_METHOD0(getWLMType, cti_wlm_type_t());

    MOCK_METHOD6(launch, std::weak_ptr<App>(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
        CStr inputFile, CStr chdirPath, CArgArray env_list));

    MOCK_METHOD6(launchBarrier, std::weak_ptr<App>(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
        CStr inputFile, CStr chdirPath, CArgArray env_list));

    MOCK_METHOD0(mock_registerJob, std::weak_ptr<App>(void));

    std::weak_ptr<App> registerJob(size_t numIds, ...) override { return mock_registerJob(); }

    MOCK_CONST_METHOD0(getHostname, std::string(void));
};

/* Types used here */

class MockApp : public App
{
public: // types
    using Nice = ::testing::NiceMock<MockApp>;

private: // variables
    pid_t      m_launcherPid; // job launcher PID
    std::string const m_jobId; // unique job identifier
    bool       m_atBarrier; // Are we at MPIR barrier?
    std::vector<std::string> m_shippedFilePaths;

public: // constructor / destructor interface
    // register case
    MockApp(MockFrontend& fe, pid_t launcherPid, MockFrontend::LaunchBarrierMode const launchBarrierMode);
    virtual ~MockApp();

public: // inherited interface
    std::string getJobId() const { return m_jobId; }
    MOCK_CONST_METHOD0(getLauncherHostname, std::string(void));
    MOCK_CONST_METHOD0(getToolPath,         std::string(void));
    MOCK_CONST_METHOD0(getAttribsPath,      std::string(void));

    MOCK_CONST_METHOD0(getExtraFiles, std::vector<std::string>(void));

    bool isRunning() const { return true; }
    MOCK_CONST_METHOD0(getNumPEs,   size_t(void));
    MOCK_CONST_METHOD0(getNumHosts, size_t(void));
    MOCK_CONST_METHOD0(getHostnameList,   std::vector<std::string>(void));
    MOCK_CONST_METHOD0(getHostsPlacement, std::vector<CTIHost>(void));
    MOCK_CONST_METHOD0(getBinaryRankMap,  std::map<std::string, std::vector<int>>(void));

    MOCK_METHOD0(releaseBarrier, void(void));
    MOCK_METHOD1(kill, void(int));
    MOCK_CONST_METHOD1(shipPackage, void(std::string const&));
    MOCK_METHOD2(startDaemon, void(const char* const [], bool));

public: // interface
    std::vector<std::string> getShippedFilePaths() const { return m_shippedFilePaths; }

};
