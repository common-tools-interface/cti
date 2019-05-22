/*********************************************************************************\
 * Session: state object representing a remote staging directory where packages
 *  of files to support CTI programs are unpacked and stored. Manages conflicts
 *  between files present on remote systems and in-progress, unshipped file
 *  lists( Manifests).
 *
 * Copyright 2019 Cray Inc.  All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
 *
 *********************************************************************************/

#pragma once

#include <string>
#include <vector>

// pointer management
#include <memory>

// file registry
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <set>
using FoldersMap = std::map<std::string, std::set<std::string>>;
using PathMap = std::unordered_map<std::string, std::string>;
using FolderFilePair = std::pair<std::string, std::string>;

#include "frontend/Frontend.hpp"

// Forward declarations
class Manifest;

class Session final : public std::enable_shared_from_this<Session> {
public: // types
    enum class Conflict {
        None = 0,     // file is not present in session
        AlreadyAdded, // same file already in session
        NameOverwrite // different file already in session; would overwrite
    };

private: // variables
    // Pointer to owning App
    std::weak_ptr<App>          m_AppPtr;
    // Sessions have direct ownership of all Manifest objects underneath it.
    std::unordered_set<std::shared_ptr<Manifest>>
                                m_manifests;
    // True if we need to check for App dependencies
    bool                        m_add_requirements;
    // Counter to track unique manifests
    int                         m_manifestCnt;
    // Counter to track shipped manifests
    int                         m_seqNum;
    FoldersMap                  m_folders;
    PathMap                     m_sourcePaths;
    std::string const           m_stageName;
    std::string const           m_stagePath;
    std::string const           m_wlmType;
    std::string                 m_ldLibraryPath;

private: // helper functions
    // generate a staging path according to CTI path rules
    static std::string generateStagePath();
    // merge manifest contents into directory of transfered files, return list of
    // duplicate files that don't need to be shipped
    std::vector<FolderFilePair> mergeTransfered(const FoldersMap& folders,
        const PathMap& paths);
    // Finalize and package manifest into archive. Ship to compute nodes.
    // This is a helper function to be used by sendManifest and startDaemon
    std::string shipManifest(std::shared_ptr<Manifest>& mani);

public: // interface
    std::shared_ptr<App> getOwningApp() {
        if (auto app = m_AppPtr.lock()) { return app; }
        throw std::runtime_error("Owning app is no longer valid.");
    }
    std::string getStagePath() { return m_stagePath; }
    // log function for Manifest / RemoteSession
    template <typename... Args>
    inline void writeLog(char const* fmt, Args&&... args) const {
        if (auto sp = m_AppPtr.lock()) {
            sp->writeLog(fmt, std::forward<Args>(args)...);
        }
    }
    // Return a list of lock file dependencies for backend to guarantee ordering.
    std::vector<std::string> getSessionLockFiles();
    // create new manifest associated with this session
    std::weak_ptr<Manifest> createManifest();
    // Used to ship a manifest to the computes and extract it.
    void sendManifest(std::shared_ptr<Manifest>& mani);
    // Used to ship a manifest and execute a tool daemon contained within.
    void execManifest(std::shared_ptr<Manifest>& mani, const char * const daemon,
        const char * const daemonArgs[], const char * const envVars[]);
    // drop reference to an existing manifest. This invalidates the manifest
    // and prevents it from being shipped.
    void removeManifest(std::shared_ptr<Manifest>& mani);
    /* fileName: filename as provided by client
        realName: basename following symlinks
        conflict rules:
        - realName not in the provided folder -> None
        - realpath(fileName) == realpath(realName) -> AlreadyAdded
        - realpath(fileName) != realpath(realName) -> NameOverwrite
        */
    Conflict hasFileConflict(const std::string& folderName, const std::string& realName,
        const std::string& candidatePath) const;

    // launch daemon to cleanup remote files. this must be called outside App destructor
    void finalize();

public: // interface
    Session(App& activeApp);
    ~Session() = default;
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) = delete;
    Session& operator=(Session&&) = delete;
};