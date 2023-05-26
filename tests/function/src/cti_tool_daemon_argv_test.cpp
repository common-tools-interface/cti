#include "cti_fe_function_test.hpp"

// Test edge cases in tool daemon argv
// - PE-48156

using namespace std::string_literals;

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

    // run printing daemons
    // Pass empty string before final arg to excercise PE-48156
    // Mix in an env list to test out more possible serialization situations
    // testSocketDamon will append nullptr on the end of these as needed
    const auto extra_argv = std::vector<const char*>{"", "", "", "PE-48156", "", "", ""};
    const auto extra_env = std::vector<const char*>{"FOO=BAR", "ZIG=ZAG", "EMPTY_VALUE_IS_VALID="};

    testSocketDaemon(sessionId, "./src/support/one_socket", extra_argv, {}, "1");
    testSocketDaemon(sessionId, "./src/support/one_socket", extra_argv, extra_env, "1");
    testSocketDaemon(sessionId, "./src/support/one_socket", {}, extra_env, "1");

    // cleanup
    assert_true(cti_destroySession(sessionId) == SUCCESS, cti_error_str());
    assert_true(cti_releaseAppBarrier(appId) == SUCCESS, cti_error_str());

    return 0;
}
