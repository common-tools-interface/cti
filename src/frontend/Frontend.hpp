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

// This is the wlm interface that all wlm implementations should implement.
class Frontend {
public:  // types
	using CStr      = const char*;
	using CArgArray = const char* const[];

	using AppId     = cti_app_id_t;

	struct CTIHost {
		std::string hostname;
		size_t      numPEs;
	};

public: // interface

	// is app valid
	virtual bool appIsValid(AppId appId) const = 0;

	// mark app as not valid
	virtual void deregisterApp(AppId appId) const = 0;

	// wlm type
	virtual cti_wlm_type getWLMType() const = 0;

	// return the string version of the job identifer
	virtual std::string const getJobId(AppId appId) const = 0;

	// launch application without barrier
	virtual AppId launch(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
	                     CStr inputFile, CStr chdirPath, CArgArray env_list) = 0;

	// launch application with barrier
	virtual AppId launchBarrier(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
	                            CStr inputFile, CStr chdirPath, CArgArray env_list) = 0;

	// release app from barrier
	virtual void releaseBarrier(AppId appId) = 0;

	// kill application
	virtual void killApp(AppId appId, int signal) = 0;

	// extra wlm specific binaries required by backend library
	virtual std::vector<std::string> const getExtraBinaries(AppId appId) const { return {}; }

	// extra wlm specific libraries required by backend library
	virtual std::vector<std::string> const getExtraLibraries(AppId appId) const { return {}; }

	// extra wlm specific library directories required by backend library
	virtual std::vector<std::string> const getExtraLibDirs(AppId appId) const { return {}; }

	// extra wlm specific files required by backend library
	virtual std::vector<std::string> const getExtraFiles(AppId appId) const { return {}; }

	// ship package to backends
	virtual void shipPackage(AppId appId, std::string const& tarPath) const = 0;

	// start backend tool daemon
	virtual void startDaemon(AppId appId, CArgArray argv) const = 0;

	// retrieve number of PEs in app
	virtual size_t getNumAppPEs(AppId appId) const = 0;

	// retrieve number of compute nodes in app
	virtual size_t getNumAppNodes(AppId appId) const = 0;

	// get hosts list for app
	virtual std::vector<std::string> const getAppHostsList(AppId appId) const = 0;

	// get PE rank/host placement for app
	virtual std::vector<CTIHost> const getAppHostsPlacement(AppId appId) const = 0;

	// get hostname of current node
	virtual std::string const getHostName(void) const = 0;

	// get hostname where the job launcher was started
	virtual std::string const getLauncherHostName(AppId appId) const = 0;

	// get backend base directory used for staging
	virtual std::string const getToolPath(AppId appId) const = 0;

	// get backend directory where the pmi_attribs file can be found
	virtual std::string const getAttribsPath(AppId appId) const = 0;
};

/* internal frontend management */
Frontend& _cti_getCurrentFrontend();

std::string const& _cti_getLdAuditPath(void);
std::string const& _cti_getOverwatchPath(void);
std::string const& _cti_getDlaunchPath(void);
std::string const& _cti_getCfgDir(void);