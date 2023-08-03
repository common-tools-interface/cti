/******************************************************************************\
 * cti_be_unit_test.hpp - be unit test for CTI
 *
 * Copyright 2019-2020 Hewlett Packard Enterprise Development LP.
 * SPDX-License-Identifier: Linux-OpenIB
 ******************************************************************************/

#pragma once

#include <unistd.h>
#include <stdio.h>

#include <memory>
#include <string>
#include <vector>

#include "backend/cti_be.h"

// include google testing files
#include "gmock/gmock.h"
#include "gtest/gtest.h"

// the fixture for unit testing the C archive interface
class CTIBeUnitTest : public ::testing::Test
{
protected:


protected:
    CTIBeUnitTest();
    ~CTIBeUnitTest();
};
