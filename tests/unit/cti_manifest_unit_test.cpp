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

//#include <sys/stat.h> // for changing binary permissions
#include <stdlib.h>

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
        remove(std::string(TEST_FILE_PATH + suf).c_str());
    }
}

CTIManifestUnitTest::~CTIManifestUnitTest() {
    for(std::string suf : file_suffixes) {
        remove(std::string(TEST_FILE_PATH + suf).c_str());
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
   f1.open(std::string(TEST_FILE_PATH + file_suffixes[0]).c_str());
   if (!f1.is_open()) {
       FAIL() << "Failed to create file for testing addFile";
   }
   f1 << TEST_FILE_PATH + file_suffixes[0];
   manifestPtr -> addFile(std::string("./" + TEST_FILE_PATH + file_suffixes[0]).c_str());
   
   // test that manifest is no longer empty
   ASSERT_EQ(manifestPtr->empty(), false);
}

TEST_F(CTIManifestUnitTest, getOwningSession) {

    // test that a session can be gotten
    ASSERT_NE(manifestPtr -> getOwningSession(), nullptr);

    // destroy the manifests current session
    sessionPtr.reset();

    // test that manifest's session no longer returns properly
    ASSERT_THROW({
        try {
            manifestPtr -> getOwningSession();
        } catch (const std::exception& ex) {
            EXPECT_STREQ("Owning Session is no longer valid.", ex.what());
            throw;
        }
   
    }, std::runtime_error);	
    
}

//////
TEST_F(CTIManifestUnitTest, addFile) {

   // create a test file to add to the manifest
   std::ofstream f1;
   f1.open(std::string(TEST_FILE_PATH + file_suffixes[0]).c_str());
   if (!f1.is_open()) {
       FAIL() << "Failed to create file for testing addFile";
   }
   f1 << TEST_FILE_PATH + file_suffixes[0];

   ASSERT_NO_THROW({
       try {
           manifestPtr -> addFile(std::string("./" + TEST_FILE_PATH + file_suffixes[0]).c_str());
       } catch (std::exception& ex) {
           FAIL() << ex.what();
           throw;
       }
   });

   // test that the file data was actually added to memory
   PathMap p = manifestPtr -> sources();
   ASSERT_EQ(p[cti::getNameFromPath(cti::getRealPath("./" + TEST_FILE_PATH + file_suffixes[0]))],
	           cti::findPath("./" + TEST_FILE_PATH + file_suffixes[0]));

   // test that there is only one data file in memory
   ASSERT_EQ(p.size(), 1);

   // test that file was added to relevant folder
   FoldersMap fm = manifestPtr -> folders();

   // test that file folder data is actually in memory
   ASSERT_EQ(*(fm[""].begin()), cti::getNameFromPath(cti::getRealPath("./" + TEST_FILE_PATH + file_suffixes[0]))); 

   // test that there was no excess folder data in memory
   ASSERT_EQ(fm[""].size(), 1); 

   //Attempt to add a file after manifest has been shipped
   manifestPtr -> finalize();  

   std::ofstream f2;
   f2.open(std::string(TEST_FILE_PATH + file_suffixes[1]).c_str());
   if (!f2.is_open()) {
      FAIL() << "Failed to create file for testing addFile";
   }
   f2 << TEST_FILE_PATH + file_suffixes[1];

   ASSERT_THROW({
       try {
           manifestPtr -> addFile(std::string("./" + TEST_FILE_PATH + file_suffixes[1]).c_str());
       } catch (const std::exception& ex) {
           EXPECT_STREQ("Attempted to modify previously shipped manifest!", ex.what());
           throw;
       }
   }, std::runtime_error);
}

//////

TEST_F(CTIManifestUnitTest, addBinary) {
    
   // test that a binary can be added
   ASSERT_NO_THROW(manifestPtr -> addBinary("./unit_tests", Manifest::DepsPolicy::Ignore));

   //chmod("./unit_tests", 
   //At the moment unit_test itself is being used. Not sure if this is a good decision.
   std::ofstream f1;
   f1.open(std::string(TEST_FILE_PATH + file_suffixes[0]).c_str());
   if (!f1.is_open()) {
       FAIL() << "Could not open addBinary file";
   }
   f1 << "I'm_a_binary";
   f1.close();

   ASSERT_THROW({
       try {
           manifestPtr -> addBinary(std::string("./" + TEST_FILE_PATH + file_suffixes[0]).c_str(), Manifest::DepsPolicy::Ignore);
       } catch (const std::exception& ex) {
           EXPECT_STREQ("Specified binary does not have execute permissions.", ex.what());
           throw;
       }
   }, std::runtime_error);
   //Attempt to add a binary after manifest has been shipped
   
   manifestPtr -> finalize();
   ASSERT_THROW({
       try {
           manifestPtr -> addBinary("./unit_tests", Manifest::DepsPolicy::Ignore);
       } catch (const std::exception& ex) {
           EXPECT_STREQ("Attempted to modify previously shipped manifest!", ex.what());
	   throw;
       }
   }, std::runtime_error);
}

TEST_F(CTIManifestUnitTest, addLibrary) {
   
   std::ofstream f1;
   f1.open(std::string(TEST_FILE_PATH + file_suffixes[0]).c_str());
   if(!f1.is_open()) {
       FAIL () << "Failed to make fake library file";
   }
   f1 << "I'm a library";
   f1.close();

   ASSERT_NO_THROW(manifestPtr -> addLibrary(std::string("./" + TEST_FILE_PATH + file_suffixes[0]).c_str(), Manifest::DepsPolicy::Ignore));
   //Attempt to add a library after manifest has been shipped

   manifestPtr -> finalize();
   ASSERT_THROW({
       try {
           manifestPtr -> addLibrary(std::string("./" + TEST_FILE_PATH + file_suffixes[0]).c_str(), Manifest::DepsPolicy::Ignore);
       } catch (const std::exception& ex) {
           EXPECT_STREQ("Attempted to modify previously shipped manifest!", ex.what());
	   throw;
       }
   }, std::runtime_error);
}

TEST_F(CTIManifestUnitTest, addLibDir) {
   // create temp 'library'
   char TEMPLATE[] = "/tmp/cti-test-XXXXXX";
   char* tdir = mkdtemp(TEMPLATE);
   if(tdir == NULL) {
       FAIL() <<"Failed to create temporary library";
   }

   // create a temporary file for the library
   std::ofstream f_temp;
   std::string f_temp_path = tdir;
   f_temp_path += "/" + TEST_FILE_PATH + "_temp_file";
   f_temp.open(f_temp_path.c_str());
   if(!f_temp.is_open()) {
       FAIL() << "Failed to create temp library file";
   }
   f_temp << "I'm a library file";
   f_temp.close();

   ASSERT_NO_THROW(manifestPtr -> addLibDir(tdir));

   // test how manifest behaves shipping a library dir after already being shipped
   manifestPtr -> finalize();
   ASSERT_THROW({
       try {
           manifestPtr -> addLibDir(tdir);
       } catch (const std::exception& ex) {
           EXPECT_STREQ("Attempted to modify previously shipped manifest!", ex.what());
           throw;
       }
   }, std::runtime_error);

   // cleanup 'library' files
   remove(f_temp_path.c_str());
   rmdir(tdir);
}
