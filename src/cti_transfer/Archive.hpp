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

	struct FdHandle {
		const int fd;
		FdHandle(const std::string& path) : fd(open(path.c_str(), O_RDONLY)) {}
		~FdHandle() { close(fd); }
		operator const void*() const { return (fd >= 0) ? this : nullptr; }
	};

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
	~Archive();

	// finalize and return path to tarball; after, only valid operations are to destruct
	const std::string& finalize() {
		archPtr.reset();
		entryScratchpad.reset();
		{ std::string cmd("cp " + archivePath + " /cray/css/users/adangelo/stash/valgrind4hpc/tests/archive.tar"); system(cmd.c_str()); }
		return archivePath;
	}
	// create archive directory entry
	void addDirEntry(const std::string& dirPath);
	// set up archive entry and call addDir / addFile based on stat
	void addPath(const std::string& entryPath, const std::string& path);
};