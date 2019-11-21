/******************************************************************************\
 * cti_be_unit_test.cpp - be unit tests for CTI
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

#include "cti_defs.h"
#include "cti_argv_defs.hpp"

#include <unordered_set>
#include <fstream>
#include <algorithm>
#include <fcntl.h>

#include "cti_be_unit_test.hpp"

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
