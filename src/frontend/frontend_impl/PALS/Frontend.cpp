/******************************************************************************\
 * Frontend.cpp - PALS specific frontend library functions.
 *
 * Copyright 2014-2019 Cray Inc. All Rights Reserved.
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
#include "cti_argv_defs.hpp"

#include <memory>
#include <thread>

#include "transfer/Manifest.hpp"

#include "PALS/Frontend.hpp"

#include "useful/cti_websocket.hpp"

// Boost JSON
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

/* helper functions */

// Load token from disk
static constexpr auto defaultTokenFilePattern = "~/.config/cray/tokens/%s.%s";
static auto readAccessToken(std::string const& tokenPath)
{
    namespace pt = boost::property_tree;

    // Load and parse token JSON
    auto root = pt::ptree{};
    try {
        pt::read_json(tokenPath, root);
    } catch (pt::json_parser::json_parser_error const& parse_ex) {
        throw std::runtime_error("failed to read token file at " + tokenPath);
    }

    // Extract token value
    try {
        return root.get<std::string>("access_token");
    } catch (pt::ptree_bad_path const& path_ex) {
        throw std::runtime_error("failed to find 'access_token' in file " + tokenPath);
    }
}

/* PALSFrontend implementation */

bool
PALSFrontend::isSupported()
{
    return false;
}

std::weak_ptr<App>
PALSFrontend::launchBarrier(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
    CStr inputFile, CStr chdirPath, CArgArray env_list)
{
    throw std::runtime_error{"not implemented: " + std::string{__func__}};
}

std::weak_ptr<App>
PALSFrontend::registerJob(size_t numIds, ...)
{
    throw std::runtime_error{"not implemented: " + std::string{__func__}};
}

std::string
PALSFrontend::getHostname() const
{
    throw std::runtime_error{"not implemented: " + std::string{__func__}};
}

std::string
PALSFrontend::getLauncherName() const
{
    throw std::runtime_error{"not implemented: " + std::string{__func__}};
}

PALSFrontend::PALSFrontend()
{
    throw std::runtime_error{"not implemented: " + std::string{__func__}};
}

/* PALSApp implementation */

std::string
PALSApp::getJobId() const
{
    throw std::runtime_error{"not implemented: " + std::string{__func__}};
}

std::string
PALSApp::getLauncherHostname() const
{
    throw std::runtime_error{"not implemented: " + std::string{__func__}};
}

bool
PALSApp::isRunning() const
{
    throw std::runtime_error{"not implemented: " + std::string{__func__}};
}

size_t
PALSApp::getNumPEs() const
{
    throw std::runtime_error{"not implemented: " + std::string{__func__}};
}

size_t
PALSApp::getNumHosts() const
{
    throw std::runtime_error{"not implemented: " + std::string{__func__}};
}

std::vector<std::string>
PALSApp::getHostnameList() const
{
    throw std::runtime_error{"not implemented: " + std::string{__func__}};
}

void
PALSApp::releaseBarrier()
{
    throw std::runtime_error{"not implemented: " + std::string{__func__}};
}

void
PALSApp::kill(int signal)
{
    throw std::runtime_error{"not implemented: " + std::string{__func__}};
}

void
PALSApp::shipPackage(std::string const& tarPath) const
{
    throw std::runtime_error{"not implemented: " + std::string{__func__}};
}

void
PALSApp::startDaemon(const char* const args[])
{
    throw std::runtime_error{"not implemented: " + std::string{__func__}};
}

PALSApp::PALSApp(PALSFrontend& fe, std::string const& apId)
    : App{fe}
    , m_beDaemonSent{}
    , m_hostsPlacement{}
    , m_toolPath{}
    , m_attribsPath{}
    , m_stagePath{}
    , m_extraFiles{}
{
    throw std::runtime_error{"not implemented: " + std::string{__func__}};
}

PALSApp::PALSApp(PALSFrontend& fe, const char * const launcher_argv[], int stdout_fd,
    int stderr_fd, const char *inputFile, const char *chdirPath, const char * const env_list[])
    : App{fe}
    , m_beDaemonSent{}
    , m_hostsPlacement{}
    , m_toolPath{}
    , m_attribsPath{}
    , m_stagePath{}
    , m_extraFiles{}
{
    throw std::runtime_error{"not implemented: " + std::string{__func__}};
}

PALSApp::~PALSApp()
{}