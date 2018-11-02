/*********************************************************************************\
 * alps_fe.h - A header file for the alps specific frontend interface.
 *
 * Copyright 2014 Cray Inc.	All Rights Reserved.
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

#include <stdint.h>
#include <sys/types.h>

#include "frontend/Frontend.hpp"

class ALPSFrontend : public Frontend {

public: // types
	using AprunInfo = cti_aprunProc_t;

public: // wlm interface
	bool appIsValid(AppId appId) const;
	void deregisterApp(AppId appId) const;
	cti_wlm_type getWLMType() const;
	std::string const getJobId(AppId appId) const;
	AppId launch(CArgArray launcher_argv, int stdout_fd, int stderr,
	             CStr inputFile, CStr chdirPath, CArgArray env_list);
	AppId launchBarrier(CArgArray launcher_argv, int stdout_fd, int stderr,
	                    CStr inputFile, CStr chdirPath, CArgArray env_list);
	void releaseBarrier(AppId appId);
	void killApp(AppId appId, int signal);

	std::vector<std::string> const getExtraLibraries(AppId appId) const;

	void shipPackage(AppId appId, std::string const& tarPath) const;
	void startDaemon(AppId appId, CArgArray argv) const;

	size_t getNumAppPEs(AppId appId) const;
	size_t getNumAppNodes(AppId appId) const;
	std::vector<std::string> const getAppHostsList(AppId appId) const;
	std::vector<CTIHost> const getAppHostsPlacement(AppId appId) const;
	std::string const getHostName(void) const;
	std::string const getLauncherHostName(AppId appId) const;
	std::string const getToolPath(AppId appId) const;
	std::string const getAttribsPath(AppId appId) const;

public: // interface
	ALPSFrontend();
	~ALPSFrontend();
	AppId registerApid(uint64_t apid);
	uint64_t getApid(pid_t appPid);
	AprunInfo* getAprunInfo(AppId appId);
	int getAlpsOverlapOrdinal(AppId appId);
};
