#include <unistd.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "cti_fe_iface_test.hpp"

#include "useful/ExecvpOutput.hpp"

/* cti frontend C interface tests */

// Tests that the frontend type was correctly detected.
TEST_F(CTIFEIfaceTest, HaveValidFrontend) {
	ASSERT_NE(cti_current_wlm(), CTI_WLM_NONE);
}

// Test that an app can launch successfully
TEST_F(CTIFEIfaceTest, Launch) {
	char const* argv[] = {"/bin/sh", nullptr};
	auto const  stdoutFd = -1;
	auto const  stderrFd = -1;
	char const* inputFile = nullptr;
	char const* chdirPath = nullptr;
	char const* const* envList  = nullptr;

	auto const appId = watchApp(cti_launchApp(argv, stdoutFd, stderrFd, inputFile, chdirPath, envList));
	ASSERT_GT(appId, 0);
	EXPECT_EQ(cti_appIsValid(appId), true);
}

// Test that an app can't be released twice
TEST_F(CTIFEIfaceTest, DoubleRelease) {
	char const* argv[] = {"/bin/sh", nullptr};
	auto const  stdoutFd = -1;
	auto const  stderrFd = -1;
	char const* inputFile = nullptr;
	char const* chdirPath = nullptr;
	char const* envList[] = {"VAR=val", nullptr};

	auto const appId = watchApp(cti_launchAppBarrier(argv, stdoutFd, stderrFd, inputFile, chdirPath, envList));
	ASSERT_GT(appId, 0);
	EXPECT_EQ(cti_releaseAppBarrier(appId), SUCCESS);
	EXPECT_EQ(cti_releaseAppBarrier(appId), FAILURE);
}

// Test that an app can redirect stdout
TEST_F(CTIFEIfaceTest, StdoutPipe) {
	// set up string contents
	auto const echoString = std::to_string(getpid());

	// set up stdout fd
	Pipe p;
	ASSERT_GT(p.getReadFd(), 0);
	ASSERT_GT(p.getWriteFd(), 0);
	FdBuf pipeInBuf{p.getReadFd()};
	std::istream pipein{&pipeInBuf};

	// set up launch arguments
	char const* argv[] = {"/usr/bin/echo", echoString.c_str(), nullptr};
	auto const  stdoutFd = p.getWriteFd();
	auto const  stderrFd = -1;
	char const* inputFile = nullptr;
	char const* chdirPath = nullptr;
	char const* const* envList  = nullptr;

	// launch app
	auto const appId = watchApp(cti_launchApp(argv, stdoutFd, stderrFd, inputFile, chdirPath, envList));
	ASSERT_GT(appId, 0);
	EXPECT_EQ(cti_appIsValid(appId), true);

	// get app output
	p.closeWrite();
	{ std::string line;
		ASSERT_TRUE(std::getline(pipein, line));
		EXPECT_EQ(line, echoString);
	}

	// cleanup
	p.closeRead();
}