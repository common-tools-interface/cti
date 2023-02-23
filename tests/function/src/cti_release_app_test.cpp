#include "cti_fe_function_test.hpp"

// Test that an app is still running after releasing it

int main(int argc, char *argv[]) {
    // set up app
    CTIFEFunctionTest app;

    // create app
    auto [app_id, test_socket] = launchSocketApp("./test_support/one_socket", {});
    assert_true(app_id > 0, cti_error_str());
    assert_true(cti_appIsValid(app_id) == true, cti_error_str());

    fprintf(stderr, "Releasing app from barrier\n");
    assert_true(cti_releaseAppBarrier(app_id) == SUCCESS, cti_error_str());
    fprintf(stderr, "Releasing app from CTI\n");
    assert_true(cti_releaseApp(app_id) == SUCCESS, cti_error_str());
    fprintf(stderr, "Deregistering app from CTI\n");
    cti_deregisterApp(app_id);

    // Run socket test
    testSocketApp(app_id, test_socket, "1", 1);

    return 0;
}
