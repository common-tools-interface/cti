#include "cti_fe_function_test.hpp"

// Test edge cases in launch env vars

using namespace std::string_literals;

void testEnv(const char* const argv[], const char* env_str) {
    const auto env = std::vector<const char*>{env_str, nullptr};

    CTIFEFunctionTest app;

    const auto appId = app.watchApp(cti_launchAppBarrier(argv, 1, 2, nullptr, nullptr, env.data()));
   
    assert_true(appId == 0, "accidental successful launch");
    assert_true(cti_appIsValid(appId) == false, "accidental valid app");
    std::cout << "Task failed successfully: " << cti_error_str() << std::endl;
}

int main(int argc, char *argv[]) {
    const auto argvStrings = createSystemArgv(argc, argv, {"./src/support/hello_mpi"});
    const auto argvPtrs = cstrVector(argvStrings);

    // test that CTI detects bad environment variables
    testEnv(argvPtrs.data(), "");
    testEnv(argvPtrs.data(), "=");
    testEnv(argvPtrs.data(), "=EMPTYNAME");

    return 0;
}
