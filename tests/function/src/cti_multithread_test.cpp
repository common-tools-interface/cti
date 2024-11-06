#include "cti_fe_function_test.hpp"

#include <signal.h>

#include <vector>
#include <fstream>
#include <thread>

static constexpr auto num_loops = 3;

int main(int argc, char* argv[])
{
	auto appArgv = createSystemArgv(argc, argv, {"./src/support/hello_mpi_wait"});
	auto app = CTIFEFunctionTest{};
	auto appId = app.watchApp(
		cti_launchAppBarrier(cstrVector(appArgv).data(), -1, -1, nullptr, nullptr, nullptr)
	);

	assert_true(appId > 0, cti_error_str());
	assert_true(cti_appIsValid(appId), cti_error_str());
	std::cerr << "Safe from launch timeout.\n";

	auto num_threads = 8;
	if (auto raw_num_threads = ::getenv("CTI_TEST_MAX_THREADS")) {
		num_threads = std::stoi(raw_num_threads);
	}

	std::cout << "Running " << num_threads << " threads of " << num_loops << " operations loops" << std::endl;

	// Run several app operations simultaneously
	auto threads = std::vector<std::thread>{};
	for (auto i = 0; i < num_threads; i++) {

		threads.emplace_back([](cti_app_id_t app_id) {

			// Create session
			auto sid = cti_createSession(app_id);
			assert_true(sid > 0, "failed to create session");

			// Add files to new manifests
			for (auto j = 0; j < num_loops; j++) {
				auto mid = cti_createManifest(sid);
				assert_true(mid > 0, "failed to create manifest");
				assert_true(cti_addManifestBinary(mid, "./src/support/hello_mpi_wait") == 0,
					"failed to add binary to manifest");

				// Ship manifest
				assert_true(cti_sendManifest(mid) == 0, "failed to send manifest");
			}

			// Clean up session
			assert_true(cti_destroySession(sid) == 0, "failed to destroy session");
		}, appId);
	}

	// Join all the threads
	for (auto&& t : threads) {
		t.join();
	}

	return 0;
}
