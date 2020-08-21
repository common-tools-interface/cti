/******************************************************************************\
 * cti_be_unit_test.cpp - be unit tests for CTI
 *
 * (C) Copyright 2019-2020 Hewlett Packard Enterprise Development LP.
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
#include "cti_argv_defs.hpp"

#include <unordered_set>
#include <fstream>
#include <algorithm>
#include <fcntl.h>

#include "cti_be_unit_test.hpp"
#include "common_tools_version.h"

using ::testing::Return;
using ::testing::_;
using ::testing::Invoke;
using ::testing::WithoutArgs;

CTIBeUnitTest::CTIBeUnitTest()
{
}

CTIBeUnitTest::~CTIBeUnitTest()
{
}

/******************************************
*             CTI_BE NONE TESTS           *
******************************************/


TEST_F(CTIBeUnitTest, cti_be_getAppIdNone)
{
    ASSERT_EQ(cti_be_getAppId(), nullptr);
}

TEST_F(CTIBeUnitTest, cti_be_current_wlmNone)
{
    ASSERT_STREQ("No WLM detected", cti_be_wlm_type_toString(cti_be_current_wlm()));
}

TEST_F(CTIBeUnitTest, cti_be_versionNone)
{
    const char* cti_package_version_str = CTI_PACKAGE_VERSION;
    int cti_package_major = CTI_PACKAGE_MAJOR;
    int cti_package_minor = CTI_PACKAGE_MINOR;
    int cti_package_revision = CTI_PACKAGE_REVISION;
    const char* cti_be_version_str = CTI_BE_VERSION;
    int cti_be_current = CTI_BE_CURRENT;
    int cti_be_age = CTI_BE_AGE;
    int cti_be_revision = CTI_BE_REVISION;
    const char* cti_fe_version_str = CTI_FE_VERSION;
    int cti_fe_current = CTI_FE_CURRENT;
    int cti_fe_age = CTI_FE_AGE;
    int cti_fe_revision = CTI_FE_REVISION;

    ASSERT_GE(cti_package_major, 0);
    ASSERT_GE(cti_package_minor, 0);
    ASSERT_GE(cti_package_revision, 0);
    ASSERT_GE(cti_be_current, 0);
    ASSERT_GE(cti_be_age, 0);
    ASSERT_LE(cti_be_age, cti_be_current);
    ASSERT_GE(cti_be_revision, 0);
    ASSERT_GE(cti_be_current, 0);
    ASSERT_GE(cti_be_age, 0);
    ASSERT_LE(cti_be_age, cti_be_current);
    ASSERT_GE(cti_be_revision,0);

    ASSERT_NE(cti_be_version(), nullptr);
}

TEST_F(CTIBeUnitTest, cti_be_findAppPidsNone)
{
    ASSERT_EQ(cti_be_findAppPids(), nullptr);
}

TEST_F(CTIBeUnitTest, cti_be_destroyPidListNone)
{
    ASSERT_NO_THROW(cti_be_destroyPidList(nullptr));
}

TEST_F(CTIBeUnitTest, cti_be_getNodeHostnameNone)
{
    ASSERT_EQ(cti_be_getNodeHostname(), nullptr);
}

TEST_F(CTIBeUnitTest, cti_be_getNodeFirstPE)
{
    ASSERT_EQ(cti_be_getNodeFirstPE(), -1);
}

TEST_F(CTIBeUnitTest, cti_be_getNodePEsNone)
{
    ASSERT_EQ(cti_be_getNodePEs(), -1);
}

TEST_F(CTIBeUnitTest, cti_be_getRootDirNone)
{
    ASSERT_EQ(cti_be_getRootDir(), nullptr);
}

TEST_F(CTIBeUnitTest, cti_be_getBinDirNone)
{
    ASSERT_EQ(cti_be_getBinDir(), nullptr);
}

TEST_F(CTIBeUnitTest, cti_be_getLibDirNone)
{
    ASSERT_EQ(cti_be_getLibDir(), nullptr);
}

TEST_F(CTIBeUnitTest, cti_be_getFileDirNone)
{
    ASSERT_EQ(cti_be_getFileDir(), nullptr);
}

/*TEST_F(CTIBeUnitTest, cti_be_getTmpDir)
{
    ASSERT_EQ(cti_be_getTmpDir(), nullptr);
}*/
