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

TEST_F(CTIManifestUnitTest, extraLibraryPath) {
    // test the extraLibraryPath getter works as expected
    ASSERT_STREQ(manifestPtr -> extraLibraryPath().c_str(), "");
}

TEST_F(CTIManifestUnitTest, instance) {
    // test the instance() getter works as expected
    ASSERT_EQ(manifestPtr -> instance(), 0);
}

TEST_F(CTIManifestUnitTest, sources) {
    // test the source() getter works as expected
    PathMap p = manifestPtr -> sources();
    ASSERT_EQ(p.size(), 0);
}

TEST_F(CTIManifestUnitTest, folders) {
    // test that folders() getter works as expected
    FoldersMap fm = manifestPtr -> folders();
    ASSERT_EQ(fm.size(), 0);
}

//////
TEST_F(CTIManifestUnitTest, addFile) {
    // test that no files exist at start
    PathMap p = manifestPtr -> sources();
    ASSERT_EQ(p.size(), 0);
 
    FoldersMap fm = manifestPtr -> folders();
    ASSERT_EQ(fm.size(), 0);
 
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
    p = manifestPtr -> sources();
    ASSERT_EQ(p[cti::getNameFromPath(cti::findPath("./" + TEST_FILE_PATH + file_suffixes[0]))],
 	           cti::findPath("./" + TEST_FILE_PATH + file_suffixes[0]));
 
    // test that there is only one data file in memory
    ASSERT_EQ(p.size(), 1);
 
    // test that file was added to relevant folder
    fm = manifestPtr -> folders();
 
    // test that file folder data is actually in memory
    ASSERT_EQ(*(fm[""].begin()), cti::getNameFromPath(cti::findPath("./" + TEST_FILE_PATH + file_suffixes[0]))); 
 
    // test that there was no excess folder data in memory
    ASSERT_EQ(fm.size(), 1);
    ASSERT_EQ(fm[""].size(), 1); 
 
    // test that manifest does not add the same file twice
    manifestPtr -> addFile(std::string("./" + TEST_FILE_PATH + file_suffixes[0]).c_str());
       
    p = manifestPtr -> sources();
    fm = manifestPtr -> folders();
 
    ASSERT_EQ(fm[""].size(), 1);
    ASSERT_EQ(p.size(), 1);
 
    // test that manifest does not add files that don't exist
    ASSERT_THROW({
        try {
            manifestPtr -> addFile(std::string("./" + TEST_FILE_PATH + file_suffixes[1]).c_str());
        } catch (const std::exception& ex) {
            EXPECT_STREQ(std::string("./" + TEST_FILE_PATH + file_suffixes[1] + ": Could not locate in PATH.").c_str(), ex.what());
            throw;
        }
    }, std::runtime_error);
 
    p = manifestPtr -> sources();
    fm = manifestPtr -> folders();
 
    ASSERT_EQ(fm[""].size(), 1);
    ASSERT_EQ(p.size(), 1);
 
    // test that manifest cannot have file added after finalizing
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
 
    p = manifestPtr -> sources();
    fm = manifestPtr -> folders();
 
    ASSERT_EQ(fm[""].size(), 1);
    ASSERT_EQ(p.size(), 1);
}

//////

TEST_F(CTIManifestUnitTest, addBinary) {

    // test that no files exist at start
    PathMap p = manifestPtr -> sources();
    ASSERT_EQ(p.size(), 0);
 
    FoldersMap fm = manifestPtr -> folders();
    ASSERT_EQ(fm.size(), 0);
     
    // test that a binary can be added
    ASSERT_NO_THROW(manifestPtr -> addBinary("./unit_tests", Manifest::DepsPolicy::Ignore));
 
    // test that the binary was actually added to memory
    p = manifestPtr -> sources();
    ASSERT_EQ(p[cti::getNameFromPath(cti::findPath("./unit_tests"))], cti::findPath("./unit_tests"));
 
    // test that there is only one data file in memory
    ASSERT_EQ(p.size(), 1);
 
    // test that binary was added to relevant folder
    fm = manifestPtr -> folders();
 
    // test that folder data is actually in memory
    ASSERT_EQ(*(fm["bin"].begin()), cti::getNameFromPath(cti::findPath("./unit_tests"))); 
 
    // test that there was no excess folder data in memory
    ASSERT_EQ(fm.size(), 1);
    ASSERT_EQ(fm["bin"].size(), 1); 
 
    // test that a non-binary file can't be added via addBinary
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
 
    p = manifestPtr -> sources();
    fm = manifestPtr -> folders();
 
    ASSERT_EQ(fm["bin"].size(), 1);
    ASSERT_EQ(p.size(), 1);
 
    // test that the same binary file can't be added twice via addBinary 
    ASSERT_NO_THROW(manifestPtr -> addBinary("./unit_tests", Manifest::DepsPolicy::Ignore));
 
    p = manifestPtr -> sources();
    fm = manifestPtr -> folders();
 
    ASSERT_EQ(fm["bin"].size(), 1);
    ASSERT_EQ(p.size(), 1);
 
    // test that manifest does not add binaries that don't exist
    ASSERT_THROW({
        try {
            manifestPtr -> addBinary(std::string("./" + TEST_FILE_PATH + file_suffixes[1]).c_str());
        } catch (const std::exception& ex) {
            EXPECT_STREQ(std::string("./" + TEST_FILE_PATH + file_suffixes[1] + ": Could not locate in PATH.").c_str(), ex.what());
 	   throw;
        }
    }, std::runtime_error);
 
    p = manifestPtr -> sources();
    fm = manifestPtr -> folders();
 
    ASSERT_EQ(fm["bin"].size(), 1);
    ASSERT_EQ(p.size(), 1);
 
    // test that manifest can't add binaries after finalizing
    manifestPtr -> finalize();
    ASSERT_THROW({
        try {
            manifestPtr -> addBinary("./unit_tests", Manifest::DepsPolicy::Ignore);
        } catch (const std::exception& ex) {
            EXPECT_STREQ("Attempted to modify previously shipped manifest!", ex.what());
 	   throw;
        }
    }, std::runtime_error);
 
    p = manifestPtr -> sources();
    fm = manifestPtr -> folders();
 
    ASSERT_EQ(fm["bin"].size(), 1);
    ASSERT_EQ(p.size(), 1);
}

TEST_F(CTIManifestUnitTest, addLibrary) {

    // test that no files exist at start
    PathMap p = manifestPtr -> sources();
    ASSERT_EQ(p.size(), 0);
 
    FoldersMap fm = manifestPtr -> folders();
    ASSERT_EQ(fm.size(), 0);
    
    std::ofstream f1;
    f1.open(std::string(TEST_FILE_PATH + file_suffixes[0]).c_str());
    if(!f1.is_open()) {
        FAIL () << "Failed to make fake library file";
    }
    f1 << "I'm a library";
    f1.close();
 
    ASSERT_NO_THROW(manifestPtr -> addLibrary(std::string("./" + TEST_FILE_PATH + file_suffixes[0]).c_str(), Manifest::DepsPolicy::Ignore));
 
    // test that the library data was actually added to memory
    p = manifestPtr -> sources();
    ASSERT_EQ(p[cti::getNameFromPath(cti::findLib("./" + TEST_FILE_PATH + file_suffixes[0]))], cti::findLib("./" + TEST_FILE_PATH + file_suffixes[0]));
 
    // test that there is only one data file in memory
    ASSERT_EQ(p.size(), 1);
 
    // test that file was added to relevant folder
    fm = manifestPtr -> folders();
 
    // test that file folder data is actually in memory
    ASSERT_EQ(*(fm["lib"].begin()), cti::getNameFromPath(cti::findLib("./" + TEST_FILE_PATH + file_suffixes[0]))); 
 
    // test that there was no excess folder data in memory
    ASSERT_EQ(fm.size(), 1);
    ASSERT_EQ(fm["lib"].size(), 1); 
 
    // test that manifest does not add the same library again
    manifestPtr -> addLibrary(std::string("./" + TEST_FILE_PATH + file_suffixes[0]).c_str(), Manifest::DepsPolicy::Ignore);
    
    p = manifestPtr -> sources();
    fm = manifestPtr -> folders();
 
    ASSERT_EQ(fm["lib"].size(), 1);
    ASSERT_EQ(p.size(), 1);
 
    // test that manifest does not add libraries that don't exist
    ASSERT_THROW({
        try {
            manifestPtr -> addLibrary(std::string("./" + TEST_FILE_PATH + file_suffixes[1]).c_str());
        } catch (const std::exception& ex) {
            EXPECT_STREQ(std::string("./" + TEST_FILE_PATH + file_suffixes[1] + ": Could not locate in LD_LIBRARY_PATH or system location.").c_str(), ex.what());
 	   throw;
        }
    }, std::runtime_error);
 
    p = manifestPtr -> sources();
    fm = manifestPtr -> folders();
 
    ASSERT_EQ(fm["lib"].size(), 1);
    ASSERT_EQ(p.size(), 1);
 
    // test that a library can't be added after manifest shipped
 
    manifestPtr -> finalize();
    ASSERT_THROW({
        try {
            manifestPtr -> addLibrary(std::string("./" + TEST_FILE_PATH + file_suffixes[0]).c_str(), Manifest::DepsPolicy::Ignore);
        } catch (const std::exception& ex) {
            EXPECT_STREQ("Attempted to modify previously shipped manifest!", ex.what());
 	   throw;
        }
    }, std::runtime_error);
 
    p = manifestPtr -> sources();
    fm = manifestPtr -> folders();
 
    ASSERT_EQ(fm["lib"].size(), 1);
    ASSERT_EQ(p.size(), 1);
}

TEST_F(CTIManifestUnitTest, addLibDir) {
    // test that no files exist at start
    PathMap p = manifestPtr -> sources();
    ASSERT_EQ(p.size(), 0);
 
    FoldersMap fm = manifestPtr -> folders();
    ASSERT_EQ(fm.size(), 0);
 
 
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
 
    // test that the file data was actually added to memory
    p = manifestPtr -> sources();
    ASSERT_EQ(p[cti::getNameFromPath(cti::getRealPath(tdir))], cti::getRealPath(tdir));
 
    // test that there is only one data file in memory
    ASSERT_EQ(p.size(), 1);
 
    // test that file was added to relevant folder
    fm = manifestPtr -> folders();
 
    // test that file folder data is actually in memory
    ASSERT_EQ(*(fm["lib"].begin()), cti::getNameFromPath(cti::getRealPath(tdir))); 
 
    // test that there was no excess folder data in memory
    ASSERT_EQ(fm.size(), 1);
    ASSERT_EQ(fm["lib"].size(), 1); 
 
    // test that manifest does not readd libdir's
    manifestPtr -> addLibDir(tdir);
  
    p = manifestPtr -> sources();
    fm = manifestPtr -> folders();
 
    ASSERT_EQ(fm["lib"].size(), 1);
    ASSERT_EQ(p.size(), 1);
 
    // test that manifest does not add libdirs that don't exist
    ASSERT_THROW({
        try {
            manifestPtr -> addLibDir(std::string("./" + TEST_FILE_PATH + file_suffixes[1]).c_str());
        } catch (const std::exception& ex) {
            EXPECT_STREQ(std::string("realpath failed.").c_str(), ex.what());
 	   throw;
        }
    }, std::runtime_error);
 
    p = manifestPtr -> sources();
    fm = manifestPtr -> folders();
 
    ASSERT_EQ(fm["lib"].size(), 1);
    ASSERT_EQ(p.size(), 1);
 
    // test how manifest behaves adding a libdir after already being finalized
    manifestPtr -> finalize();
    ASSERT_THROW({
        try {
            manifestPtr -> addLibDir(tdir);
        } catch (const std::exception& ex) {
            EXPECT_STREQ("Attempted to modify previously shipped manifest!", ex.what());
            throw;
        }
    }, std::runtime_error);
 
    p = manifestPtr -> sources();
    fm = manifestPtr -> folders();
 
    ASSERT_EQ(fm["lib"].size(), 1);
    ASSERT_EQ(p.size(), 1);
 
    // cleanup 'library' files
    remove(f_temp_path.c_str());
    rmdir(tdir);
}
