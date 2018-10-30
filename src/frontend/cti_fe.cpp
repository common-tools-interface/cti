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

#include "wlm_detect.h"

#include "alps_fe.hpp"
#include "cray_slurm_fe.hpp"
#include "slurm_fe.hpp"
#include "ssh_fe.hpp"

typedef struct
{
	void *			handle;
	char *			(*wlm_detect_get_active)(void);
	const char *	(*wlm_detect_get_default)(void);
} cti_wlm_detect_t;

/* Static prototypes */
static void			_cti_setup_base_dir(void);
static int			_cti_checkDirPerms(const char *);
bool				_cti_is_cluster_system();

/* static global vars */
static std::string _cti_cfg_dir;      // config dir that we can use as temporary storage
static std::string _cti_ld_audit_lib; // ld audit library location
static std::string _cti_overwatch_bin;// overwatch binary location
static std::string _cti_dlaunch_bin;  // dlaunch binary location
static std::string _cti_slurm_util;   // slurm utility binary location
static std::vector<std::string> const _cti_default_dir_locs = {DEFAULT_CTI_LOCS};

/* global wlm frontend object */
static std::unique_ptr<Frontend> currentFrontend = nullptr;

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

	// Use the workload manager in the environment variable if it is set 
	char* wlm_name_env;
	if ((wlm_name_env = getenv(CTI_WLM)) != nullptr) {
		if(strcasecmp(wlm_name_env, "alps") == 0) {
			currentFrontend = shim::make_unique<ALPSFrontend>();
		} else if(strcasecmp(wlm_name_env, "slurm") == 0) {
			// Check to see if we are on a cluster. If so, use the cluster slurm prototype.
			if (_cti_is_cluster_system()) {
				currentFrontend = shim::make_unique<SLURMFrontend>();
			} else {
				currentFrontend = shim::make_unique<CraySLURMFrontend>();
			}
		}
		else if(strcasecmp(wlm_name_env, "generic") == 0) {
			currentFrontend = shim::make_unique<SSHFrontend>();
		} else {
			fprintf(stderr, "Invalid workload manager argument %s provided in %s\n", wlm_name_env, CTI_WLM);
		}

		return;
	}

	cti_wlm_detect_t _cti_wlm_detect;
	if ((_cti_wlm_detect.handle = dlopen(WLM_DETECT_LIB_NAME, RTLD_LAZY)) == nullptr) {
		// Check to see if we are on a cluster. If so, use the slurm proto
		if (_cti_is_cluster_system()) {
			currentFrontend = shim::make_unique<SLURMFrontend>();
		} else {
			currentFrontend = shim::make_unique<DefaultFrontend>();
		}
		return;
	}
	
	// Clear any existing error
	dlerror();
	
	// load wlm_detect_get_active
	_cti_wlm_detect.wlm_detect_get_active = (char*(*)())dlsym(_cti_wlm_detect.handle, "wlm_detect_get_active");
	if (dlerror() != nullptr) {
		dlclose(_cti_wlm_detect.handle);
		currentFrontend = shim::make_unique<DefaultFrontend>();
		return;
	}
	
	// try to get the active wlm
	std::string wlmName;
	if (char *active_wlm = (*_cti_wlm_detect.wlm_detect_get_active)()) {
		wlmName = std::string(active_wlm);
		free(active_wlm);
	} else {
		// load wlm_detect_get_default
		_cti_wlm_detect.wlm_detect_get_default = (const char*(*)())dlsym(_cti_wlm_detect.handle, "wlm_detect_get_default");
		if (dlerror() != nullptr) {
			dlclose(_cti_wlm_detect.handle);
			currentFrontend = shim::make_unique<DefaultFrontend>();
			return;
		}
		// use the default wlm
		wlmName = std::string((const char *)(*_cti_wlm_detect.wlm_detect_get_default)());
	}
	
	// parse the returned result
	if (!wlmName.compare("ALPS")) {
		currentFrontend = shim::make_unique<ALPSFrontend>();
	} else if (!wlmName.compare("SLURM")) {
		currentFrontend = shim::make_unique<CraySLURMFrontend>();
	} else {
		// fallback to use the default
		currentFrontend = shim::make_unique<DefaultFrontend>();
	}
	
	// close the wlm_detect handle, we are done with it
	dlclose(_cti_wlm_detect.handle);
}

// Destructor function
void __attribute__ ((destructor))
_cti_fini(void) {
	// Ensure this is only called once
	if (currentFrontend) {
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

static bool is_accessible_directory(std::string const& dirPath){
	struct stat st;
	return !stat(dirPath.c_str(), &st) // make sure this directory exists
	    && S_ISDIR(st.st_mode) // make sure it is a directory
	    && !access(dirPath.c_str(), R_OK | X_OK); // check if we can access the directory
}

static void
_cti_setup_base_dir(void)
{
	const char * base_dir = getenv(BASE_DIR_ENV_VAR);
	if ((base_dir == nullptr) || !is_accessible_directory(base_dir)) {
		for (auto const& defaultPath : _cti_default_dir_locs) {
			if (is_accessible_directory(defaultPath.c_str())) {
				base_dir = defaultPath.c_str();
				break;
			}
		}
	}
	std::string const baseDir(base_dir);
	
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

Frontend const& _cti_getCurrentFrontend() {
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

static int
_cti_checkDirPerms(const char *dir)
{
	struct stat		st;
	
	if (dir == NULL)
		return 1;
	
	// Stat the tmp_dir
	if (stat(dir, &st))
	{
		// could not stat the directory
		return 1;
	}
	
	if (!S_ISDIR(st.st_mode))
	{
		// this is not a directory
		return 1;
	}
	
	// check if we can access the directory
	if (access(dir, R_OK | W_OK | X_OK))
	{
		// directory doesn't have proper permissions
		return 1;
	}
	
	return 0;
}

extern const char * cti_error_str(void);
static const char *_cti_getCfgDir_old(void);
std::string const _cti_getCfgDir(void) {
	// return if we already have the value
	if (!_cti_cfg_dir.empty()) {
		return _cti_cfg_dir;
	}

	// call the char-returning function
	if (auto cfg_dir = _cti_getCfgDir_old()) {
		// set the global variable
		_cti_cfg_dir = std::string(cfg_dir);
	} else {
		throw std::runtime_error(cti_error_str());
	}

	return _cti_cfg_dir;
}

static const char *_cti_getCfgDir_old(void) {
	char *			cfg_dir;
	char *			tmp_dir;
	struct passwd *	pw;
	struct stat		st;

	// Get the pw info, this is used in the unique name part of cfg directories
	// and when doing the final ownership check
	if ((pw = getpwuid(getuid())) == NULL)
	{
		_cti_set_error("_cti_getCfgDir: getpwuid() %s", strerror(errno));
		return NULL;
	}
	
	// get the cfg dir settings
	if ((cfg_dir = getenv(CFG_DIR_VAR)) == NULL)
	{
		// Not found.
		// Ideally we want to use TMPDIR or /tmp, the directory name should be 
		// unique to the user.
		
		// Check to see if TMPDIR is set
		tmp_dir = getenv("TMPDIR");
		
		// check if can write to tmp_dir
		if (_cti_checkDirPerms(tmp_dir))
		{
			// We couldn't write to tmp_dir, so lets try using /tmp
			tmp_dir = strdup("/tmp");
			if (_cti_checkDirPerms(tmp_dir))
			{
				// We couldn't write to /tmp, so lets use the home directory
				tmp_dir = getenv("HOME");
				
				// Check if we can write to HOME
				if (_cti_checkDirPerms(tmp_dir))
				{
					// We have no where to create a temporary directory...
					_cti_set_error("Cannot find suitable config directory. Try setting the %s env variable.", CFG_DIR_VAR);
					return NULL;
				}
			}
		}
		
		// Create the directory name string - we default this to have the name cray_cti-<username>
		if (asprintf(&cfg_dir, "%s/cray_cti-%s", tmp_dir, pw->pw_name) <= 0)
		{
			_cti_set_error("_cti_getCfgDir: asprintf failed.");
			return NULL;
		}
		
		// Create and setup the directory
		
		// try to stat the directory
		if (stat(cfg_dir, &st))
		{
			// the directory doesn't exist so we need to create it
			// use perms 700
			if (mkdir(cfg_dir, S_IRWXU))
			{
				_cti_set_error("_cti_getCfgDir: mkdir() %s", strerror(errno));
				return NULL;
			}
		} else
		{
			// directory already exists, so chmod it if has bad permissions.
			// We created this directory previously.
			if ((st.st_mode & (S_ISUID | S_ISGID | S_IRWXU | S_IRWXG | S_IRWXO)) & ~S_IRWXU)
			{
				if (chmod(cfg_dir, S_IRWXU))
				{
					_cti_set_error("_cti_getCfgDir: chmod() %s", strerror(errno));
					return NULL;
				}
			}
		}
		
		// make sure we have a good path string
		tmp_dir = cfg_dir;
		if ((cfg_dir = realpath(tmp_dir, NULL)) == NULL)
		{
			_cti_set_error("_cti_getCfgDir: realpath() %s", strerror(errno));
			free((char*)tmp_dir);
			return NULL;
		}
		free((char*)tmp_dir);
	} else
	{
		// The user set CFG_DIR_VAR, we *ALWAYS* want to use that
		
		// Check to see if we can write to this directory
		if (_cti_checkDirPerms(cfg_dir))
		{
			_cti_set_error("Bad directory specified by environment variable %s.", CFG_DIR_VAR);
			return NULL;
		}
		
		// verify that it has the permissions we expect
		if (stat(cfg_dir, &st))
		{
			// could not stat the directory
			_cti_set_error("_cti_getCfgDir: stat() %s", strerror(errno));
			return NULL;
		}
		if ((st.st_mode & (S_ISUID | S_ISGID | S_IRWXU | S_IRWXG | S_IRWXO)) & ~S_IRWXU)
		{
			// bits other than S_IRWXU are set
			_cti_set_error("Bad permissions for directory specified by environment variable %s. Only 0700 allowed.", CFG_DIR_VAR);
			return NULL;
		}
		
		// call realpath on cfg_dir
		tmp_dir = cfg_dir;
		if ((cfg_dir = realpath(tmp_dir, NULL)) == NULL)
		{
			_cti_set_error("_cti_getCfgDir: realpath() %s", strerror(errno));
			return NULL;
		}
	}
	
	// Ensure we have ownership of this directory, otherwise it is untrusted
	memset(&st, 0, sizeof(st));
	if (stat(cfg_dir, &st))
	{
		// could not stat the directory
		_cti_set_error("_cti_getCfgDir: stat() %s", strerror(errno));
		free(cfg_dir);
		return NULL;
	}
	// verify that we have ownership of this directory
	if (st.st_uid != pw->pw_uid)
	{
		_cti_set_error("_cti_getCfgDir: Directory %s already exists", cfg_dir);
		free(cfg_dir);
		return NULL;
	}
	
	return cfg_dir;
}

/************************
* API defined functions
************************/

const char *
cti_version(void)
{
	return CTI_FE_VERSION;
}

cti_wlm_type
cti_current_wlm(void)
{
	return currentFrontend->getWLMType();
}

const char *
cti_wlm_type_toString(cti_wlm_type wlm_type)
{
	switch (wlm_type)
	{
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
