#include <unistd.h>

#include "cti_fe_iface_test.hpp"

/* cti frontend C interface tests */

// Tests that the frontend type was correctly detected.
TEST_F(cti_fe_ifaceTest, HaveValidFrontend) {
	EXPECT_NE(cti_current_wlm(), CTI_WLM_NONE);
}

// Test that an app can launch successfully
TEST_F(cti_fe_ifaceTest, Launch) {
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
TEST_F(cti_fe_ifaceTest, DoubleRelease) {
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
