#include "cti_fe_function_test.hpp"

// Test that an app can create a transfer manifest

int main(int argc, char* argv[]) {
    // set up app
    auto const  appArgv = createSystemArgv(argc, argv, {"./support/hello_mpi"});
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

    // create app's session
    auto const sessionId = cti_createSession(appId);
    assert_true(cti_sessionIsValid(sessionId) == true, cti_error_str());

    // create manifest
    auto const manifestId = cti_createManifest(sessionId);
    assert_true(cti_manifestIsValid(manifestId) == true, cti_error_str());

    // cleanup
    assert_true(cti_destroySession(sessionId) == SUCCESS, cti_error_str());
    assert_true(cti_releaseAppBarrier(appId) == SUCCESS, cti_error_str());

    return 0;
}
