/******************************************************************************\
 * cti_session_unit_test.hpp - Session unit tests for CTI
 *
 * Copyright 2019-2020 Hewlett Packard Enterprise Development LP.
 * SPDX-License-Identifier: Linux-OpenIB
 ******************************************************************************/

#pragma once

#include <memory>

#include "frontend/cti_fe_iface.hpp"

#include "MockFrontend/Frontend.hpp"

#include "frontend/transfer/Session.hpp"
#include "frontend/transfer/Manifest.hpp"

#include "cti_fe_unit_test.hpp"

// The fixture for unit testing the session
class CTISessionUnitTest : public CTIAppUnitTest
{
protected: // variables
    std::shared_ptr<Session> sessionPtr;
    std::vector<std::string> file_names;

    // string constants
    const std::string TEST_FILE_NAME = "archive_test_file";

protected: // interface
    CTISessionUnitTest();
    ~CTISessionUnitTest();
};
