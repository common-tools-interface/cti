#include "cti_fe_function_test.hpp"

// Test transferring a file in a manifest

int main(int argc, char* argv[]) {
    auto const  appArgv = createSystemArgv(argc, argv, {"./support/hello_mpi"});
    auto const  stdoutFd = -1;
    auto const  stderrFd = -1;
    char const* inputFile = nullptr;
    char const* chdirPath = nullptr;
    char const* const* envList  = nullptr;
    char const* filename = "./static/testing.info";
    char * file_loc;
    int r;

    auto const myapp = cti_launchAppBarrier(cstrVector(appArgv).data(), stdoutFd, stderrFd, inputFile, chdirPath, envList);
    assert_true(myapp != 0, cti_error_str());

    // Ensure app is valid
    assert_true(cti_appIsValid(myapp) == 1, "appIsValid");

    // Create a new session based on the app_id
    auto const mysid = cti_createSession(myapp);
    assert_true(mysid != 0, cti_error_str());

    // Ensure session is valid
    assert_true(cti_sessionIsValid(mysid) != 0, "sessionIsValid");

    // Create a manifest based on the session
    auto const mymid = cti_createManifest(mysid);
    assert_true(mymid != 0, cti_error_str());

    // Ensure manifest is valid
    assert_true(cti_manifestIsValid(mymid) != 0, "manifestIsValid");

    // Add the file to the manifest
    r = cti_addManifestFile(mymid, filename);
    assert_true(r == 0, cti_error_str());

    // Ensure manifest is valid
    assert_true(cti_manifestIsValid(mymid) != 0, "manifestIsValid");

    // Send the manifest to the compute node
    r = cti_sendManifest(mymid);
    assert_true(r == 0, cti_error_str());

    // Ensure manifest is no longer valid
    assert_true(cti_manifestIsValid(mymid) == 0, "manifestIsValid");

    // Get the location of the directory where the file now resides on the
    // compute node
    file_loc = cti_getSessionFileDir(mysid);
    assert_true(file_loc != nullptr, cti_error_str());
    auto const file = std::string(file_loc) + "/testing.info";

    std::cout << "Sent testing.info to " << file << " on the compute node(s).\n";

    testSocketDaemon(mysid, "./support/remote_filecheck", {file.c_str()}, "1");

    assert_true(cti_destroySession(mysid) == SUCCESS, cti_error_str());
    assert_true(cti_releaseAppBarrier(myapp) == SUCCESS, cti_error_str());
}