/******************************************************************************\
 * cti_manifest_unit_test.cpp - Manifest unit tests for CTI
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

//#include <sys/stat.h> // for changing binary permissions
#include <stdlib.h>

// CTI Transfer includes
#include "frontend/transfer/Manifest.hpp"
#include "frontend/transfer/Session.hpp"

#include "useful/cti_wrappers.hpp"

#include "cti_manifest_unit_test.hpp"

using ::testing::Return;
using ::testing::_;
using ::testing::Invoke;
using ::testing::WithoutArgs;

static constexpr char const* mockArgv[] = {"/usr/bin/true", nullptr};

CTIManifestUnitTest::CTIManifestUnitTest()
    : CTIAppUnitTest{}
    , sessionPtr{Session::make_Session(mockApp)}
    , manifestPtr{Manifest::make_Manifest(sessionPtr)}
{
    file_names.push_back(TEST_FILE_NAME + "1");
    file_names.push_back(TEST_FILE_NAME + "2");

    // remove any lingering test files
    for(auto&& fil : file_names) {
        remove(fil.c_str());
    }
}

CTIManifestUnitTest::~CTIManifestUnitTest() {
    for(auto&& fil : file_names) {
        remove(fil.c_str());
    }

    for(auto&& t_fil : temp_file_names) {
        remove(t_fil.c_str());
    }

    for(auto&& t_dir : temp_dir_names) {
        rmdir(t_dir.c_str());
    }
}

TEST_F(CTIManifestUnitTest, empty) {
    // test manifest empty at start
    ASSERT_EQ(manifestPtr->empty(), true);

    // create a test file to add to the manifest
    {
        std::ofstream f1;
        f1.open(file_names[0].c_str());
        if (!f1.is_open()) {
            FAIL() << "Failed to create file for testing addFile";
        }
        f1 << file_names[0];
    }
    manifestPtr -> addFile(std::string("./" + file_names[0]).c_str());

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
    ASSERT_EQ(manifestPtr -> instance(), 1);
}

TEST_F(CTIManifestUnitTest, sources) {
    // test the source() getter works as expected
    auto& fileSources = manifestPtr -> sources();
    ASSERT_EQ(fileSources.size(), 0);
}

TEST_F(CTIManifestUnitTest, folders) {
    // test that folders() getter works as expected
    auto& manifestFolders = manifestPtr -> folders();
    ASSERT_EQ(manifestFolders.size(), 0);
}

//////
TEST_F(CTIManifestUnitTest, addFile) {
    // test that no files exist at start
    auto& fileSources = manifestPtr -> sources();
    ASSERT_EQ(fileSources.size(), 0);

    auto& manifestFolders = manifestPtr -> folders();
    ASSERT_EQ(manifestFolders.size(), 0);

    // create a test file to add to the manifest
    {
        std::ofstream f1;
        f1.open(file_names[0].c_str());
        if (!f1.is_open()) {
            FAIL() << "Failed to create file for testing addFile";
        }
        f1 << file_names[0];
    }

    ASSERT_NO_THROW({
        try {
            manifestPtr -> addFile(std::string("./" + file_names[0]).c_str());
        } catch (std::exception& ex) {
            FAIL() << ex.what();
            throw;
        }
    });

    // test that the file data was actually added to memory
    ASSERT_EQ(fileSources[cti::getNameFromPath(cti::findPath("./" + file_names[0]))],
               cti::getRealPath("./" + file_names[0]));

    // test that there is only one data file in memory
    ASSERT_EQ(fileSources.size(), 1);

    // test that file was added to relevant folder

    // test that file folder data is actually in memory
    ASSERT_EQ(*(manifestFolders[""].begin()), cti::getNameFromPath(cti::findPath("./" + file_names[0])));

    // test that there was no excess folder data in memory
    ASSERT_EQ(manifestFolders.size(), 1);
    ASSERT_EQ(manifestFolders[""].size(), 1);

    // test that manifest does not add the same file twice
    manifestPtr -> addFile(std::string("./" + file_names[0]).c_str());

    ASSERT_EQ(manifestFolders[""].size(), 1);
    ASSERT_EQ(fileSources.size(), 1);

    // test that manifest does not add files that don't exist
    ASSERT_THROW({
        try {
            manifestPtr -> addFile(std::string("./" + file_names[1]).c_str());
        } catch (const std::exception& ex) {
            EXPECT_STREQ(std::string("./" + file_names[1] + ": Could not locate in PATH.").c_str(), ex.what());
            throw;
        }
    }, std::runtime_error);

    ASSERT_EQ(manifestFolders[""].size(), 1);
    ASSERT_EQ(fileSources.size(), 1);

    // test that manifest cannot have file added after finalizing
    manifestPtr -> finalize();

    // create a test file to attempt to add
    {
        std::ofstream f2;
        f2.open(file_names[1].c_str());
        if (!f2.is_open()) {
           FAIL() << "Failed to create file for testing addFile";
        }
        f2 << file_names[1];
    }

    ASSERT_THROW({
        try {
            manifestPtr -> addFile(std::string("./" + file_names[1]).c_str());
        } catch (const std::exception& ex) {
            EXPECT_STREQ("Attempted to modify previously shipped manifest!", ex.what());
            throw;
        }
    }, std::runtime_error);

    ASSERT_EQ(manifestFolders[""].size(), 1);
    ASSERT_EQ(fileSources.size(), 1);
}

//////

TEST_F(CTIManifestUnitTest, addBinary) {

    // test that no files exist at start
    auto& fileSources = manifestPtr -> sources();
    ASSERT_EQ(fileSources.size(), 0);

    auto& manifestFolders = manifestPtr -> folders();
    ASSERT_EQ(manifestFolders.size(), 0);

    // test that a binary can be added
    ASSERT_NO_THROW(manifestPtr -> addBinary("../test_support/one_socket", Manifest::DepsPolicy::Ignore));

    // test that the binary was actually added to memory
    ASSERT_EQ(fileSources[cti::getNameFromPath(cti::findPath("../test_support/one_socket"))], cti::getRealPath("../test_support/one_socket"));

    // test that there is only one data file in memory
    ASSERT_EQ(fileSources.size(), 1);

    // test that folder data is actually in memory
    ASSERT_EQ(*(manifestFolders["bin"].begin()), cti::getNameFromPath(cti::findPath("../test_support/one_socket")));

    // test that there was no excess folder data in memory
    ASSERT_EQ(manifestFolders["bin"].size(), 1);
    ASSERT_EQ(manifestFolders.size(), 1);

    // test that additional dependencies can be added when Manifest::DepsPolicy::Staged is used
    ASSERT_NO_THROW(manifestPtr -> addBinary("../test_support/one_socket", Manifest::DepsPolicy::Stage));
    // this should have added created the lib folder and added 12 dependencies to it

    ASSERT_EQ(manifestFolders.size(), 2);
    ASSERT_EQ(manifestFolders["bin"].size(), 1);
    ASSERT_EQ(manifestFolders["lib"].size(), 1);
    ASSERT_EQ(fileSources.size(), 2);

    ASSERT_STREQ((*(manifestFolders["lib"].begin())).c_str(), "libmessage.so");

    // test that a non-binary file can't be added via addBinary
    {
        std::ofstream f1;
        f1.open(file_names[0].c_str());
        if (!f1.is_open()) {
            FAIL() << "Could not open addBinary file";
        }
        f1 << "I'm_a_binary";
        f1.close();
    }

    ASSERT_THROW({
        try {
            manifestPtr -> addBinary(std::string("./" + file_names[0]).c_str(), Manifest::DepsPolicy::Ignore);
        } catch (const std::exception& ex) {
            EXPECT_STREQ("Specified binary does not have execute permissions.", ex.what());
            throw;
        }
    }, std::runtime_error);

    ASSERT_EQ(manifestFolders["bin"].size(), 1);
    ASSERT_EQ(fileSources.size(), 2);

    // test that the same binary file can't be added twice via addBinary
    ASSERT_NO_THROW(manifestPtr -> addBinary("./unit_tests", Manifest::DepsPolicy::Ignore));

    ASSERT_EQ(manifestFolders["bin"].size(), 2);
    ASSERT_EQ(fileSources.size(), 3);

    ASSERT_NO_THROW(manifestPtr -> addBinary("./unit_tests", Manifest::DepsPolicy::Ignore));

    ASSERT_EQ(manifestFolders["bin"].size(), 2);
    ASSERT_EQ(fileSources.size(), 3);

    // test that manifest does not add binaries that don't exist
    ASSERT_THROW({
        try {
            manifestPtr -> addBinary(std::string("./" + file_names[1]).c_str());
        } catch (const std::exception& ex) {
            EXPECT_STREQ(std::string("./" + file_names[1] + ": Could not locate in PATH.").c_str(), ex.what());
       throw;
        }
    }, std::runtime_error);

    ASSERT_EQ(manifestFolders["bin"].size(), 2);
    ASSERT_EQ(fileSources.size(), 3);

    // test that manifest can't add binaries after finalizing
    manifestPtr -> finalize();
    ASSERT_THROW({
        try {
            manifestPtr -> addBinary("../test_support/one_socket", Manifest::DepsPolicy::Ignore);
        } catch (const std::exception& ex) {
            EXPECT_STREQ("Attempted to modify previously shipped manifest!", ex.what());
       throw;
        }
    }, std::runtime_error);

    ASSERT_EQ(manifestFolders["bin"].size(), 2);
    ASSERT_EQ(fileSources.size(), 3);
}

TEST_F(CTIManifestUnitTest, addLibrary) {

    // test that no files exist at start
    auto& fileSources = manifestPtr -> sources();
    ASSERT_EQ(fileSources.size(), 0);

    auto& manifestFolders = manifestPtr -> folders();
    ASSERT_EQ(manifestFolders.size(), 0);

    {
        std::ofstream f1;
        f1.open(file_names[0].c_str());
        if(!f1.is_open()) {
            FAIL () << "Failed to make fake library file";
        }
        f1 << "I'm_a_library";
        f1.close();
    }

    ASSERT_NO_THROW(manifestPtr -> addLibrary(std::string("./" + file_names[0]).c_str(), Manifest::DepsPolicy::Ignore));

    // test that the library data was actually added to memory
    ASSERT_EQ(fileSources[cti::getNameFromPath(cti::findLib("./" + file_names[0]))], cti::getRealPath("./" + file_names[0]));

    // test that there is only one data file in memory
    ASSERT_EQ(fileSources.size(), 1);

    // test that file folder data is actually in memory
    ASSERT_EQ(*(manifestFolders["lib"].begin()), cti::getNameFromPath(cti::findLib("./" + file_names[0])));

    // test that there was no excess folder data in memory
    ASSERT_EQ(manifestFolders.size(), 1);
    ASSERT_EQ(manifestFolders["lib"].size(), 1);

    // test that manifest does not add the same library again
    manifestPtr -> addLibrary(std::string("./" + file_names[0]).c_str(), Manifest::DepsPolicy::Ignore);

    ASSERT_EQ(manifestFolders["lib"].size(), 1);
    ASSERT_EQ(fileSources.size(), 1);

    // test that manifest can add libraries with Manifest::DepsPolicy::Stage
    manifestPtr -> addLibrary("../test_support/one_socket", Manifest::DepsPolicy::Stage);
    ASSERT_EQ(manifestFolders["lib"].size(), 3);
    ASSERT_EQ(fileSources.size(), 3);
    ASSERT_STREQ((*std::next(manifestFolders["lib"].begin(), 1)).c_str(), "libmessage.so");

    // test that manifest does not add libraries that don't exist
    ASSERT_THROW({
        try {
            manifestPtr -> addLibrary(std::string("./" + file_names[1]).c_str());
        } catch (const std::exception& ex) {
            EXPECT_STREQ(std::string("./" + file_names[1] + ": Could not locate in LD_LIBRARY_PATH or system location.").c_str(), ex.what());
       throw;
        }
    }, std::runtime_error);

    ASSERT_EQ(manifestFolders["lib"].size(), 3);
    ASSERT_EQ(fileSources.size(), 3);

    // test that a library can't be added after manifest shipped

    manifestPtr -> finalize();
    ASSERT_THROW({
        try {
            manifestPtr -> addLibrary(std::string("./" + file_names[0]).c_str(), Manifest::DepsPolicy::Ignore);
        } catch (const std::exception& ex) {
            EXPECT_STREQ("Attempted to modify previously shipped manifest!", ex.what());
       throw;
        }
    }, std::runtime_error);

    ASSERT_EQ(manifestFolders["lib"].size(), 3);
    ASSERT_EQ(fileSources.size(), 3);
}

TEST_F(CTIManifestUnitTest, addLibDir) {
    // test that no files exist at start
    auto& fileSources = manifestPtr -> sources();
    ASSERT_EQ(fileSources.size(), 0);

    auto& manifestFolders = manifestPtr -> folders();
    ASSERT_EQ(manifestFolders.size(), 0);


    // create temp 'library'
    char TEMPLATE[] = "/tmp/cti-test-XXXXXX";
    char* tdir = mkdtemp(TEMPLATE);
    if(tdir == NULL) {
        FAIL() <<"Failed to create temporary library";
    }
    temp_dir_names.push_back(std::string(tdir));


    std::string f_temp_path = tdir;
    // create a temporary file for the lib dir
    // this should not be added as addLibDir does not add inner files
    {
        std::ofstream f_temp;
        f_temp_path += "/" + TEST_FILE_NAME + "_temp_file";
        f_temp.open(f_temp_path.c_str());
        if(!f_temp.is_open()) {
            FAIL() << "Failed to create temp library file";
        }
        f_temp << "I'm a library file";
        f_temp.close();
    temp_file_names.push_back(f_temp_path);
    }
    ASSERT_NO_THROW(manifestPtr -> addLibDir(tdir));

    // test that the file data was actually added to memory
    ASSERT_EQ(fileSources[cti::getNameFromPath(cti::getRealPath(tdir))], cti::getRealPath(tdir));

    // test that there is only one data file in memory
    ASSERT_EQ(fileSources.size(), 1);

    // test that file folder data is actually in memory
    ASSERT_EQ(*(manifestFolders["lib"].begin()), cti::getNameFromPath(cti::getRealPath(tdir)));

    // test that there was no excess folder data in memory
    ASSERT_EQ(manifestFolders.size(), 1);
    ASSERT_EQ(manifestFolders["lib"].size(), 1);

    // test that manifest does not readd libdir's
    manifestPtr -> addLibDir(tdir);

    ASSERT_EQ(manifestFolders["lib"].size(), 1);
    ASSERT_EQ(fileSources.size(), 1);

    // test that manifest does not add libdirs that don't exist
    ASSERT_THROW({
        try {
            manifestPtr -> addLibDir(std::string("./" + file_names[1]).c_str());
        } catch (const std::exception& ex) {
            EXPECT_STREQ("realpath failed.", ex.what());
       throw;
        }
    }, std::runtime_error);

    ASSERT_EQ(manifestFolders["lib"].size(), 1);
    ASSERT_EQ(fileSources.size(), 1);

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

    // test thta nothing was added
    ASSERT_EQ(manifestFolders["lib"].size(), 1);
    ASSERT_EQ(fileSources.size(), 1);
}
