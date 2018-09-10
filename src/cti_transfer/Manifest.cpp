#include <unistd.h>
#include <fcntl.h>

#include <string>

#include "Manifest.hpp"
#include "Session.hpp"

#include "cti_fe.h"
#include "useful/cti_useful.h"
#include "ld_val/ld_val.h"


/* wrappers for CTI helper functions */

static CharPtr findPath(const std::string& fileName) {
	if (auto fullPath = CharPtr(_cti_pathFind(fileName.c_str(), nullptr), free)) {
		return fullPath;
	} else { // _cti_pathFind failed with nullptr result
		throw std::runtime_error(fileName + ": Could not locate in PATH.");
	}
}

static CharPtr findLib(const std::string& fileName) {
	if (auto fullPath = CharPtr(_cti_libFind(fileName.c_str()), free)) {
		return fullPath;
	} else { // _cti_libFind failed with nullptr result
		throw std::runtime_error(fileName + ": Could not locate in LD_LIBRARY_PATH or system location.");
	}
}
static CharPtr getNameFromPath(const std::string& filePath) {
	if (auto realName = CharPtr(_cti_pathToName(filePath.c_str()), free)) {
		return realName;
	} else { // _cti_pathToName failed with nullptr result
		throw std::runtime_error("Could not convert the fullname to realname.");
	}
}

static CharPtr getRealPath(const std::string& filePath) {
	if (auto realPath = CharPtr(realpath(filePath.c_str(), nullptr), free)) {
		return realPath;
	} else { // realpath failed with nullptr result
		throw std::runtime_error("realpath failed.");
	}
}

// add dynamic library dependencies to manifest
void Manifest::addLibDeps(const std::string& filePath) {
	// get array of library paths using ld_val libArray
	if (auto libArray = StringArray(
			_cti_ld_val(filePath.c_str(), _cti_getLdAuditPath()),
		stringArrayDeleter)) {

		// add to manifest
		for (char** elem = libArray.get(); *elem != nullptr; elem++) {
			addLibrary(*elem, Manifest::DepsPolicy::Ignore);
		}
	}
}

/* manifest file add implementations */

// add file to manifest if session reports no conflict on realname / filepath
void Manifest::checkAndAdd(const std::shared_ptr<Session>& liveSession,
	const std::string& folder, const std::string& filePath, const std::string& realName) {

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
	const std::string filePath(findPath(rawName).get());
	const std::string realName(getNameFromPath(filePath).get());

	// check permissions
	if (access(filePath.c_str(), R_OK | X_OK)) {
		throw std::runtime_error("Specified binary does not have execute permissions.");
	}

	checkAndAdd(getSessionHandle(), "bin", filePath, realName);

	// add libraries if needed
	if (depsPolicy == DepsPolicy::Stage) {
		addLibDeps(filePath);
	}
}

void Manifest::addLibrary(const std::string& rawName, DepsPolicy depsPolicy) {
	auto liveSession = getSessionHandle();

	// get path and real name of file
	const std::string filePath(findLib(rawName).get());
	const std::string realName(getNameFromPath(filePath).get());

	/* TODO: We need to create a way to ship conflicting libraries. Since
		most libraries are sym links to their proper version, name collisions
		are possible. In the future, the launcher should be able to handle
		this by pointing its LD_LIBRARY_PATH to a custom directory containing
		the conflicting lib. Don't actually implement this until the need arises.
	*/

	checkAndAdd(liveSession, "lib", filePath, realName);

	// add libraries if needed
	if (depsPolicy == DepsPolicy::Stage) {
		addLibDeps(filePath);
	}
}

void Manifest::addLibDir(const std::string& rawPath) {
	// get real path and real name of directory
	const std::string realPath(getRealPath(rawPath).get());
	const std::string realName(getNameFromPath(realPath).get());

	checkAndAdd(getSessionHandle(), "lib", realPath, realName);
}

void Manifest::addFile(const std::string& rawName) {
	// get path and real name of file
	const std::string filePath(findPath(rawName).get());
	const std::string realName(getNameFromPath(filePath).get());

	checkAndAdd(getSessionHandle(), "", filePath, realName);
}

/* manifest transfer / wlm interface implementations */

#include "ArgvDefs.hpp"
#include "Archive.hpp"

#include <iostream>
std::string Manifest::shipAndFinalize() {
	auto liveSession = getSessionHandle();

	// todo: resolveManifestConflicts with liveSession
	// if no files to ship, return
	if (empty()) { return ""; }

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

	// create archive on disk
	const std::string archivePath(liveSession->configPath + "/" + archiveName);

	// todo: block signals handle race with file creation
	Archive archive(archivePath);

	// setup basic entries
	DEBUG("ship " << instanceCount << ": " << instanceCount << ": addDirEntry" << std::endl);
	archive.addDirEntry(liveSession->stageName);
	archive.addDirEntry(liveSession->stageName + "/bin");
	archive.addDirEntry(liveSession->stageName + "/lib");
	archive.addDirEntry(liveSession->stageName + "/tmp");

	// add files to archive
	for (auto folderIt : folders) {
		for (auto fileIt : folderIt.second) {
			const std::string destPath(liveSession->stageName + "/" + folderIt.first +
				"/" + fileIt);
			DEBUG("ship " << instanceCount << ": addPath(" << destPath << ", " << sourcePaths[fileIt] << ")" << std::endl);
			archive.addPath(destPath, sourcePaths[fileIt]);
		}
	}

	DEBUG("ship " << instanceCount << ": finalizing and shipping" << std::endl);
	const std::string& finalizedArchivePath = archive.finalize();
	liveSession->shipPackage(finalizedArchivePath.c_str());
	// todo: end block signals

	// manifest is finalized, no changes can be made
	sessionPtr.reset();

	return archiveName;
}

void Manifest::finalizeAndExtract() {
	auto liveSession = getSessionHandle();

	// ship with helper
	const std::string& archiveName = shipAndFinalize();
	if (archiveName.empty()) { return; }

	// create DaemonArgv
	OutgoingArgv<DaemonArgv> daemonArgv("cti_daemon");
	{ using DA = DaemonArgv;
		daemonArgv.add(DA::ApID,         liveSession->jobId);
		daemonArgv.add(DA::ToolPath,     liveSession->toolPath);
		daemonArgv.add(DA::WLMEnum,      liveSession->wlmEnum);
		daemonArgv.add(DA::ManifestName, archiveName);
		daemonArgv.add(DA::Directory,    liveSession->stageName);
		daemonArgv.add(DA::InstSeqNum,   std::to_string(instanceCount));
		if (getenv(DBG_ENV_VAR)) { daemonArgv.add(DA::Debug); };
	}

	// call transfer function with DaemonArgv
	DEBUG("finalizeAndExtract " << instanceCount << ": starting daemon" << std::endl);
	// wlm_startDaemon adds the argv[0] automatically, so argv.get() + 1 for arguments.
	liveSession->startDaemon(daemonArgv.get() + 1);

	// merge manifest into session
	DEBUG("finalizeAndExtract " << instanceCount << ": merge into session" << std::endl);
	liveSession->mergeTransfered(folders, sourcePaths);

	DEBUG("finalizeAndExtract " << instanceCount << ": done" << std::endl);
}

void Manifest::finalizeAndRun(const char * const daemonBinary, const char * const daemonArgs[], const char * const envVars[]) {
	auto liveSession = getSessionHandle();

	// add daemon binary and ship manifest files if applicable
	addBinary(daemonBinary);
	const std::string& archiveName = shipAndFinalize(); // won't add manifest arg if empty

	// get real name of daemon binary
	const std::string binaryName(getNameFromPath(findPath(daemonBinary).get()).get());

	// create DaemonArgv
	DEBUG("finalizeAndRun: creating daemonArgv for " << daemonBinary << std::endl);
	OutgoingArgv<DaemonArgv> daemonArgv("cti_daemon");
	{ using DA = DaemonArgv;
		daemonArgv.add(DA::ApID,         liveSession->jobId);
		daemonArgv.add(DA::ToolPath,     liveSession->toolPath);
		if (!liveSession->attribsPath.empty()) {
			daemonArgv.add(DA::PMIAttribsPath, liveSession->attribsPath);
		}
		daemonArgv.add(DA::WLMEnum,      liveSession->wlmEnum);
		if (!archiveName.empty()) { daemonArgv.add(DA::ManifestName, archiveName); }
		daemonArgv.add(DA::Binary,       binaryName);
		daemonArgv.add(DA::Directory,    liveSession->stageName);
		daemonArgv.add(DA::InstSeqNum,   std::to_string(instanceCount));
		daemonArgv.add(DA::Directory,    liveSession->stageName);
		if (getenv(DBG_ENV_VAR)) { daemonArgv.add(DA::Debug); };
	}

	// add env vars
	if (envVars != nullptr) {
		for (const char* const* var = envVars; *var != nullptr; var++) {
			daemonArgv.add(DaemonArgv::EnvVariable, *var);
		}
	}

	ManagedArgv rawArgVec(daemonArgv.eject());

	// add daemon arguments
	if (daemonArgs != nullptr) {
		rawArgVec.add("--");
		for (const char* const* var = daemonArgs; *var != nullptr; var++) {
			rawArgVec.add(*var);
		}
	}

	// call launch function with DaemonArgv
	DEBUG("finalizeAndRun: starting daemon" << std::endl);
	// wlm_startDaemon adds the argv[0] automatically, so argv.get() + 1 for arguments.
	liveSession->startDaemon(rawArgVec.get() + 1);

	DEBUG("finalizeAndRun: done" << std::endl);
}