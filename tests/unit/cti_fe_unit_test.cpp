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

/* cti frontend C interface tests */

// Tests that the frontend type is set to mock.
TEST_F(CTIFEUnitTest, GetWLMType)
{
	MockFrontend& mockFrontend = dynamic_cast<MockFrontend&>(_cti_getCurrentFrontend());

	// describe behavior of mock getWLMType
	EXPECT_CALL(mockFrontend, getWLMType())
		.WillOnce(Return(CTI_WLM_MOCK));

	// run the test
	ASSERT_EQ(cti_current_wlm(), CTI_WLM_MOCK);
}

// Tests that the frontend will produce an App pointer.
TEST_F(CTIFEUnitTest, LaunchBarrier)
{
	MockFrontend& mockFrontend = dynamic_cast<MockFrontend&>(_cti_getCurrentFrontend());

	// describe behavior of mock launchBarrier
	EXPECT_CALL(mockFrontend, launchBarrier(mockArgv, -1, -1, nullptr, nullptr, nullptr))
		.Times(1);

	// run the test
	auto const appId = cti_launchAppBarrier(mockArgv, -1, -1, nullptr, nullptr, nullptr);
	ASSERT_NE(appId, APP_ERROR);
	EXPECT_EQ(cti_releaseAppBarrier(appId), SUCCESS);
}

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

// Tests that the app will return a number of app PEs
TEST_F(CTIAppUnitTest, GetNumAppPEs)
{
	// describe behavior of mock getToolPath
	EXPECT_CALL(mockApp, getNumPEs())
		.WillOnce(Return(getpid()));

	// run the test
	EXPECT_EQ(cti_getNumAppPEs(appId), getpid());
}

// Tests that the app will return a number of hosts
TEST_F(CTIAppUnitTest, GetNumAppNodes)
{
	// describe behavior of mock getToolPath
	EXPECT_CALL(mockApp, getNumHosts())
		.WillOnce(Return(getpid()));

	// run the test
	EXPECT_EQ(cti_getNumAppNodes(appId), getpid());
}

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