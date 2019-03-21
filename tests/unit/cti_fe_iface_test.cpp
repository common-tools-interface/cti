#include <stdio.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "cti_fe_iface_test.hpp"

#include "useful/ExecvpOutput.hpp"

// generate temporary filename and delete it when finished
class temp_file_handle
{
private:
	char const* path;

public:
	temp_file_handle()
		: path{tmpnam(nullptr)}
	{
		if (path == nullptr) {
			throw std::runtime_error("tmpnam failed");
		}
	}

	temp_file_handle(temp_file_handle&& moved)
		: path{moved.path}
	{
		moved.path = nullptr;
	}

	~temp_file_handle()
	{
		if ((path != nullptr) && (remove(path) < 0)) {
			// path could have been generated but not opened as a file
			std::cerr << "warning: remove " << std::string{path} << " failed" << std::endl;
		}
	}

	char const* get() const { return path; }
};

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
	char const* const* envList = nullptr;

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
	ASSERT_GE(p.getReadFd(), 0);
	ASSERT_GE(p.getWriteFd(), 0);
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

// Test that an app can read input from a file
TEST_F(CTIFEIfaceTest, InputFile) {
	// set up string contents
	auto const echoString = std::to_string(getpid());

	// set up input file
	auto const inputPath = temp_file_handle{};
	{ auto inputFile = std::unique_ptr<FILE, decltype(&::fclose)>(fopen(inputPath.get(), "w"), ::fclose);
		fprintf(inputFile.get(), "%s\n", echoString.c_str());
	}

	// set up stdout fd
	Pipe p;
	ASSERT_GE(p.getReadFd(), 0);
	ASSERT_GE(p.getWriteFd(), 0);
	FdBuf pipeInBuf{p.getReadFd()};
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
	p.closeWrite();
	{ std::string line;
		ASSERT_TRUE(std::getline(pipein, line));
		EXPECT_EQ(line, echoString);
	}

	// cleanup
	p.closeRead();
}

// Test that an app can forward environment variables
TEST_F(CTIFEIfaceTest, EnvVars) {
	// set up string contents
	auto const envVar = std::string{"CTI_TEST_VAR"};
	auto const envVal = std::to_string(getpid());
	auto const envString = envVar + "=" + envVal;

	// set up stdout fd
	Pipe p;
	ASSERT_GE(p.getReadFd(), 0);
	ASSERT_GE(p.getWriteFd(), 0);
	FdBuf pipeInBuf{p.getReadFd()};
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
	p.closeWrite();
	{ std::string line;
		bool found = false;
		while (std::getline(pipein, line)) {
			auto const var = line.substr(0, line.find('='));
			auto const val = line.substr(line.find('=') + 1);

			if (!var.compare(envVar) && !val.compare(envVal)) {
				found = true;
				break;
			}
		}
		EXPECT_TRUE(found);
	}

	// cleanup
	p.closeRead();
}