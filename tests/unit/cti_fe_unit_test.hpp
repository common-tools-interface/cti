/******************************************************************************\
 * cti_fe_unit_test.hpp - Frontend unit tests for CTI
 *
 * Copyright 2019-2020 Hewlett Packard Enterprise Development LP.
 * SPDX-License-Identifier: Linux-OpenIB
 ******************************************************************************/

#pragma once

#include <memory>

#include "frontend/cti_fe_iface.hpp"

#include "MockFrontend/Frontend.hpp"

static constexpr auto SUCCESS = int{0};
static constexpr auto FAILURE = int{1};

static constexpr auto APP_ERROR = cti_app_id_t{0};
static constexpr auto SESSION_ERROR  = cti_session_id_t{0};
static constexpr auto MANIFEST_ERROR  = cti_manifest_id_t{0};

// The fixture for unit testing the C frontend interface
class CTIFEUnitTest : public ::testing::Test
{
protected:
    CTIFEUnitTest();
    virtual ~CTIFEUnitTest();
};

// The fixture for unit testing the C app interface
class CTIAppUnitTest : public CTIFEUnitTest
{
protected: // variables
    cti_app_id_t const appId;
    std::shared_ptr<MockApp::Nice> mockApp;

protected: // interface
    CTIAppUnitTest();
    ~CTIAppUnitTest();
};
