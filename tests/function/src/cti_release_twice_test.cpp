#include "cti_fe_function_test.hpp"

// Test that an app can't be released twice
int main(int argc, char* argv[]) {
    auto const  appArgv = createSystemArgv(argc, argv, {"./src/support/hello_mpi"});
    auto const  stdoutFd = -1;
    auto const  stderrFd = -1;
    char const* inputFile = nullptr;
    char const* chdirPath = nullptr;
    char const* const* envList = nullptr;

    CTIFEFunctionTest app;

    auto const appId = app.watchApp(cti_launchAppBarrier(cstrVector(appArgv).data(), stdoutFd, stderrFd, inputFile, chdirPath, envList));
    assert_true(appId > 0, cti_error_str());
    assert_true(cti_releaseAppBarrier(appId) == SUCCESS, cti_error_str());
    assert_true(cti_releaseAppBarrier(appId) == FAILURE, cti_error_str());
    std::cerr << "Safe from launch timeout.\n";

    return 0;
}
