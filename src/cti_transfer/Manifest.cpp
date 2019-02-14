#include <unistd.h>
#include <fcntl.h>

#include "Manifest.hpp"
#include "Session.hpp"

#include "useful/cti_wrappers.hpp"

// promote session pointer to a shared pointer (otherwise throw)
static std::shared_ptr<Session> getSessionHandle(std::weak_ptr<Session> sessionPtr) {
	if (auto liveSession = sessionPtr.lock()) { return liveSession; }
	throw std::runtime_error("Manifest is not valid, already shipped.");
}

// add dynamic library dependencies to manifest
void Manifest::addLibDeps(const std::string& filePath) {
	// get array of library paths using ld_val libArray helper
	if (auto libArray = ld_val::getFileDependencies(filePath)) {
		// add to manifest
		for (char** elem = libArray.get(); *elem != nullptr; elem++) {
			addLibrary(*elem, Manifest::DepsPolicy::Ignore);
		}
	}
}

/* manifest file add implementations */

// add file to manifest if session reports no conflict on realname / filepath
void Manifest::checkAndAdd(const std::string& folder, const std::string& filePath,
	const std::string& realName) {

	auto liveSession = getSessionHandle(sessionPtr);

	// check for conflicts in session
	switch (liveSession->hasFileConflict(folder, realName, filePath)) {
		case Session::Conflict::None: break;
		case Session::Conflict::AlreadyAdded: return;
		case Session::Conflict::NameOverwrite:
			throw std::runtime_error(realName + ": session conflict");
	}

	// add to manifest registry
	folders[folder].emplace(realName);
	sourcePaths[realName] = filePath;
}

void Manifest::addBinary(const std::string& rawName, DepsPolicy depsPolicy) {
	// get path and real name of file
	const std::string filePath(cti::findPath(rawName));
	const std::string realName(cti::getNameFromPath(filePath));

	// check permissions
	if (access(filePath.c_str(), R_OK | X_OK)) {
		throw std::runtime_error("Specified binary does not have execute permissions.");
	}

	checkAndAdd("bin", filePath, realName);

	// add libraries if needed
	if (depsPolicy == DepsPolicy::Stage) {
		addLibDeps(filePath);
	}
}

void Manifest::addLibrary(const std::string& rawName, DepsPolicy depsPolicy) {
	// get path and real name of file
	const std::string filePath(cti::findLib(rawName));
	const std::string realName(cti::getNameFromPath(filePath));

	auto liveSession = getSessionHandle(sessionPtr);

	// check for conflicts in session
	switch (liveSession->hasFileConflict("lib", realName, filePath)) {
		case Session::Conflict::None:
			// add to manifest registry
			folders["lib"].emplace(realName);
			sourcePaths[realName] = filePath;
		case Session::Conflict::AlreadyAdded: return;
		case Session::Conflict::NameOverwrite:
			/* the launcher handles by pointing its LD_LIBRARY_PATH to the 
				override directory containing the conflicting lib.
			*/
			if (ldLibraryOverrideFolder.empty()) {
				ldLibraryOverrideFolder = "lib." + std::to_string(instanceCount);
			}

			folders[ldLibraryOverrideFolder].emplace(realName);
			sourcePaths[realName] = filePath;
	}

	// add libraries if needed
	if (depsPolicy == DepsPolicy::Stage) {
		addLibDeps(filePath);
	}
}

void Manifest::addLibDir(const std::string& rawPath) {
	// get real path and real name of directory
	const std::string realPath(cti::getRealPath(rawPath));
	const std::string realName(cti::getNameFromPath(realPath));

	checkAndAdd("lib", realPath, realName);
}

void Manifest::addFile(const std::string& rawName) {
	// get path and real name of file
	const std::string filePath(cti::findPath(rawName));
	const std::string realName(cti::getNameFromPath(filePath));

	checkAndAdd("", filePath, realName);
}

/* manifest transfer / wlm interface implementations */

#include "Archive.hpp"

RemotePackage Manifest::createAndShipArchive(const std::string& archiveName,
	std::shared_ptr<Session>& liveSession) {

	// todo: block signals handle race with file creation

	// create and fill archive
	Archive archive(liveSession->configPath + "/" + archiveName);

	// setup basic archive entries
	archive.addDirEntry(liveSession->stageName);
	archive.addDirEntry(liveSession->stageName + "/bin");
	archive.addDirEntry(liveSession->stageName + "/lib");
	archive.addDirEntry(liveSession->stageName + "/tmp");

	// add files to archive
	for (auto folderIt : folders) {
		for (auto fileIt : folderIt.second) {
			const std::string destPath(liveSession->stageName + "/" + folderIt.first +
				"/" + fileIt);
			DEBUG_PRINT("ship " << instanceCount << ": addPath(" << destPath << ", " << sourcePaths.at(fileIt) << ")" << std::endl);
			archive.addPath(destPath, sourcePaths.at(fileIt));
		}
	}

	// ship package and finalize manifest with session
	RemotePackage remotePackage(archive.finalize(), archiveName, liveSession, instanceCount);

	// todo: end block signals

	return remotePackage;
} // archive destructor: remove archive from local disk

RemotePackage Manifest::finalizeAndShip() {
	auto liveSession = getSessionHandle(sessionPtr);

	const std::string archiveName(liveSession->stageName + std::to_string(instanceCount) +
		".tar");

	// create the hidden name for the cleanup file. This will be checked by future
	// runs to try assisting in cleanup if we get killed unexpectedly. This is cludge
	// in an attempt to cleanup. The ideal situation is to be able to tell the kernel
	// to remove the tarball if the process exits, but no mechanism exists today that
	// I know about.
	{ const std::string cleanupFilePath(liveSession->configPath + "/." + archiveName);
		auto cleanupFileHandle = UniquePtrDestr<FILE>(
			fopen(cleanupFilePath.c_str(), "w"), fclose);
		pid_t pid = getpid();
		if (fwrite(&pid, sizeof(pid), 1, cleanupFileHandle.get()) != 1) {
			throw std::runtime_error("fwrite to cleanup file failed.");
		}
	}
	
	// merge manifest into session and get back list of files to remove
	DEBUG_PRINT("finalizeAndShip " << instanceCount << ": merge into session" << std::endl);
	{ auto toRemove = liveSession->mergeTransfered(folders, sourcePaths);
		for (auto folderFilePair : toRemove) {
			folders[folderFilePair.first].erase(folderFilePair.second);
			sourcePaths.erase(folderFilePair.second);
		}
	}
	if (!ldLibraryOverrideFolder.empty()) {
		liveSession->pushLdLibraryPath(ldLibraryOverrideFolder);
	}

	// create manifest archive with libarchive and ship package with WLM transfer function
	auto remotePackage = createAndShipArchive(archiveName, liveSession);

	// manifest is finalized, no changes can be made
	invalidate();
	return remotePackage;
}
