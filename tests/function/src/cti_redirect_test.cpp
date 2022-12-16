#include "cti_fe_function_test.hpp"

// Test that an app can redirect stdout
int main(int argc, char* argv[]) {
    // set up string contents
    auto const echoString = std::to_string(getpid());

    int pipes[2];
    int r = 0;

    r = pipe(pipes);
    assert_true(r == 0, "Failed to create a pipe.");

    FILE *piperead = fdopen(pipes[0], "r");
    assert_true(piperead != nullptr, "Failed to open pipe for reading.");

    // set up launch arguments
    std::vector<std::string> appArgv = createSystemArgv(argc, argv, {"./src/support/mpi_wrapper", "/usr/bin/echo", echoString.c_str()});
    auto const  stdoutFd = pipes[1];
    auto const  stderrFd = -1;
    char const* inputFile = nullptr;
    char const* chdirPath = nullptr;
    char const* const* envList  = nullptr;

    CTIFEFunctionTest app;

    // launch app
    auto const appId = app.watchApp(cti_launchAppBarrier(cstrVector(appArgv).data(), stdoutFd, stderrFd, inputFile, chdirPath, envList));
    assert_true(appId > 0, cti_error_str());
    assert_true(cti_appIsValid(appId) == true, cti_error_str());
    std::cerr << "Safe from launch timeout.\n";

    assert_true(cti_releaseAppBarrier(appId) == SUCCESS, cti_error_str());

    char buf[64];
    memset(buf, '\0', 64);

    // count number of pes launched
    int num_pes = cti_getNumAppPEs(appId);
    assert_true(num_pes > 0, cti_error_str());
    std::cout << num_pes << " pes launched...\n";

    // get app output
    for (int i = 0; i < num_pes; ++i) {
        assert_true(fgets(buf, 64, piperead) != nullptr, "Failed to read app output from pipe.");
        std::cout << "Got: " << buf;
        assert_true(std::string(buf) == echoString + "\n", "buf != echoString");
    }

    fclose(piperead);
    close(pipes[0]);
    close(pipes[1]);

    return 0;
}

