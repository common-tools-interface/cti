/******************************************************************************\
 * cti_manifest_unit_test.hpp - Manifest unit tests for CTI
 *
 * Copyright 2019-2020 Hewlett Packard Enterprise Development LP.
 * SPDX-License-Identifier: Linux-OpenIB
 ******************************************************************************/

#pragma once

#include <memory>
#include <vector>

#include "frontend/cti_fe_iface.hpp"

#include "MockFrontend/Frontend.hpp"

#include "frontend/transfer/Session.hpp"
#include "frontend/transfer/Manifest.hpp"

#include "cti_fe_unit_test.hpp"

// The fixture for unit testing the manifest
class CTIManifestUnitTest : public CTIAppUnitTest
{
protected: // variables
    std::shared_ptr<Session>  sessionPtr;
    std::shared_ptr<Manifest> manifestPtr;
    std::vector<std::string> file_names;

    // vectors to store temp files in
    std::vector<std::string> temp_dir_names;
    std::vector<std::string> temp_file_names;

    // consts for test files
    const std::string TEST_FILE_NAME = "archive_test_file";

protected: // interface
    CTIManifestUnitTest();
    ~CTIManifestUnitTest();
};
