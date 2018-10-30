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
private: // types
	using AppId     = cti_app_id_t;
	using CStr      = const char *;
	using CArgArray = const char * const[];

	struct CTIHost {
		std::string hostname;
		size_t      numPEs;
	};

public: // interface
	// wlm type
	cti_wlm_type getWLMType() const = 0;

	// return the string version of the job identifer
	std::string const getJobId() const = 0;

	// launch application without barrier
	AppId launch(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
	             CStr inputFile, CStr chdirPath, CArgArray env_list[]) = 0;

	// launch application with barrier
	AppId launchBarrier(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
	                    CStr inputFile, CStr chdirPath, CArgArray env_list[]) = 0;

	// release app from barrier
	void releaseBarrier(AppId appId) = 0;

	// kill application
	void killApp(AppId appId, int signal) = 0;

	// extra wlm specific binaries required by backend library
	std::vector<std::string> const getExtraBinaries() = 0;

	// extra wlm specific libraries required by backend library
	std::vector<std::string> const getExtraLibraries() = 0;

	// extra wlm specific library directories required by backend library
	std::vector<std::string> const getExtraLibDirs() = 0;

	// extra wlm specific files required by backend library
	std::vector<std::string> const getExtraFiles() = 0;

	// ship package to backends
	void shipPackage(AppId appId, std::string const& tarPath) = 0;

	// start backend tool daemon
	void startDaemon(AppId appId, CArgArray argv) = 0;

	// retrieve number of PEs in app
	size_t getNumAppPEs(AppId appId) = 0;

	// retrieve number of compute nodes in app
	size_t getNumAppNodes(AppId appId) = 0;

	// get hosts list for app
	std::vector<std::string> const getAppHostsList(AppId appId) = 0;

	// get PE rank/host placement for app
	std::vector<CTIHost> const getAppHostsPlacement(AppId appId) = 0;

	// get hostname of current node
	std::string const getHostName(void) = 0;

	// get hostname where the job launcher was started
	std::string const getLauncherHostName(AppId appId) = 0;

	// get backend base directory used for staging
	std::string const getToolPath(AppId appId) = 0;

	// get backend directory where the pmi_attribs file can be found
	std::string const getAttribsPath(AppId appId) = 0;
};
