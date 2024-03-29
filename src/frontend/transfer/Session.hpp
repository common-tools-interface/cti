/*********************************************************************************\
 * Session: state object representing a remote staging directory where packages
 *  of files to support CTI programs are unpacked and stored. Manages conflicts
 *  between files present on remote systems and in-progress, unshipped file
 *  lists( Manifests).
 *
 * Copyright 2013-2020 Hewlett Packard Enterprise Development LP.
 * SPDX-License-Identifier: Linux-OpenIB
 ******************************************************************************/

#pragma once

#include <string>
#include <vector>

// pointer management
#include <memory>

#include "frontend/Frontend.hpp"

#include "Manifest.hpp"

class Session : public std::enable_shared_from_this<Session> {
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
    // merge manifest contents into directory of transfered files, return list of
    // duplicate files that don't need to be shipped
    std::vector<FolderFilePair> mergeTransfered(const FoldersMap& folders,
        const PathMap& paths);
    // Finalize and package manifest into archive. Ship to compute nodes.
    // This is a helper function to be used by sendManifest and startDaemon
    std::string shipManifest(std::shared_ptr<Manifest> mani);
    // drop reference to an existing manifest. This invalidates the manifest
    // and prevents it from being shipped.
    void removeManifest(std::shared_ptr<Manifest> const& mani);
    // ensure that Session owns given Manifest
    void verifyOwnership(std::shared_ptr<Manifest> const& manifest) {
        if (m_manifests.count(manifest) == 0) {
            throw std::invalid_argument("manifest not owned by session");
        }
    }

private: // friend shared with Manifest
    // Used to ship a manifest to the computes and extract it.
    void sendManifest(std::shared_ptr<Manifest> const& mani);
    friend void Manifest::sendManifest();

    // Used to ship a manifest and execute a tool daemon contained within.
    void execManifest(std::shared_ptr<Manifest> const& mani, const char * const daemon,
        const char * const daemonArgs[], const char * const envVars[]);
    friend void Manifest::execManifest(const char * const daemon, const char * const daemonArgs[],
        const char * const envVars[]);

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

    // Get manifest count and advance
    int nextManifestCount() { return ++m_manifestCnt; }

    // Return a list of lock file dependencies for backend to guarantee ordering.
    std::vector<std::string> getSessionLockFiles();
    // create new manifest associated with this session
    std::weak_ptr<Manifest> createManifest();
    // get canonical source path of file for conflict detection. if not present, return empty string
    std::string getSourcePath(const std::string& folderName, const std::string& realName) const;

    // launch daemon to cleanup remote files. this must be called outside App destructor
    void finalize();

private:
    // shared_from_this is used in implementation, must enforce that Session is shared_ptr-only
    Session(std::shared_ptr<App> owningApp);
public: // interface
    static std::shared_ptr<Session> make_Session(std::shared_ptr<App> owningApp);
    virtual ~Session() = default;
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) = delete;
    Session& operator=(Session&&) = delete;
};
