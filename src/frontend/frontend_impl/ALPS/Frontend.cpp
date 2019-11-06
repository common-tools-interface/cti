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

#include <algorithm>

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

/* constructors / destructors */

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
    throw std::runtime_error("not implemented: "+ std::string{__func__});
}

uint64_t
ALPSFrontend::getApid(pid_t aprunPid) const
{
    throw std::runtime_error("not implemented: "+ std::string{__func__});
}

ALPSFrontend::ALPSFrontend()
    : libAlpsPath{cti::accessiblePath(getBaseDir() + "/lib/" + ALPS_FE_LIB_NAME)}
    , libAlps{libAlpsPath}
{
    throw std::runtime_error("not implemented: "+ std::string{__func__});
}

void
ALPSApp::redirectOutput(int stdoutFd, int stderrFd)
{
    throw std::runtime_error("not implemented: "+ std::string{__func__});
}

std::string
ALPSApp::getJobId() const
{
    throw std::runtime_error("not implemented: "+ std::string{__func__});
}

std::string
ALPSApp::getLauncherHostname() const
{
    throw std::runtime_error("not implemented: "+ std::string{__func__});
}

size_t
ALPSApp::getNumPEs() const
{
    throw std::runtime_error("not implemented: "+ std::string{__func__});
}

size_t
ALPSApp::getNumHosts() const
{
    throw std::runtime_error("not implemented: "+ std::string{__func__});
}

std::vector<std::string>
ALPSApp::getHostnameList() const
{
    throw std::runtime_error("not implemented: "+ std::string{__func__});
}

std::vector<CTIHost>
ALPSApp::getHostsPlacement() const
{
    throw std::runtime_error("not implemented: "+ std::string{__func__});
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

int
ALPSApp::getAlpsOverlapOrdinal() const
{
    throw std::runtime_error("not implemented: "+ std::string{__func__});
}

ALPSApp::ALPSApp(ALPSFrontend& fe, FE_daemon::MPIRResult&& mpirData)
    : App{fe}
{
    throw std::runtime_error("not implemented: "+ std::string{__func__});
}

ALPSApp::ALPSApp(ALPSFrontend& fe, uint32_t jobid, uint32_t stepid)
    : App{fe}
{
    throw std::runtime_error("not implemented: "+ std::string{__func__});
}

ALPSApp::ALPSApp(ALPSFrontend& fe, const char * const launcher_argv[], int stdout_fd,
    int stderr_fd, const char *inputFile, const char *chdirPath, const char * const env_list[])
    : App{fe}
{
    throw std::runtime_error("not implemented: "+ std::string{__func__});
}

ALPSApp::~ALPSApp()
{
    throw std::runtime_error("not implemented: "+ std::string{__func__});
}