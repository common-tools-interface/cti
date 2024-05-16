#include "cti_fe_common.h"

#include "cti_fe_function_test.hpp"

// Test that an MPMD app can run a tool daemon

int main(int argc, char *argv[])
{
    switch (cti_current_wlm()) {
        case CTI_WLM_SLURM: break;
        default:
            std::cerr << "MPMD daemon test only valid for Slurm" << std::endl;
    }

    // set up app
    auto appArgv = createSystemArgv(argc, argv, {"-n2", "./src/support/hello_mpi"});
    auto appArgv2 = createSystemArgv(argc, argv, {"-n1", "./src/support/hello_mpi"});
    appArgv.push_back(":");
    appArgv.insert(appArgv.end(), appArgv2.begin(), appArgv2.end());

    auto const  stdoutFd = 1;
    auto const  stderrFd = 2;
    char const* inputFile = nullptr;
    char const* chdirPath = nullptr;
    char const* const* envList  = nullptr;

    CTIFEFunctionTest app;

    // create app
    auto const appId = app.watchApp(cti_launchAppBarrier(cstrVector(appArgv).data(), stdoutFd, stderrFd, inputFile, chdirPath, envList));
    assert_true(appId > 0, cti_error_str());
    assert_true(cti_appIsValid(appId) == true, cti_error_str());
    std::cerr << "Safe from launch timeout.\n";

    // Print application information
    cti_test_fe(appId);

    // create app's session
    auto const sessionId = cti_createSession(appId);
    assert_true(cti_sessionIsValid(sessionId) == true, cti_error_str());

    // run printing daemons
    // One copy on each node, Slurm MPMD runs one node per hetereogeneous job portion
    testSocketDaemon(sessionId, "./src/support/one_socket", {}, {}, "1", 2);

    // cleanup
    assert_true(cti_destroySession(sessionId) == SUCCESS, cti_error_str());
    assert_true(cti_releaseAppBarrier(appId) == SUCCESS, cti_error_str());

    return 0;
}
