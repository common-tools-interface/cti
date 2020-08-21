/******************************************************************************\
 * Archive.hpp
 *
 * (C) Copyright 2013-2020 Hewlett Packard Enterprise Development LP.
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

#pragma once

#include <unistd.h>
#include <fcntl.h>

#include <archive.h>
#include <archive_entry.h>

#include <memory>
#include <functional>

class Archive {
private: // variables
    static constexpr size_t CTI_BLOCK_SIZE = 65536;

    std::unique_ptr<struct archive,       decltype(&archive_write_free)> m_archPtr;
    std::unique_ptr<struct archive_entry, decltype(&archive_entry_free)> m_entryScratchpad;
    std::unique_ptr<char[]> m_readBuf;
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
        m_readBuf.reset();
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
