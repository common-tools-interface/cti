#include <unistd.h>

#include "gtest/gtest.h"

#include "frontend/cti_fe_iface.h"

namespace {

// The fixture for testing C interface results
class CInterfaceTest : public ::testing::Test
{
	protected:
		CInterfaceTest()
		{
			// You can do set-up work for each test here.
		}

		~CInterfaceTest() override
		{
			// You can do clean-up work that doesn't throw exceptions here.
		}
};

static const auto SUCCESS = int{0};
static const auto FAILURE = int{1};

// Tests that the frontend type was correctly detected.
TEST_F(CInterfaceTest, HaveValidFrontend) {
	EXPECT_NE(cti_current_wlm(), CTI_WLM_NONE);
}

// Test that an app can launch successfully
TEST_F(CInterfaceTest, Launch) {
	char const* argv[] = {"/bin/sh", nullptr};
	auto const  stdoutFd = -1;
	auto const  stderrFd = -1;
	char const* inputFile = nullptr;
	char const* chdirPath = nullptr;
	char const* const* envList  = nullptr;

	auto const appId = cti_launchApp(argv, stdoutFd, stderrFd, inputFile, chdirPath, envList);
	EXPECT_GT(appId, 0);
	EXPECT_EQ(cti_appIsValid(appId), true);
	cti_deregisterApp(appId);
	EXPECT_EQ(cti_appIsValid(appId), false);
}

// Test that an app can't be released twice
TEST_F(CInterfaceTest, DoubleRelease) {
	char const* argv[] = {"/bin/sh", nullptr};
	auto const  stdoutFd = -1;
	auto const  stderrFd = -1;
	char const* inputFile = nullptr;
	char const* chdirPath = nullptr;
	char const* envList[] = {"VAR=val", nullptr};

	auto const appId = cti_launchAppBarrier(argv, stdoutFd, stderrFd, inputFile, chdirPath, envList);
	EXPECT_GT(appId, 0);
	EXPECT_EQ(cti_releaseAppBarrier(appId), SUCCESS);
	EXPECT_EQ(cti_releaseAppBarrier(appId), FAILURE);
	cti_deregisterApp(appId);
}

} // namespace

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}