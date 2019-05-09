/******************************************************************************\
 * cti_fe_unit_test.hpp - Frontend unit tests for CTI
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

#include <memory>

#include "frontend/cti_fe_iface.hpp"

#include "MockFrontend/Frontend.hpp"

App& _cti_getApp(cti_app_id_t const appId);

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
