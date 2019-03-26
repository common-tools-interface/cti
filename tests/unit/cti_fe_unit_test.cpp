#include "cti_fe_unit_test.hpp"

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
	auto const rawHostname = std::unique_ptr<char, decltype(&::free)>{cti_getHostname(), ::free};
	ASSERT_TRUE(rawHostname != nullptr);
}

// Tests that the app will return a hostname
TEST_F(CTIAppUnitTest, GetLauncherHostname)
{
	// describe behavior of mock getLauncherHostname
	EXPECT_CALL(mockApp, getLauncherHostname())
		.WillOnce(Return(std::string{"remote-hostname"}));

	// run the test
	auto const rawHostname = std::unique_ptr<char, decltype(&::free)>{cti_getLauncherHostName(appId), ::free};
	ASSERT_TRUE(rawHostname != nullptr);
}