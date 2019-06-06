/******************************************************************************\
 * cti_manifest_unit_test.cpp - Manifest unit tests for CTI
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

#include <memory>
#include <unordered_set>
#include <fstream>

// CTI Transfer includes
#include "frontend/cti_transfer/Manifest.hpp"
#include "frontend/cti_transfer/Session.hpp"

#include "useful/cti_wrappers.hpp"

#include "cti_manifest_unit_test.hpp"

using ::testing::Return;
using ::testing::_;
using ::testing::Invoke;
using ::testing::WithoutArgs;

static constexpr char const* mockArgv[] = {"/usr/bin/true", nullptr};
CTIManifestUnitTest::CTIManifestUnitTest()
    : CTIAppUnitTest{}
    , sessionPtr{std::make_shared<Session>(*mockApp)}
    , manifestPtr{std::make_shared<Manifest>(0, *sessionPtr)}
{
    // create a list of suffixes that will be used in test files
    file_suffixes.push_back("1.txt"); 
    file_suffixes.push_back("2.txt");

    // remove any lingering test files
    for(std::string suf : file_suffixes) {
       remove(std::string(test_file_path + suf).c_str());
    }
}

CTIManifestUnitTest::~CTIManifestUnitTest() {
    for(std::string suf : file_suffixes) {
       remove(std::string(test_file_path + suf).c_str());
    }
}

/****************************
 *TODO
 *Cause a session conflict
 *inside of checkAndAdd
 *which is called through
 *most functions and
 *confirm that realName + :
 * session conflict occurs
 *
 *
 *
 *****************************/

TEST_F(CTIManifestUnitTest, empty) {
    // test manifest empty at start
    ASSERT_EQ(manifestPtr->empty(), true);

   // create a test file to add to the manifest
   std::ofstream f1;
   f1.open(std::string(test_file_path + file_suffixes[0]).c_str());
   if (!f1.is_open()) {
      FAIL() << "Failed to create file for testing addFile";
   }
   f1 << test_file_path + file_suffixes[0];
   manifestPtr -> addFile(std::string("./" + test_file_path + file_suffixes[0]).c_str());
   
   // test that manifest is no longer empty
   ASSERT_EQ(manifestPtr->empty(), false);
}

TEST_F(CTIManifestUnitTest, getOwningSession) {

    // test that a session can be gotten
    ASSERT_NE(manifestPtr->getOwningSession(), nullptr);

    //Attempt to a get a session when none exist
    /* ASSERT_THROW({
	    try {
            
	    } catch (const std::exception& ex) {
		 EXPECT_STREQ("Owning Session is no longer valid.", ex.what());
		 throw;
	    }
   
    }, std::runtime_error);	
    */
}

//////
TEST_F(CTIManifestUnitTest, addFile) {

   // create a test file to add to the manifest
   std::ofstream f1;
   f1.open(std::string(test_file_path + file_suffixes[0]).c_str());
   if (!f1.is_open()) {
      FAIL() << "Failed to create file for testing addFile";
   }
   f1 << test_file_path + file_suffixes[0];

   ASSERT_NO_THROW({
      try {
          manifestPtr -> addFile(std::string("./" + test_file_path + file_suffixes[0]).c_str());
      } catch (std::exception& ex) {
          FAIL() << ex.what();
	  throw;
      }
   });

   //Attempt to add a file after manifest has been shipped
   manifestPtr -> finalize();  

   std::ofstream f2;
   f2.open(std::string(test_file_path + file_suffixes[1]).c_str());
   if (!f2.is_open()) {
      FAIL() << "Failed to create file for testing addFile";
   }
   f2 << test_file_path + file_suffixes[1];

   ASSERT_THROW({
	   try {
                manifestPtr -> addFile(std::string("./" + test_file_path + file_suffixes[1]).c_str());
	   } catch (const std::exception& ex) {
                EXPECT_STREQ("Attempted to modify previously shipped manifest!", ex.what());
		throw;
	   }
   }, std::runtime_error);
   

}

//////
/*
TEST_F(CTIManifestUnitTest, addBinary) {
    
   // test that a binary can be added
   
   //TODO: create the binary to be added 
   ASSERT_NO_THROW(testManifest -> addBinary(std::string(test_file_path + file_suffixes[1]).c_str()));

   ASSERT_THROW({
	   try {
           //TODO: Add a binary to the manifest where permissions are not set properly
	   } catch (const std::exception& ex) {
		 EXPECT_STREQ("Specified binary does not have execute permissoins.", ex.what());
		 throw;
	   }
   
   }, std::runtime_error);

   //Attempt to add a binary after manifest has been shipped
   ASSERT_THROW({
 	    try {

	    } catch (const std::exception& ex) {
                 EXPECT_STREQ("Attempted to modify previously shipped manifest!", ex.what());
	    }
   }, std::runtime_error);
}

TEST_F(CTIManifestUnitTest, addLibrary) {

   //Attempt to add a library after manifest has been shipped
   ASSERT_THROW({
	   try {

	   } catch (const std::exception& ex) {
                EXPECT_STREQ("Attempted to modify previously shipped manifest!", ex.what());
	   }
   }, std::runtime_error);

}

TEST_F(CTIManifestUnitTest, addLibDir) {

   //Attempt to add a library directory after manifest has been shipped
   ASSERT_THROW({
           try {

	   } catch (const std::exception& ex) {
	        EXPECT_STREQ("Attempted to modify previously shipped manifest!", ex.what());
	   }
   }, std::runtime_error);
}
*/
