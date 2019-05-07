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

    // check for conflicts in session
    switch (sess->hasFileConflict(folder, realName, filePath)) {
        case Session::Conflict::None: break;
        case Session::Conflict::AlreadyAdded: return;
        case Session::Conflict::NameOverwrite:
            throw std::runtime_error(realName + ": session conflict");
    }

    // add to manifest registry
    m_folders[folder].emplace(realName);
    m_sourcePaths[realName] = filePath;
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
        // Need access to FE object
        auto sess = getOwningSession();
        auto app = sess->getOwningApp();
        auto& fe = app->getFrontend();
        addLibDeps(filePath, fe.getLdAuditPath());
    }
}

void
Manifest::addLibrary(const std::string& rawName, DepsPolicy depsPolicy) {
    enforceValid();
    // get path and real name of file
    const std::string filePath(cti::findLib(rawName));
    const std::string realName(cti::getNameFromPath(filePath));

    auto sess = getOwningSession();

    // check for conflicts in session
    switch (sess->hasFileConflict("lib", realName, filePath)) {
        case Session::Conflict::None:
            // add to manifest registry
            m_folders["lib"].emplace(realName);
            m_sourcePaths[realName] = filePath;
            break;
        case Session::Conflict::AlreadyAdded:
            return;
        case Session::Conflict::NameOverwrite:
            /* the launcher handles by pointing its LD_LIBRARY_PATH to the
                override directory containing the conflicting lib.
            */
            if (m_ldLibraryOverrideFolder.empty()) {
                m_ldLibraryOverrideFolder = "lib." + std::to_string(m_instance);
            }

            m_folders[m_ldLibraryOverrideFolder].emplace(realName);
            m_sourcePaths[realName] = filePath;
            break;
    }

    // add libraries if needed
    if (depsPolicy == DepsPolicy::Stage) {
        // Need access to FE object
        auto app = sess->getOwningApp();
        auto& fe = app->getFrontend();
        addLibDeps(filePath, fe.getLdAuditPath());
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
