/******************************************************************************\
 * cti_fe_unit_test.hpp - Frontend unit tests for CTI
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
