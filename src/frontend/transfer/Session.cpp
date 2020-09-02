/******************************************************************************\
 * Session.cpp - Session object impl
 *
 * Copyright 2013-2020 Hewlett Packard Enterprise Development LP.
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

// This pulls in config.h
#include "cti_defs.h"
#include "cti_argv_defs.hpp"

#include "Archive.hpp"
#include "Manifest.hpp"
#include "Session.hpp"

#include "useful/cti_wrappers.hpp"

std::string
Session::generateStagePath(FE_prng& charSource) {
    std::string stageName;
    // remove placeholder Xs from DEFAULT_STAGE_DIR
    const std::string stageFormat(DEFAULT_STAGE_DIR);
    stageName = stageFormat.substr(0, stageFormat.find("X"));
    // replace 'X' characters in the stage_name string with random characters
    size_t numChars = stageFormat.length() - stageName.length();
    for (size_t i = 0; i < numChars; i++) {
        stageName.push_back(charSource.genChar());
    }
    return stageName;
}

Session::Session(std::shared_ptr<App> owningApp)
    : m_AppPtr{owningApp}
    , m_manifests{}
    , m_add_requirements{true}
    , m_manifestCnt{0}
    , m_seqNum{0}
    , m_folders{}
    , m_sourcePaths{}
    , m_stageName{generateStagePath(owningApp->getFrontend().Prng())}
    , m_stagePath{owningApp->getToolPath() + "/" + m_stageName}
    , m_wlmType{std::to_string(owningApp->getFrontend().getWLMType())}
    , m_ldLibraryPath{m_stagePath + "/lib"} // default libdir /tmp/cti_daemonXXXXXX/lib
{ }

std::shared_ptr<Session> Session::make_Session(std::shared_ptr<App> owningApp)
{
    struct ConstructibleSession : public Session {
        ConstructibleSession(std::shared_ptr<App> owningApp)
            : Session{std::move(owningApp)}
        {}
    };
    return std::make_shared<ConstructibleSession>(std::move(owningApp));
}

void Session::finalize() {
    // Check to see if we need to try cleanup on compute nodes. We bypass the
    // cleanup if we never shipped a manifest.
    if (m_seqNum == 0) {
        return;
    }
    // Get owning app
    auto app = getOwningApp();
    // Get frontend reference
    auto&& fe = app->getFrontend();
    writeLog("launchCleanup: creating daemonArgv for cleanup\n");
    // create DaemonArgv
    cti::OutgoingArgv<DaemonArgv> daemonArgv(CTI_BE_DAEMON_BINARY);
    daemonArgv.add(DaemonArgv::ApID,                app->getJobId());
    daemonArgv.add(DaemonArgv::ToolPath,            app->getToolPath());
    auto apath = app->getAttribsPath();
    if (!apath.empty()) {
        daemonArgv.add(DaemonArgv::PMIAttribsPath,  apath);
    }
    daemonArgv.add(DaemonArgv::WLMEnum,             m_wlmType);
    daemonArgv.add(DaemonArgv::Directory,           m_stageName);
    daemonArgv.add(DaemonArgv::InstSeqNum,          std::to_string(m_seqNum));
    daemonArgv.add(DaemonArgv::Clean);
    if (fe.m_debug) { daemonArgv.add(DaemonArgv::Debug); };
    auto env_vars = fe.getDefaultEnvVars();
    for (auto i : env_vars) {
        daemonArgv.add(DaemonArgv::EnvVariable, i);
    }
    // call cleanup function with DaemonArgv
    // wlm_startDaemon adds the argv[0] automatically, so argv.get() + 1 for arguments.
    writeLog("launchCleanup: launching daemon for cleanup\n");
    app->startDaemon(daemonArgv.get() + 1);
}

std::weak_ptr<Manifest>
Session::createManifest() {
    auto ret = m_manifests.emplace(Manifest::make_Manifest(shared_from_this()));
    if (!ret.second) {
        throw std::runtime_error("Failed to create new Manifest object.");
    }
    return *ret.first;
}

std::string
Session::shipManifest(std::shared_ptr<Manifest> const& mani) {
    // Get owning app
    auto app = getOwningApp();
    // Get frontend reference
    auto&& fe = app->getFrontend();
    // Check to see if we need to add baseline App dependencies
    if ( m_add_requirements ) {
        // ship WLM-specific base files
        for (auto const& path : app->getExtraBinaries()) {
            mani->addBinary(path);
        }
        for (auto const& path : app->getExtraLibraries()) {
            mani->addLibrary(path);
        }
        for (auto const& path : app->getExtraLibDirs()) {
            mani->addLibDir(path);
        }
        for (auto const& path : app->getExtraFiles()) {
            mani->addFile(path);
        }
        m_add_requirements = false;
    }

    // Finalize and drop our reference to the manifest.
    // Note we keep it alive via our shared_ptr. We do this early on
    // in case an error happens to guarantee cleanup.
    removeManifest(mani);
    // Instance number of this manifest
    auto inst = mani->instance();
    // Name of archive to create for the manifest files
    const std::string archiveName(m_stageName + std::to_string(inst) + ".tar");
    writeLog("shipManifest %d: merge into session\n", inst);
    // merge manifest into session and get back list of files to remove
    auto&& folders = mani->folders();
    auto&& sources = mani->sources();
    auto toRemove = mergeTransfered(folders, sources);
    for (auto&& folderFilePair : toRemove) {
        folders[folderFilePair.first].erase(folderFilePair.second);
        sources.erase(folderFilePair.second);
    }
    // Check to see if we have an extra LD_LIBRARY_PATH entry to deal with
    auto&& libPath = mani->extraLibraryPath();
    if ( !libPath.empty() ) {
        std::string const remoteLibDirPath{m_stagePath + "/" + libPath};
        m_ldLibraryPath = remoteLibDirPath + ":" + m_ldLibraryPath;
    }
    // todo: block signals handle race with file creation
    // create and fill archive
    // Register the cleanup file with the frontend for this archive
    fe.addFileCleanup(archiveName);
    Archive archive(fe.getCfgDir() + "/" + archiveName);
    // setup basic archive entries
    archive.addDirEntry(m_stageName);
    archive.addDirEntry(m_stageName + "/bin");
    archive.addDirEntry(m_stageName + "/lib");
    archive.addDirEntry(m_stageName + "/tmp");
    // add the unique files to archive
    for (auto&& folderIt : folders) {
        for (auto&& fileIt : folderIt.second) {
            const std::string destPath(m_stageName + "/" + folderIt.first +
                "/" + fileIt);
            writeLog("shipManifest %d: addPath(%s, %s)\n", inst, destPath.c_str(), sources.at(fileIt).c_str());
            archive.addPath(destPath, sources.at(fileIt));
        }
    }
    // ship package
    app->shipPackage(archive.finalize());
    return archiveName;
}

void
Session::sendManifest(std::shared_ptr<Manifest> const& mani) {
    verifyOwnership(mani);

    // Short circuit if there is nothing to send
    if (mani->empty()) {
        removeManifest(mani);
        return;
    }
    // get instance
    auto inst = mani->instance();
    // Get owning app
    auto app = getOwningApp();
    // Get frontend reference
    auto&& fe = app->getFrontend();
    // Ship the manifest
    auto archiveName = shipManifest(mani);
    // create DaemonArgv
    cti::OutgoingArgv<DaemonArgv> daemonArgv(CTI_BE_DAEMON_BINARY);
    daemonArgv.add(DaemonArgv::ApID,         app->getJobId());
    daemonArgv.add(DaemonArgv::ToolPath,     app->getToolPath());
    daemonArgv.add(DaemonArgv::WLMEnum,      m_wlmType);
    daemonArgv.add(DaemonArgv::ManifestName, archiveName);
    daemonArgv.add(DaemonArgv::Directory,    m_stageName);
    daemonArgv.add(DaemonArgv::InstSeqNum,   std::to_string(m_seqNum));
    if (fe.m_debug) { daemonArgv.add(DaemonArgv::Debug); };
    auto env_vars = fe.getDefaultEnvVars();
    for (auto i : env_vars) {
        daemonArgv.add(DaemonArgv::EnvVariable, i);
    }
    // call transfer function with DaemonArgv
    writeLog("sendManifest %d: starting daemon\n", inst);
    // wlm_startDaemon adds the argv[0] automatically, so argv.get() + 1 for arguments.
    app->startDaemon(daemonArgv.get() + 1);
    // Increment shipped manifests at this point. No exception was thrown.
    ++m_seqNum;
}

void
Session::execManifest(std::shared_ptr<Manifest> const& mani, const char * const daemon,
        const char * const daemonArgs[], const char * const envVars[]) {
    verifyOwnership(mani);

    // Add daemon to the manifest
    mani->addBinary(daemon);
    // Get the owning app
    auto app = getOwningApp();
    // Get frontend reference
    auto&& fe = app->getFrontend();
    // Check to see if there is a manifest to send
    std::string archiveName;
    if (!mani->empty()) {
        archiveName = shipManifest(mani);
    }
    else {
        // No need to ship an empty manifest.
        removeManifest(mani);
    }
    // get real name of daemon binary
    const std::string binaryName(cti::getNameFromPath(cti::findPath(daemon)));
    // create DaemonArgv
    writeLog("execManifest: creating daemonArgv for %s\n", daemon);
    cti::OutgoingArgv<DaemonArgv> daemonArgv(CTI_BE_DAEMON_BINARY);
    daemonArgv.add(DaemonArgv::ApID,                app->getJobId());
    daemonArgv.add(DaemonArgv::ToolPath,            app->getToolPath());
    auto apath = app->getAttribsPath();
    if (!apath.empty()) {
        daemonArgv.add(DaemonArgv::PMIAttribsPath,  apath);
    }
    if (!m_ldLibraryPath.empty()) {
        daemonArgv.add(DaemonArgv::LdLibraryPath,   m_ldLibraryPath);
    }
    daemonArgv.add(DaemonArgv::WLMEnum,             m_wlmType);
    if (!archiveName.empty()) {
        daemonArgv.add(DaemonArgv::ManifestName,    archiveName);
    }
    daemonArgv.add(DaemonArgv::Binary,              binaryName);
    daemonArgv.add(DaemonArgv::Directory,           m_stageName);
    daemonArgv.add(DaemonArgv::InstSeqNum,          std::to_string(m_seqNum));
    if (fe.m_debug) { daemonArgv.add(DaemonArgv::Debug); };
    auto env_vars = fe.getDefaultEnvVars();
    for (auto i : env_vars) {
        daemonArgv.add(DaemonArgv::EnvVariable, i);
    }
    // add env vars
    if (envVars != nullptr) {
        for (const char* const* var = envVars; *var != nullptr; var++) {
            daemonArgv.add(DaemonArgv::EnvVariable, *var);
        }
    }
    // add daemon arguments
    cti::ManagedArgv rawArgVec(daemonArgv.eject());
    if (daemonArgs != nullptr) {
        rawArgVec.add("--");
        for (const char* const* var = daemonArgs; *var != nullptr; var++) {
            rawArgVec.add(*var);
        }
    }
    // call launch function with DaemonArgv
    writeLog("execManifest: starting daemon\n");
    // wlm_startDaemon adds the argv[0] automatically, so argv.get() + 1 for arguments.
    app->startDaemon(rawArgVec.get() + 1);
    writeLog("execManifest: daemon started\n");
    // Increment shipped manifests at this point. No exception was thrown.
    ++m_seqNum;
}

void
Session::removeManifest(std::shared_ptr<Manifest> const& mani) {
    verifyOwnership(mani);

    // Finalize manifest
    mani->finalize();
    // drop the shared_ptr
    m_manifests.erase(mani);
}

std::string
Session::getSourcePath(const std::string& folderName, const std::string& realName) const {
    // has /folderName/realName been shipped to the backend?
    const std::string fileArchivePath{folderName + "/" + realName};
    auto namePathPair = m_sourcePaths.find(fileArchivePath);
    if (namePathPair != m_sourcePaths.end()) {
        return namePathPair->second;
    }

    return "";
}

std::vector<FolderFilePair>
Session::mergeTransfered(const FoldersMap& newFolders, const PathMap& newPaths) {
    std::vector<FolderFilePair> toRemove;
    for (auto&& folderContentsPair : newFolders) {
        auto const& folderName = folderContentsPair.first;
        auto const& folderContents = folderContentsPair.second;
        for (auto&& fileName : folderContents) {
            // mark fileName to be located at /folderName/fileName
            m_folders[folderName].insert(fileName);
            // map /folderName/fileName to source file path newPaths[fileName]
            const std::string fileArchivePath(folderName + "/" + fileName);
            if (m_sourcePaths.find(fileArchivePath) != m_sourcePaths.end()) {
                auto const fileSourcePath = newPaths.at(fileName);
                if (cti::isSameFile(m_sourcePaths[fileArchivePath], fileSourcePath)) {
                    // duplicate, tell manifest to not bother shipping
                    writeLog("mergeTransfered: skip already shipped %s\n", fileSourcePath.c_str());
                    toRemove.push_back(std::make_pair(folderName, fileName));
                } else {
                    writeLog("mergeTransfered: conflict: shipped %s and tried to merge %s\n",
                        m_sourcePaths.at(fileArchivePath).c_str(),
                        fileSourcePath.c_str());
                    throw std::runtime_error(
                        std::string("tried to merge transfered file ") + fileArchivePath +
                        " but it was already in the session!");
                }
            } else {
                // register new file as coming from Manifest's source
                auto const realFilePath = cti::getRealPath(newPaths.at(fileName));
                writeLog("mergeTransfered: registering new file %s\n", realFilePath.c_str());
                m_sourcePaths[fileArchivePath] = realFilePath;
            }
        }
    }
    return toRemove;
}

std::vector<std::string>
Session::getSessionLockFiles() {
    std::vector<std::string> ret;
    // Get the owning app
    auto app = getOwningApp();
    auto tp = app->getToolPath();
    // Create the lock files based on the current sequence number
    for(int i = 0; i < m_seqNum; ++i) {
        ret.emplace_back(tp + "/.lock_" + m_stageName + "_" + std::to_string(i));
    }
    return ret;
}
