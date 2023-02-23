#include "cti_fe_function_test.hpp"

#include <linux/limits.h>

// Test that LD_PRELOAD is restored to environment of job
// one_socket is dynamically linked to message_one/libmessage.so
// libmessage implements get_message() that will return a value of 1, then sent over socket to FE.
// The test will first verify that one_socket normally sends a value of 1.
// Then, it will LD_PRELOAD message_two/libmessage.so, which implements get_message() returning value 2.
// The test will then verify that LD_PRELOAD overrides the get_message() impl. to send a value of 2.
int main(int argc, char* argv[])
{
    // Wait for any previous cleanups to finish (see PE-26018)
    sleep(5);
    auto port = std::string{};

    // Get address accessible from compute node
    auto const address = getExternalAddress();

    // build 'server' socket
    auto const test_socket = bindAny(address);

    // Begin listening on socket
    assert_true(listen(test_socket, 1) == 0, "Failed to listen on test_socket socket");

    // get my sockets info
    struct sockaddr_in sa;
    socklen_t sa_len = sizeof(sa);
    memset(&sa, 0, sizeof(sa));
    assert_true(getsockname(test_socket, (struct sockaddr*) &sa, &sa_len) == 0, "getsockname");
    port = std::to_string(ntohs(sa.sin_port));

    // doing the C way to get cwd so we don't have to include internal headers
    char buf[PATH_MAX + 1];
    auto const cwd_cstr = getcwd(buf, PATH_MAX);
    assert_true(cwd_cstr != nullptr, "getcwd failed.");
    std::string cwd = std::string(cwd_cstr);

    // Get program and library paths
    auto const testSupportPath = cwd + "/src/support";
    auto const oneSocketPath = testSupportPath + "/one_socket";
    auto const messageTwoPath = testSupportPath + "/message_two/libmessage.so";
    auto const ldPreload = "LD_PRELOAD=" + messageTwoPath;
    auto       ldLibPath = "LD_LIBRARY_PATH=" + testSupportPath + "message_one";

    if (std::getenv("LD_LIBRARY_PATH") != nullptr) {
        if (std::string(std::getenv("LD_LIBRARY_PATH")) != "") {
            ldLibPath += ":" + std::string(std::getenv("LD_LIBRARY_PATH"));
        }
    }

    std::cout << "Lib path is: " << ldLibPath << std::endl;

    { // Launch application without preload, expect response of 1
        // set up app
        auto const  appArgv = createSystemArgv(argc, argv, {"./src/support/mpi_wrapper", oneSocketPath, address, port}); auto const  stdoutFd = -1;
        auto const  stderrFd = -1;
        char const* inputFile = nullptr;
        char const* chdirPath = nullptr;
        char const* envList[] = {ldLibPath.c_str(), nullptr};

        CTIFEFunctionTest app;

        // create app
        auto const appId = app.watchApp(cti_launchAppBarrier(cstrVector(appArgv).data(), stdoutFd, stderrFd, inputFile, chdirPath, envList));
        // don't emit safe from launch timeout until second launch is completed
        // std::cerr << "Safe from launch timeout.\n";
        assert_true(appId > 0, cti_error_str());
        assert_true(cti_appIsValid(appId) == true, cti_error_str());

        assert_true(cti_releaseAppBarrier(appId) == SUCCESS, cti_error_str());

        // count number of sockets launched
        int num_pes   = cti_getNumAppPEs(appId);
        assert_true(num_pes > 0, cti_error_str());
        std::cout << num_pes << " sockets launched...\n";

        // accept recently launched applications connection
        int app_socket;
        struct sockaddr_in wa;
        socklen_t wa_len = sizeof(wa);

        for (int i = 0; i < num_pes; ++i) {
            app_socket = accept(test_socket, (struct sockaddr*) &wa, &wa_len);
            assert_true(app_socket > 0, "socket");

            std::cout << "Got something...\n";

            // read data returned from app
            char buffer[16] = {0};
            int length = read(app_socket, buffer, 16);
            std::cout << "Read " << length << " bytes.\n";
            assert_true(length < 16, "length >= 16");
            buffer[length] = '\0';

            std::cout << "Got: " << buffer << std::endl;

            // check for correctness
            assert_true(strcmp(buffer, "1") == 0, "incorrect number returned");
        }
    }

    std::cout << "Finished part 1\n";
    std::cout << "Lib path is: " << ldLibPath << std::endl;
    std::cout << "ldPreload path is: " << ldPreload << std::endl;

    { // Launch application with preload, expect response of 2
        // set up app
        auto const  appArgv = createSystemArgv(argc, argv, {"./src/support/mpi_wrapper", oneSocketPath.c_str(), address.c_str(), port.c_str()});
        auto const  stdoutFd = -1;
        auto const  stderrFd = -1;
        char const* inputFile = nullptr;
        char const* chdirPath = nullptr;
        char const* const envList[] = {ldLibPath.c_str(), ldPreload.c_str(), nullptr};

        CTIFEFunctionTest app;

        // create app
        auto const appId = app.watchApp(cti_launchAppBarrier(cstrVector(appArgv).data(), stdoutFd, stderrFd, inputFile, chdirPath, envList));
        assert_true(appId > 0, cti_error_str());
        assert_true(cti_appIsValid(appId) == true, cti_error_str());
        std::cerr << "Safe from launch timeout.\n";

        assert_true(cti_releaseAppBarrier(appId) == SUCCESS, cti_error_str());

        // count number of sockets launched
        int num_pes   = cti_getNumAppPEs(appId);
        assert_true(num_pes > 0, cti_error_str());
        std::cout << num_pes << " sockets launched...\n";

        // accept recently launched applications connection
        int app_socket;
        struct sockaddr_in wa;
        socklen_t wa_len = sizeof(wa);

        for (int i = 0; i < num_pes; ++i) {
            app_socket = accept(test_socket, (struct sockaddr*) &wa, &wa_len);
            assert_true(app_socket > 0, "socket");

            // read data returned from app
            char buffer[16] = {0};
            int length = read(app_socket, buffer, 16);
            std::cout << "Read " << length << " bytes.\n";
            assert_true(length < 16, "length >= 16");
            buffer[length] = '\0';

            std::cout << "Got: " << buffer << std::endl;

            // check for correctness
            assert_true(strcmp(buffer, "2") == 0, "incorrect number returned");
        }
    }

    // close socket
    close(test_socket);
}

