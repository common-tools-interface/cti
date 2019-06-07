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
#include <fstream> //TODO: Consider replacing with C implementation or easier temp style file.
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

// File template for temporary directory
char LOCAL_FILE_TEMPLATE[] = "/tmp/cti-dir-test-temp-XXXXXX";

// Other constants
// Base directory for all archive files:
const std::string DIR_PATH = "u_test";

// Base name for all test files
const std::string TEST_FILE_PATH = "archive_test_file";

// Number of files to be tested with
const int FILE_COUNT = 3;

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
    for(int i = 0; i < FILE_COUNT; i++) {
        file_suffixes.push_back(std::to_string(i+1) + ".txt");
    }
    file_suffixes.push_back("_pipe");

    // add all the directories for temp files.
    dirs.push_back(DIR_PATH);
    dirs.push_back(DIR_PATH + "/lib");
    dirs.push_back(DIR_PATH + "/tmp");
    dirs.push_back(DIR_PATH + "/bin");

    // ensure no test files still exist from previous tests
    for(std::string suf : file_suffixes) {
    	remove(std::string(TEST_FILE_PATH + suf).c_str());
    }
    
}

CTIArchiveUnitTest::~CTIArchiveUnitTest()
{
    // clear up allocated memory
    if(temp_file_path){
        delete temp_file_path;
        temp_file_path = nullptr;
    }

    if(archive){
        delete archive;
        archive = nullptr;
    }

    // remove all test files from current directory
    for(std::string suf : file_suffixes) {
    	remove(std::string(TEST_FILE_PATH + suf).c_str());
    }
    
}

// test that archive can create directory entries properly 
TEST_F(CTIArchiveUnitTest, addDirEntry)
{
    // test all dir entries (/u_test/...{lib}, {tmp}, {bin}) can be added
    for(std::string path : dirs) {
        ASSERT_NO_THROW(archive -> addDirEntry(path));
    }
}


// test that archive can add files/dirs properly 
TEST_F(CTIArchiveUnitTest, addPath)
{

   // create a directory to add to the archive
   char* tdir = mkdtemp(LOCAL_FILE_TEMPLATE);
   if(tdir == NULL) {
       FAIL() <<"Failed to create temporary directory";
   }

   std::ofstream f_temp;
   std::string f_temp_path = tdir;
   f_temp_path += "/" + TEST_FILE_PATH + "_temp_file";

   // open temporary file
   f_temp.open(f_temp_path.c_str());
   if(!f_temp.is_open()){
       FAIL() << "Failed to create test file temp_file";
   }

   // create some files to add in the test
   std::ofstream f[FILE_COUNT];
   for(int i = 0; i < FILE_COUNT; i++) {
       f[i].open(std::string(TEST_FILE_PATH + file_suffixes[i]).c_str());
       if(!f[i].is_open()) {
           FAIL()<< "Failed to create test file " << i;
       }
   }

   // create a pipe to attempt to send
   int pipe = mkfifo(std::string(TEST_FILE_PATH + "_pipe").c_str(), S_IRWXU);
   if(pipe == -1){
       FAIL() <<"Failed to create pipe";
   }
  
   // write test output and close all test files
   for(int i = 0; i < FILE_COUNT; i++) {
       // write the files path to the file. makes checking contents easy later
       f[i] << DIR_PATH + "/" + TEST_FILE_PATH + file_suffixes[i];
       f[i].close();
   }

   f_temp << f_temp_path;
   f_temp.close();

   // this vector is used to ensure all files appear when the archive is checked later
   std::vector<std::string> test_paths;

   test_paths.push_back(DIR_PATH + "/" + tdir + "/"); //extra / added as thats how archive reads back dirs
   test_paths.push_back(DIR_PATH + "/" + f_temp_path);

   for(int i = 0; i < FILE_COUNT; i++) {
       ASSERT_NO_THROW(archive -> addPath(dirs[i] + TEST_FILE_PATH + file_suffixes[i], TEST_FILE_PATH + file_suffixes[i]));
       test_paths.push_back(dirs[i] + TEST_FILE_PATH + file_suffixes[i]);
   }

   // add a directory and its included file
   ASSERT_NO_THROW(archive -> addPath(DIR_PATH + "/" + tdir, tdir));

   //Clean up temporary files now to prevent them from not cleaning if test fails out

   //Delete f_temp
   remove(f_temp_path.c_str());

   //Delete temp directory
   rmdir(tdir);

   // attempt to add a file that does not exist
   ASSERT_THROW({
       try {
           archive -> addPath(DIR_PATH + "/tmp/" + TEST_FILE_PATH + "_fail.txt", TEST_FILE_PATH + "_fail.txt");
       } catch (const std::exception& ex) {
           EXPECT_STREQ(std::string(TEST_FILE_PATH + "_fail.txt" + " failed stat call").c_str(), ex.what());
           throw;
       }
   }, std::runtime_error);
   
   // attempt to add a pipe to the archive
   ASSERT_THROW({
       try {
           archive -> addPath(DIR_PATH + "/tmp/" + TEST_FILE_PATH + "_pipe", TEST_FILE_PATH + "_pipe");
       } catch (const std::exception& ex) {
           EXPECT_STREQ(std::string(TEST_FILE_PATH + "_pipe" + " has invalid file type.").c_str(), ex.what());
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

   // make sure all dirs and files were shipped properly
   bool found = false;
   struct archive_entry *entry;
   while (archive_read_next_header(archPtr.get(), &entry) == ARCHIVE_OK) {
       auto const path = std::string{archive_entry_pathname(entry)};
       // search for each entry. should take O(1) as it should always be the first entry for a proper archive
       for(unsigned int i = 0; i < test_paths.size(); i++) {
           if(test_paths[i] == path) {
               found = true;
	       //TODO: check that file contents are correct
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
   EXPECT_STREQ(temp_file_path->get(), std::string(archive -> finalize()).c_str());

   // create a file to attempt to add
   std::ofstream f1;

   // open test file
   f1.open(std::string(TEST_FILE_PATH + "1.txt").c_str());
   if(!f1.is_open()) {
       FAIL() << "Failed to create test file";
   }

   // write to test file four
   f1 << "f1 test data";
   f1.close();

   // attempt to add another file when finalized already
   ASSERT_THROW({
       try {
           archive -> addPath(DIR_PATH + "/bin/" + TEST_FILE_PATH + "1.txt", TEST_FILE_PATH + "1.txt");
       } catch (const std::exception& ex) {
           EXPECT_STREQ(std::string(std::string(temp_file_path -> get()) + " tried to add a path after finalizing").c_str(), ex.what());
	   throw;
       }
   }, std::runtime_error);

   // attempt to add another directory when finalized already
   ASSERT_THROW({
       try {
           archive -> addDirEntry(DIR_PATH + "/fail");
       } catch (const std::exception& ex) {
           EXPECT_STREQ(std::string(std::string(temp_file_path -> get()) + " tried to add a path after finalizing").c_str(), ex.what());
           throw;
       }
   }, std::runtime_error);
}

// Test that tarball is deleted on destruction of archive
TEST_F(CTIArchiveUnitTest, destruc_check) {

   // Delete the archive
   if(archive) {
       delete archive;
       archive = nullptr;	
   }

   // make sure remove fails as there should be no file anymore
   EXPECT_NE(0, remove(temp_file_path -> get()));

}
