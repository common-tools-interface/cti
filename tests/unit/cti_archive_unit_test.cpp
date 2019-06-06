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

// File template for temporary directory. TODO: REMOVE ME.
char LOCAL_FILE_TEMPLATE[] = "/tmp/cti-dir-temp-XXXXXX";

using ::testing::Return;
using ::testing::_;
using ::testing::Invoke;
using ::testing::WithoutArgs;

const std::string dirPath = "u_test";
const std::string test_file_path = "archive_test_file";

CTIArchiveUnitTest::CTIArchiveUnitTest()
{
    // setup the temp file and archive for testing
    temp_file_path = new cti::temp_file_handle{CROSSMOUNT_FILE_TEMPLATE};
    archive = new Archive(temp_file_path -> get());

    // add all the file suffixes to the vector
    file_suffixes.push_back("_one.txt");
    file_suffixes.push_back("_two.txt");
    file_suffixes.push_back("_three.txt");
    file_suffixes.push_back("_four.txt");
    file_suffixes.push_back("_pipe");

    // ensure no test files still exist from previous tests
    // TODO: replace with less hardcoding
    for(std::string suf : file_suffixes) {
    	remove(std::string(test_file_path + suf).c_str());
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
    // TODO: replace with less hardcoding
    for(std::string suf : file_suffixes) {
    	remove(std::string(test_file_path + suf).c_str());
    }
    
}

//Ensure that archive can create directory entries properly 
TEST_F(CTIArchiveUnitTest, addDirEntry)
{
    // run the test
 
    // add /u_test directory
    ASSERT_NO_THROW(archive -> addDirEntry(dirPath));

    // add /u_test/lib directory
    ASSERT_NO_THROW(archive -> addDirEntry(dirPath + "/lib"));

    // add /u_test/tmp directory
    ASSERT_NO_THROW(archive -> addDirEntry(dirPath + "/tmp"));

    // add /u_test/bin directory
    ASSERT_NO_THROW(archive -> addDirEntry(dirPath + "/bin"));
}


//Ensure that archive can add files/dirs properly 
TEST_F(CTIArchiveUnitTest, addPath)
{
   // create some files to add in the test
   //std::ofstream f1, f2, f3;
   std::ofstream f[3];

   // create a directory to add to the archive
   char* tdir = mkdtemp(LOCAL_FILE_TEMPLATE);
   if(tdir == NULL) {
     FAIL() <<"Failed to create temporary directory";
   }

   std::ofstream f_temp;
   std::string f_temp_path = tdir;
   f_temp_path += "/" + test_file_path + "_temp_file";

   //Open all files 
   f_temp.open(f_temp_path.c_str());
   if(!f_temp.is_open()){
     FAIL() << "Failed to create test file temp_file";
   }

   for(int i = 0; i < 3; i++) {
   	f[i].open(std::string(test_file_path + file_suffixes[i]).c_str());
	if(!f[i].is_open()) {
		FAIL()<< "Failed to create test file " << i;
	}
   }

   // create a pipe to attempt to send
   int pipe = mkfifo(std::string(test_file_path + "_pipe").c_str(), S_IRWXU);
   if(pipe == -1){
     FAIL() <<"Failed to create pipe";
   }
  
   // write test output and close all test files
   for(int i = 0; i < 3; i++) {
   	f[i] << "f" << i << " test data\n";
	f[i].close();
   }
   f_temp << "ftemp test data\n";
   f_temp.close();
   // run the test
   
   // add /u_test/archive_unit_test_one.txt
   ASSERT_NO_THROW(archive -> addPath(dirPath + "/" + test_file_path + "_one.txt", test_file_path + "_one.txt"));
   
   // add /u_test/lib/archive_unit_test_two.txt
   ASSERT_NO_THROW(archive -> addPath(dirPath + "/lib/" + test_file_path + "_two.txt", test_file_path + "_two.txt"));
   
   // add /u_test/bin/archive_unit_test_three.txt	   
   ASSERT_NO_THROW(archive -> addPath(dirPath + "/bin/" + test_file_path + "_three.txt", test_file_path + "_three.txt"));

   // add a directory and its included file
   ASSERT_NO_THROW(archive -> addPath(dirPath + "/" + tdir, tdir));

   // attempt to add a file that does not exist
   ASSERT_THROW({
	   try {
	   	archive -> addPath(dirPath + "/tmp/" + test_file_path + "_fail.txt", test_file_path + "_fail.txt");
           } catch (const std::exception& ex) {
	   	EXPECT_STREQ(std::string(test_file_path + "_fail.txt" + " failed stat call").c_str(), ex.what());
		throw;
	   }
   }, std::runtime_error);
   
   // attempt to add a pipe to the archive
   ASSERT_THROW({
	   try {
	   	archive -> addPath(dirPath + "/tmp/" + test_file_path + "_pipe", test_file_path + "_pipe");
           } catch (const std::exception& ex) {
	   	EXPECT_STREQ(std::string(test_file_path + "_pipe" + " has invalid file type.").c_str(), ex.what());
		throw;
	   }

   }, std::runtime_error);

   //Clean up temporary files

   //Delete f_temp
   remove(f_temp_path.c_str());

   //Delete temp directory
   rmdir(tdir);

}

TEST_F(CTIArchiveUnitTest, finalize) {
   // finalize the archive
   EXPECT_STREQ(temp_file_path->get(), std::string(archive -> finalize()).c_str());

   // create a file to attempt to add
   std::ofstream f4;

   // open test file four
   f4.open(std::string(test_file_path + "_four.txt").c_str());
   if(!f4.is_open()) {
     FAIL() << "Failed to create test file four";
   }

   // write to test file four
   f4 << "f4 test data";
   f4.close();

   // attempt to add another file when finalized already
   ASSERT_THROW({
   	   try {
           	archive -> addPath(dirPath + "/bin/" + test_file_path + "_four.txt", test_file_path + "_four.txt");
	   } catch (const std::exception& ex) {
           	EXPECT_STREQ(std::string(std::string(temp_file_path -> get()) + " tried to add a path after finalizing").c_str(), ex.what());
		throw;
	   }
   }, std::runtime_error);

   // attempt to add another directory when finalized already
   ASSERT_THROW({
   	   try {
           	archive -> addDirEntry(dirPath + "/fail");
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

// Test to confirm data written in previous tests is in fact written and can be read from again.
/*TEST_F(CTIArchiveUnitTest, data_check) {

   // setup archive check struct
   auto archPtr = cti::make_unique_destr(archive_read_new(), archive_read_free);
   archive_read_support_filter_all(archPtr.get());
   archive_read_support_format_all(archPtr.get());

   // open check archive
   ASSERT_EQ(archive_read_open_filename(archPtr.get(), temp_file_path -> get(), 10240), ARCHIVE_OK);

   // make sure all dirs and files were shipped properly
   struct archive_entry *entry;
   while (archive_read_next_header(archPtr.get(), &entry) == ARCHIVE_OK) {
       auto const path = std::string{archive_entry_pathname(entry)};
       EXPECT_STREQ(path, "FAILME");
   }
}*/
