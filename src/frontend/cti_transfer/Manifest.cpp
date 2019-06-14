/******************************************************************************\
 * Manifest.cpp - In-progress file list that is owned by a session.
 *                It is the sessions responsibility to ship a manifest.
 *
 * Copyright 2013-2019 Cray Inc.    All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
 *
 ******************************************************************************/

// This pulls in config.h
#include "cti_defs.h"

#include "Manifest.hpp"
#include "Session.hpp"

#include "useful/cti_wrappers.hpp"

// add dynamic library dependencies to manifest
void
Manifest::addLibDeps(const std::string& filePath, const std::string& auditPath) {
    // get array of library paths using ld_val libArray helper
    if (auto libArray = cti::ld_val::getFileDependencies(filePath, auditPath)) {
        // add to manifest
        for (char** elem = libArray.get(); *elem != nullptr; elem++) {
            addLibrary(*elem, Manifest::DepsPolicy::Ignore);
        }
    }
}

/* manifest file add implementations */

// add file to manifest if session reports no conflict on realname / filepath
void
Manifest::checkAndAdd(const std::string& folder, const std::string& filePath,
    const std::string& realName) {

    auto sess = getOwningSession();

    // canonicalize path
    auto const canonicalPath = cti::getRealPath(filePath);

    // check session for files shipped to the same subfolder
    auto const shippedSourcePath = sess->getSourcePath(folder, realName);
    if (!shippedSourcePath.empty()) {

        // check for conflicts
        if (shippedSourcePath.compare(canonicalPath)) {

            // source paths are not the same, conflict
            throw std::runtime_error("conflict: shipping " + canonicalPath + " to " +
                folder + "/" + realName + " would conflict with file already shipped from " + shippedSourcePath);
        }

        // source paths are the same, the same file was already shipped
    }

    // add to manifest registry
    m_folders[folder].emplace(realName);
    m_sourcePaths[realName] = canonicalPath;
}

void
Manifest::addBinary(const std::string& rawName, DepsPolicy depsPolicy) {
    enforceValid();
    // get path and real name of file
    const std::string filePath(cti::findPath(rawName));
    const std::string realName(cti::getNameFromPath(filePath));

    // check permissions
    if (!cti::fileHasPerms(filePath.c_str(), R_OK|X_OK)) {
        throw std::runtime_error("Specified binary does not have execute permissions.");
    }

    checkAndAdd("bin", filePath, realName);

    // add libraries if needed
    if (depsPolicy == DepsPolicy::Stage) {
        // Avoid hodling onto promotion of pointers through recursion call
        auto const ldAuditPath = getOwningSession()->getOwningApp()->getFrontend().getLdAuditPath();
        addLibDeps(filePath, ldAuditPath);
    }
}

void
Manifest::addLibrary(const std::string& rawName, DepsPolicy depsPolicy) {
    enforceValid();
    // get path and real name of file
    const std::string filePath(cti::findLib(rawName));
    const std::string realName(cti::getNameFromPath(filePath));

    auto sess = getOwningSession();

    try {

        // check for conflicts in session and add to library directory
        checkAndAdd("lib", filePath, realName);

    } catch (std::exception const& ex) {

        if (depsPolicy == DepsPolicy::Stage) {
            // this was an explicitly-added library, inform user of error
            throw;

        } else {
            // this was an implicitly-added library, use library override directory
            // the launcher handles this by pointing its LD_LIBRARY_PATH to the override directory
            if (m_ldLibraryOverrideFolder.empty()) {
                m_ldLibraryOverrideFolder = "lib." + std::to_string(m_instance);
            }

            // add to library override directory
            checkAndAdd(m_ldLibraryOverrideFolder, filePath, realName);
        }
    }

    // add library dependencies if needed
    if (depsPolicy == DepsPolicy::Stage) {
        // Avoid hodling onto promotion of pointers through recursion call
        auto const ldAuditPath = sess->getOwningApp()->getFrontend().getLdAuditPath();
        addLibDeps(filePath, ldAuditPath);
    }
}

void
Manifest::addLibDir(const std::string& rawPath) {
    enforceValid();
    // get real path and real name of directory
    const std::string realPath(cti::getRealPath(rawPath));
    const std::string realName(cti::getNameFromPath(realPath));

    checkAndAdd("lib", realPath, realName);
}

void
Manifest::addFile(const std::string& rawName) {
    enforceValid();
    // get path and real name of file
    const std::string filePath(cti::findPath(rawName));
    const std::string realName(cti::getNameFromPath(filePath));

    checkAndAdd("", filePath, realName);
}

void
Manifest::sendManifest() {
    getOwningSession()->sendManifest(shared_from_this());
}

void
Manifest::execManifest(const char * const daemon, const char * const daemonArgs[],
    const char * const envVars[]) {
    getOwningSession()->execManifest(shared_from_this(),
        daemon, daemonArgs, envVars);
}

Manifest::Manifest(std::shared_ptr<Session> owningSession)
    : m_sessionPtr{owningSession}
    , m_instance{owningSession->nextManifestCount()}
    , m_isValid{true}
{ }

std::shared_ptr<Manifest> Manifest::make_Manifest(std::shared_ptr<Session> owningSession)
{
    struct ConstructibleManifest : public Manifest {
        ConstructibleManifest(std::shared_ptr<Session> owningSession)
            : Manifest{std::move(owningSession)}
        {}
    };
    return std::make_shared<ConstructibleManifest>(std::move(owningSession));
}