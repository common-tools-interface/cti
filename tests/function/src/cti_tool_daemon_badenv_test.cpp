#include "cti_fe_function_test.hpp"

// Test edge cases in tool daemon env vars

using namespace std::string_literals;

void testEnv(cti_session_id_t session, const char* env_str) {
    const auto argv = std::vector<const char*>{nullptr};
    const auto env = std::vector<const char*>{env_str, nullptr};
    const auto manifest = cti_createManifest(session);
    assert_true(cti_manifestIsValid(manifest), cti_error_str());

    assert_true(
        cti_execToolDaemon(manifest, "/usr/bin/hostname", argv.data(), env.data()) != SUCCESS,
        "failed to detect bad env var: "s + env_str
    );
    std::cout << "Sucessfully caught error: " << cti_error_str() << std::endl;
}

int main(int argc, char *argv[]) {
    // set up app
    auto const  appArgv = createSystemArgv(argc, argv, {"./src/support/hello_mpi"});
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

    // create app's session
    auto const sessionId = cti_createSession(appId);
    assert_true(cti_sessionIsValid(sessionId) == true, cti_error_str());

    // test that CTI detects bad environment variables
    testEnv(sessionId, "");
    testEnv(sessionId, "=");
    testEnv(sessionId, "=EMPTYNAME");

    // cleanup
    assert_true(cti_destroySession(sessionId) == SUCCESS, cti_error_str());
    assert_true(cti_releaseAppBarrier(appId) == SUCCESS, cti_error_str());

    return 0;
}
