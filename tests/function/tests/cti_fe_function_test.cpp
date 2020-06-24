/*
 * Copyright 2019 Cray Inc. All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/stat.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netdb.h>

#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <memory>

#include "cti_fe_function_test.hpp"

#include "common_tools_fe.h"

#include "useful/cti_execvp.hpp"
#include "useful/cti_wrappers.hpp"

/* cti frontend C interface tests */

// Find my external IP
static auto getExternalAddress()
{
    // Get information structs about all network interfaces
    struct ifaddrs *ifaddr = nullptr;
    if (getifaddrs(&ifaddr) < 0) {
        throw std::runtime_error(strerror(errno));
    }

    // Find the first IP address that isn't localhost
    for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) {
            continue;
        }

        // Limit to IPv4 and IPv6
        auto const family = ifa->ifa_addr->sa_family;
        if ((family == AF_INET) || (family == AF_INET6)) {

            // Skip if loopback
            if (ifa->ifa_flags & IFF_LOOPBACK) {
                continue;
            }

            // Get hostname for interface
            char address[NI_MAXHOST];
            auto const sockaddr_size = (family == AF_INET)
                ? sizeof(struct sockaddr_in)
                : sizeof(struct sockaddr_in6);

            if (auto const rc = getnameinfo(ifa->ifa_addr, sockaddr_size,
                address, NI_MAXHOST, nullptr, 0, NI_NUMERICHOST)) {

                // Clean up ifaddr
                freeifaddrs(ifaddr);

                throw std::runtime_error(strerror(errno));
            }

            // Clean up ifaddr
            freeifaddrs(ifaddr);

            return std::string{address};
        }
    }

    // Clean up ifaddr
    freeifaddrs(ifaddr);

    throw std::runtime_error("failed to find any external address");
}

static auto bindAny(std::string const& address)
{
    // setup hints
    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICSERV;

    // uses external_ip in order to bind socket to external IP and not localhost
    // if NULL is used this will ALWAYS give localhost which is not non-wbox compatible
    struct addrinfo *raw_listener = nullptr;
    if (auto const rc = getaddrinfo(address.c_str(), "0", &hints, &raw_listener)) {
        throw std::runtime_error(gai_strerror(rc));
    }
    auto listener = cti::take_pointer_ownership(std::move(raw_listener), freeaddrinfo);
    raw_listener = nullptr;

    // Create the socket
    auto const socketFd = socket(listener->ai_family, listener->ai_socktype, listener->ai_protocol);
    if (socketFd < 0) {
        throw std::runtime_error(strerror(errno));
    }

    // Bind the socket
    if (bind(socketFd, listener->ai_addr, listener->ai_addrlen) < 0) {
        throw std::runtime_error(strerror(errno));
    }

    return socketFd;
}

static void
testSocketDaemon(cti_session_id_t sessionId, char const* daemonPath, std::vector<char const*> extra_argv, std::string const& expecting) {
    // Wait for any previous cleanups to finish (see PE-26018)
    sleep(5);

    std::cout << "Getting address and starting to listen...\n";
    // Get address accessible from compute node
    auto const address = getExternalAddress();

    // build 'server' socket
    auto const test_socket = bindAny(address);

    // Begin listening on socket
    ASSERT_EQ(listen(test_socket, 1), 0) << "Failed to listen on test_socket socket";

    // get my sockets info
    struct sockaddr_in sa;
    socklen_t sa_len = sizeof(sa);
    memset(&sa, 0, sizeof(sa));
    ASSERT_EQ(getsockname(test_socket, (struct sockaddr*) &sa, &sa_len), 0);

    // build required parameters for launching external app
    {
        std::cout << "Launching app...\n";
        // create manifest and args
        auto const manifestId = cti_createManifest(sessionId);
        ASSERT_EQ(cti_manifestIsValid(manifestId), true) << cti_error_str();
        std::vector<char const*> v_argv = {address.c_str(), std::to_string(ntohs(sa.sin_port)).c_str()};
        v_argv.insert(v_argv.end(), extra_argv.begin(), extra_argv.end());
        v_argv.push_back(nullptr);

        // launch app
        ASSERT_EQ(cti_execToolDaemon(manifestId, daemonPath, v_argv.data(), nullptr), SUCCESS) << cti_error_str();
        std::cout << "App launched. Net info: " << address << " " << std::to_string(ntohs(sa.sin_port)) << "\n";
    }

    // accept recently launched applications connection
    std::cout << "Waiting for communication from app...\n";
    int app_socket;
    struct sockaddr_in wa;
    socklen_t wa_len = sizeof(wa);
    ASSERT_GE(app_socket = accept(test_socket, (struct sockaddr*) &wa, &wa_len), 0);

    // read data returned from app
    std::cout << "Reading data...\n";
    char buffer[16] = {0};
    int length = read(app_socket, buffer, 16);
    ASSERT_LT(length, 16);
    buffer[length] = '\0';

    // check for correctness
    std::cout << "Checking for correctness...\n";
    ASSERT_STREQ(buffer, expecting.c_str());

    // close socket
    std::cout << "Closing socket...\n";
    close(test_socket);
    
    std::cout << "Done!\n";
}

// Test that an app can launch two tool daemons using different libraries with the same name
// This test is at the start to avoid a race condition that causes failure if ran later
TEST_F(CTIFEFunctionTest, DaemonLibDir) {
    // set up app
    char const* argv[] = {"./hello_mpi", nullptr};
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
    testSocketDaemon(sessionId, "../../test_support/one_socket", {}, "1");
    testSocketDaemon(sessionId, "../../test_support/two_socket", {}, "2");

    // cleanup
    EXPECT_EQ(cti_destroySession(sessionId), SUCCESS) << cti_error_str();
    EXPECT_EQ(cti_releaseAppBarrier(appId), SUCCESS) << cti_error_str();
}

// Tests that the frontend type was correctly detected.
TEST_F(CTIFEFunctionTest, HaveValidFrontend) {
    ASSERT_NE(cti_current_wlm(), CTI_WLM_NONE) << cti_error_str();
}

// Test that LD_PRELOAD is restored to environment of job
// one_socket is dynamically linked to message_one/libmessage.so
// libmessage implements get_message() that will return a value of 1, then sent over socket to FE.
// The test will first verify that one_socket normally sends a value of 1.
// Then, it wil LD_PRELOAD message_two/libmessage.so, which implements get_message() returning value 2.
// The test will then verify that LD_PRELOAD overrides the get_message() impl. to send a value of 2.
TEST_F(CTIFEFunctionTest, LdPreloadSet)
{
    // Wait for any previous cleanups to finish (see PE-26018)
    sleep(5);
    auto port = std::string{};

    // Get address accessible from compute node
    auto const address = getExternalAddress();

    // build 'server' socket
    auto const test_socket = bindAny(address);

    // Begin listening on socket
    ASSERT_EQ(listen(test_socket, 1), 0) << "Failed to listen on test_socket socket";

    // get my sockets info
    struct sockaddr_in sa;
    socklen_t sa_len = sizeof(sa);
    memset(&sa, 0, sizeof(sa));
    ASSERT_EQ(getsockname(test_socket, (struct sockaddr*) &sa, &sa_len), 0);
    port = std::to_string(ntohs(sa.sin_port));

    // Get program and library paths
    auto const testSupportPath = cti::cstr::getcwd() + "/../../test_support/";
    auto const oneSocketPath = testSupportPath + "one_socket";
    auto const messageTwoPath = testSupportPath + "message_two/libmessage.so";
    auto const ldPreload = "LD_PRELOAD=" + messageTwoPath;
    auto const ldLibPath = "LD_LIBRARY_PATH=" + testSupportPath + "message_one";

    { // Launch application without preload, expect response of 1
        // set up app
        char const* argv[] = {"./mpi_wrapper", oneSocketPath.c_str(), address.c_str(), port.c_str(), nullptr};
        auto const  stdoutFd = -1;
        auto const  stderrFd = -1;
        char const* inputFile = nullptr;
        char const* chdirPath = nullptr;
        char const* envList[] = {ldLibPath.c_str(), nullptr};

        // create app
        auto const appId = watchApp(cti_launchAppBarrier(argv, stdoutFd, stderrFd, inputFile, chdirPath, envList));
        ASSERT_GT(appId, 0) << cti_error_str();
        EXPECT_EQ(cti_appIsValid(appId), true) << cti_error_str();

        EXPECT_EQ(cti_releaseAppBarrier(appId), SUCCESS) << cti_error_str();

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
        ASSERT_STREQ(buffer, "1");
    }

    { // Launch application with preload, expect response of 2
        // set up app
        char const* argv[] = {"./mpi_wrapper", oneSocketPath.c_str(), address.c_str(), port.c_str(), nullptr};
        auto const  stdoutFd = -1;
        auto const  stderrFd = -1;
        char const* inputFile = nullptr;
        char const* chdirPath = nullptr;
        char const* const envList[] = {ldLibPath.c_str(), ldPreload.c_str(), nullptr};

        // create app
        auto const appId = replaceApp(cti_launchAppBarrier(argv, stdoutFd, stderrFd, inputFile, chdirPath, envList));
        ASSERT_GT(appId, 0) << cti_error_str();
        EXPECT_EQ(cti_appIsValid(appId), true) << cti_error_str();

        EXPECT_EQ(cti_releaseAppBarrier(appId), SUCCESS) << cti_error_str();

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
        ASSERT_STREQ(buffer, "2");
    }

    // close socket
    close(test_socket);
}

// Test that an app can launch successfully
TEST_F(CTIFEFunctionTest, Launch) {
    char const* argv[] = {"sleep", "10", nullptr};
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
    char const* argv[] = {"./hello_mpi", nullptr};
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
    char const* argv[] = {"./mpi_wrapper", "/usr/bin/echo", echoString.c_str(), nullptr};
    auto const  stdoutFd = p.getWriteFd();
    auto const  stderrFd = -1;
    char const* inputFile = nullptr;
    char const* chdirPath = nullptr;
    char const* const* envList  = nullptr;

    // launch app
    auto const appId = watchApp(cti_launchAppBarrier(argv, stdoutFd, stderrFd, inputFile, chdirPath, envList));
    ASSERT_GT(appId, 0) << cti_error_str();
    EXPECT_EQ(cti_appIsValid(appId), true) << cti_error_str();

    EXPECT_EQ(cti_releaseAppBarrier(appId), SUCCESS) << cti_error_str();

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

    char const* argv[] = {"./mpi_wrapper", "/usr/bin/cat", nullptr};
    auto const  stdoutFd = p.getWriteFd();
    auto const  stderrFd = -1;
    char const* inputFile = "../../test_support/inputFileData.txt";
    char const* chdirPath = nullptr;
    char const* const* envList  = nullptr;

    // launch app
    auto const appId = watchApp(cti_launchAppBarrier(argv, stdoutFd, stderrFd, inputFile, chdirPath, envList));
    ASSERT_GT(appId, 0) << cti_error_str();
    EXPECT_EQ(cti_appIsValid(appId), true) << cti_error_str();

    EXPECT_EQ(cti_releaseAppBarrier(appId), SUCCESS) << cti_error_str();

    // get app output
    { std::string line;
        ASSERT_TRUE(std::getline(pipein, line));
        EXPECT_EQ(line, "see InputFile in cti_fe_function_test.cpp");
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
    char const* argv[] = {"./mpi_wrapper", "/usr/bin/env", nullptr};
    auto const  stdoutFd = p.getWriteFd();
    auto const  stderrFd = -1;
    char const* inputFile = nullptr;
    char const* chdirPath = nullptr;
    char const* const envList[] = {envString.c_str(), nullptr};

    // launch app
    auto const appId = watchApp(cti_launchAppBarrier(argv, stdoutFd, stderrFd, inputFile, chdirPath, envList));
    ASSERT_GT(appId, 0) << cti_error_str();
    EXPECT_EQ(cti_appIsValid(appId), true) << cti_error_str();

    EXPECT_EQ(cti_releaseAppBarrier(appId), SUCCESS) << cti_error_str();

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
    char const* argv[] = {"./hello_mpi", nullptr};
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
    char const* argv[] = {"./hello_mpi", nullptr};
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
    char const* argv[] = {"./hello_mpi", nullptr};
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
    testSocketDaemon(sessionId, "../../test_support/one_socket", {}, "1");

    // cleanup
    EXPECT_EQ(cti_destroySession(sessionId), SUCCESS) << cti_error_str();
    EXPECT_EQ(cti_releaseAppBarrier(appId), SUCCESS) << cti_error_str();
}

// Test transferring a file in a manifest
TEST_F(CTIFEFunctionTest, Transfer) {
    char const* argv[] = {"./hello_mpi", nullptr};
    auto const  stdoutFd = -1;
    auto const  stderrFd = -1;
    char const* inputFile = nullptr;
    char const* chdirPath = nullptr;
    char const* const* envList  = nullptr;
    char const* filename = "./testing.info";
    char * file_loc;
    int r;

    auto const myapp = cti_launchAppBarrier(argv, stdoutFd, stderrFd, inputFile, chdirPath, envList);
    ASSERT_NE(myapp, 0) << cti_error_str();

    // Ensure app is valid
    ASSERT_NE(cti_appIsValid(myapp), 0);

    // Create a new session based on the app_id
    auto const mysid = cti_createSession(myapp);
    ASSERT_NE(mysid, 0) << cti_error_str();

    // Ensure session is valid
    ASSERT_NE(cti_sessionIsValid(mysid), 0);

    // Create a manifest based on the session
    auto const mymid = cti_createManifest(mysid);
    ASSERT_NE(mymid, 0) << cti_error_str();

    // Ensure manifest is valid
    ASSERT_NE(cti_manifestIsValid(mymid), 0);

    // Add the file to the manifest
    r = cti_addManifestFile(mymid, filename);
    ASSERT_EQ(r, 0) << cti_error_str();

    // Ensure manifest is valid
    ASSERT_NE(cti_manifestIsValid(mymid), 0);

    // Send the manifest to the compute node
    r = cti_sendManifest(mymid);
    ASSERT_EQ(r, 0) << cti_error_str();

    // Ensure manifest is no longer valid
    ASSERT_EQ(cti_manifestIsValid(mymid), 0);

    // Get the location of the directory where the file now resides on the
    // compute node
    file_loc = cti_getSessionFileDir(mysid);
    ASSERT_NE(file_loc, nullptr) << cti_error_str();
    auto const file = std::string(file_loc) + "/testing.info";

    std::cout << "Sent testing.info to " << file << " on the compute node(s).\n";

    testSocketDaemon(mysid, "../../test_support/remote_filecheck", {file.c_str()}, "1");
    
    EXPECT_EQ(cti_destroySession(mysid), SUCCESS) << cti_error_str();
    EXPECT_EQ(cti_releaseAppBarrier(myapp), SUCCESS) << cti_error_str();
}