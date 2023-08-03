/******************************************************************************\
 * cti_useful_unit_test.hpp - /Useful unit tests for CTI
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

#include "useful/cti_argv.hpp"
#include "useful/cti_dlopen.hpp"
#include "useful/cti_execvp.hpp"
#include "useful/cti_log.h"
#include "useful/cti_path.h"
#include "useful/cti_stack.h"
#include "useful/cti_wrappers.hpp"

// include google testing files
#include "gmock/gmock.h"
#include "gtest/gtest.h"

// the fixture for unit testing the C archive interface
class CTIUsefulUnitTest : public ::testing::Test
{
protected:


protected:
    CTIUsefulUnitTest();
    ~CTIUsefulUnitTest();
};
