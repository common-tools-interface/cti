#include "cti_fe_function_test.hpp"

// Test that an app can forward environment variables
int main(int argc, char* argv[]) {
    // set up string contents
    auto const envVar = std::string{"CTI_TEST_VAR"};
    auto const envVal = std::to_string(getpid());
    auto const envString = envVar + "=" + envVal;

    int pipes[2];
    int r = 0;

    r = pipe(pipes);
    assert_true(r == 0, "Failed to create a pipe.");

    FILE *piperead = fdopen(pipes[0], "r");
    assert_true(piperead != nullptr, "Failed to open pipe for reading.");

    // set up launch arguments
    auto const  appArgv = createSystemArgv(argc, argv, {"./src/support/mpi_wrapper", "/usr/bin/env"});
    auto const  stdoutFd = pipes[1];
    auto const  stderrFd = -1;
    char const* inputFile = nullptr;
    char const* chdirPath = nullptr;
    char const* const envList[] = {envString.c_str(), nullptr};

    CTIFEFunctionTest app;

    // launch app
    auto const appId = app.watchApp(cti_launchAppBarrier(cstrVector(appArgv).data(), stdoutFd, stderrFd, inputFile, chdirPath, envList));
    assert_true(appId > 0, cti_error_str());
    assert_true(cti_appIsValid(appId) == true, cti_error_str());
    std::cerr << "Safe from launch timeout.\n";

    assert_true(cti_releaseAppBarrier(appId) == SUCCESS, cti_error_str());

    char buf[512];
    memset(buf, '\0', 512);

    // count number of pes launched
    int num_pes = cti_getNumAppPEs(appId);
    assert_true(num_pes > 0, cti_error_str());
    std::cout << num_pes << " pes launched...\n";

    // get app output
    bool found = false;
    for (int i = 0; i < num_pes; ++i) {
        while (fgets(buf, 512, piperead) != nullptr) {
            std::string line = std::string(buf);
            auto const var = line.substr(0, line.find('='));
            auto const val = line.substr(line.find('=') + 1);

            if (!var.compare(envVar) && !val.compare(envVal + '\n')) {
                found = true;
                break;
            }
        }
        assert_true(found, "found != true");
    }

    fclose(piperead);
    close(pipes[0]);
    close(pipes[1]);

    return 0;
}

