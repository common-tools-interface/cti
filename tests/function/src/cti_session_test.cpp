#include "cti_fe_function_test.hpp"

// Test that an app can create a transfer session

int main(int argc, char* argv[]) {
    // set up app
    auto const  appArgv = createSystemArgv(argc, argv, {"./src/support/hello_mpi"});
    auto const  stdoutFd = -1;
    auto const  stderrFd = -1;
    char const* inputFile = nullptr;
    char const* chdirPath = nullptr;
    char const* const* envList  = nullptr;

    CTIFEFunctionTest app;

    // create app
    auto const appId = app.watchApp(cti_launchAppBarrier(cstrVector(appArgv).data(), stdoutFd, stderrFd, inputFile, chdirPath, envList));
    assert_true(appId > 0, cti_error_str());
    assert_true(cti_appIsValid(appId) == true, cti_error_str());
    std::cerr << "Safe from launch timeout.\n";

    // create app's session
    auto const sessionId = cti_createSession(appId);
    assert_true(cti_sessionIsValid(sessionId) == true, cti_error_str());

    // cleanup
    assert_true(cti_destroySession(sessionId) == SUCCESS, cti_error_str());
    assert_true(cti_releaseAppBarrier(appId) == SUCCESS, cti_error_str());
}

