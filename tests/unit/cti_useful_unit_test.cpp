/******************************************************************************\
 * cti_useful_unit_test.cpp - /Useful unit tests for CTI
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

#include <unordered_set>
#include <fstream>
#include <algorithm>

#include "cti_useful_unit_test.hpp"

using ::testing::Return;
using ::testing::_;
using ::testing::Invoke;
using ::testing::WithoutArgs;

CTIUsefulUnitTest::CTIUsefulUnitTest()
{
}

CTIUsefulUnitTest::~CTIUsefulUnitTest()
{
}

// test that
TEST_F(CTIUsefulUnitTest, cti_argv)
{ 
    // Begin ManagedArgv tests
    cti::ManagedArgv argv1;

    // test that an argv with no data is of size 0
    ASSERT_EQ(argv1.size(), 1);
    char** argv_data = nullptr;

    // test that an argv with no data has a nullptr for its data 
    argv_data = argv1.get();
    ASSERT_EQ(argv_data[0], nullptr);

    // test that after adding a string these have changed as expected
    std::string arg0 = "arg0";
    argv1.add(arg0);
    ASSERT_EQ(argv1.size(), 2);

    // test that this new arg can be gotten using .get()
    argv_data = argv1.get();
    ASSERT_STREQ(argv_data[0], "arg0");

    // add a string via a character array and check for approritate results
    char arg1[5] = "arg1";
    arg1[4] = '\0';
    argv1.add(arg1);
    ASSERT_EQ(argv1.size(), 3);
 
    // test that this data can be retrieved
    argv_data = argv1.get();
    ASSERT_STREQ(argv_data[1], "arg1");

    // test that initializing a managedArgv with an initialization list works as expected
    std::initializer_list<std::string const> strlist {"0","1","2","3","4"};
    cti::ManagedArgv argv2(strlist);
    ASSERT_EQ(argv2.size(), 6);
    
    // test that all data is present
    char** argv_data2 = argv2.get();
    for(int i = 0; i < 5; i++) {
        ASSERT_STREQ(argv_data2[i], std::to_string(i).c_str());
    }

    /**********************************************************************************/
    // test additional argv classes
}
/*
// test that
TEST_F(CTIUsefulUnitTest, cti_dlopen)
{ 
}

// test that
TEST_F(CTIUsefulUnitTest, cti_execvp)
{ 
}

// test that
TEST_F(CTIUsefulUnitTest, cti_log)
{ 
}

// test that
TEST_F(CTIUsefulUnitTest, cti_path)
{ 
}

// test that
TEST_F(CTIUsefulUnitTest, cti_split)
{ 
}

// test that11111111111111
TEST_F(CTIUsefulUnitTest, cti_wrappers)
{ 
}
*/
