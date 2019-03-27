#include "cti_fe_unit_test.hpp"

#include "useful/make_unique_destr.hpp"

using ::testing::Return;
using ::testing::_;
using ::testing::Invoke;
using ::testing::WithoutArgs;

CTIFEUnitTest::CTIFEUnitTest()
{
	// manually set the frontend to the custom mock frontend
	_cti_setFrontend(std::make_unique<MockFrontend::Nice>());
}

CTIFEUnitTest::~CTIFEUnitTest()
{
	// destruct the mock frontend so that final checks can be performed
	_cti_setFrontend(nullptr);
}

static constexpr char const* mockArgv[] = {"/usr/bin/true", nullptr};
CTIAppUnitTest::CTIAppUnitTest()
	: CTIFEUnitTest{}
	, appId{cti_launchAppBarrier(mockArgv, -1, -1, nullptr, nullptr, nullptr)}
	, mockApp{dynamic_cast<MockApp::Nice&>(_cti_getApp(appId))}
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

// const char *	cti_version(void);
// Tests that the frontend will return a version string
TEST_F(CTIFEUnitTest, Version)
{
	// run the test
	ASSERT_TRUE(cti_version() != nullptr);
}

// cti_wlm_type	cti_current_wlm(void);
// Tests that the frontend type is set to mock.
TEST_F(CTIFEUnitTest, CurrentWLM)
{
	MockFrontend& mockFrontend = dynamic_cast<MockFrontend&>(_cti_getCurrentFrontend());

	// describe behavior of mock getWLMType
	EXPECT_CALL(mockFrontend, getWLMType())
		.WillOnce(Return(CTI_WLM_MOCK));

	// run the test
	ASSERT_EQ(cti_current_wlm(), CTI_WLM_MOCK);
}

// const char *	cti_wlm_type_toString(cti_wlm_type);
// Tests that the frontend type string is nonnull
TEST_F(CTIFEUnitTest, WLMTypeToString)
{
	// run the test
	ASSERT_TRUE(cti_wlm_type_toString(cti_current_wlm()) != nullptr);
}

// char *      	cti_getHostname(void);
// Tests that the frontend will return a hostname
TEST_F(CTIFEUnitTest, GetHostname)
{
	MockFrontend& mockFrontend = dynamic_cast<MockFrontend&>(_cti_getCurrentFrontend());

	// describe behavior of mock getWLMType
	EXPECT_CALL(mockFrontend, getHostname())
		.WillOnce(Return(std::string{"local-hostname"}));

	// run the test
	auto const rawHostname = make_unique_destr(cti_getHostname(), std::free);
	ASSERT_TRUE(rawHostname != nullptr);
}

// int         	cti_setAttribute(cti_attr_type attrib, const char *value);
// Tests that the frontend can set an attribute
TEST_F(CTIFEUnitTest, SetAttribute)
{
	// run the test
	ASSERT_EQ(cti_setAttribute(CTI_ATTR_STAGE_DEPENDENCIES, "1"), SUCCESS);
}

/* running app information query tests */

// char *           	cti_getLauncherHostName(cti_app_id_t);
// Tests that the app will return a hostname
TEST_F(CTIAppUnitTest, GetLauncherHostname)
{
	// describe behavior of mock getLauncherHostname
	EXPECT_CALL(mockApp, getLauncherHostname())
		.WillOnce(Return(std::string{"remote-hostname"}));

	// run the test
	auto const rawHostname = make_unique_destr(cti_getLauncherHostName(appId), std::free);
	ASSERT_TRUE(rawHostname != nullptr);
	EXPECT_EQ(std::string{rawHostname.get()}, "remote-hostname");
}

// int              	cti_getNumAppPEs(cti_app_id_t);
// Tests that the app will return a number of app PEs
TEST_F(CTIAppUnitTest, GetNumAppPEs)
{
	// describe behavior of mock getToolPath
	EXPECT_CALL(mockApp, getNumPEs())
		.WillOnce(Return(getpid()));

	// run the test
	EXPECT_EQ(cti_getNumAppPEs(appId), getpid());
}

// int              	cti_getNumAppNodes(cti_app_id_t);
// Tests that the app will return a number of hosts
TEST_F(CTIAppUnitTest, GetNumAppNodes)
{
	// describe behavior of mock getToolPath
	EXPECT_CALL(mockApp, getNumHosts())
		.WillOnce(Return(getpid()));

	// run the test
	EXPECT_EQ(cti_getNumAppNodes(appId), getpid());
}

// char **          	cti_getAppHostsList(cti_app_id_t);
// Tests that the app will return a list of hostnames
TEST_F(CTIAppUnitTest, GetAppHostsList)
{
	// describe behavior of mock getHostnameList
	EXPECT_CALL(mockApp, getHostnameList())
		.WillOnce(Return(std::vector<std::string>{"remote-hostname"}));

	// run the test
	auto const rawHostsList = make_unique_destr(cti_getAppHostsList(appId), free_ptr_list<char*>);
	ASSERT_TRUE(rawHostsList != nullptr);
	EXPECT_EQ(std::string{rawHostsList.get()[0]}, "remote-hostname");
	EXPECT_EQ(rawHostsList.get()[1], nullptr);
}

// cti_hostsList_t *	cti_getAppHostsPlacement(cti_app_id_t);
// void             	cti_destroyHostsList(cti_hostsList_t *);
// Tests that the app will return a list of host placements
TEST_F(CTIAppUnitTest, GetAppHostsPlacement)
{
	// describe behavior of mock getHostsPlacement
	EXPECT_CALL(mockApp, getHostsPlacement())
		.WillOnce(Return(std::vector<CTIHost>{CTIHost
			{ .hostname = "remote-hostname"
			, .numPEs = (size_t)getpid()
		}}));

	// run the test
	auto const rawHostsList = make_unique_destr(cti_getAppHostsPlacement(appId), cti_destroyHostsList);
	ASSERT_TRUE(rawHostsList != nullptr);
	EXPECT_EQ(rawHostsList->numHosts, 1);
	ASSERT_TRUE(rawHostsList->hosts != nullptr);
	ASSERT_TRUE(rawHostsList->hosts[0].hostname != nullptr);
	EXPECT_EQ(std::string{rawHostsList->hosts[0].hostname}, "remote-hostname");
	EXPECT_EQ(rawHostsList->hosts[0].numPEs, getpid());
}

/* app lifecycle management tests */

// int         	cti_appIsValid(cti_app_id_t);
// Tests that the interface recognizes a valid app
TEST_F(CTIAppUnitTest, AppIsValid)
{
	// run the test
	EXPECT_EQ(cti_appIsValid(appId), true);
}

// cti_app_id_t	cti_launchApp(const char * const [], int, int, const char *, const char *, const char * const []);
// void        	cti_deregisterApp(cti_app_id_t);
// Tests that the interface will call the Frontend to launch an App
TEST_F(CTIFEUnitTest, LaunchApp)
{
	MockFrontend& mockFrontend = dynamic_cast<MockFrontend&>(_cti_getCurrentFrontend());

	// describe behavior of mock launchBarrier
	EXPECT_CALL(mockFrontend, launchBarrier(mockArgv, -1, -1, nullptr, nullptr, nullptr))
		.Times(1);

	// run the test
	auto const appId = cti_launchApp(mockArgv, -1, -1, nullptr, nullptr, nullptr);
	ASSERT_NE(appId, APP_ERROR);
	EXPECT_EQ(cti_releaseAppBarrier(appId), FAILURE);

	// clean up app launch
	cti_deregisterApp(appId);
}

// cti_app_id_t	cti_launchAppBarrier(const char * const [], int, int, const char *, const char *, const char * const []);
// Tests that the interface will call the Frontend to launch an App at barrier
TEST_F(CTIFEUnitTest, LaunchAppBarrier)
{
	MockFrontend& mockFrontend = dynamic_cast<MockFrontend&>(_cti_getCurrentFrontend());

	// describe behavior of mock launchBarrier
	EXPECT_CALL(mockFrontend, launchBarrier(mockArgv, -1, -1, nullptr, nullptr, nullptr))
		.Times(1);

	// run the test
	auto const appId = cti_launchAppBarrier(mockArgv, -1, -1, nullptr, nullptr, nullptr);
	ASSERT_NE(appId, APP_ERROR);
	EXPECT_EQ(cti_releaseAppBarrier(appId), SUCCESS);

	// clean up app launch
	cti_deregisterApp(appId);
}

// int         	cti_releaseAppBarrier(cti_app_id_t);
// Tests that the interface will call the App's barrier release
TEST_F(CTIAppUnitTest, ReleaseAppBarrier)
{
	// describe behavior of mock releaseBarrier
	EXPECT_CALL(mockApp, releaseBarrier())
		.Times(1);

	// run the test
	EXPECT_EQ(cti_releaseAppBarrier(appId), SUCCESS);
}

// int         	cti_killApp(cti_app_id_t, int);
// Tests that the interface will call the App's kill
TEST_F(CTIAppUnitTest, KillApp)
{
	// describe behavior of mock kill
	EXPECT_CALL(mockApp, kill(_))
		.Times(1);

	// run the test
	EXPECT_EQ(cti_killApp(appId, 0), SUCCESS);
}

/* transfer session management tests */

// cti_session_id_t	cti_createSession(cti_app_id_t appId);
// int             	cti_sessionIsValid(cti_session_id_t sid);
// int             	cti_destroySession(cti_session_id_t sid);
// void _cti_consumeSession(void* sidPtr); // destroy session via appentry's session list

/* transfer session directory listings tests */

// char **	cti_getSessionLockFiles(cti_session_id_t sid);
// char * 	cti_getSessionRootDir(cti_session_id_t sid);
// char * 	cti_getSessionBinDir(cti_session_id_t sid);
// char * 	cti_getSessionLibDir(cti_session_id_t sid);
// char * 	cti_getSessionFileDir(cti_session_id_t sid);
// char * 	cti_getSessionTmpDir(cti_session_id_t sid);


/* transfer manifest management tests */

// cti_manifest_id_t	cti_createManifest(cti_session_id_t sid);
// int              	cti_manifestIsValid(cti_manifest_id_t mid);
// int              	cti_addManifestBinary(cti_manifest_id_t mid, const char * rawName);
// int              	cti_addManifestLibrary(cti_manifest_id_t mid, const char * rawName);
// int              	cti_addManifestLibDir(cti_manifest_id_t mid, const char * rawName);
// int              	cti_addManifestFile(cti_manifest_id_t mid, const char * rawName);
// int              	cti_sendManifest(cti_manifest_id_t mid);

/* tool daemon management tests */

// int cti_execToolDaemon(cti_manifest_id_t mid, const char *daemonPath,
// const char * const daemonArgs[], const char * const envVars[]);