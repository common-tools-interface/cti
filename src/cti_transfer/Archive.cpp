#include <dirent.h>
#include <string.h>

#include <archive.h>
#include <archive_entry.h>

#include "Archive.hpp"

using ArchPtr = Archive::ArchPtr;
using EntryPtr = Archive::EntryPtr;

EntryPtr& Archive::freshEntry() {
	if (m_entryScratchpad) {
		archive_entry_clear(m_entryScratchpad.get());
		return m_entryScratchpad;
	} else {
		throw std::runtime_error(m_archivePath + " tried to add a path after finalizing");
	}
}

Archive::Archive(const std::string& archivePath) :
	m_archPtr{archive_write_new(), archive_write_free},
	m_entryScratchpad{archive_entry_new(), archive_entry_free},
	m_archivePath{archivePath} {

	if (m_archPtr == nullptr) {
		throw std::runtime_error("archive_write_new_failed");
	}

	if (archive_write_set_format_gnutar(m_archPtr.get()) != ARCHIVE_OK) {
		throw std::runtime_error(archive_error_string(m_archPtr.get()));
	}

	// todo: block signals
	if (archive_write_open_filename(m_archPtr.get(), m_archivePath.c_str()) != ARCHIVE_OK) {
		throw std::runtime_error(archive_error_string(m_archPtr.get()));
	}
	// todo: unblock signals
}

static void archiveWriteRetry(struct archive* arch, struct archive_entry* entry) {
	while (true) {
		switch (archive_write_header(arch, entry)) {
			case ARCHIVE_RETRY:
				continue;
			case ARCHIVE_FATAL:
				throw std::runtime_error(archive_error_string(arch));
			default: return;
		}
	}
}

void Archive::addDirEntry(const std::string& entryPath) {
	auto& entryPtr = freshEntry();

	// get the current time
	struct timespec	 tv;
	clock_gettime(CLOCK_REALTIME, &tv);

	// setup the archive header
	archive_entry_set_pathname (entryPtr.get(), entryPath.c_str());
	archive_entry_set_filetype (entryPtr.get(), AE_IFDIR);
	archive_entry_set_size     (entryPtr.get(), 0);
	archive_entry_set_perm     (entryPtr.get(), S_IRWXU);
	archive_entry_set_atime    (entryPtr.get(), tv.tv_sec, tv.tv_nsec);
	archive_entry_set_birthtime(entryPtr.get(), tv.tv_sec, tv.tv_nsec);
	archive_entry_set_ctime    (entryPtr.get(), tv.tv_sec, tv.tv_nsec);
	archive_entry_set_mtime    (entryPtr.get(), tv.tv_sec, tv.tv_nsec);

	archiveWriteRetry(m_archPtr.get(), entryPtr.get());
}

void Archive::addDir(const std::string& entryPath, const std::string& dirPath) {
	if (auto dirHandle = UniquePtrDestr<DIR>(opendir(dirPath.c_str()), closedir)) {
		errno = 0;
		for (struct dirent *d = readdir(dirHandle.get()); d != nullptr;
			d = readdir(dirHandle.get())) {

			// check for failure on readdir
			if (errno != 0) {
				throw std::runtime_error(dirPath + " had readdir failure: " + 
					strerror(errno));
			}

			// make sure not . or ..
			if (!strncmp(d->d_name, ".", 1) || !strncmp(d->d_name, "..", 2)) {
				continue;
			}

			// recursively add to archive
			addPath(entryPath + "/" + d->d_name, dirPath + "/" + d->d_name);
		}
	} else {
		throw std::runtime_error(dirPath + " failed opendir call");
	}
}


struct FdHandle {
	const int fd;
	FdHandle(const std::string& path) : fd(open(path.c_str(), O_RDONLY)) {}
	~FdHandle() { close(fd); }
	operator const void*() const { return (fd >= 0) ? this : nullptr; }
};

void Archive::addFile(const std::string& entryPath, const std::string& filePath) {
	// copy data from file to archive
	if (auto fdHandle = FdHandle(filePath)) {
		const size_t CTI_BLOCK_SIZE = (1 << 8);

		char readBuf[CTI_BLOCK_SIZE];
		while (true) {
			size_t readLen = read(fdHandle.fd, readBuf, CTI_BLOCK_SIZE);
			if (readLen < 0) {
				throw std::runtime_error(filePath + " failed read call");
			} else if (readLen == 0) {
				break;
			}

			size_t writeLen = archive_write_data(m_archPtr.get(), readBuf, readLen);
			if (writeLen < 0) {
				throw std::runtime_error(filePath + " failed archive_write_data: " +
					archive_error_string(m_archPtr.get()));
			} else if (writeLen != readLen) {
				throw std::runtime_error(filePath +
					" had archive_write_data length mismatch.");
			}
		}
	}
}

void Archive::addPath(const std::string& entryPath, const std::string& path) {
	struct stat st;
	if (stat(path.c_str(), &st)) {
		throw std::runtime_error(path + " failed stat call");
	}

	// create archive entry from stat
	{ auto& entryPtr = freshEntry();
		archive_entry_copy_stat(entryPtr.get(), &st);
		archive_entry_set_pathname(entryPtr.get(), entryPath.c_str());
		archiveWriteRetry(m_archPtr.get(), entryPtr.get());
	}

	// call proper file/diradd functions
	if (S_ISDIR(st.st_mode)) {
		addDir(entryPath, path);
	} else if (!S_ISREG(st.st_mode)) {
		// This is an unsuported file and should not be added to the manifest
		throw std::runtime_error(path + " has invalid file type.");
	} else {
		addFile(entryPath, path);
	}
}