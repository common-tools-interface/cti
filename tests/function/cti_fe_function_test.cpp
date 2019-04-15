#include <stdio.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <fstream>
#include <memory>

#include "cti_fe_function_test.hpp"

#ifdef OUTPUT_TESTS
#include "useful/cti_wrappers.hpp"
#endif

#include "useful/cti_execvp.hpp"

/* cti frontend C interface tests */

static constexpr auto OUTPUT_FILE_TEMPLATE = "/lus/scratch/tmp/cti-test-XXXXXX";

// Tests that the frontend type was correctly detected.
TEST_F(CTIFEFunctionTest, HaveValidFrontend) {
	ASSERT_NE(cti_current_wlm(), CTI_WLM_NONE);
}

// Test that an app can launch successfully
TEST_F(CTIFEFunctionTest, Launch) {
	char const* argv[] = {"/usr/bin/true", nullptr};
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
TEST_F(CTIFEFunctionTest, DoubleRelease) {
	char const* argv[] = {"/usr/bin/true", nullptr};
	auto const  stdoutFd = -1;
	auto const  stderrFd = -1;
	char const* inputFile = nullptr;
	char const* chdirPath = nullptr;
	char const* const* envList = nullptr;

	auto const appId = watchApp(cti_launchAppBarrier(argv, stdoutFd, stderrFd, inputFile, chdirPath, envList));
	ASSERT_GT(appId, 0);
	EXPECT_EQ(cti_releaseAppBarrier(appId), SUCCESS);
	EXPECT_EQ(cti_releaseAppBarrier(appId), FAILURE);
}

// Test that an app can redirect stdout
TEST_F(CTIFEFunctionTest, StdoutPipe) {
	// set up string contents
	auto const echoString = std::to_string(getpid());

	// set up stdout fd
	cti::Pipe p;
	ASSERT_GE(p.getReadFd(), 0);
	ASSERT_GE(p.getWriteFd(), 0);
	cti::FdBuf pipeInBuf{p.getReadFd()};
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
	{ std::string line;
		ASSERT_TRUE(std::getline(pipein, line));
		EXPECT_EQ(line, echoString);
	}
}

#ifdef OUTPUT_TESTS
// Test that an app can read input from a file
TEST_F(CTIFEFunctionTest, InputFile) {
	// set up string contents
	auto const echoString = std::to_string(getpid());

	// set up input file
	auto const inputPath = cti::temp_file_handle{OUTPUT_FILE_TEMPLATE};
	{ auto inputFile = std::unique_ptr<FILE, decltype(&::fclose)>(fopen(inputPath.get(), "w"), ::fclose);
		fprintf(inputFile.get(), "%s\n", echoString.c_str());
	}

	// set up stdout fd
	cti::Pipe p;
	ASSERT_GE(p.getReadFd(), 0);
	ASSERT_GE(p.getWriteFd(), 0);
	cti::FdBuf pipeInBuf{p.getReadFd()};
	std::istream pipein{&pipeInBuf};

	// set up launch arguments
	char const* argv[] = {"/usr/bin/cat", nullptr};
	auto const  stdoutFd = p.getWriteFd();
	auto const  stderrFd = -1;
	char const* inputFile = inputPath.get();
	char const* chdirPath = nullptr;
	char const* const* envList  = nullptr;

	// launch app
	auto const appId = watchApp(cti_launchApp(argv, stdoutFd, stderrFd, inputFile, chdirPath, envList));
	ASSERT_GT(appId, 0);
	EXPECT_EQ(cti_appIsValid(appId), true);

	// get app output
	{ std::string line;
		ASSERT_TRUE(std::getline(pipein, line));
		EXPECT_EQ(line, echoString);
	}
}
#endif

// Test that an app can forward environment variables
TEST_F(CTIFEFunctionTest, EnvVars) {
	// set up string contents
	auto const envVar = std::string{"CTI_TEST_VAR"};
	auto const envVal = std::to_string(getpid());
	auto const envString = envVar + "=" + envVal;

	// set up stdout fd
	cti::Pipe p;
	ASSERT_GE(p.getReadFd(), 0);
	ASSERT_GE(p.getWriteFd(), 0);
	cti::FdBuf pipeInBuf{p.getReadFd()};
	std::istream pipein{&pipeInBuf};

	// set up launch arguments
	char const* argv[] = {"/usr/bin/env", nullptr};
	auto const  stdoutFd = p.getWriteFd();
	auto const  stderrFd = -1;
	char const* inputFile = nullptr;
	char const* chdirPath = nullptr;
	char const* const envList[] = {envString.c_str(), nullptr};

	// launch app
	auto const appId = watchApp(cti_launchApp(argv, stdoutFd, stderrFd, inputFile, chdirPath, envList));
	ASSERT_GT(appId, 0);
	EXPECT_EQ(cti_appIsValid(appId), true);

	// get app output
	bool found = false;
	{ std::string line;
		while (std::getline(pipein, line)) {
			auto const var = line.substr(0, line.find('='));
			auto const val = line.substr(line.find('=') + 1);

			if (!var.compare(envVar) && !val.compare(envVal)) {
				found = true;
				break;
			}
		}
	}
	EXPECT_TRUE(found);
}

// Test that an app can create a transfer session
TEST_F(CTIFEFunctionTest, CreateSession) {
	// set up app
	char const* argv[] = {"/usr/bin/true", nullptr};
	auto const  stdoutFd = -1;
	auto const  stderrFd = -1;
	char const* inputFile = nullptr;
	char const* chdirPath = nullptr;
	char const* const* envList  = nullptr;

	// create app
	auto const appId = watchApp(cti_launchAppBarrier(argv, stdoutFd, stderrFd, inputFile, chdirPath, envList));
	ASSERT_GT(appId, 0);
	EXPECT_EQ(cti_appIsValid(appId), true);

	// create app's session
	auto const sessionId = cti_createSession(appId);
	ASSERT_EQ(cti_sessionIsValid(sessionId), true);

	// cleanup
	EXPECT_EQ(cti_destroySession(sessionId), SUCCESS);
	EXPECT_EQ(cti_releaseAppBarrier(appId), SUCCESS);
}

// Test that an app can create a transfer manifest
TEST_F(CTIFEFunctionTest, CreateManifest) {
	// set up app
	char const* argv[] = {"/usr/bin/true", nullptr};
	auto const  stdoutFd = -1;
	auto const  stderrFd = -1;
	char const* inputFile = nullptr;
	char const* chdirPath = nullptr;
	char const* const* envList  = nullptr;

	// create app
	auto const appId = watchApp(cti_launchAppBarrier(argv, stdoutFd, stderrFd, inputFile, chdirPath, envList));
	ASSERT_GT(appId, 0);
	EXPECT_EQ(cti_appIsValid(appId), true);

	// create app's session
	auto const sessionId = cti_createSession(appId);
	ASSERT_EQ(cti_sessionIsValid(sessionId), true);

	// create manifest
	auto const manifestId = cti_createManifest(sessionId);
	ASSERT_EQ(cti_manifestIsValid(manifestId), true);

	// cleanup
	EXPECT_EQ(cti_destroySession(sessionId), SUCCESS);
	EXPECT_EQ(cti_releaseAppBarrier(appId), SUCCESS);
}

#ifdef OUTPUT_TESTS
static void
testPrintingDaemon(cti_session_id_t sessionId, char const* daemonPath, std::string const& expecting)
{
	// create manifest
	auto const manifestId = cti_createManifest(sessionId);
	ASSERT_EQ(cti_manifestIsValid(manifestId), true);

	// set up output file
	auto const outputPath = cti::temp_file_handle{OUTPUT_FILE_TEMPLATE};
	char const* toolDaemonArgs[] = {outputPath.get(), nullptr};
	ASSERT_EQ(cti_execToolDaemon(manifestId, daemonPath, toolDaemonArgs, nullptr), SUCCESS);
	sleep(1); // let tool daemon run

	// read output file
	{ std::ifstream outputFile(outputPath.get());
		ASSERT_TRUE(outputFile.is_open());
		std::string line;
		ASSERT_TRUE(std::getline(outputFile, line));
		EXPECT_EQ(line, expecting);
	}
}

// Test that an app can run a tool daemon
TEST_F(CTIFEFunctionTest, ExecToolDaemon) {
	// set up app
	char const* argv[] = {"/usr/bin/true", nullptr};
	auto const  stdoutFd = -1;
	auto const  stderrFd = -1;
	char const* inputFile = nullptr;
	char const* chdirPath = nullptr;
	char const* const* envList  = nullptr;

	// create app
	auto const appId = watchApp(cti_launchAppBarrier(argv, stdoutFd, stderrFd, inputFile, chdirPath, envList));
	ASSERT_GT(appId, 0);
	EXPECT_EQ(cti_appIsValid(appId), true);

	// create app's session
	auto const sessionId = cti_createSession(appId);
	ASSERT_EQ(cti_sessionIsValid(sessionId), true);

	// run printing daemons
	testPrintingDaemon(sessionId, "../test_support/one_printer", "1");

	// cleanup
	EXPECT_EQ(cti_destroySession(sessionId), SUCCESS);
	EXPECT_EQ(cti_releaseAppBarrier(appId), SUCCESS);
}

// Test that an app can launch two tool daemons using different libraries with the same name
TEST_F(CTIFEFunctionTest, DaemonLibDir) {
	// set up app
	char const* argv[] = {"/usr/bin/true", nullptr};
	auto const  stdoutFd = -1;
	auto const  stderrFd = -1;
	char const* inputFile = nullptr;
	char const* chdirPath = nullptr;
	char const* const* envList  = nullptr;

	// create app
	auto const appId = watchApp(cti_launchAppBarrier(argv, stdoutFd, stderrFd, inputFile, chdirPath, envList));
	ASSERT_GT(appId, 0);
	EXPECT_EQ(cti_appIsValid(appId), true);

	// create app's session
	auto const sessionId = cti_createSession(appId);
	ASSERT_EQ(cti_sessionIsValid(sessionId), true);

	// run printing daemons
	testPrintingDaemon(sessionId, "../test_support/one_printer", "1");
	testPrintingDaemon(sessionId, "../test_support/two_printer", "2");

	// cleanup
	EXPECT_EQ(cti_destroySession(sessionId), SUCCESS);
	EXPECT_EQ(cti_releaseAppBarrier(appId), SUCCESS);
}
#endif
