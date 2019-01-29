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

#include <unordered_map>
#include <memory>

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

	/* app host setup accessors */

	// return the string version of the job identifer
	virtual std::string getJobId() const = 0;

	// get hostname where the job launcher was started
	virtual std::string getLauncherHostname() const = 0;

	// get backend base directory used for staging
	virtual std::string getToolPath() const = 0;

	// get backend directory where the pmi_attribs file can be found
	virtual std::string getAttribsPath() const = 0;

	/* app file setup accessors */

	// extra wlm specific binaries required by backend library
	virtual std::vector<std::string> getExtraBinaries() const { return {}; }

	// extra wlm specific libraries required by backend library
	virtual std::vector<std::string> getExtraLibraries() const { return {}; }

	// extra wlm specific library directories required by backend library
	virtual std::vector<std::string> getExtraLibDirs() const { return {}; }

	// extra wlm specific files required by backend library
	virtual std::vector<std::string> getExtraFiles() const { return {}; }

	/* running app information accessors */

	// retrieve number of PEs in app
	virtual size_t getNumPEs() const = 0;

	// retrieve number of compute nodes in app
	virtual size_t getNumHosts() const = 0;

	// get hosts list for app
	virtual std::vector<std::string> getHostnameList() const = 0;

	// get PE rank/host placement for app
	virtual std::vector<CTIHost> getHostsPlacement() const = 0;

	/* running app interaction interface */

	// release app from barrier
	virtual void releaseBarrier() = 0;

	// kill application
	virtual void kill(int signal) = 0;

	// ship package to backends
	virtual void shipPackage(std::string const& tarPath) const = 0;

	// start backend tool daemon
	virtual void startDaemon(CArgArray argv) = 0;

};

// This is the wlm interface that all wlm implementations should implement.
class Frontend {
public: // types
	using AppId   = cti_app_id_t;

private: // variables
	std::unordered_map<AppId, std::unique_ptr<App>> appList;
	static const AppId APP_ERROR = 0;

private: // helpers
	static AppId newAppId() noexcept {
		static AppId nextId = 1;
		return nextId++;
	}
protected: // derivable helpers
	AppId registerAppPtr(std::unique_ptr<App>&& created) {
		auto const appId = newAppId();
		appList.emplace(appId, std::move(created));
		return appId;
	}

public: // impl.-specific interface
	// wlm type
	virtual cti_wlm_type
	getWLMType() const = 0;

	// launch application without barrier
	virtual AppId
	launch(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
	       CStr inputFile, CStr chdirPath, CArgArray env_list) = 0;

	// launch application with barrier
	virtual AppId
	launchBarrier(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
	              CStr inputFile, CStr chdirPath, CArgArray env_list) = 0;

	// get hostname of current node
	virtual std::string
	getHostname(void) const = 0;


public: // app management
	App&
	getApp(AppId appId) const {
		auto app = appList.find(appId);
		if (app != appList.end()) {
			return *(app->second);
		}

		throw std::runtime_error("invalid appId: " + std::to_string(appId));
	}

	bool
	appIsValid(AppId appId) const {
		return appList.find(appId) != appList.end();
	}

	void
	deregisterApp(AppId appId) {
		appList.erase(appId);
	}
};

/* internal frontend management */
Frontend& _cti_getCurrentFrontend();

std::string const& _cti_getLdAuditPath(void);
std::string const& _cti_getOverwatchPath(void);
std::string const& _cti_getDlaunchPath(void);
std::string const& _cti_getCfgDir(void);