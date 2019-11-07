/******************************************************************************\
 * Frontend.cpp - ALPS specific frontend library functions.
 *
 * Copyright 2014-2019 Cray Inc. All Rights Reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 *  - Redistributions of source code must retain the above
 *copyright notice, this list of conditions and the following
 *disclaimer.
 *
 *  - Redistributions in binary form must reproduce the above
 *copyright notice, this list of conditions and the following
 *disclaimer in the documentation and/or other materials
 *provided with the distribution.
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

#include <fstream>
#include <algorithm>
#include <functional>
#include <numeric>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <sys/socket.h>
#include <netdb.h>

#include "alps/apInfo.h"

// Pull in manifest to properly define all the forward declarations
#include "transfer/Manifest.hpp"

#include "ALPS/Frontend.hpp"

#include "useful/cti_argv.hpp"
#include "useful/cti_execvp.hpp"
#include "useful/cti_split.hpp"
#include "useful/cti_wrappers.hpp"

/* helper functions */

static auto
getSvcNid()
{
    static auto const svcNid = []() {
        auto result = int{};

        // Open NID file
        auto alpsXTNidFile = std::fstream{};
        alpsXTNidFile.open(ALPS_XT_NID, std::ios_base::in);

        // Read NID from file
        alpsXTNidFile >> result;

        return result;
    }();

    return svcNid;
}

/* ALPSFrontend implementation */

bool
ALPSFrontend::isSupported()
{
    try {
        return !cti::findPath(APRUN).empty();
    } catch (...) {
        return false;
    }
}

std::weak_ptr<App>
ALPSFrontend::launchBarrier(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
    CStr inputFile, CStr chdirPath, CArgArray env_list)
{
    throw std::runtime_error("not implemented: "+ std::string{__func__});
}

std::weak_ptr<App>
ALPSFrontend::registerJob(size_t numIds, ...)
{
    throw std::runtime_error("not implemented: "+ std::string{__func__});
}

std::string
ALPSFrontend::getHostname() const
{
    // Format NID into XC hostname
    return cti::cstr::asprintf(ALPS_XT_HOSTNAME_FMT, getSvcNid());
}

ALPSFrontend::AprunInfo
ALPSFrontend::getAprunInfo(uint64_t aprunId)
{
    // Produce managed objects from libALPS allocating function
    auto const getAppInfoVer2Err = [&](uint64_t aprunId) {
        // Allocate and fill ALPS data structures from libALPS
        auto alpsAppInfo = std::make_unique<appInfo_t>();
        cmdDetail_t *alpsCmdDetail;
        placeNodeList_t *alpsPlaceNodeList;
        char *libAlpsError = nullptr;

        alpsAppInfo->apid = aprunId;

        // Run and check result
        if (m_libAlps.alps_get_appinfo_ver2_err(aprunId,
            alpsAppInfo.get(), &alpsCmdDetail, &alpsPlaceNodeList,
            &libAlpsError, (int*)nullptr) != 1) {

            throw std::runtime_error((libAlpsError != nullptr)
                ? libAlpsError
                : "alps_get_appinfo_ver2_err");
        }

        return std::make_tuple(
            std::move(alpsAppInfo),
            cti::take_pointer_ownership(std::move(alpsCmdDetail),     ::free),
            cti::take_pointer_ownership(std::move(alpsPlaceNodeList), ::free)
        );
    };

    // Run libALPS query
    auto [alpsAppInfo, alpsCmdDetail, alpsPlaceNodeList] = getAppInfoVer2Err(aprunId);

    // Fill result
    auto result = AprunInfo
        { .alpsAppInfo = std::move(alpsAppInfo)
        , .hostsPlacement = {}
    };

    result.hostsPlacement.reserve(result.alpsAppInfo->numPlaces);
    for (int i = 0; i < result.alpsAppInfo->numPlaces; i++) {
        result.hostsPlacement.emplace_back( CTIHost
            { cti::cstr::asprintf(ALPS_XT_HOSTNAME_FMT, alpsPlaceNodeList.get()[i].nid)
            , (size_t)alpsPlaceNodeList.get()[i].numPEs
        } );
    }

    return result;
}

uint64_t
ALPSFrontend::getApid(pid_t aprunPid) const
{
    // Look up apid using NID and aprun PID
    return m_libAlps.alps_get_apid(getSvcNid(), aprunPid);
}

ALPSFrontend::ALPSFrontend()
    : m_libAlpsPath{cti::accessiblePath(getBaseDir() + "/lib/" + ALPS_FE_LIB_NAME)}
    , m_libAlps{m_libAlpsPath}
{}

/* ALPSApp implementation */

std::string
ALPSApp::getJobId() const
{
    return std::to_string(m_alpsAppInfo->apid);
}

std::string
ALPSApp::getLauncherHostname() const
{
    return cti::cstr::asprintf(ALPS_XT_HOSTNAME_FMT, m_alpsAppInfo->aprunNid);
}

size_t
ALPSApp::getNumPEs() const
{
    return std::accumulate(m_hostsPlacement.begin(), m_hostsPlacement.end(), 0,
        [](int total, CTIHost const& host) { return total + host.numPEs; });
}

size_t
ALPSApp::getNumHosts() const
{
    return m_hostsPlacement.size();
}

std::vector<std::string>
ALPSApp::getHostnameList() const
{
    auto result = std::vector<std::string>{};

    std::transform(m_hostsPlacement.begin(), m_hostsPlacement.end(), std::back_inserter(result),
        [](CTIHost const& host) { return host.hostname; });

    return result;
}

void
ALPSApp::releaseBarrier()
{
    throw std::runtime_error("not implemented: "+ std::string{__func__});
}

void
ALPSApp::kill(int signal)
{
    throw std::runtime_error("not implemented: "+ std::string{__func__});
}

void
ALPSApp::shipPackage(std::string const& tarPath) const
{
    throw std::runtime_error("not implemented: "+ std::string{__func__});
}

void
ALPSApp::startDaemon(const char* const args[])
{
    throw std::runtime_error("not implemented: "+ std::string{__func__});
}

uint64_t
ALPSApp::getApid() const
{
    return m_alpsAppInfo->apid;
}

cti_aprunProc_t
ALPSApp::get_cti_aprunProc_t() const
{
    return cti_aprunProc_t
        { m_alpsAppInfo->apid
        , m_launcherPid
    };
}

int
ALPSApp::getAlpsOverlapOrdinal() const
{
    char *libAlpsError = nullptr;

    auto const result = m_libAlpsRef.alps_get_overlap_ordinal(m_alpsAppInfo->apid, &libAlpsError, nullptr);

    if (result < 0) {
        throw std::runtime_error((libAlpsError != nullptr)
            ? libAlpsError
            : "alps_get_overlap_ordinal");
    }

    return result;
}

ALPSApp::ALPSApp(ALPSFrontend& fe, ALPSFrontend::AprunInfo&& aprunInfo, FE_daemon::MPIRResult&& mpirData)
    : App{fe}

    , m_launcherPid{}
    , m_daemonAppId{}
    , m_beDaemonSent{false}

    , m_alpsAppInfo{std::move(aprunInfo.alpsAppInfo)}
    , m_hostsPlacement{std::move(aprunInfo.hostsPlacement)}
    , m_libAlpsRef{fe.m_libAlps}

    , m_toolPath{}
    , m_attribsPath{}
    , m_stagePath{}
    , m_extraFiles{}
{
    throw std::runtime_error("not implemented: "+ std::string{__func__});
}

ALPSApp::ALPSApp(ALPSFrontend& fe, ALPSFrontend::AprunInfo&& aprunInfo)
    : App{fe}

    , m_libAlpsRef{fe.m_libAlps}
{
    throw std::runtime_error("not implemented: "+ std::string{__func__});
}

ALPSApp::ALPSApp(ALPSFrontend& fe, const char * const launcher_argv[], int stdout_fd,
    int stderr_fd, const char *inputFile, const char *chdirPath, const char * const env_list[])
    : App{fe}

    , m_libAlpsRef{fe.m_libAlps}
{
    throw std::runtime_error("not implemented: "+ std::string{__func__});
}

ALPSApp::~ALPSApp()
{
    throw std::runtime_error("not implemented: "+ std::string{__func__});
}