#pragma once

#include "gtest/gtest.h"

#include "frontend/cti_fe_iface.h"

static constexpr auto SUCCESS = int{0};
static constexpr auto FAILURE = int{1};

static constexpr auto APP_ERROR = cti_app_id_t{0};

// The fixture for unit testing C interface
class CTIFEUnitTest : public ::testing::Test
{
private:

protected:
	CTIFEUnitTest()
	{}

	~CTIFEUnitTest() override
	{}
};
