/******************************************************************************\
 * cti_archive_unit_test.hpp - Archive unit tests for CTI
 *
 * Copyright 2019 Cray Inc. All Rights Reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
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
