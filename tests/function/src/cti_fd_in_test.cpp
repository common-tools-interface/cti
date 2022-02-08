#include "cti_fe_function_test.hpp"

// Test that an app can read input from file descriptor

int main(int argc, char* argv[]) {
    // set up string contents
    auto const echoString = std::to_string(getpid()) + "\n";

    int stdin_pipe[2];
    int stdout_pipe[2];
    int r = 0;

    r = pipe(stdin_pipe);
    assert_true(r == 0, "Failed to create a pipe.");
    r = pipe(stdout_pipe);
    assert_true(r == 0, "Failed to create a pipe.");

    FILE *job_stdout = fdopen(stdout_pipe[0], "r");
    assert_true(job_stdout != nullptr, "Failed to open pipe for reading.");

    // set up launch arguments
    std::vector<std::string> appArgv = createSystemArgv(argc, argv, {"./support/mpi_wrapper", "/usr/bin/cat"});
    auto const  stdoutFd = stdout_pipe[1];
    auto const  stderrFd = -1;
    auto const  stdinFd  = stdin_pipe[0];
    char const* chdirPath = nullptr;
    char const* const* envList  = nullptr;

    CTIFEFunctionTest app;

    // launch app
    auto const appId = app.watchApp(cti_launchAppBarrier_fd(cstrVector(appArgv).data(), stdoutFd, stderrFd, stdinFd, chdirPath, envList));
    assert_true(appId > 0, cti_error_str());
    assert_true(cti_appIsValid(appId) == true, cti_error_str());

    assert_true(cti_releaseAppBarrier(appId) == SUCCESS, cti_error_str());

    // write app input
    write(stdin_pipe[1], echoString.c_str(), echoString.length() + 1);
    close(stdin_pipe[1]);

    // get app output
    char buf[64];
    memset(buf, '\0', 64);

    assert_true(fgets(buf, sizeof(buf) - 1, job_stdout) != nullptr, "Failed to read app output from pipe.");
    assert_true(std::string(buf) == echoString, "buf != echoString");

    return 0;
}

