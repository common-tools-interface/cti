/******************************************************************************\
 * cti_session_unit_test.cpp - Session unit tests for CTI
 *
 * Copyright 2019 Cray Inc.  All Rights Reserved.
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
    , sessionPtr{Session::make_Session(mockApp)}
{
    file_names.push_back(TEST_FILE_NAME + "1.txt");
    for(auto&& fil : file_names) {
        remove(fil.c_str());
    }
}

CTISessionUnitTest::~CTISessionUnitTest()
{
    for(auto&& fil : file_names) {
        remove(fil.c_str());
    }
}

///////

TEST_F(CTISessionUnitTest, getStagePath) {

    //Ensure after creation session has a stage path
    ASSERT_STRNE(sessionPtr -> getStagePath().c_str(), "");
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

    auto test_manifest  = (sessionPtr -> createManifest()).lock();
    auto test_manifest2 = (sessionPtr -> createManifest()).lock(); 
    // create a test file to add to the manifest so it can be shipped properly
    {
         std::ofstream f1;
         f1.open(file_names[0].c_str());
         if(!f1.is_open()) {
         FAIL() << "Could not create test file";
	 }
         f1 << "f1";
         f1.close();
    }

    ASSERT_NO_THROW(test_manifest -> addFile(std::string("./" + file_names[0]).c_str()));
    ASSERT_NO_THROW(test_manifest -> sendManifest());

    // test that duplicate manifests aren't shipped
    ASSERT_NO_THROW(test_manifest2 -> addFile(std::string("./" + file_names[0]).c_str()));
    ASSERT_NO_THROW(test_manifest2 -> sendManifest());

    // test that manifest can't have files added after shipped
    ASSERT_THROW({
        try {
            test_manifest -> addFile(std::string("./" + file_names[0]).c_str());
	} catch (std::exception& ex) {
            EXPECT_STREQ("Attempted to modify previously shipped manifest!", ex.what());
	    throw;
        }
    }, std::runtime_error);

    // test that shipping an empty manifest doesn't do anything
    auto empty_manifest = (sessionPtr -> createManifest()).lock();
    ASSERT_NO_THROW(empty_manifest -> sendManifest());
}

TEST_F(CTISessionUnitTest, getSessionLockFiles) {

    // test that there are no session lock files when no manifests shipped
    ASSERT_EQ(0, int((sessionPtr -> getSessionLockFiles()).size()));

    // create a manifest, send it, and check that lock files have changed.
    auto test_manifest = (sessionPtr -> createManifest()).lock();

    // create a file to add to manifest so it can be validly sent.
    {
        std::ofstream f1;
        f1.open(file_names[0].c_str());
        if(!f1.is_open()) {
            FAIL() << "Couldn't make test file";
        }
        f1 << "f1";
        f1.close();
    }

    test_manifest -> addFile(std::string("./" + file_names[0]).c_str());
    test_manifest -> sendManifest();
    // test that there is a session lock file for the newly shipped manifest
    ASSERT_EQ(1, int((sessionPtr -> getSessionLockFiles()).size()));

    // test that no lock file is created for an empty manifest
    auto empty_manifest = (sessionPtr -> createManifest()).lock();
    empty_manifest -> sendManifest();
    ASSERT_EQ(1, int((sessionPtr -> getSessionLockFiles()).size()));
}

TEST_F(CTISessionUnitTest, finalize_file) {

    // test finalize when no manifests shipped
    ASSERT_NO_THROW(sessionPtr -> finalize());

    // test how finalize behaves with a non-empty manifest
    auto test_manifest = (sessionPtr -> createManifest()).lock();

    // create a test file to add to the manifest so it can be shipped properly
    {
        std::ofstream f1;
        f1.open(file_names[0].c_str());
        if(!f1.is_open()) {
            FAIL() << "Could not create test file";
        }
        f1 << "f1";
        f1.close();
    }
    test_manifest -> addFile(std::string("./" + file_names[0]).c_str());
    test_manifest -> sendManifest();

    // test finalize when a manifest has been shipped
    ASSERT_NO_THROW(sessionPtr -> finalize());
}

TEST_F(CTISessionUnitTest, finalize_empty) {

    // test finalize when no manifests shipped
    ASSERT_NO_THROW(sessionPtr -> finalize());

    // test how finalize behaves with an empty manifest
    auto empty_manifest = (sessionPtr -> createManifest()).lock();
    ASSERT_NO_THROW(empty_manifest -> sendManifest());

    // test finalize when a manifest has been shipped
    ASSERT_NO_THROW(sessionPtr -> finalize());
}

TEST_F(CTISessionUnitTest, finalize_dup) {

    //test finalize when two manifests have the same file
    ASSERT_NO_THROW(sessionPtr -> finalize());

    auto test_manifest  = (sessionPtr -> createManifest()).lock();
    auto test_manifest2 = (sessionPtr -> createManifest()).lock();
    // create a test file to add to the manifest so it can be shipped properly
    {
        std::ofstream f1;
        f1.open(file_names[0].c_str());
        if(!f1.is_open()) {
            FAIL() << "Could not create test file";
        }
        f1 << "f1";
        f1.close();
    }

    test_manifest -> addFile(std::string("./" + file_names[0]).c_str());
    test_manifest2 -> addFile(std::string("./" + file_names[0]).c_str());

    test_manifest -> sendManifest();
    test_manifest2 -> sendManifest();

    {
        std::ofstream f1;
        f1.open(file_names[0].c_str());
        if(!f1.is_open()) {
            FAIL() << "Could not create test file";
        }
        f1 << "notf1";
        f1.close();
    }

    // test finalize when a manifest has been shipped
    ASSERT_NO_THROW(sessionPtr -> finalize());
}
