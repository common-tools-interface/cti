#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/stat.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <memory>

#include "cti_fe_function_test.hpp"

#include "cray_tools_fe.h"

#include "useful/cti_execvp.hpp"

/* cti frontend C interface tests */

static void
testSocketDaemon(cti_session_id_t sessionId, char const* daemonPath, std::string const& expecting) {
    // Wait for any previous cleanups to finish (see PE-26018)
    sleep(5);
    std::string external_ip = "";

    // Find my external IP
    {
        struct ifaddrs *ifaddr, *ifa;
        int family, s;
        char host[NI_MAXHOST];
        ASSERT_NE(getifaddrs(&ifaddr), -1);
        for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr == NULL) {
                continue;
            }
            family = ifa->ifa_addr->sa_family;
            if (family == AF_INET || family == AF_INET6) {
                s = getnameinfo(ifa->ifa_addr, (family==AF_INET) ?
                    sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6),
                    host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
                if (s != 0) {
                    FAIL() << "Error while trying to find non-locahost IP";
                }

                // Accept the first IP that is not localhost.
                if(std::string(host) != "127.0.0.1") {
                    external_ip=host;
                    break;
                }
            }
        }
        // clean up
        freeifaddrs(ifaddr);
    }

    ASSERT_NE(external_ip, "");
   
    // build 'server' socket  
    int test_socket;
    {
        // setup hints
        struct addrinfo hints;
        memset(&hints, 0, sizeof(struct addrinfo));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_NUMERICSERV;

        struct addrinfo *listener;

        // uses external_ip in order to bind socket to external IP and not localhost
        // if NULL is used this will ALWAYS give localhost which is not non-wbox compatible
        ASSERT_EQ(getaddrinfo(external_ip.c_str(), "0", &hints, &listener), 0); 
    
        // Create the socket
        ASSERT_NE(test_socket = socket(listener->ai_family, listener->ai_socktype, listener->ai_protocol), -1) << "Failed to create test_socket socket";
    
        // Bind the socket
        ASSERT_EQ(bind(test_socket, listener->ai_addr, listener->ai_addrlen), 0) << "Failed to bind test_socket socket";

        // Clean up listener
        freeaddrinfo(listener);
    }

    // Begin listening on socket
    ASSERT_EQ(listen(test_socket, 1), 0) << "Failed to listen on test_socket socket";

    // get my sockets info
    struct sockaddr_in sa;
    socklen_t sa_len = sizeof(sa);
    memset(&sa, 0, sizeof(sa));
    ASSERT_EQ(getsockname(test_socket, (struct sockaddr*) &sa, &sa_len), 0);

    // build required parameters for launching external app
    {   
        // create manifest and args
        auto const manifestId = cti_createManifest(sessionId);
        ASSERT_EQ(cti_manifestIsValid(manifestId), true) << cti_error_str();
        char const* sockDaemonArgs[] = {external_ip.c_str(), std::to_string(ntohs(sa.sin_port)).c_str(), nullptr};

        // launch app
        ASSERT_EQ(cti_execToolDaemon(manifestId, daemonPath, sockDaemonArgs, nullptr), SUCCESS) << cti_error_str();
    }

    // accept recently launched applications connection
    int app_socket;
    struct sockaddr_in wa;
    socklen_t wa_len = sizeof(wa);
    ASSERT_GE(app_socket = accept(test_socket, (struct sockaddr*) &wa, &wa_len), 0);

    // read data returned from app
    char buffer[16] = {0};
    int length = read(app_socket, buffer, 16);
    ASSERT_LT(length, 16);
    buffer[length] = '\0';

    // check for correctness
    ASSERT_STREQ(buffer, expecting.c_str());
    
    // close socket
    close(test_socket);
}

// Test that an app can launch two tool daemons using different libraries with the same name
// This test is at the start to avoid a race condition that causes failure if ran later
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
	ASSERT_GT(appId, 0) << cti_error_str();
	EXPECT_EQ(cti_appIsValid(appId), true) << cti_error_str();

	// create app's session
	auto const sessionId = cti_createSession(appId);
	ASSERT_EQ(cti_sessionIsValid(sessionId), true) << cti_error_str();

	// run printing daemons
	testSocketDaemon(sessionId, "../test_support/one_socket", "1");
	testSocketDaemon(sessionId, "../test_support/two_socket", "2");

	// cleanup
	EXPECT_EQ(cti_destroySession(sessionId), SUCCESS) << cti_error_str();
	EXPECT_EQ(cti_releaseAppBarrier(appId), SUCCESS) << cti_error_str();
}

// Tests that the frontend type was correctly detected.
TEST_F(CTIFEFunctionTest, HaveValidFrontend) {
	ASSERT_NE(cti_current_wlm(), CTI_WLM_NONE) << cti_error_str();
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
	ASSERT_GT(appId, 0) << cti_error_str();
	EXPECT_EQ(cti_appIsValid(appId), true) << cti_error_str();
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
	ASSERT_GT(appId, 0) << cti_error_str();
	EXPECT_EQ(cti_releaseAppBarrier(appId), SUCCESS) << cti_error_str();
	EXPECT_EQ(cti_releaseAppBarrier(appId), FAILURE) << cti_error_str();
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
	ASSERT_GT(appId, 0) << cti_error_str();
	EXPECT_EQ(cti_appIsValid(appId), true) << cti_error_str();

	// get app output
	{ std::string line;
		ASSERT_TRUE(std::getline(pipein, line));
		EXPECT_EQ(line, echoString);
	}
}

// Test that an app can read input from a file
TEST_F(CTIFEFunctionTest, InputFile) {

	// set up stdout fd
	cti::Pipe p;
	ASSERT_GE(p.getReadFd(), 0);
	ASSERT_GE(p.getWriteFd(), 0);
	cti::FdBuf pipeInBuf{p.getReadFd()};
	std::istream pipein{&pipeInBuf};

	char const* argv[] = {"/usr/bin/cat", nullptr};
	auto const  stdoutFd = p.getWriteFd();
	auto const  stderrFd = -1;
	char const* inputFile = "./test_data.txt";
	char const* chdirPath = nullptr;
	char const* const* envList  = nullptr;

	// launch app
	auto const appId = watchApp(cti_launchApp(argv, stdoutFd, stderrFd, inputFile, chdirPath, envList));
	ASSERT_GT(appId, 0) << cti_error_str();
	EXPECT_EQ(cti_appIsValid(appId), true) << cti_error_str();

	// get app output
	{ std::string line;
		ASSERT_TRUE(std::getline(pipein, line));
		EXPECT_EQ(line, "cat");
	}
}

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
	ASSERT_GT(appId, 0) << cti_error_str();
	EXPECT_EQ(cti_appIsValid(appId), true) << cti_error_str();

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
	ASSERT_GT(appId, 0) << cti_error_str();
	EXPECT_EQ(cti_appIsValid(appId), true) << cti_error_str();

	// create app's session
	auto const sessionId = cti_createSession(appId);
	ASSERT_EQ(cti_sessionIsValid(sessionId), true) << cti_error_str();

	// cleanup
	EXPECT_EQ(cti_destroySession(sessionId), SUCCESS) << cti_error_str();
	EXPECT_EQ(cti_releaseAppBarrier(appId), SUCCESS) << cti_error_str();
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
	ASSERT_GT(appId, 0) << cti_error_str();
	EXPECT_EQ(cti_appIsValid(appId), true) << cti_error_str();

	// create app's session
	auto const sessionId = cti_createSession(appId);
	ASSERT_EQ(cti_sessionIsValid(sessionId), true) << cti_error_str();

	// create manifest
	auto const manifestId = cti_createManifest(sessionId);
	ASSERT_EQ(cti_manifestIsValid(manifestId), true) << cti_error_str();

	// cleanup
	EXPECT_EQ(cti_destroySession(sessionId), SUCCESS) << cti_error_str();
	EXPECT_EQ(cti_releaseAppBarrier(appId), SUCCESS) << cti_error_str();
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
	ASSERT_GT(appId, 0) << cti_error_str();
	EXPECT_EQ(cti_appIsValid(appId), true) << cti_error_str();

	// create app's session
	auto const sessionId = cti_createSession(appId);
	ASSERT_EQ(cti_sessionIsValid(sessionId), true) << cti_error_str();

	// run printing daemons
	testSocketDaemon(sessionId, "../test_support/one_socket", "1");

	// cleanup
	EXPECT_EQ(cti_destroySession(sessionId), SUCCESS) << cti_error_str();
	EXPECT_EQ(cti_releaseAppBarrier(appId), SUCCESS) << cti_error_str();
}
