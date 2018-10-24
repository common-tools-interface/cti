#pragma once

#include <unistd.h>
#include <fcntl.h>

#include <memory>

// forward declare
struct archive;
struct archive_entry;

class Archive {
private: // types
	template <typename T>
	using UniquePtrDestr = std::unique_ptr<T, std::function<void(T*)>>;

public: // types
	using ArchPtr = UniquePtrDestr<struct archive>;
	using EntryPtr = UniquePtrDestr<struct archive_entry>;

private: // variables
	ArchPtr archPtr;
	EntryPtr entryScratchpad;
	const std::string archivePath;

private: // functions
	// refresh the entry scratchpad without reallocating
	EntryPtr& freshEntry();
	// recursively add directory and contents to archive
	void addDir(const std::string& entryPath, const std::string& dirPath);
	// block-copy file to archive
	void addFile(const std::string& entryPath, const std::string& filePath);

public: // interface
	// create archive on disk and set format
	Archive(const std::string& archivePath);

	Archive(Archive&& moved) :
		archPtr(std::move(moved.archPtr)),
		entryScratchpad(std::move(moved.entryScratchpad)),
		archivePath(std::move(moved.archivePath)) {}

	// remove archive from disk
	~Archive() {
		if (archPtr != nullptr) {
			unlink(archivePath.c_str());
		}
	}

	// finalize and return path to tarball; after, only valid operations are to destruct
	const std::string& finalize() {
		archPtr.reset();
		entryScratchpad.reset();
		return archivePath;
	}
	// create archive directory entry
	void addDirEntry(const std::string& dirPath);
	// set up archive entry and call addDir / addFile based on stat
	void addPath(const std::string& entryPath, const std::string& path);
};