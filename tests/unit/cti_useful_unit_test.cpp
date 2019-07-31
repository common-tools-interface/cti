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
#include <fcntl.h>

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
    }
    /**********************************************************************************/
    // test additional argv classes
}

/*
// test that
TEST_F(CTIUsefulUnitTest, cti_dlopen)
{ 
    
}
*/

// test that
TEST_F(CTIUsefulUnitTest, cti_execvp)
{ 
    {
        // test fdbuf
        ASSERT_THROW({
            try {
                cti::FdBuf buf_fail(-1);
            } catch (const std::exception& ex) {
                ASSERT_STREQ("Invalid file descriptor", ex.what());
                throw;
            }
        }, std::invalid_argument);

        int fd = open("testfile.txt", O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
        EXPECT_NO_THROW(cti::FdBuf buf(fd));
        remove("testfile.txt");
    }

    {
        // test FdPair

        // test creation of a fdpair
        ASSERT_NO_THROW(cti::FdPair fdp);

        // test that close fd's behave as exxpected
        cti::FdPair testfdp;
        {
            bool ex = 0;
            try {
                testfdp.closeRead();
            } catch (...) {
                ex = 1;
            }
            ASSERT_EQ(ex,1);
        }

        {
            bool ex = 0;
            try {
                testfdp.closeWrite();
            } catch (...) {
                ex = 1;
            }
            ASSERT_EQ(ex,1);
        }
    }

    {
        // test Pipe
        cti::Pipe testpipe;
        ASSERT_NE(-1, testpipe.getReadFd());
        ASSERT_NE(-1, testpipe.getWriteFd());

        // test close
        ASSERT_NO_THROW(testpipe.closeWrite());
        ASSERT_NO_THROW(testpipe.closeRead());
        {
            bool ex = 0;
            try {
                testpipe.closeRead();
            } catch (...) {
                ex = 1;
            }
            ASSERT_EQ(ex,1);
        }

        {
            bool ex = 0;
            try {
                testpipe.closeWrite();
            } catch (...) {
                ex = 1;
            }
            ASSERT_EQ(ex,1);
        }
    }

    // test that cti::Execvp fails as expected
    {
        std::initializer_list<std::string const> strlist {"it_will"};
        cti::ManagedArgv argv(strlist);
        cti::Execvp test_fail("/this/will/fail", argv.get());
        EXPECT_EQ(test_fail.getExitStatus(), 1);
    }
    
    // test that cti::Execvp works as expected
    {
        std::initializer_list<std::string const> strlist {"-n", "T"};
        cti::ManagedArgv argv(strlist);
        cti::Execvp test("/bin/echo", argv.get());

        // test that output is what is expected
        std::istream& out = test.stream();
        if(out.fail())
            FAIL() << "Failed to get istream";
        if(out.eof())
            FAIL() << "No data to read";
        char result = out.get();
        EXPECT_EQ(result, 'T');

        // test that the exit status is correct
        ASSERT_EQ(test.getExitStatus(), 0);
    }
}

// test that
TEST_F(CTIUsefulUnitTest, cti_log)
{ 
    // test that logs aren't created when no filename is given
    {
        cti_log_t* log_fail = _cti_create_log(NULL, NULL, 0);
        ASSERT_EQ(log_fail, nullptr);
        errno = 0;
    }

    // test that a log is created when proper params are given and works as expected
    {
        cti_log_t* log_succ = _cti_create_log("./", "test_log", 0);
        EXPECT_NE(log_succ, nullptr);
        
        EXPECT_EQ(_cti_write_log(log_succ, "TEST"), 0);
        EXPECT_EQ(_cti_close_log(log_succ), 0);

        // check that data wrote correctly
        std::ifstream check;
        check.open("./dbglog_test_log.0.log", std::ifstream::in);
        if(!check.is_open())
            FAIL() << "Log file created but somehow not openable...";
        std::string res;
        check >> res;
        check.close();
        EXPECT_STREQ(res.c_str(), "TEST");
        remove("./dbglog_test_log.0.log");
    }

    // test the logs hookstdoe function
    {   
        int fout, ferr;
        fout = dup(STDOUT_FILENO);
        ferr = dup(STDERR_FILENO);

        cti_log_t* log_hook = _cti_create_log("./", "test_log", 1);
        EXPECT_NE(log_hook, nullptr);
        EXPECT_EQ(_cti_hook_stdoe(log_hook), 0);
        std::cout << "TEST\n" << std::flush;
        EXPECT_EQ(_cti_close_log(log_hook), 0);

        // check that data wrote correctly
        std::ifstream check;
        check.open("./dbglog_test_log.1.log", std::ifstream::in);
        if(!check.is_open())
            FAIL() << "Log file created but somehow not openable...";
        
        std::string res;
        check >> res;
        check.close();
        EXPECT_STREQ(res.c_str(), "TEST");
        remove("./dbglog_test_log.1.log");

        // Reset stdout and stderr so testing can continue as normal
        dup2(fout, STDOUT_FILENO);
        dup2(ferr, STDERR_FILENO);
        close(fout);
        close(ferr);
    }
}
/*
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
