/******************************************************************************\
 * Manifest.hpp - In-progress file list that is owned by a session.
 *                It is the sessions responsibility to ship a manifest.
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

#pragma once

#include <string>

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

// Forward declarations
class Session;

class Manifest : public std::enable_shared_from_this<Manifest> {
public: // types
    enum class DepsPolicy {
        Ignore = 0,
        Stage
    };

private: // variables
    std::weak_ptr<Session>  m_sessionPtr;
    int const               m_instance;
    FoldersMap              m_folders;
    PathMap                 m_sourcePaths;
    std::string             m_ldLibraryOverrideFolder;
    bool                    m_isValid;

private: // helper functions
    void enforceValid() {
        if (!m_isValid) {
            throw std::runtime_error("Attempted to modify previously shipped manifest!");
        }
    }

    // add dynamic library dependencies to manifest
    void addLibDeps(const std::string& filePath, const std::string& auditPath);

    // if no session conflicts, add to manifest (otherwise throw)
    void checkAndAdd(const std::string& folder, const std::string& filePath,
        const std::string& realName);

public: // interface
    // Get a shared_ptr to the owning session
    std::shared_ptr<Session> getOwningSession() {
        if (auto sess = m_sessionPtr.lock()) { return sess; }
        throw std::runtime_error("Owning Session is no longer valid.");
    }

    // add files and optionally their dependencies to manifest
    void addBinary(const std::string& rawName, DepsPolicy depsPolicy = DepsPolicy::Stage);
    void addLibrary(const std::string& rawName, DepsPolicy depsPolicy = DepsPolicy::Stage);
    void addLibDir(const std::string& rawPath);
    void addFile(const std::string& rawName);

    // Getters
    // Returns true if there is nothing in the manifest
    bool empty() { return m_sourcePaths.empty(); }
    size_t instance() { return m_instance; }
    FoldersMap& folders() { return m_folders; }
    PathMap& sources() { return m_sourcePaths; }
    std::string& extraLibraryPath() { return m_ldLibraryOverrideFolder; }

    // Used to ship a manifest to the computes and extract it.
    void sendManifest();

    // Ship a manifest and execute a tool daemon contained within.
    void execManifest(const char * const daemon, const char * const daemonArgs[],
        const char * const envVars[]);

    // Called by the session when it ships the manifest. This denotes that the manifest
    // is no longer modifyable
    void finalize() { m_isValid = false; }

protected:
    // can only construct via make_Manifest
    Manifest(std::shared_ptr<Session> owningSession);
public:
    static std::shared_ptr<Manifest> make_Manifest(std::shared_ptr<Session> owningSession);
    ~Manifest() = default;
    Manifest(const Manifest&) = delete;
    Manifest& operator=(const Manifest&) = delete;
    Manifest(Manifest&&) = delete;
    Manifest& operator=(Manifest&&) = delete;
};
