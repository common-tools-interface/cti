/******************************************************************************\
 * cti_manifest_unit_test.hpp - Manifest unit tests for CTI
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
#include <vector>

#include "frontend/cti_fe_iface.hpp"

#include "MockFrontend/Frontend.hpp"

#include "frontend/cti_transfer/Session.hpp"
#include "frontend/cti_transfer/Manifest.hpp"

// The fixture for unit testing the C frontend interface
class CTIFEMUnitTest : public ::testing::Test
{
protected:
	CTIFEMUnitTest();
	virtual ~CTIFEMUnitTest();
};

// The fixture for unit testing the manifest
class CTIManifestUnitTest : public CTIFEMUnitTest
{
protected: // variables
	cti_app_id_t const appId;
	MockApp::Nice& mockApp;


	Session* testSession;
	Manifest* testManifest;
	std::vector<std::string> file_suffixes;

	// string constants

        // Consts for to be created test files
        const std::string dirPath = "u_test";
        const std::string test_file_path = "archive_test_file";

protected: // interface
	CTIManifestUnitTest();
	~CTIManifestUnitTest();
};
