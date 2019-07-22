/******************************************************************************\
 * unit_tests.cpp - Unit test driver
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

#include "cti_defs.h"

#include <stdlib.h>

// CTI Transfer includes
#include "frontend/cti_transfer/Manifest.hpp"
#include "frontend/cti_transfer/Session.hpp"

// Unit Test includes
#include "cti_fe_unit_test.hpp"
#include "cti_archive_unit_test.hpp"
#include "cti_session_unit_test.hpp"
#include "cti_manifest_unit_test.hpp"

class CTI_Environment : public ::testing::Environment
{
public:
    // Ensure we override the install env var to our prefix
    void SetUp() { setenv(CTI_BASE_DIR_ENV_VAR, INSTALL_PATH, 1); }
};

int main(int argc, char **argv) {
    ::testing::Environment* const cti_env = ::testing::AddGlobalTestEnvironment(new CTI_Environment);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
