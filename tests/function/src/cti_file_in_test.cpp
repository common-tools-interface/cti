#include "cti_fe_function_test.hpp"

// Test that an app can read input from a file

int main(int argc, char *argv[]) {

    int pipes[2];
    int r = 0;

    r = pipe(pipes);
    assert_true(r == 0, "Failed to create a pipe.");

    FILE *piperead = fdopen(pipes[0], "r");
    assert_true(piperead != nullptr, "Failed to open pipe for reading.");

    auto const  appArgv = createSystemArgv(argc, argv, {"./support/mpi_wrapper", "/usr/bin/cat"});
    auto const  stdoutFd = pipes[1];
    auto const  stderrFd = -1;
    char const* inputFile = "./static/inputFileData.txt";
    char const* chdirPath = nullptr;
    char const* const* envList  = nullptr;

    CTIFEFunctionTest app;

    // launch app
    auto const appId = app.watchApp(cti_launchAppBarrier(cstrVector(appArgv).data(), stdoutFd, stderrFd, inputFile, chdirPath, envList));
    assert_true(appId > 0, cti_error_str());
    assert_true(cti_appIsValid(appId) == true, cti_error_str());

    assert_true(cti_releaseAppBarrier(appId) == SUCCESS, cti_error_str());

    char buf[128];
    memset(buf, '\0', 128);

    // get app output
    assert_true(fgets(buf, 128, piperead) != nullptr, "Failed to read app output from pipe.");
    std::cout << "Got: " << buf;
    assert_true(std::string(buf) == "see InputFile in cti_fe_function_test.cpp\n", "buf != expected string");

    fclose(piperead);
    close(pipes[0]);
    close(pipes[1]);

    return 0;
}