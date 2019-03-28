#pragma once

#include "gtest/gtest.h"

#include "include/cray_tools_fe.h"

static constexpr auto SUCCESS = int{0};
static constexpr auto FAILURE = int{1};

static constexpr auto APP_ERROR = cti_app_id_t{0};

// The fixture for testing C interface function
class CTIFEFunctionTest : public ::testing::Test
{
private:
	cti_app_id_t runningApp;

protected:
	CTIFEFunctionTest()
		: runningApp{APP_ERROR}
	{}

	~CTIFEFunctionTest() override
	{
		if (runningApp != APP_ERROR) {
			// send sigkill to app
			if (cti_killApp(runningApp, SIGKILL) != SUCCESS) {
				std::cerr << "warning: failed to kill app on test cleanup" << std::endl;
			}

			// force deregister app
			cti_deregisterApp(runningApp);
		}
	}

	// note the running app ID so that we can clean it up later
	cti_app_id_t watchApp(cti_app_id_t appId)
	{
		if (runningApp == APP_ERROR) {
			runningApp = appId;
			return runningApp;
		} else {
			throw std::logic_error("assigned multiple apps to a test");
		}
	}
};
