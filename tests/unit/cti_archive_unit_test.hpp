/******************************************************************************\
 * cti_archive_unit_test.hpp - Archive unit tests for CTI
 *
 * Copyright 2019 Cray Inc.  All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
 *
 ******************************************************************************/

#pragma once

#include <unistd.h>
#include <stdio.h>

#include <memory>
#include <string>
#include <vector>

#include "frontend/cti_transfer/Archive.hpp"

#include "useful/cti_wrappers.hpp"

// include google testing files
#include "gmock/gmock.h"
#include "gtest/gtest.h"

//static constexpr auto SUCCESS = int{0};
//static constexpr auto FAILURE = int{1};

//static constexpr auto MANIFEST_ERROR  = cti_manifest_id_t{0};

// the fixture for unit testing the C archive interface
class CTIArchiveUnitTest : public ::testing::Test
{
protected:
	//TODO: Use shared pointers instead of normal(?)
	// used for creating temporary archive file
	cti::temp_file_handle* temp_file_path;

	// the archive to be used for testing
        Archive* archive;

	// TODO: add vector containing all file names and dirs
        std::vector<std::string> file_suffixes;

protected:
	CTIArchiveUnitTest();
	~CTIArchiveUnitTest();
};
