/******************************************************************************\
 * cti_archive_unit_test.hpp - Archive unit tests for CTI
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

#include "frontend/transfer/Archive.hpp"

#include "useful/cti_wrappers.hpp"

// include google testing files
#include "gmock/gmock.h"
#include "gtest/gtest.h"

// the fixture for unit testing the C archive interface
class CTIArchiveUnitTest : public ::testing::Test
{
protected:
    // used for creating temporary archive file
    cti::temp_file_handle temp_file_path;

    // the archive to be used for testing
    Archive archive;

    // vectors for storing testing files
    std::vector<std::string> file_names;
    std::vector<std::string> dir_names;

    // vectors for storing testing files in temp directory
    std::vector<std::string> temp_dir_names;
    std::vector<std::string> temp_file_names;

    // string constants
    const std::string TEST_DIR_NAME = "u_test";
    const std::string TEST_FILE_NAME = "archive_test_file";

    // other constants
    const int FILE_COUNT = 3;

protected:
    CTIArchiveUnitTest();
    ~CTIArchiveUnitTest();
};
