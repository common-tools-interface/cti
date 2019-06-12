/******************************************************************************\
 * cti_fe_unit_test.cpp - Frontend unit tests for CTI
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

#include <unordered_set>
#include <fstream>
#include <algorithm>

// Includes for file creation
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <archive.h>
#include <archive_entry.h>

// CTI Transfer includes

#include "cti_archive_unit_test.hpp"

// Crossmount file templates

#define ON_WHITEBOX
#ifdef ON_WHITEBOX
static constexpr auto CROSSMOUNT_FILE_TEMPLATE = "/tmp/cti-test-XXXXXX";
#else
static constexpr auto CROSSMOUNT_FILE_TEMPLATE = "/lus/scratch/tmp/cti-test-XXXXXX";
#endif

// file template for temporary directory
char LOCAL_FILE_TEMPLATE[] = "/tmp/cti-dir-test-temp-XXXXXX";

using ::testing::Return;
using ::testing::_;
using ::testing::Invoke;
using ::testing::WithoutArgs;

CTIArchiveUnitTest::CTIArchiveUnitTest()
{
    // setup the temp file and archive for testing
    temp_file_path = new cti::temp_file_handle{CROSSMOUNT_FILE_TEMPLATE};
    archive = new Archive(temp_file_path -> get());

    // add all the file suffixes to the vector
    for (int i = 0; i < FILE_COUNT; i++) {
        file_names.push_back(TEST_FILE_NAME + std::to_string(i+1) + ".txt");
    }
    file_names.push_back(TEST_FILE_NAME + "_pipe");

    // add all the directories for temp files.
    dir_names.push_back(TEST_DIR_NAME);
    dir_names.push_back(TEST_DIR_NAME + "/lib");
    dir_names.push_back(TEST_DIR_NAME + "/tmp");
    dir_names.push_back(TEST_DIR_NAME + "/bin");

    // ensure no test files still exist from previous tests
    for (auto&& fil : file_names) {
    	remove(fil.c_str());
    }
    
}

CTIArchiveUnitTest::~CTIArchiveUnitTest()
{
    // clear up allocated memory
    if (temp_file_path){
        delete temp_file_path;
        temp_file_path = nullptr;
    }

    if (archive){
        delete archive;
        archive = nullptr;
    }

    // remove all test files from current directory
    for (auto&& fil : file_names) {
    	remove(fil.c_str());
    }
}

// test that archive can create directory entries properly 
TEST_F(CTIArchiveUnitTest, addDirEntry)
{
    // test all dir entries (/u_test/...{lib}, {tmp}, {bin}) can be added
    for (auto&& dir : dir_names) {
        ASSERT_NO_THROW(archive -> addDirEntry(dir));
    }
}


// test that archive can add files/dir_names properly 
TEST_F(CTIArchiveUnitTest, addPath)
{

    // create a directory to add to the archive
    char* tdir = mkdtemp(LOCAL_FILE_TEMPLATE);
    if (tdir == NULL) {
        FAIL() <<"Failed to create temporary directory";
    }
 
    std::string f_temp_path = tdir;

    {
        std::ofstream f_temp;
        f_temp_path += "/" + TEST_FILE_NAME + "_temp_file";
 
        // create file to add to temporary directory
        f_temp.open(f_temp_path.c_str());
        if (!f_temp.is_open()){
            FAIL() << "Failed to create test file temp_file";
        }
        f_temp << f_temp_path;
        f_temp.close();
    }

    // create some files to add in the test
    {
        std::ofstream f[FILE_COUNT];
        for (int i = 0; i < FILE_COUNT; i++) {
            f[i].open(std::string(file_names[i]).c_str());
            if (!f[i].is_open()) {
                FAIL()<< "Failed to create test file " << i;
            }
            f[i] << TEST_DIR_NAME + "/" + file_names[i];
            f[i].close();
        }
    }

    // create a pipe to attempt to send
    int pipe = mkfifo(std::string(TEST_FILE_NAME + "_pipe").c_str(), S_IRWXU);
    if (pipe == -1){
        remove(f_temp_path.c_str());
        rmdir(tdir);
        FAIL() <<"Failed to create pipe";
    }
 
    
 
    // this vector is used to ensure all files appear when the archive is checked later
    std::vector<std::string> test_paths;
 
    test_paths.push_back(TEST_DIR_NAME + "/" + tdir + "/"); //extra / added as thats how archive reads back dir
    test_paths.push_back(TEST_DIR_NAME + "/" + f_temp_path);
 
 
    
    for (int i = 0; i < FILE_COUNT; i++) {
        EXPECT_NO_THROW(archive -> addPath(dir_names[i] + file_names[i], file_names[i]));
        test_paths.push_back(dir_names[i] + file_names[i]);
    }
 
    // add a directory and its included file
    EXPECT_NO_THROW(archive -> addPath(TEST_DIR_NAME + "/" + tdir, tdir));
 
    // clean up temporary files now to prevent them from not cleaning if test fails out
    // delete f_temp
    remove(f_temp_path.c_str());
 
    // delete temp directory
    rmdir(tdir);
 
    // test that archive does not add files that don't exist
    ASSERT_THROW({
        try {
            archive -> addPath(TEST_DIR_NAME + "/tmp/" + TEST_FILE_NAME + "_fail.txt", TEST_FILE_NAME + "_fail.txt");
        } catch (const std::exception& ex) {
            EXPECT_STREQ(std::string(TEST_FILE_NAME + "_fail.txt" + " failed stat call").c_str(), ex.what());
            throw;
        }
    }, std::runtime_error);
    
    // test that archive does not allow for non-traditional files like pipes to be added
    ASSERT_THROW({
        try {
            archive -> addPath(TEST_DIR_NAME + "/tmp/" + TEST_FILE_NAME + "_pipe", TEST_FILE_NAME + "_pipe");
        } catch (const std::exception& ex) {
            EXPECT_STREQ(std::string(TEST_FILE_NAME + "_pipe" + " has invalid file type.").c_str(), ex.what());
            throw;
        }
    }, std::runtime_error);
 
    // finalize the archive and check all data is there.
    archive -> finalize();
 
    // setup archive check struct
    auto archPtr = cti::make_unique_destr(archive_read_new(), archive_read_free);
    archive_read_support_filter_all(archPtr.get());
    archive_read_support_format_all(archPtr.get());
 
    // open check archive
    ASSERT_EQ(archive_read_open_filename(archPtr.get(), temp_file_path -> get(), 10240), ARCHIVE_OK);
 
    // make sure all dir and files were shipped properly
    bool found = false;
    struct archive_entry *entry;
    while (archive_read_next_header(archPtr.get(), &entry) == ARCHIVE_OK) {
        auto const path = std::string{archive_entry_pathname(entry)};
        // search for the entry. should always be the first if archive worked correctly
        for (unsigned int i = 0; i < test_paths.size(); i++) {
            if (test_paths[i] == path) {
                found = true;
 	        test_paths.erase(test_paths.begin() + i);
                break;
 	   }
        }
        if (!found) {
          FAIL() << "Unexpected file: " << path;
        }
        found = false;
        archive_read_data_skip(archPtr.get());
    }
}

TEST_F(CTIArchiveUnitTest, finalize) {
    // finalize the archive
    EXPECT_STREQ(std::string(temp_file_path->get()).c_str(), archive -> finalize().c_str());
 
    // create a file to attempt to add
    {
        std::ofstream f1;
        f1.open(file_names[0].c_str());
        if(!f1.is_open()) {
            FAIL() << "Failed to create test file";
        }
 
        // write to test file
        f1 << "f1 test data";
        f1.close();
    }
 
    // test that archive does not allow adding files after finalizing
    ASSERT_THROW({
        try {
            archive -> addPath(TEST_DIR_NAME + "/bin/" + file_names[0], file_names[0]);
        } catch (const std::exception& ex) {
            EXPECT_STREQ(std::string(std::string(temp_file_path -> get()) + " tried to add a path after finalizing").c_str(), ex.what());
            throw;
        }
    }, std::runtime_error);
 
    // test that archive does not allow adding directories after finalizing
    ASSERT_THROW({
        try {
            archive -> addDirEntry(TEST_DIR_NAME + "/fail");
        } catch (const std::exception& ex) {
            EXPECT_STREQ(std::string(std::string(temp_file_path -> get()) + " tried to add a path after finalizing").c_str(), ex.what());
            throw;
        }
    }, std::runtime_error);
}

// test that tarball is deleted on destruction of archive
TEST_F(CTIArchiveUnitTest, destruc_check) {

    // delete the archive
    if (archive) {
        delete archive;
        archive = nullptr;	
    }
 
    // test that the tarball is properly deleted
    EXPECT_NE(0, remove(temp_file_path -> get()));
}
