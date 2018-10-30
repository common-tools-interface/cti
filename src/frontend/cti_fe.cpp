/******************************************************************************\
 * cti_fe.cpp - C implementation for the cti frontend.
 *
 * Copyright 2014-2016 Cray Inc.  All Rights Reserved.
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
 ******************************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <errno.h>
#include <dlfcn.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>

#include <memory>

#include "cti_fe.h"
#include "cti_defs.h"
#include "cti_error.h"

#include "cti_transfer.h"

#include "useful/cti_useful.h"
#include "useful/make_unique.hpp"
#include "useful/Dlopen.hpp"

#include "wlm_detect.h"

#include "alps_fe.hpp"
#include "cray_slurm_fe.hpp"
#include "slurm_fe.hpp"
#include "ssh_fe.hpp"

/* Static prototypes */
static void			_cti_setup_base_dir(void);
bool				_cti_is_cluster_system();

/* static global vars */
static std::string _cti_cfg_dir;      // config dir that we can use as temporary storage
static std::string _cti_ld_audit_lib; // ld audit library location
static std::string _cti_overwatch_bin;// overwatch binary location
static std::string _cti_dlaunch_bin;  // dlaunch binary location
static std::string _cti_slurm_util;   // slurm utility binary location
static std::vector<std::string> const _cti_default_dir_locs = {DEFAULT_CTI_LOCS};

/* global wlm frontend object */
std::unique_ptr<Frontend> currentFrontend = nullptr;

/*
 * This routine initializes CTI so it is set up for usage by the executable with which it is linked.
 * Part of this includes automatically determining the active Workload Manager. The
 * user can force SSH as the "WLM" by setting the environment variable CTI_LAUNCHER_NAME.
 * In the case of complete failure to determine the WLM, the default value of _cti_nonenessProto
 * is used.
*/
void __attribute__((constructor))
_cti_init(void) {
	using DefaultFrontend = ALPSFrontend;
	
	// only init once
	if (currentFrontend) {
		return;
	}

	// We do not want to call init if we are running on the backend inside of
	// a tool daemon! It is possible for BE libraries to link against both the
	// CTI fe and be libs (e.g. MRNet) and we do not want to call the FE init
	// in that case.
	if (getenv(BE_GUARD_ENV_VAR) != NULL) {
		return;
	}

	// setup base directory info
	_cti_setup_base_dir();

	std::string wlmName;

	// Use the workload manager in the environment variable if it is set
	if (const char* wlm_name_env = getenv(CTI_WLM)) {
		wlmName = std::string(wlm_name_env);
	} else {
		// use wlm_detect to load wlm name
		Dlopen::Handle wlmDetect(WLM_DETECT_LIB_NAME);

		using GetActiveFnType = char*(void);
		using GetDefaultFnType = const char*(void);
		auto getActive = wlmDetect.loadFailable<GetActiveFnType>("wlm_detect_get_active");
		auto getDefault = wlmDetect.loadFailable<GetDefaultFnType>("wlm_detect_get_default");

		char *active_wlm;
		const char *default_wlm;
		if (getActive && (active_wlm = getActive())) {
			wlmName = std::string(active_wlm);
			free(active_wlm);
		} else {
			if (getDefault && (default_wlm = getDefault())) {
				wlmName = std::string(default_wlm);
			} else {
				currentFrontend = shim::make_unique<DefaultFrontend>();
				return;
			}
		}
	}

	// parse the returned result
	if (!wlmName.compare("ALPS") || !wlmName.compare("alps")) {
		currentFrontend = shim::make_unique<ALPSFrontend>();
	} else if (!wlmName.compare("SLURM") || !wlmName.compare("slurm")) {
		// Check to see if we are on a cluster. If so, use the cluster slurm prototype.
		if (_cti_is_cluster_system()) {
			currentFrontend = shim::make_unique<SLURMFrontend>();
		} else {
			currentFrontend = shim::make_unique<CraySLURMFrontend>();
		}
	} else if (!wlmName.compare("generic")) {
		currentFrontend = shim::make_unique<SSHFrontend>();
	} else {
		// fallback to use the default
		fprintf(stderr, "Invalid workload manager argument %s provided in %s\n", wlmName.c_str(), CTI_WLM);
		currentFrontend = shim::make_unique<DefaultFrontend>();
	}
}

// Destructor function
void __attribute__ ((destructor))
_cti_fini(void) {
	// Ensure this is only called once
	if (!currentFrontend) {
		return;
	}

	// call the transfer fini function
	_cti_transfer_fini();

	// reset wlm frontend to nullptr
	currentFrontend.reset();
	
	return;
}

/*********************
** internal functions 
*********************/

bool _cti_is_cluster_system(){
	struct stat sb;
	return (stat(CLUSTER_FILE_TEST, &sb) == 0);
}

static bool
_cti_hasDirPerms(std::string const& dirPath, decltype(R_OK) perms) {
	struct stat st;
	return !stat(dirPath.c_str(), &st) // make sure this directory exists
	    && S_ISDIR(st.st_mode) // make sure it is a directory
	    && !access(dirPath.c_str(), perms); // check if we can access the directory
}

static void
_cti_setup_base_dir(void) {
	std::string baseDir;

	const char * base_dir_env = getenv(BASE_DIR_ENV_VAR);
	if ((base_dir_env == nullptr) || !_cti_hasDirPerms(base_dir_env, R_OK | X_OK)) {
		for (auto const& defaultPath : _cti_default_dir_locs) {
			if (_cti_hasDirPerms(defaultPath, R_OK | X_OK)) {
				baseDir = defaultPath;
				break;
			}
		}
	} else {
		baseDir = std::string(base_dir_env);
	}

	// setup location paths
	auto verifyPath = [&](std::string const& path) {
		return !access(path.c_str(), R_OK | X_OK) ? path : "";
	};
	_cti_ld_audit_lib  = verifyPath(baseDir + "/lib/"     + LD_AUDIT_LIB_NAME);
	_cti_overwatch_bin = verifyPath(baseDir + "/libexec/" + CTI_OVERWATCH_BINARY);
	_cti_dlaunch_bin   = verifyPath(baseDir + "/libexec/" + CTI_LAUNCHER);
	_cti_slurm_util    = verifyPath(baseDir + "/libexec/" + SLURM_STEP_UTIL);
}

// getter functions for paths

Frontend& _cti_getCurrentFrontend() {
	if (!currentFrontend) {
		throw std::runtime_error("frontend not initialized");
	}

	return *currentFrontend;
}

std::string const _cti_getLdAuditPath(void) {
	return _cti_ld_audit_lib;
}

std::string const _cti_getOverwatchPath(void) {
	return _cti_overwatch_bin;
}

std::string const _cti_getDlaunchPath(void) {
	return _cti_dlaunch_bin;
}

std::string const _cti_getSlurmUtilPath(void) {
	return _cti_slurm_util;
}

std::string const _cti_getCfgDir(void) {

	if (!_cti_cfg_dir.empty()) {
		return _cti_cfg_dir;
	}

	// Get the pw info, this is used in the unique name part of cfg directories
	// and when doing the final ownership check
	std::string username;
	decltype(passwd::pw_uid) uid;
	if (struct passwd *pw = getpwuid(getuid())) {
		username = std::string(pw->pw_name);
		uid = pw->pw_uid;
	} else {
		throw std::runtime_error(std::string("_cti_getCfgDir: getpwuid() ") + strerror(errno));
	}

	// get the cfg dir settings
	std::string customCfgDir, cfgDir;
	if (const char* cfg_dir_env = getenv(CFG_DIR_VAR)) {
		customCfgDir = std::string(cfg_dir_env);
	} else {
		// look in this order: $TMPDIR, /tmp, $HOME
		std::vector<const char*> defaultDirs {getenv("TMPDIR"), "/tmp", getenv("HOME")};
		for (const char* dir_var : defaultDirs) {
			if ((dir_var != nullptr) && _cti_hasDirPerms(dir_var, R_OK | W_OK | X_OK)) {
				cfgDir = std::string(dir_var);
				break;
			}
		}
	}

	// Create the directory name string - we default this to have the name cray_cti-<username>
	std::string cfgPath;
	if (!customCfgDir.empty()) {
		cfgPath = cfgDir + "/cray_cti-" + username;
	} else if (!cfgDir.empty()) {
		cfgPath = customCfgDir + "/cray_cti-" + username;
	} else {
		// We have no where to create a temporary directory...
		throw std::runtime_error(std::string("Cannot find suitable config directory. Try setting the env variable ") + CFG_DIR_VAR);
	}

	if (customCfgDir.empty()) {
		// default cfgdir behavior: create if not exist, chmod if bad perms

		// try to stat the directory
		struct stat st;
		if (stat(cfgPath.c_str(), &st)) {
			// the directory doesn't exist so we need to create it using perms 700
			if (mkdir(cfgPath.c_str(), S_IRWXU)) {
				throw std::runtime_error(std::string("_cti_getCfgDir: mkdir() ") + strerror(errno));
			}
		} else {
			// directory already exists, so chmod it if has bad permissions.
			// We created this directory previously.
			if ((st.st_mode & (S_ISUID | S_ISGID | S_IRWXU | S_IRWXG | S_IRWXO)) & ~S_IRWXU) {
				if (chmod(cfgPath.c_str(), S_IRWXU)) {
					throw std::runtime_error(std::string("_cti_getCfgDir: chmod() ") + strerror(errno));
				}
			}
		}
	} else {
		// The user set CFG_DIR_VAR, we *ALWAYS* want to use that
		// custom cfgdir behavior: error if not exist or bad perms

		// Check to see if we can write to this directory
		if (!_cti_hasDirPerms(cfgPath.c_str(), R_OK | W_OK | X_OK)) {
			throw std::runtime_error(std::string("Bad directory specified by environment variable ") + CFG_DIR_VAR);
		}

		// verify that it has the permissions we expect
		struct stat st;
		if (stat(cfgPath.c_str(), &st)) {
			// could not stat the directory
			throw std::runtime_error(std::string("_cti_getCfgDir: stat() ") + strerror(errno));
		}
		if ((st.st_mode & (S_ISUID | S_ISGID | S_IRWXU | S_IRWXG | S_IRWXO)) & ~S_IRWXU) {
			// bits other than S_IRWXU are set
			throw std::runtime_error(std::string("Bad permissions (Only 0700 allowed) for directory specified by environment variable ") + CFG_DIR_VAR);
		}
	}

	// make sure we have a good path string
	if (char *realCfgPath = realpath(cfgPath.c_str(), nullptr)) {
		cfgPath = std::string(realCfgPath);
		free(realCfgPath);
	} else {
		throw std::runtime_error(std::string("_cti_getCfgDir: realpath() ") + strerror(errno));
	}

	// Ensure we have ownership of this directory, otherwise it is untrusted
	struct stat st;
	memset(&st, 0, sizeof(st));
	if (stat(cfgPath.c_str(), &st)) {
		throw std::runtime_error(std::string("_cti_getCfgDir: stat() ") + strerror(errno));
	}
	if (st.st_uid != uid) {
		throw std::runtime_error(std::string("_cti_getCfgDir: Directory already exists: ") + cfgPath);
	}

	_cti_cfg_dir = cfgPath;
	return cfgPath;
}

/************************
* API defined functions
************************/

const char *
cti_version(void) {
	return CTI_FE_VERSION;
}

cti_wlm_type
cti_current_wlm(void) {
	return currentFrontend->getWLMType();
}

const char *
cti_wlm_type_toString(cti_wlm_type wlm_type) {
	switch (wlm_type) {
		case CTI_WLM_ALPS:
			return "Cray ALPS";
			
		case CTI_WLM_CRAY_SLURM:
			return "Cray based SLURM";
	
		case CTI_WLM_SLURM:
			return "SLURM";

		case CTI_WLM_SSH:
			return "Fallback (SSH based) workload manager";
			
		case CTI_WLM_NONE:
			return "No WLM detected";
	}
	
	// Shouldn't get here
	return "Invalid WLM.";
}

int
cti_appIsValid(cti_app_id_t appId) {
	return currentFrontend->appIsValid(appId);
}

void
cti_deregisterApp(cti_app_id_t appId) {
	return currentFrontend->deregisterApp(appId);
}

int
cti_getNumAppPEs(cti_app_id_t appId) {
	return currentFrontend->getNumAppPEs(appId);
}

int
cti_getNumAppNodes(cti_app_id_t appId) {
	return currentFrontend->getNumAppNodes(appId);
}

char **
cti_getAppHostsList(cti_app_id_t appId) {
	auto const hostList = currentFrontend->getAppHostsList(appId);

	char **host_list = (char**)malloc(hostList.size() * sizeof(char*));
	for (size_t i = 0; i < hostList.size(); i++) {
		host_list[i] = strdup(hostList[i].c_str());
	}

	return host_list;
}

cti_hostsList_t *
cti_getAppHostsPlacement(cti_app_id_t appId) {
	auto const hostPlacement = currentFrontend->getAppHostsPlacement(appId);

	cti_hostsList_t *result = (cti_hostsList_t*)malloc(sizeof(cti_hostsList_t));
	result->hosts = (cti_host_t*)malloc(sizeof(cti_host_t) * hostPlacement.size());

	result->numHosts = hostPlacement.size();
	for (size_t i = 0; i < hostPlacement.size(); i++) {
		result->hosts[i].hostname = strdup(hostPlacement[i].hostname.c_str());
		result->hosts[i].numPEs   = hostPlacement[i].numPEs;
	}

	return result;
}

void
cti_destroyHostsList(cti_hostsList_t *placement_list) {
	if (placement_list == nullptr) {
		return;
	}

	if (placement_list->hosts) {
		for (int i = 0; i < placement_list->numHosts; i++) {
			free(placement_list->hosts[i].hostname);
		}
		free(placement_list->hosts);
	}
	free(placement_list);
}

char *
cti_getHostname() {
	return strdup(currentFrontend->getHostName().c_str());
}

char *
cti_getLauncherHostName(cti_app_id_t appId) {
	return strdup(currentFrontend->getLauncherHostName(appId).c_str());
}
