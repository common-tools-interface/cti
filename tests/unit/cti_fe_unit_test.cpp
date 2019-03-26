#include "cti_fe_unit_test.hpp"

using ::testing::Return;
using ::testing::_;
using ::testing::Invoke;
using ::testing::WithoutArgs;

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
	EXPECT_CALL(mockFrontend, launchBarrier(_, _, _, _, _, _))
		.WillOnce(WithoutArgs(Invoke([]() {
			return std::make_unique<MockApp>(getpid());
		})));

	// run the test
	char const* argv[] = {"/usr/bin/true", nullptr};
	auto const appId = cti_launchAppBarrier(argv, -1, -1, nullptr, nullptr, nullptr);
	ASSERT_NE(appId, APP_ERROR);
	EXPECT_EQ(cti_releaseAppBarrier(appId), SUCCESS);
}

// Tests that the frontend will return the correct hostname
TEST_F(CTIFEUnitTest, GetHostname)
{
	MockFrontend& mockFrontend = dynamic_cast<MockFrontend&>(_cti_getCurrentFrontend());

	// describe behavior of mock getWLMType
	EXPECT_CALL(mockFrontend, getHostname())
		.WillOnce(Return(std::string{"hostname"}));

	// run the test
	auto const rawHostname = std::unique_ptr<char, decltype(&::free)>{cti_getHostname(), ::free};
	ASSERT_TRUE(rawHostname != nullptr);
}