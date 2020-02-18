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

//UNTESTED THINGS:
// /useful/ld_val/*
// /useful/cti_path.c : adjustPath
// /useful/cti_path.c : removeDirectory
// /useful/cti_path.c : libFind

#include "cti_defs.h"
#include "cti_argv_defs.hpp"

#include <unordered_set>
#include <fstream>
#include <algorithm>
#include <fcntl.h>
#include "useful/cti_split.hpp"

#include "cti_useful_unit_test.hpp"

using ::testing::Return;
using ::testing::_;
using ::testing::Invoke;
using ::testing::WithoutArgs;
using ::testing::EndsWith;

CTIUsefulUnitTest::CTIUsefulUnitTest()
{
}

CTIUsefulUnitTest::~CTIUsefulUnitTest()
{
}

/******************************************
*             CTI_ARGV TESTS              *
******************************************/

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

    // test that adding a NULL pointer throws
    cti::ManagedArgv argv3;
    ASSERT_NO_THROW(argv3.add(""));
    ASSERT_THROW(argv3.add(NULL), std::logic_error);
}
    /**********************************************************************************/
    // test additional argv classes

TEST_F(CTIUsefulUnitTest, cti_argv_OutgoingArgv) 
{
    cti::OutgoingArgv<cti::Argv> test_OA("./unit_tests");

    // test adding short and long flags
    test_OA.add(cti::Argv::Option(nullptr, 's'));
    test_OA.add(cti::Argv::Option("long_test", '\0'));

    // test adding a parameterized flag
    test_OA.add(cti::Argv::Parameter(nullptr, 'p'), "short");
    test_OA.add(cti::Argv::Parameter("long_test_param", '\0'), "long");

    // test adding an argument
    test_OA.add(cti::Argv::Argument("AnArg"));

    // test that everything is as it should be
    char* const* check = test_OA.get();
    ASSERT_STREQ(check[0], "./unit_tests");
    ASSERT_STREQ(check[1], "-s");
    ASSERT_STREQ(check[2], "--long_test");
    ASSERT_STREQ(check[3], "-p");
    ASSERT_STREQ(check[4], "short");
    ASSERT_STREQ(check[5], "--long_test_param=long");
    ASSERT_STREQ(check[6], "AnArg");

    cti::ManagedArgv move = test_OA.eject();
    char* const* check_moved = move.get();
    ASSERT_STREQ(check_moved[0], "./unit_tests");
    ASSERT_STREQ(check_moved[1], "-s");
    ASSERT_STREQ(check_moved[2], "--long_test");
    ASSERT_STREQ(check_moved[3], "-p");
    ASSERT_STREQ(check_moved[4], "short");
    ASSERT_STREQ(check_moved[5], "--long_test_param=long");
    ASSERT_STREQ(check_moved[6], "AnArg");

    char* const* check_empty = test_OA.get();
    ASSERT_EQ(check_empty, nullptr);
}

TEST_F(CTIUsefulUnitTest, cti_argv_IncomingArgv) 
{
    // setup test argv for IncomingArgv
    // DaemonArgv used as only Argv with long_options which is required by IncomingArgv
    cti::OutgoingArgv<DaemonArgv> test_OA("CTI_BE_DAEMON_BINARY");
    test_OA.add(DaemonArgv::ApID, "1");
    test_OA.add(DaemonArgv::ToolPath, "./unit_tests");

    // create IncomingArgv
    cti::IncomingArgv<CTIFEDaemonArgv> test_IA(3, test_OA.get());
    char* const* check = test_IA.get_rest();
    EXPECT_STREQ(check[0], "--apid=1");
    EXPECT_STREQ(check[1], "--path=./unit_tests");
}

/******************************************
*             CTI_EXECVP TESTS            *
******************************************/

TEST_F(CTIUsefulUnitTest, cti_execvp_fdbuf)
{ 
    // test fdbuf class
    
    // test that fdbuf recognizes invalid file descriptors
    ASSERT_THROW({
        try {
            cti::FdBuf buf_fail(-1);
        } catch (const std::exception& ex) {
            ASSERT_STREQ("Invalid file descriptor", ex.what());
            throw;
        }
    }, std::invalid_argument);

    // test that fdbuf works with proper file descriptors
    int fd = open("testfile.txt", O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
    EXPECT_NO_THROW(cti::FdBuf buf(fd));
    remove("testfile.txt");
}

TEST_F(CTIUsefulUnitTest, cti_execvp_FdPair)
{
    
    // test FdPair class

    // test creation of a fdpair
    ASSERT_NO_THROW(cti::FdPair fdp);

    // test that fdpair closeRead behaves as expected given no fd
    cti::FdPair testfdp;
    {
        bool ex = 0;
        try {
            testfdp.closeRead();
        } catch (...) { // this strange catch required due to closeRead throwing a string
            ex = 1;
        }
        ASSERT_EQ(ex,1);
    }

    // test that fdpair closeWrite behaves as expected given no fd
    {
        bool ex = 0;
        try {
            testfdp.closeWrite();
        } catch (...) { // this strange catch required due to closeWrite throwing a string
            ex = 1;
        }
        ASSERT_EQ(ex,1);
    }
}

TEST_F(CTIUsefulUnitTest, cti_execvp_pipe)
{
    // test Pipe class
    cti::Pipe testpipe;
    ASSERT_NE(-1, testpipe.getReadFd());
    ASSERT_NE(-1, testpipe.getWriteFd());

    // test that closes function ends open properly
    ASSERT_NO_THROW(testpipe.closeWrite());
    ASSERT_NO_THROW(testpipe.closeRead());

    // test that closeRead fails when end already closed
    {
        bool ex = 0;
        try {
            testpipe.closeRead();
        } catch (...) {
            ex = 1;
        }
        ASSERT_EQ(ex,1);
    }
    
    // test that closeWrite fails when end already closed
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

TEST_F(CTIUsefulUnitTest, cti_execvp_execvp_failure)
{
    // test that cti::Execvp fails as expected
    std::initializer_list<std::string const> strlist {"it_will"};
    cti::ManagedArgv argv(strlist);

    // give bogus binary path and check that exit status indicates failure
    cti::Execvp test_fail("/this/will/fail", argv.get());
    EXPECT_NE(test_fail.getExitStatus(), 0);

}

TEST_F(CTIUsefulUnitTest, cti_execvp_execvp_success)
{
    // test that cti::Execvp works as expected    
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

/******************************************
*             CTI_LOG TESTS               *
******************************************/

TEST_F(CTIUsefulUnitTest, cti_log_cti_log_failure)
{ 
    // test that logs aren't created when no filename is given
    cti_log_t* log_fail = _cti_create_log(NULL, NULL, 0);
    ASSERT_EQ(log_fail, nullptr);
}

TEST_F(CTIUsefulUnitTest, cti_log_cti_log_normal) 
{
    // test that a log is created when proper params are given and works as expected  
    cti_log_t* log_succ = _cti_create_log("./", "test_log", 0);
    EXPECT_NE(log_succ, nullptr);
        
    // test that a log can be written to and closed
    EXPECT_EQ(_cti_write_log(log_succ, "TEST"), 0);
    EXPECT_EQ(_cti_close_log(log_succ), 0);

    // test that file is openable
    std::ifstream check;
    check.open("./dbglog_test_log.0.log", std::ifstream::in);
    if(!check.is_open())
        FAIL() << "Log file created but somehow not openable...";

    // test that data is correct
    std::string res;
    getline(check, res);
    check.close();

    ASSERT_THAT(res.c_str(), EndsWith("TEST"));
    // remove log file
    remove("./dbglog_test_log.0.log");
}

TEST_F(CTIUsefulUnitTest, cti_log_cti_log_hookstdoe)
{
    // test the logs hookstdoe function   

    // duplicate the stdout and stderr file descriptors so they can be reset later
    int fout, ferr;
    fout = dup(STDOUT_FILENO);
    ferr = dup(STDERR_FILENO);

    // create the log file
    cti_log_t* log_hook = _cti_create_log("./", "test_log", 1);
    EXPECT_NE(log_hook, nullptr);

    // engage the hook and 'write' to the file via stdout
    EXPECT_EQ(_cti_hook_stdoe(log_hook), 0);
    std::cout << "TEST\n" << std::flush; // fails without std::flush present
    EXPECT_EQ(_cti_close_log(log_hook), 0);

    // test that file exists
    std::ifstream check;
    check.open("./dbglog_test_log.1.log", std::ifstream::in);
    if(!check.is_open())
        FAIL() << "Log file created but somehow not openable...";
        
    // test that data was written correctly
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

/******************************************
*             CTI_PATH TESTS              *
******************************************/

TEST_F(CTIUsefulUnitTest, cti_path_Find)
{ 
    // test _cti_pathFind with local and non-local paths
    ASSERT_STREQ(_cti_pathFind("./unit_tests", nullptr), "./unit_tests");
    ASSERT_STREQ(_cti_pathFind("/bin/echo", nullptr), "/bin/echo");
    ASSERT_EQ(_cti_pathFind("../unit", nullptr), nullptr);
    ASSERT_EQ(_cti_pathFind("./DNE", nullptr), nullptr);
    ASSERT_NE(_cti_pathFind("echo", nullptr), nullptr);
    ASSERT_EQ(_cti_pathFind("DOESNOTEXISTATALL", nullptr), nullptr);
    
}

TEST_F(CTIUsefulUnitTest, cti_path_adjustPaths)
{
    // test that _cti_adjustPaths works as expected
    ASSERT_EQ(_cti_adjustPaths(nullptr, nullptr), 1);
    ASSERT_EQ(_cti_adjustPaths("/DOESNOTEXIST", nullptr), 1);
}

TEST_F(CTIUsefulUnitTest, cti_path_pathToName)
{
    // test pathToName to ensure it works properly
    ASSERT_STREQ(_cti_pathToName("/a/b/c/d/e/f"), "f");
    ASSERT_EQ(_cti_pathToName(""), nullptr);
} 
 
TEST_F(CTIUsefulUnitTest, cti_path_pathToDir)
{  
    // test pathToDir
    ASSERT_STREQ(_cti_pathToDir("a/b/c/d/e"), "a/b/c/d");
    ASSERT_EQ(_cti_pathToDir(""), nullptr);
}


/******************************************
*             CTI_SPLIT TESTS             *
******************************************/

TEST_F(CTIUsefulUnitTest, cti_split)
{ 
    // basic test string with whitespace
    std::string test = "      Test         ";
    test = cti::split::removeLeadingWhitespace(test, " ");
    ASSERT_STREQ(test.c_str(), "Test");
    
    // test with a slighty more complex "whitespace"
    test = "thequickbrownfoxjumpedoverthelazydog";
    test = cti::split::removeLeadingWhitespace(test, "theQUICKbrownfoxjumpedoverthelazydog");
    ASSERT_STREQ(test.c_str(), "quick");
}

/******************************************
*             CTI_STACK TESTS             *
******************************************/

TEST_F(CTIUsefulUnitTest, cti_stack_null)
{
    // test stack calls with no parameters provided
    ASSERT_NO_THROW(_cti_consumeStack(nullptr));
    ASSERT_EQ(_cti_push(nullptr, nullptr), 0);
    ASSERT_EQ(_cti_pop(nullptr), nullptr);
}

TEST_F(CTIUsefulUnitTest, cti_stack_main)
{
    // setup stack and pointers to push onto it
    cti_stack_t* stack = _cti_newStack();
    int a=0,b=1,c=2;
    int* ap=&a;
    int* bp=&b;
    int* cp=&c;

    // push some data and check that pushed properly
    EXPECT_EQ(_cti_push(stack, ap), 0);
    int* pop_test;
    pop_test =(int*) _cti_pop(stack);
    EXPECT_EQ(pop_test, ap);
    EXPECT_EQ(*pop_test, a);

    // push some more data and check that it still works as expected
    EXPECT_EQ(_cti_push(stack, ap), 0);
    EXPECT_EQ(_cti_push(stack, bp), 0);
    EXPECT_EQ(_cti_push(stack, cp), 0);

    pop_test =(int*) _cti_pop(stack);
    EXPECT_EQ(pop_test, cp);
    EXPECT_EQ(*pop_test, c);

    pop_test =(int*) _cti_pop(stack);
    EXPECT_EQ(pop_test, bp);
    EXPECT_EQ(*pop_test, b);

    pop_test =(int*) _cti_pop(stack);
    EXPECT_EQ(pop_test, ap);
    EXPECT_EQ(*pop_test, a);

    // test how pop behaves when no data on stack
    EXPECT_EQ(_cti_pop(stack), nullptr);

    // free the stack with one element on it for ideal testing
    EXPECT_EQ(_cti_push(stack, ap), 0);
    ASSERT_NO_THROW(_cti_consumeStack(stack));
}

/******************************************
*           CTI_WRAPPERS TESTS            *
******************************************/

TEST_F(CTIUsefulUnitTest, cti_wrappers_temp_file_handle_fail)
{
    // test that a temp file handle won't be made when on template is provided
    ASSERT_THROW({
        try {
            cti::temp_file_handle test_fail("");
        } catch (const std::exception& ex) {
            ASSERT_STREQ("mktemp failed", ex.what());
            throw;
        }
    },std::runtime_error);
}

TEST_F(CTIUsefulUnitTest, cti_wrappers_temp_file_handle_success)
{
    // test that a temp file handle can be made when a valid template is provided
    char const* path;
    struct stat buffer;
    {
        cti::temp_file_handle test_succ("/tmp/cti-dir-test-temp-XXXXXX");
        path = test_succ.get();
        std::ofstream file(path);
        file.close();
        EXPECT_EQ(stat(path, &buffer), 0);
    }
    EXPECT_NE(stat(path, &buffer), 0);
}

TEST_F(CTIUsefulUnitTest, cti_wrappers_canWriteFd_Fail)
{
    // test that canWriteFd fails on invalid file descriptor
    ASSERT_EQ(cti::canWriteFd(-1), false);

    // test that canWriteFd fails when file has invalid permissions
    int rdonly = open("./rdonly.txt", O_RDONLY | O_CREAT, S_IRUSR);
    EXPECT_EQ(cti::canWriteFd(rdonly), false);
    remove("./rdonly.txt");
}

TEST_F(CTIUsefulUnitTest, cti_wrappers_canWriteFd_Success)
{
    // that test canWriteFd succeeds when valid permissions
    int wr = open("./wr.txt", O_WRONLY | O_CREAT, S_IWUSR);
    EXPECT_EQ(cti::canWriteFd(wr), true);
    remove("./wr.txt");
}

TEST_F(CTIUsefulUnitTest, cti_wrappers_accessiblePath)
{
    // test that accessible path fails when invalid path provided
    ASSERT_THROW({
        try {
            cti::accessiblePath("./WILLFAIL");
        } catch (const std::exception& ex) {
            ASSERT_STREQ("path inacessible: ./WILLFAIL", ex.what());
            throw;
        }
    }, std::runtime_error);

    // test that an accessible path is accessible
    ASSERT_NO_THROW(cti::accessiblePath("./unit_tests"));
}


TEST_F(CTIUsefulUnitTest, cti_wrappers_isSameFile)
{
    // test that isSameFile works as expected
    ASSERT_EQ(cti::isSameFile("./unit_tests", "./unit_tests"), true);
    ASSERT_EQ(cti::isSameFile("./unit_tests", "./cti_useful_unit_test.cpp"), false);
}

TEST_F(CTIUsefulUnitTest, cti_wrappers_pathExists)
{
    // test that pathExists works as expected
    ASSERT_EQ(cti::pathExists("./unit_tests"), true);
    ASSERT_EQ(cti::pathExists("./DNE"), false);
}

TEST_F(CTIUsefulUnitTest, cti_wrappers_fileHasPerms)
{
    // test that fileHasPerms works as expected
    ASSERT_EQ(cti::fileHasPerms("./unit_tests", X_OK), true); // valid file valid perms
    ASSERT_EQ(cti::fileHasPerms("./cti_useful_unit_test.cpp", X_OK), false); // invalid perms
    ASSERT_EQ(cti::fileHasPerms("./DNE", X_OK), false); // invalid file
    ASSERT_EQ(cti::fileHasPerms("../unit/", R_OK), false); // invalid file type
}

TEST_F(CTIUsefulUnitTest, cti_wrappers_dirHasPerms)
{
    // test that dirHasPerms works as expected
    ASSERT_EQ(cti::dirHasPerms("../unit/", R_OK), true);  // valid dir valid perms
    ASSERT_EQ(cti::dirHasPerms("./unit_tests", X_OK), false); // invalid file type
    ASSERT_EQ(cti::dirHasPerms("./DNE/", R_OK), false); // invalid directory
}

TEST_F(CTIUsefulUnitTest, cti_wrappers_getRealPath)
{
    // test that getRealpath works as expected
    ASSERT_STREQ(cti::getRealPath("/dev/null").c_str(), "/dev/null");
    ASSERT_STRNE(cti::getRealPath("./unit_tests").c_str(), "./unit_tests");
}

TEST_F(CTIUsefulUnitTest, cti_wrappers_getNameFromPath)
{
    // test that getNameFromPath works as expected
    ASSERT_STREQ(cti::getNameFromPath("../unit/unit_tests").c_str(), "unit_tests");

    // test that getNameFromPath fails when no path provided
    ASSERT_THROW({
        try {
            cti::getNameFromPath("");
        } catch (const std::exception& ex) {
            ASSERT_STREQ("Could not convert the fullname to realname.", ex.what());
            throw;
        }
    }, std::runtime_error);
}

TEST_F(CTIUsefulUnitTest, cti_wrappers_findPath)
{
    ASSERT_STREQ(cti::findPath("./unit_tests").c_str(), "./unit_tests");
    ASSERT_STREQ(cti::findPath("/bin/echo").c_str(), "/bin/echo");
    ASSERT_THROW({
        try {
            cti::findPath("../unit").c_str();
        } catch(std::exception& ex) {  
            ASSERT_STREQ("../unit: Could not locate in PATH.", ex.what());
            throw;
        }
    }, std::runtime_error);

    ASSERT_THROW({
        try {
            cti::findPath("./DNE").c_str();
        } catch(std::exception& ex) {  
            ASSERT_STREQ("./DNE: Could not locate in PATH.", ex.what());
            throw;
        }
    }, std::runtime_error);

    ASSERT_NO_THROW(cti::findPath("echo").c_str());

    ASSERT_THROW({
        try {
            cti::findPath("DOESNOTEXISTATALL").c_str();
        } catch(std::exception& ex) {  
            ASSERT_STREQ("DOESNOTEXISTATALL: Could not locate in PATH.", ex.what());
            throw;
        }
    }, std::runtime_error);
}

TEST_F(CTIUsefulUnitTest, cti_wrappers_fd_handle_fail)
{
    // test that fd_handle fails when an invalid file descriptor is given
    ASSERT_THROW({
        try {
            cti::fd_handle test_fdh_fail(-1);
        } catch (const std::exception& ex) {
            ASSERT_STREQ("File descriptor creation failed.", ex.what());
            throw;
        }
    }, std::runtime_error);    
}

TEST_F(CTIUsefulUnitTest, cti_wrappers_fd_handle)
{
    // test that a fdhandle can be made
    int file = open("./fd_handle_test.txt", O_WRONLY | O_CREAT, S_IWUSR);
    cti::fd_handle test_fdh(file);
    EXPECT_EQ(test_fdh.fd(), file);
    remove("./fd_handle_test.txt");

    // test that the move constructor works
    cti::fd_handle move_fdh = std::move(test_fdh);
    EXPECT_EQ(move_fdh.fd(), file);
    EXPECT_EQ(test_fdh.fd(), -1);

    // tests that the overloaded = operator works
    cti::fd_handle eq_fdh;
    EXPECT_EQ(eq_fdh.fd(), -1);
    eq_fdh = cti::fd_handle(file);
    EXPECT_EQ(eq_fdh.fd(), file);
}

TEST_F(CTIUsefulUnitTest, cti_wrappers_cstr)
{
    ASSERT_NO_THROW(cti::cstr::gethostname());
    
    ASSERT_STREQ(cti::cstr::asprintf("./test/%s/testing", "test").c_str(), "./test/test/testing");
    
    std::string dir = "";
    ASSERT_NO_THROW(dir = cti::cstr::mkdtemp("/tmp/cti-test-XXXXXX"));
    rmdir(dir.c_str());
    
}

TEST_F(CTIUsefulUnitTest, cti_wrappers_file)
{
    ASSERT_NO_THROW(auto fp = cti::file::open("./wrapper_file_test.txt", "w+"));
    remove("./wrapper_file_test.txt");
    
    FILE* fw = fopen("./wrapper_file_test2.txt", "w+");
    cti::file::writeT<char>(fw, 'w');
    fclose(fw);

    FILE* fr = fopen("./wrapper_file_test2.txt", "r");
    char data_check = cti::file::readT<char>(fr);
    EXPECT_EQ(data_check, 'w');
    remove("./wrapper_file_test2.txt");
}
