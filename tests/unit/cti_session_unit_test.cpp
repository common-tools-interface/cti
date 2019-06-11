/******************************************************************************\
 * cti_session_unit_test.cpp - Session unit tests for CTI
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

#include "cti_session_unit_test.hpp"

using ::testing::Return;
using ::testing::_;
using ::testing::Invoke;
using ::testing::WithoutArgs;

static constexpr char const* mockArgv[] = {"/usr/bin/true", nullptr};
CTISessionUnitTest::CTISessionUnitTest()
    : CTIAppUnitTest{}
    , sessionPtr{std::make_shared<Session>(*mockApp)}
{ 
    fileSuffixes.push_back("1.txt");
}

CTISessionUnitTest::~CTISessionUnitTest()
{
    for(std::string suf : fileSuffixes) {
        remove(std::string(testFilePath + suf).c_str());
    }
}

///////

TEST_F(CTISessionUnitTest, getStagePath) {

    //Ensure after creation session has a stage path
    ASSERT_STRNE("", sessionPtr -> getStagePath().c_str());
}

TEST_F(CTISessionUnitTest, getOwningApp) {

    //Confirm the app is valid at the start of program
    ASSERT_NE(sessionPtr -> getOwningApp(), nullptr);
}

TEST_F(CTISessionUnitTest, createManifest) {

    //Ensure session can create a manifest w/o runtime:error
    ASSERT_NO_THROW(sessionPtr -> createManifest());
}


// due to tight coupling this mostly tests manifest 
TEST_F(CTISessionUnitTest, sendManifest) {

     std::shared_ptr<Manifest> testMan = (sessionPtr -> createManifest()).lock();
     
     // create a test file to add to the manifest so it can be shipped properly
     std::ofstream f1;
     f1.open(std::string(testFilePath + fileSuffixes[0]).c_str());
     if(!f1.is_open()) {
        FAIL() << "Could not create test file";
     }
     f1 << "f1";
     f1.close();
     
     ASSERT_NO_THROW(testMan -> sendManifest());
     
     // test that manifest can't have files added after shipped
     ASSERT_THROW({
        try {
            testMan -> addFile(std::string("./" + testFilePath + fileSuffixes[0]).c_str());
	} catch (std::exception& ex) {
            EXPECT_STREQ("Attempted to modify previously shipped manifest!", ex.what());
	    throw;
	}
     }, std::runtime_error);
}

TEST_F(CTISessionUnitTest, getSessionLockFiles) {

    // test that there are no session lock files when no manifests shipped
    ASSERT_EQ(0, int((sessionPtr -> getSessionLockFiles()).size()));

    // create a manifest, send it, and check that lock files have changed.
    std::shared_ptr<Manifest> testMan = (sessionPtr -> createManifest()).lock();

    // create a file to add to manifest so it can be validly sent.
    std::ofstream f1;
    f1.open(std::string(testFilePath + fileSuffixes[0]).c_str());
    if(!f1.is_open()) {
        FAIL() << "Couldn't make test file";
    }
    f1 << "f1";
    f1.close();

    testMan -> addFile(std::string("./" + testFilePath + fileSuffixes[0]).c_str());
    testMan -> sendManifest();
    // test that there is a session lock file for the newly shipped manifest
    ASSERT_EQ(1, int((sessionPtr -> getSessionLockFiles()).size()));
}

TEST_F(CTISessionUnitTest, finalize) {

    // test finalize when no manifests shipped
    ASSERT_NO_THROW(sessionPtr -> finalize());
    std::shared_ptr<Manifest> testMan = (sessionPtr -> createManifest()).lock();
   
    // create a test file to add to the manifest so it can be shipped properly
    std::ofstream f1;
    f1.open(std::string(testFilePath + fileSuffixes[0]).c_str());
    if(!f1.is_open()) {
       FAIL() << "Could not create test file";
    }
    f1 << "f1";
    f1.close();
    
    ASSERT_NO_THROW(testMan -> sendManifest());

    // test finalize when a manifest has been shipped
    ASSERT_NO_THROW(sessionPtr -> finalize());
}
