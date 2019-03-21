#pragma once

#include "gtest/gtest.h"

#include "include/cray_tools_fe.h"

// The fixture for testing C interface results
class CTIFEIfaceTest : public ::testing::Test
{
protected:
	CTIFEIfaceTest()
	{}

	~CTIFEIfaceTest() override
	{}
};

static const auto SUCCESS = int{0};
static const auto FAILURE = int{1};
