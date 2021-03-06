/******************************************************************************\
 * unit_tests.cpp - Unit test driver
 *
 * Copyright 2019-2020 Hewlett Packard Enterprise Development LP.
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

#include "cti_defs.h"

#include <stdlib.h>

// CTI Transfer includes
#include "frontend/transfer/Manifest.hpp"
#include "frontend/transfer/Session.hpp"

// Unit Test includes
#include "cti_fe_unit_test.hpp"
#include "cti_archive_unit_test.hpp"
#include "cti_session_unit_test.hpp"
#include "cti_manifest_unit_test.hpp"
#include "cti_useful_unit_test.hpp"
#include "cti_be_unit_test.hpp"

class CTI_Environment : public ::testing::Environment
{
public:
    void SetUp() {
        // Ensure we override the install env var to our prefix
        setenv(CTI_BASE_DIR_ENV_VAR, INSTALL_PATH, 1);
        // Set a dummy LD_PRELOAD to ensure frontend removes it
        setenv("LD_PRELOAD", "/dev/null", 1);
    }
};

int main(int argc, char **argv) {
    ::testing::Environment* const cti_env = ::testing::AddGlobalTestEnvironment(new CTI_Environment);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
