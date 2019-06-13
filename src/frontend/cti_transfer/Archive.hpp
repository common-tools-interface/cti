/******************************************************************************\
 * Archive.hpp
 *
 * Copyright 2013-2019 Cray Inc.    All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
 *
 ******************************************************************************/

#pragma once

#include <unistd.h>
#include <fcntl.h>

#include <archive.h>
#include <archive_entry.h>

#include <memory>
#include <functional>

class Archive {
private: // variables
    std::unique_ptr<struct archive,       decltype(&archive_write_free)> m_archPtr;
    std::unique_ptr<struct archive_entry, decltype(&archive_entry_free)> m_entryScratchpad;
    std::string const m_archivePath;

private: // functions
    // refresh the entry scratchpad without reallocating
    decltype(m_entryScratchpad)& freshEntry();
    // recursively add directory and contents to archive
    void addDir(const std::string& entryPath, const std::string& dirPath);
    // block-copy file to archive
    void addFile(const std::string& entryPath, const std::string& filePath);

public: // interface
    // finalize and return path to tarball; after, only valid operations are to destruct
    const std::string& finalize() {
        m_archPtr.reset();
        m_entryScratchpad.reset();
        return m_archivePath;
    }
    // create archive directory entry
    void addDirEntry(const std::string& dirPath);
    // set up archive entry and call addDir / addFile based on stat
    void addPath(const std::string& entryPath, const std::string& path);

public: // Constructor/destructors
    // create archive on disk and set format
    Archive(const std::string& archivePath);
    // remove archive from disk
    ~Archive() {
        if (!m_archivePath.empty()) {
            unlink(m_archivePath.c_str());
        }
    }
    // Explicitly delete move/copy constructors.
    // Archive has file ownership on disk.
    // We must ensure that gets cleaned up during destructor.
    Archive(const Archive&) = delete;
    Archive& operator=(const Archive&) = delete;
    Archive(Archive&&) = delete;
    Archive& operator=(Archive&&) = delete;
};
