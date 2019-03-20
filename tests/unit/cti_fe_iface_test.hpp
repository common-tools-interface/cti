#pragma once

#include "gtest/gtest.h"

#include "include/cray_tools_fe.h"

// The fixture for testing C interface results
class cti_fe_ifaceTest : public ::testing::Test
{
	protected:
		cti_fe_ifaceTest()
		{}

		~cti_fe_ifaceTest() override
		{}
};

static const auto SUCCESS = int{0};
static const auto FAILURE = int{1};
