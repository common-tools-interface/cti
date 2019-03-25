#pragma once

#include "gtest/gtest.h"

#include "frontend/cti_fe_iface.h"

#include "MockFrontend/Frontend.hpp"

// internal cti function to set the current frontend
void _cti_setFrontend(std::unique_ptr<Frontend>&& expiring);

static constexpr auto SUCCESS = int{0};
static constexpr auto FAILURE = int{1};

static constexpr auto APP_ERROR = cti_app_id_t{0};

// The fixture for unit testing C interface
class CTIFEUnitTest : public ::testing::Test
{
private:

protected:
	CTIFEUnitTest()
	{
		// manually set the frontend to the custom mock frontend
		_cti_setFrontend(std::make_unique<MockFrontend>());
	}

	~CTIFEUnitTest() override
	{
		// destruct the mock frontend so that final checks can be performed
		_cti_setFrontend(nullptr);
	}
};
