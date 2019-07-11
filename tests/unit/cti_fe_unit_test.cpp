/******************************************************************************\
 * cti_fe_unit_test.cpp - Frontend unit tests for CTI
 *
 * Copyright 2019 Cray Inc.  All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
 *
 ******************************************************************************/

#include "cti_defs.h"

#include <memory>
#include <unordered_set>

// CTI Transfer includes
#include "frontend/cti_transfer/Manifest.hpp"
#include "frontend/cti_transfer/Session.hpp"

#include "cti_fe_unit_test.hpp"

#include "useful/cti_wrappers.hpp"

using ::testing::Return;
using ::testing::_;
using ::testing::Invoke;
using ::testing::WithoutArgs;

CTIFEUnitTest::CTIFEUnitTest()
{
    // manually set the frontend to the custom mock frontend
    new MockFrontend::Nice{};
}

CTIFEUnitTest::~CTIFEUnitTest()
{
    Frontend::destroy();
}

static constexpr char const* mockArgv[] = {"/usr/bin/true", nullptr};
CTIAppUnitTest::CTIAppUnitTest()
    : CTIFEUnitTest{}
    , appId{cti_launchAppBarrier(mockArgv, -1, -1, nullptr, nullptr, nullptr)}
    , mockApp{std::dynamic_pointer_cast<MockApp::Nice>(Frontend::inst().Iface().getApp(appId))}
{
    if (appId == APP_ERROR) {
        throw std::runtime_error("failed to launch mock app");
    }
}

CTIAppUnitTest::~CTIAppUnitTest()
{
    if (appId != APP_ERROR) {
        cti_deregisterApp(appId);
    }
}

/* current frontend information query tests */

// const char * cti_error_str(void)
// Test the the current error string is not set
TEST_F(CTIFEUnitTest, error_str)
{
    // run the test
    ASSERT_EQ(std::string{cti_error_str()}, DEFAULT_ERR_STR);
}

// const char * cti_version(void);
// Tests that the frontend will return a version string
TEST_F(CTIFEUnitTest, Version)
{
    // run the test
    ASSERT_TRUE(cti_version() != nullptr);
}

// cti_wlm_type_t cti_current_wlm(void);
// Tests that the frontend type is set to mock.
TEST_F(CTIFEUnitTest, CurrentWLM)
{
    MockFrontend& mockFrontend = dynamic_cast<MockFrontend&>(Frontend::inst());

    // describe behavior of mock getWLMType
    EXPECT_CALL(mockFrontend, getWLMType())
        .WillOnce(Return(CTI_WLM_MOCK));

    // run the test
    ASSERT_EQ(cti_current_wlm(), CTI_WLM_MOCK);
}

// const char * cti_wlm_type_toString(cti_wlm_type_t);
// Tests that the frontend type string is nonnull
TEST_F(CTIFEUnitTest, WLMTypeToString)
{
    // run the test
    ASSERT_TRUE(cti_wlm_type_toString(cti_current_wlm()) != nullptr) << cti_error_str();
}

// char *       cti_getHostname(void);
// Tests that the frontend will return a hostname
TEST_F(CTIFEUnitTest, GetHostname)
{
    MockFrontend& mockFrontend = dynamic_cast<MockFrontend&>(Frontend::inst());

    // describe behavior of mock getWLMType
    EXPECT_CALL(mockFrontend, getHostname())
        .WillOnce(Return(std::string{"local-hostname"}));

    // run the test
    auto const rawHostname = cti::move_pointer_ownership(cti_getHostname(), std::free);
    ASSERT_TRUE(rawHostname != nullptr) << cti_error_str();
}

// int          cti_setAttribute(cti_attr_type attrib, const char *value);
// Tests that the frontend can set an attribute
TEST_F(CTIFEUnitTest, SetAttribute)
{
    // run the test
    ASSERT_EQ(cti_setAttribute(CTI_ATTR_STAGE_DEPENDENCIES, "1"), SUCCESS);
}

/* running app information query tests */

// char *               cti_getLauncherHostName(cti_app_id_t);
// Tests that the app will return a hostname
TEST_F(CTIAppUnitTest, GetLauncherHostname)
{
    // describe behavior of mock getLauncherHostname
    EXPECT_CALL(*mockApp, getLauncherHostname())
        .WillOnce(Return(std::string{"remote-hostname"}));

    // run the test
    auto const rawHostname = cti::move_pointer_ownership(cti_getLauncherHostName(appId), std::free);
    ASSERT_TRUE(rawHostname != nullptr) << cti_error_str();
    EXPECT_EQ(std::string{rawHostname.get()}, "remote-hostname");
}

// int                  cti_getNumAppPEs(cti_app_id_t);
// Tests that the app will return a number of app PEs
TEST_F(CTIAppUnitTest, GetNumAppPEs)
{
    // describe behavior of mock getToolPath
    EXPECT_CALL(*mockApp, getNumPEs())
        .WillOnce(Return(getpid()));

    // run the test
    EXPECT_EQ(cti_getNumAppPEs(appId), getpid()) << cti_error_str();
}

// int                  cti_getNumAppNodes(cti_app_id_t);
// Tests that the app will return a number of hosts
TEST_F(CTIAppUnitTest, GetNumAppNodes)
{
    // describe behavior of mock getToolPath
    EXPECT_CALL(*mockApp, getNumHosts())
        .WillOnce(Return(getpid()));

    // run the test
    EXPECT_EQ(cti_getNumAppNodes(appId), getpid()) << cti_error_str();
}

// char **              cti_getAppHostsList(cti_app_id_t);
// Tests that the app will return a list of hostnames
TEST_F(CTIAppUnitTest, GetAppHostsList)
{
    // describe behavior of mock getHostnameList
    EXPECT_CALL(*mockApp, getHostnameList())
        .WillOnce(Return(std::vector<std::string>{"remote-hostname"}));

    // run the test
    auto const rawHostsList = cti::move_pointer_ownership(cti_getAppHostsList(appId), cti::free_ptr_list<char*>);
    ASSERT_TRUE(rawHostsList != nullptr) << cti_error_str();
    EXPECT_EQ(std::string{rawHostsList.get()[0]}, "remote-hostname");
    EXPECT_EQ(rawHostsList.get()[1], nullptr);
}

// cti_hostsList_t *    cti_getAppHostsPlacement(cti_app_id_t);
// void                 cti_destroyHostsList(cti_hostsList_t *);
// Tests that the app will return a list of host placements
TEST_F(CTIAppUnitTest, GetAppHostsPlacement)
{
    // describe behavior of mock getHostsPlacement
    EXPECT_CALL(*mockApp, getHostsPlacement())
        .WillOnce(Return(std::vector<CTIHost>{CTIHost
            { .hostname = "remote-hostname"
            , .numPEs = (size_t)getpid()
        }}));

    // run the test
    auto const rawHostsList = cti::move_pointer_ownership(cti_getAppHostsPlacement(appId), cti_destroyHostsList);
    ASSERT_TRUE(rawHostsList != nullptr) << cti_error_str();
    EXPECT_EQ(rawHostsList->numHosts, 1);
    ASSERT_TRUE(rawHostsList->hosts != nullptr);
    ASSERT_TRUE(rawHostsList->hosts[0].hostname != nullptr);
    EXPECT_EQ(std::string{rawHostsList->hosts[0].hostname}, "remote-hostname");
    EXPECT_EQ(rawHostsList->hosts[0].numPes, getpid());
}

/* app lifecycle management tests */

// int          cti_appIsValid(cti_app_id_t);
// Tests that the interface recognizes a valid app
TEST_F(CTIAppUnitTest, AppIsValid)
{
    // run the test
    EXPECT_EQ(cti_appIsValid(appId), true) << cti_error_str();
}

// cti_app_id_t cti_launchApp(const char * const [], int, int, const char *, const char *, const char * const []);
// void         cti_deregisterApp(cti_app_id_t);
// Tests that the interface will call the Frontend to launch an App
TEST_F(CTIFEUnitTest, LaunchApp)
{
    MockFrontend& mockFrontend = dynamic_cast<MockFrontend&>(Frontend::inst());

    // describe behavior of mock launchBarrier
    EXPECT_CALL(mockFrontend, launchBarrier(mockArgv, -1, -1, nullptr, nullptr, nullptr))
        .Times(1);

    // run the test
    auto const appId = cti_launchApp(mockArgv, -1, -1, nullptr, nullptr, nullptr);
    ASSERT_NE(appId, APP_ERROR) << cti_error_str();
    EXPECT_EQ(cti_releaseAppBarrier(appId), FAILURE) << cti_error_str();

    // clean up app launch
    cti_deregisterApp(appId);
}

// cti_app_id_t cti_launchAppBarrier(const char * const [], int, int, const char *, const char *, const char * const []);
// Tests that the interface will call the Frontend to launch an App at barrier
TEST_F(CTIFEUnitTest, LaunchAppBarrier)
{
    MockFrontend& mockFrontend = dynamic_cast<MockFrontend&>(Frontend::inst());

    // describe behavior of mock launchBarrier
    EXPECT_CALL(mockFrontend, launchBarrier(mockArgv, -1, -1, nullptr, nullptr, nullptr))
        .Times(1);

    // run the test
    auto const appId = cti_launchAppBarrier(mockArgv, -1, -1, nullptr, nullptr, nullptr);
    ASSERT_NE(appId, APP_ERROR) << cti_error_str();
    EXPECT_EQ(cti_releaseAppBarrier(appId), SUCCESS) << cti_error_str();

    // clean up app launch
    cti_deregisterApp(appId);
}

// int          cti_releaseAppBarrier(cti_app_id_t);
// Tests that the interface will call the App's barrier release
TEST_F(CTIAppUnitTest, ReleaseAppBarrier)
{
    // describe behavior of mock releaseBarrier
    EXPECT_CALL(*mockApp, releaseBarrier())
        .Times(1);

    // run the test
    EXPECT_EQ(cti_releaseAppBarrier(appId), SUCCESS) << cti_error_str();
}

// int          cti_killApp(cti_app_id_t, int);
// Tests that the interface will call the App's kill
TEST_F(CTIAppUnitTest, KillApp)
{
    // describe behavior of mock kill
    EXPECT_CALL(*mockApp, kill(_))
        .Times(1);

    // run the test
    EXPECT_EQ(cti_killApp(appId, 0), SUCCESS) << cti_error_str();
}

/* transfer session management tests */

// cti_session_id_t cti_createSession(cti_app_id_t appId);
// Tests that the interface can create a session using an app
TEST_F(CTIAppUnitTest, CreateSession)
{
    // run the test
    auto const sessionId = cti_createSession(appId);
    ASSERT_NE(sessionId, SESSION_ERROR) << cti_error_str();

    // cleanup session
    EXPECT_EQ(cti_destroySession(sessionId), SUCCESS);
}

// int              cti_sessionIsValid(cti_session_id_t sid);
// Tests that the interface can create a valid session
TEST_F(CTIAppUnitTest, SessionIsValid)
{
    // run the test
    auto const sessionId = cti_createSession(appId);
    ASSERT_NE(sessionId, SESSION_ERROR) << cti_error_str();
    EXPECT_EQ(cti_sessionIsValid(sessionId), true) << cti_error_str();

    // cleanup session
    EXPECT_EQ(cti_destroySession(sessionId), SUCCESS) << cti_error_str();
}

// int              cti_destroySession(cti_session_id_t sid);
// Tests that the interface can destroy a session
TEST_F(CTIAppUnitTest, DestroySession)
{
    // run the test
    auto const sessionId = cti_createSession(appId);
    ASSERT_NE(sessionId, SESSION_ERROR) << cti_error_str();
    EXPECT_EQ(cti_destroySession(sessionId), SUCCESS) << cti_error_str();
}

/* transfer session directory listings tests */

// char **  cti_getSessionLockFiles(cti_session_id_t sid);
// Tests that the interface can get a session's lock files
TEST_F(CTIAppUnitTest, GetSessionLockFilesNoManifest)
{
    // run the test
    auto const sessionId = cti_createSession(appId);
    ASSERT_NE(sessionId, SESSION_ERROR) << cti_error_str();

    // Sessions without manifests should not have any lock files
    auto lockFilesList = cti::move_pointer_ownership(cti_getSessionLockFiles(sessionId), cti::free_ptr_list<char*>);
    ASSERT_TRUE(lockFilesList == nullptr) << cti_error_str();

    // cleanup session
    EXPECT_EQ(cti_destroySession(sessionId), SUCCESS) << cti_error_str();
}

// char **  cti_getSessionLockFiles(cti_session_id_t sid);
// Tests that the interface can get a session's lock files
TEST_F(CTIAppUnitTest, GetSessionLockFilesOneManifest)
{
    // run the test
    auto const sessionId = cti_createSession(appId);
    ASSERT_NE(sessionId, SESSION_ERROR) << cti_error_str();

    // Create one manifest
    auto const manifestId = cti_createManifest(sessionId);
    ASSERT_NE(manifestId, MANIFEST_ERROR) << cti_error_str();

    // add and finalize file
    ASSERT_EQ(cti_addManifestFile(manifestId, "../test_support/message_one/message.c"), SUCCESS) << cti_error_str();
    ASSERT_EQ(cti_sendManifest(manifestId), SUCCESS) << cti_error_str();

    // Get the lock files
    auto lockFilesList = cti::move_pointer_ownership(cti_getSessionLockFiles(sessionId), cti::free_ptr_list<char*>);
    ASSERT_TRUE(lockFilesList != nullptr) << cti_error_str();

    // there should have been one lock file
    EXPECT_TRUE(lockFilesList.get()[0] != nullptr);
    EXPECT_TRUE(lockFilesList.get()[1] == nullptr);

    // cleanup session
    EXPECT_EQ(cti_destroySession(sessionId), SUCCESS) << cti_error_str();
}

// char **  cti_getSessionLockFiles(cti_session_id_t sid);
// Tests that the interface can get a session's lock files
TEST_F(CTIAppUnitTest, GetSessionLockFilesTwoManifests)
{
    // run the test
    auto const sessionId = cti_createSession(appId);
    ASSERT_NE(sessionId, SESSION_ERROR) << cti_error_str();

    // Create two manifest
    auto const manifestId = cti_createManifest(sessionId);
    ASSERT_NE(manifestId, MANIFEST_ERROR) << cti_error_str();
    auto const manifestId2 = cti_createManifest(sessionId);
    ASSERT_NE(manifestId2, MANIFEST_ERROR) << cti_error_str();

    // add and finalize file
    ASSERT_EQ(cti_addManifestFile(manifestId, "../test_support/message_one/message.c"), SUCCESS) << cti_error_str();
    ASSERT_EQ(cti_sendManifest(manifestId), SUCCESS) << cti_error_str();
    ASSERT_EQ(cti_addManifestFile(manifestId2, "../test_support/message_two/message.h"), SUCCESS) << cti_error_str();
    ASSERT_EQ(cti_sendManifest(manifestId2), SUCCESS) << cti_error_str();

    // Get the lock files
    auto lockFilesList = cti::move_pointer_ownership(cti_getSessionLockFiles(sessionId), cti::free_ptr_list<char*>);
    ASSERT_TRUE(lockFilesList != nullptr) << cti_error_str();

    // there should have been two lock files
    EXPECT_TRUE(lockFilesList.get()[0] != nullptr);
    EXPECT_TRUE(lockFilesList.get()[1] != nullptr);
    EXPECT_TRUE(lockFilesList.get()[2] == nullptr);

    // cleanup session
    EXPECT_EQ(cti_destroySession(sessionId), SUCCESS) << cti_error_str();
}

// char *   cti_getSessionRootDir(cti_session_id_t sid);
// Tests that the interface can get a session's root directory
TEST_F(CTIAppUnitTest, GetSessionRootDir)
{
    // run the test
    auto const sessionId = cti_createSession(appId);
    ASSERT_NE(sessionId, SESSION_ERROR) << cti_error_str();

    auto const rawRootDir = cti::move_pointer_ownership(cti_getSessionRootDir(sessionId), ::free);
    ASSERT_TRUE(rawRootDir != nullptr) << cti_error_str();

    // cleanup session
    EXPECT_EQ(cti_destroySession(sessionId), SUCCESS) << cti_error_str();
}

// char *   cti_getSessionBinDir(cti_session_id_t sid);
// Tests that the interface can get a session's bin directory
TEST_F(CTIAppUnitTest, GetSessionBinDir)
{
    // run the test
    auto const sessionId = cti_createSession(appId);
    ASSERT_NE(sessionId, SESSION_ERROR) << cti_error_str();

    auto const rawRootDir = cti::move_pointer_ownership(cti_getSessionRootDir(sessionId), ::free);
    ASSERT_TRUE(rawRootDir != nullptr) << cti_error_str();
    auto const rawBinDir = cti::move_pointer_ownership(cti_getSessionBinDir(sessionId), ::free);
    ASSERT_TRUE(rawBinDir != nullptr) << cti_error_str();
    ASSERT_EQ(std::string{rawBinDir.get()}, std::string{rawRootDir.get()} + "/bin");

    // cleanup session
    EXPECT_EQ(cti_destroySession(sessionId), SUCCESS) << cti_error_str();
}

// char *   cti_getSessionLibDir(cti_session_id_t sid);
// Tests that the interface can get a session's lib directory
TEST_F(CTIAppUnitTest, GetSessionLibDir)
{
    // run the test
    auto const sessionId = cti_createSession(appId);
    ASSERT_NE(sessionId, SESSION_ERROR) << cti_error_str();

    auto const rawRootDir = cti::move_pointer_ownership(cti_getSessionRootDir(sessionId), ::free);
    ASSERT_TRUE(rawRootDir != nullptr) << cti_error_str();
    auto const rawLibDir = cti::move_pointer_ownership(cti_getSessionLibDir(sessionId), ::free);
    ASSERT_TRUE(rawLibDir != nullptr) << cti_error_str();
    ASSERT_EQ(std::string{rawLibDir.get()}, std::string{rawRootDir.get()} + "/lib");

    // cleanup session
    EXPECT_EQ(cti_destroySession(sessionId), SUCCESS) << cti_error_str();
}

// char *   cti_getSessionFileDir(cti_session_id_t sid);
// Tests that the interface can get a session's file directory
TEST_F(CTIAppUnitTest, GetSessionFileDir)
{
    // run the test
    auto const sessionId = cti_createSession(appId);
    ASSERT_NE(sessionId, SESSION_ERROR);

    auto const rawRootDir = cti::move_pointer_ownership(cti_getSessionRootDir(sessionId), ::free);
    ASSERT_TRUE(rawRootDir != nullptr) << cti_error_str();
    auto const rawFileDir = cti::move_pointer_ownership(cti_getSessionFileDir(sessionId), ::free);
    ASSERT_TRUE(rawFileDir != nullptr) << cti_error_str();
    ASSERT_EQ(std::string{rawFileDir.get()}, std::string{rawRootDir.get()});

    // cleanup session
    EXPECT_EQ(cti_destroySession(sessionId), SUCCESS) << cti_error_str();
}

// char *   cti_getSessionTmpDir(cti_session_id_t sid);
// Tests that the interface can get a session's temp directory
TEST_F(CTIAppUnitTest, GetSessionTmpDir)
{
    // run the test
    auto const sessionId = cti_createSession(appId);
    ASSERT_NE(sessionId, SESSION_ERROR) << cti_error_str();

    auto const rawRootDir = cti::move_pointer_ownership(cti_getSessionRootDir(sessionId), ::free);
    ASSERT_TRUE(rawRootDir != nullptr) << cti_error_str();
    auto const rawTmpDir = cti::move_pointer_ownership(cti_getSessionTmpDir(sessionId), ::free);
    ASSERT_TRUE(rawTmpDir != nullptr) << cti_error_str();
    ASSERT_EQ(std::string{rawTmpDir.get()}, std::string{rawRootDir.get()} + "/tmp");

    // cleanup session
    EXPECT_EQ(cti_destroySession(sessionId), SUCCESS) << cti_error_str();
}

/* transfer manifest management tests */

// cti_manifest_id_t    cti_createManifest(cti_session_id_t sid);
// Tests that the interface can create a manifest
TEST_F(CTIAppUnitTest, CreateManifest)
{
    auto const sessionId = cti_createSession(appId);
    ASSERT_NE(sessionId, SESSION_ERROR) << cti_error_str();

    // run the test
    auto const manifestId = cti_createManifest(sessionId);
    ASSERT_NE(manifestId, MANIFEST_ERROR) << cti_error_str();

    // cleanup
    EXPECT_EQ(cti_destroySession(sessionId), SUCCESS) << cti_error_str();
}

// int                  cti_manifestIsValid(cti_manifest_id_t mid);
// Tests that the interface can create a valid manifest
TEST_F(CTIAppUnitTest, ManifestIsValid)
{
    auto const sessionId = cti_createSession(appId);
    ASSERT_NE(sessionId, SESSION_ERROR) << cti_error_str();

    // run the test
    auto const manifestId = cti_createManifest(sessionId);
    ASSERT_NE(manifestId, MANIFEST_ERROR) << cti_error_str();
    ASSERT_EQ(cti_manifestIsValid(manifestId), true) << cti_error_str();

    // cleanup
    EXPECT_EQ(cti_destroySession(sessionId), SUCCESS) << cti_error_str();
}

// int                  cti_addManifestBinary(cti_manifest_id_t mid, const char * rawName);
// Tests that the interface can add a binary to a manifest
TEST_F(CTIAppUnitTest, AddManifestBinary)
{
    auto const sessionId = cti_createSession(appId);
    ASSERT_NE(sessionId, SESSION_ERROR) << cti_error_str();

    // run the test
    auto const manifestId = cti_createManifest(sessionId);
    ASSERT_NE(manifestId, MANIFEST_ERROR) << cti_error_str();

    // add and finalize binaries
    ASSERT_EQ(cti_addManifestBinary(manifestId, "../test_support/one_socket"), SUCCESS) << cti_error_str();
    ASSERT_EQ(cti_sendManifest(manifestId), SUCCESS) << cti_error_str();

    // check for expected contents
    auto const shippedFilePaths = mockApp->getShippedFilePaths();
    ASSERT_TRUE(!shippedFilePaths.empty());

    auto const tarRoot = shippedFilePaths[0].substr(0, shippedFilePaths[0].find("/") + 1);
    auto const expectedPaths = std::unordered_set<std::string>
        { tarRoot + "bin/one_socket"
        , tarRoot + "lib/libmessage.so"
    };

    for (auto&& path : mockApp->getShippedFilePaths()) {
        EXPECT_TRUE(expectedPaths.find(path) != expectedPaths.end()) << "Could not find " << path;
    }

    // cleanup
    EXPECT_EQ(cti_destroySession(sessionId), SUCCESS) << cti_error_str();
}

// int                  cti_addManifestLibrary(cti_manifest_id_t mid, const char * rawName);
// Tests that the interface can add a library to a manifest
TEST_F(CTIAppUnitTest, AddManifestLibrary)
{
    auto const sessionId = cti_createSession(appId);
    ASSERT_NE(sessionId, SESSION_ERROR) << cti_error_str();

    // run the test
    auto const manifestId = cti_createManifest(sessionId);
    ASSERT_NE(manifestId, MANIFEST_ERROR) << cti_error_str();

    // add and finalize libraries
    ASSERT_EQ(cti_addManifestLibrary(manifestId, "../test_support/message_one/libmessage.so"), SUCCESS) << cti_error_str();
    ASSERT_EQ(cti_sendManifest(manifestId), SUCCESS) << cti_error_str();

    // check for expected contents
    auto const shippedFilePaths = mockApp->getShippedFilePaths();
    ASSERT_TRUE(!shippedFilePaths.empty());

    auto const tarRoot = shippedFilePaths[0].substr(0, shippedFilePaths[0].find("/") + 1);
    auto const expectedPaths = std::unordered_set<std::string>
        { tarRoot + "lib/libmessage.so"
    };

    for (auto&& path : mockApp->getShippedFilePaths()) {
        EXPECT_TRUE(expectedPaths.find(path) != expectedPaths.end()) << "Could not find " << path;
    }

    // cleanup
    EXPECT_EQ(cti_destroySession(sessionId), SUCCESS) << cti_error_str();
}

// int                  cti_addManifestLibDir(cti_manifest_id_t mid, const char * rawName);
// Tests that the interface can add a library directory to a manifest
TEST_F(CTIAppUnitTest, AddManifestLibDir)
{
    auto const sessionId = cti_createSession(appId);
    ASSERT_NE(sessionId, SESSION_ERROR) << cti_error_str();

    // run the test
    auto const manifestId = cti_createManifest(sessionId);
    ASSERT_NE(manifestId, MANIFEST_ERROR) << cti_error_str();

    // add and finalize libraries
    ASSERT_EQ(cti_addManifestLibDir(manifestId, "../test_support/message_one/"), SUCCESS) << cti_error_str();
    ASSERT_EQ(cti_sendManifest(manifestId), SUCCESS) << cti_error_str();

    // check for expected contents
    auto const shippedFilePaths = mockApp->getShippedFilePaths();
    ASSERT_TRUE(!shippedFilePaths.empty());

    auto const tarRoot = shippedFilePaths[0].substr(0, shippedFilePaths[0].find("/") + 1);
    auto const expectedPaths = std::unordered_set<std::string>
        { tarRoot + "lib/message_one/libmessage.so"
        , tarRoot + "lib/message_one/message.c"
        , tarRoot + "lib/message_one/message.h"
    };

    for (auto&& path : mockApp->getShippedFilePaths()) {
        EXPECT_TRUE(expectedPaths.find(path) != expectedPaths.end()) << "Could not find " << path;
    }

    // cleanup
    EXPECT_EQ(cti_destroySession(sessionId), SUCCESS) << cti_error_str();
}

// int                  cti_addManifestFile(cti_manifest_id_t mid, const char * rawName);
// int                  cti_sendManifest(cti_manifest_id_t mid);
// Tests that the interface can add a file to a manifest
TEST_F(CTIAppUnitTest, AddManifestFile)
{
    auto const sessionId = cti_createSession(appId);
    ASSERT_NE(sessionId, SESSION_ERROR) << cti_error_str();

    // run the test
    auto const manifestId = cti_createManifest(sessionId);
    ASSERT_NE(manifestId, MANIFEST_ERROR) << cti_error_str();

    // add and finalize file
    ASSERT_EQ(cti_addManifestFile(manifestId, "../test_support/message_one/message.c"), SUCCESS) << cti_error_str();
    ASSERT_EQ(cti_sendManifest(manifestId), SUCCESS) << cti_error_str();

    // check for expected contents
    auto const shippedFilePaths = mockApp->getShippedFilePaths();
    ASSERT_TRUE(!shippedFilePaths.empty());

    auto const tarRoot = shippedFilePaths[0].substr(0, shippedFilePaths[0].find("/") + 1);
    auto const expectedPaths = std::unordered_set<std::string>
        { tarRoot + "/message.c"
    };

    for (auto&& path : mockApp->getShippedFilePaths()) {
        EXPECT_TRUE(expectedPaths.find(path) != expectedPaths.end()) << "Could not find " << path;
    }

    // cleanup
    EXPECT_EQ(cti_destroySession(sessionId), SUCCESS) << cti_error_str();
}

/* tool daemon management tests */

// int cti_execToolDaemon(cti_manifest_id_t mid, const char *daemonPath,
//  const char * const daemonArgs[], const char * const envVars[]);
// Tests that the interface can exec a tool daemon
TEST_F(CTIAppUnitTest, ExecToolDaemon)
{
    auto const sessionId = cti_createSession(appId);
    ASSERT_NE(sessionId, SESSION_ERROR) << cti_error_str();

    // run the test
    auto const manifestId = cti_createManifest(sessionId);
    ASSERT_NE(manifestId, MANIFEST_ERROR) << cti_error_str();

    // finalize manifest and run tooldaemon
    ASSERT_EQ(cti_execToolDaemon(manifestId, "../test_support/one_socket", mockArgv, nullptr), SUCCESS) << cti_error_str();

    // check for expected contents
    auto const shippedFilePaths = mockApp->getShippedFilePaths();
    ASSERT_TRUE(!shippedFilePaths.empty());

    auto const tarRoot = shippedFilePaths[0].substr(0, shippedFilePaths[0].find("/") + 1);
    auto const expectedPaths = std::unordered_set<std::string>
        { tarRoot + "bin/one_socket"
        , tarRoot + "lib/libmessage.so"
    };

    for (auto&& path : mockApp->getShippedFilePaths()) {
        EXPECT_TRUE(expectedPaths.find(path) != expectedPaths.end()) << "Could not find " << path;
    }

    // cleanup
    EXPECT_EQ(cti_destroySession(sessionId), SUCCESS) << cti_error_str();
}

TEST_F(CTIAppUnitTest, ManifestLibraryConflict)
{
    auto const sessionId = cti_createSession(appId);
    ASSERT_NE(sessionId, SESSION_ERROR) << cti_error_str();

    // run the test
    { auto const manifestId = cti_createManifest(sessionId);
        ASSERT_NE(manifestId, MANIFEST_ERROR) << cti_error_str();

        ASSERT_EQ(cti_addManifestLibrary(manifestId, "../test_support/message_one/libmessage.so"), SUCCESS) << cti_error_str();
        ASSERT_EQ(cti_sendManifest(manifestId), SUCCESS) << cti_error_str();
    }

    { auto const manifestId = cti_createManifest(sessionId);
        ASSERT_NE(manifestId, MANIFEST_ERROR) << cti_error_str();

        ASSERT_EQ(cti_addManifestLibrary(manifestId, "../test_support/message_two/libmessage.so"), FAILURE) <<
            "Expected failure when manually adding conflicting libraries";
        ASSERT_EQ(cti_sendManifest(manifestId), SUCCESS) << cti_error_str();
    }

    // cleanup
    EXPECT_EQ(cti_destroySession(sessionId), SUCCESS) << cti_error_str();
}
