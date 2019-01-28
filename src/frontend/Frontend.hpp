/*********************************************************************************\
 * Frontend.hpp - define workload manager frontend interface
 *
 * Copyright 2014-2015 Cray Inc.	All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
 *
 * $HeadURL$
 * $Date$
 * $Rev$
 * $Author$
 *
 *********************************************************************************/

#pragma once

#include <vector>
#include <string>

#include "cti_fe.h"

struct CTIHost {
	std::string hostname;
	size_t      numPEs;
};

using CStr      = const char*;
using CArgArray = const char* const[];

// This is the app instance interface that all wlms should implement.
class App {
public: // interface

	// return the string version of the job identifer
	virtual std::string const getJobId() const = 0;

	// release app from barrier
	virtual void releaseBarrier() = 0;

	// kill application
	virtual void kill(int signal) = 0;

	// extra wlm specific binaries required by backend library
	virtual std::vector<std::string> const getExtraBinaries() const { return {}; }

	// extra wlm specific libraries required by backend library
	virtual std::vector<std::string> const getExtraLibraries() const { return {}; }

	// extra wlm specific library directories required by backend library
	virtual std::vector<std::string> const getExtraLibDirs() const { return {}; }

	// extra wlm specific files required by backend library
	virtual std::vector<std::string> const getExtraFiles() const { return {}; }

	// ship package to backends
	virtual void shipPackage(std::string const& tarPath) const = 0;

	// start backend tool daemon
	virtual void startDaemon(CArgArray argv) const = 0;

	// retrieve number of PEs in app
	virtual size_t getNumPEs() const = 0;

	// retrieve number of compute nodes in app
	virtual size_t getNumHosts() const = 0;

	// get hosts list for app
	virtual std::vector<std::string> const getHostnameList() const = 0;

	// get PE rank/host placement for app
	virtual std::vector<CTIHost> const getHostPlacement() const = 0;

	// get hostname where the job launcher was started
	virtual std::string const getLauncherHostname() const = 0;

	// get backend base directory used for staging
	virtual std::string const getToolPath() const = 0;

	// get backend directory where the pmi_attribs file can be found
	virtual std::string const getAttribsPath() const = 0;
};

// This is the wlm interface that all wlm implementations should implement.
class Frontend {
public: // types
	using AppUPtr = std::unique_ptr<App>;

public: // interface

	// wlm type
	virtual cti_wlm_type getWLMType() const = 0;

	// launch application without barrier
	virtual AppUPtr launch(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
	                       CStr inputFile, CStr chdirPath, CArgArray env_list) = 0;

	// launch application with barrier
	virtual AppUPtr launchBarrier(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
	                              CStr inputFile, CStr chdirPath, CArgArray env_list) = 0;

	// get hostname of current node
	virtual std::string const getHostname(void) const = 0;
};

/* internal frontend management */
Frontend& _cti_getCurrentFrontend();

std::string const& _cti_getLdAuditPath(void);
std::string const& _cti_getOverwatchPath(void);
std::string const& _cti_getDlaunchPath(void);
std::string const& _cti_getCfgDir(void);