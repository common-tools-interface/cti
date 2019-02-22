/******************************************************************************\
 * cti_fe.cpp - C implementation for the cti frontend.
 *
 * Copyright 2014-2019 Cray Inc.  All Rights Reserved.
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
#include <fcntl.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>

#include <unordered_map>
#include <memory>
#include <sstream>

// CTI definition includes
#include "cti_fe.h"
#include "cti_defs.h"
#include "cti_error.h"

// CTI Transfer includes
#include "frontend/Frontend.hpp"
#include "cti_transfer/Manifest.hpp"
#include "cti_transfer/Session.hpp"

// CTI Frontend / App implementations
#include "Frontend.hpp"
#include "cray_slurm_fe.hpp"
#define USE_CRAY_SLURM_ONLY 1
#if !USE_CRAY_SLURM_ONLY
#include "alps_fe.hpp"
#include "slurm_fe.hpp"
#include "ssh_fe.hpp"
#endif

// WLM detection library interface
#include "wlm_detect.h"

// utility includes
#include "useful/cti_useful.h"
#include "useful/Dlopen.hpp"

/* static global vars */
static std::string _cti_cfg_dir;      // config dir that we can use as temporary storage
static std::string _cti_ld_audit_lib; // ld audit library location
static std::string _cti_overwatch_bin;// overwatch binary location
static std::string _cti_dlaunch_bin;  // dlaunch binary location
static const char* const _cti_default_dir_locs[] = {DEFAULT_CTI_LOCS};

/* global wlm frontend / app objects */

static auto currentFrontendPtr = std::unique_ptr<Frontend>{};

// app management
static auto appList = std::unordered_map<cti_app_id_t, std::unique_ptr<App>>{};
static const cti_app_id_t APP_ERROR = 0;
static cti_app_id_t newAppId() noexcept {
	static cti_app_id_t nextId = 1;
	return nextId++;
}
static App& getApp(cti_app_id_t appId) {
	if (!cti_appIsValid(appId)) {
		throw std::runtime_error("invalid app id " + std::to_string(appId));
	}
	return *(appList.at(appId));
}

// transfer session management
static auto sessionList = std::unordered_map<cti_session_id_t, std::shared_ptr<Session>>{};
static const cti_session_id_t SESSION_ERROR = 0;
static cti_session_id_t newSessionId() noexcept {
	static cti_session_id_t nextId = 1;
	return nextId++;
}
static Session& getSession(cti_session_id_t sid) {
	if (!cti_sessionIsValid(sid)) {
		throw std::runtime_error("invalid session id " + std::to_string(sid));
	}
	return *(sessionList.at(sid));
}

// transfer manifest management
static auto manifestList = std::unordered_map<cti_manifest_id_t, std::shared_ptr<Manifest>>{};
static const cti_manifest_id_t MANIFEST_ERROR = 0;
static cti_manifest_id_t newManifestId() noexcept {
	static cti_manifest_id_t nextId = 1;
	return nextId++;
}
static Manifest& getManifest(cti_manifest_id_t mid) {
	if (!cti_manifestIsValid(mid)) {
		throw std::runtime_error("invalid manifest id " + std::to_string(mid));
	}
	return *(manifestList.at(mid));
}

/* helper functions */

namespace cti_conventions {
	static bool isClusterSystem(){
		struct stat sb;
		return (stat(CLUSTER_FILE_TEST, &sb) == 0);
	}

	static bool
	hasDirPerms(std::string const& dirPath, decltype(R_OK) perms) {
		struct stat st;
		return !stat(dirPath.c_str(), &st) // make sure this directory exists
		    && S_ISDIR(st.st_mode) // make sure it is a directory
		    && !access(dirPath.c_str(), perms); // check if we can access the directory
	}

	static void
	setupBaseDir(void) {
		std::string baseDir;

		const char * base_dir_env = getenv(BASE_DIR_ENV_VAR);
		if ((base_dir_env == nullptr) || !cti_conventions::hasDirPerms(base_dir_env, R_OK | X_OK)) {
			for (const char* const* pathPtr = _cti_default_dir_locs; *pathPtr != nullptr; pathPtr++) {
				if (cti_conventions::hasDirPerms(*pathPtr, R_OK | X_OK)) {
					baseDir = std::string(*pathPtr);
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
	}

	// This does sanity checking on args in common for both launchApp and launchAppBarrier
	static void
	verifyLaunchArgs(int stdout_fd, int stderr_fd, const char *inputFile, const char *chdirPath)
	{
		auto canWriteFd = [](int const fd) {
			// if fd is -1, then the fd arg is meant to be ignored
			if (fd == -1) {
				return true;
			}
			errno = 0;
			int accessFlags = fcntl(fd, F_GETFL) & O_ACCMODE;
			if (errno != 0) {
				return false;
			}
			return (accessFlags & O_WRONLY) || (accessFlags & O_WRONLY);
		};

		// ensure stdout, stderr can be written to
		if (!canWriteFd(stdout_fd)) {
			throw std::runtime_error("Invalid stdout_fd argument. No write access.");
		}
		if (!canWriteFd(stderr_fd)) {
			throw std::runtime_error("Invalid stderr_fd argument. No write access.");
		}

		// verify inputFile is a file that can be read
		if (inputFile != nullptr) {
			struct stat st;
			if (stat(inputFile, &st)) { // make sure inputfile exists
				throw std::runtime_error("Invalid inputFile argument. File does not exist.");
			}
			if (!S_ISREG(st.st_mode)) { // make sure it is a regular file
				throw std::runtime_error("Invalid inputFile argument. The file is not a regular file.");
			}
			if (access(inputFile, R_OK)) { // make sure we can access it
				throw std::runtime_error("Invalid inputFile argument. Bad permissions.");
			}
		}

		// verify chdirPath is a directory that can be read, written, and executed
		if (chdirPath != nullptr) {
			struct stat st;
			if (stat(chdirPath, &st)) { // make sure chdirpath exists
				throw std::runtime_error("Invalid chdirPath argument. Directory does not exist.");
			}
			if (!S_ISDIR(st.st_mode)) { // make sure it is a directory
				throw std::runtime_error("Invalid chdirPath argument. The file is not a directory.");
			}
			if (access(chdirPath, R_OK | W_OK | X_OK)) { // make sure we can access it
				throw std::runtime_error("Invalid chdirPath argument. Bad permissions.");
			}
		}
	}
}

namespace {
	static constexpr auto SUCCESS = int{0};
	static constexpr auto FAILURE = int{1};
}

// run code that can throw and use it to set cti error instead
template <typename FuncType, typename ReturnType = decltype(std::declval<FuncType>()())>
static ReturnType runSafely(std::string const& caller, FuncType&& func, ReturnType const onError) {
	try {
		return std::forward<FuncType>(func)();
	} catch (std::exception const& ex) {
		_cti_set_error((caller + ": " + ex.what()).c_str());
		return onError;
	}
}

template <typename WLMType>
static WLMType* downcastCurrentFE() {
	if (auto wlmPtr = dynamic_cast<WLMType*>(currentFrontendPtr.get())) {
		return wlmPtr;
	} else {
		std::string const wlmName(cti_wlm_type_toString(currentFrontendPtr->getWLMType()));
		throw std::runtime_error("Invalid call. " + wlmName + " not in use.");
	}
}

/* API function prototypes */

/*
 * This routine initializes CTI so it is set up for usage by the executable with which it is linked.
 * Part of this includes automatically determining the active Workload Manager. The
 * user can force SSH as the "WLM" by setting the environment variable CTI_LAUNCHER_NAME.
 * In the case of complete failure to determine the WLM, the default value of _cti_nonenessProto
 * is used.
*/
void __attribute__((constructor))
_cti_init(void) {
	using DefaultFrontend = CraySLURMFrontend;// ALPSFrontend;
	
	// only init once
	if (currentFrontendPtr != nullptr) {
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
	cti_conventions::setupBaseDir();

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
				currentFrontendPtr = std::make_unique<DefaultFrontend>();
				return;
			}
		}
	}

	// parse the returned result
	currentFrontendPtr = std::make_unique<CraySLURMFrontend>();
	#if !USE_CRAY_SLURM_ONLY
	if (!wlmName.compare("ALPS") || !wlmName.compare("alps")) {
		currentFrontendPtr = std::make_unique<ALPSFrontend>();
	} else if (!wlmName.compare("SLURM") || !wlmName.compare("slurm")) {
		// Check to see if we are on a cluster. If so, use the cluster slurm prototype.
		if (cti_conventions::isClusterSystem()) {
			currentFrontendPtr = std::make_unique<SLURMFrontend>();
		} else {
			currentFrontendPtr = std::make_unique<CraySLURMFrontend>();
		}
	} else if (!wlmName.compare("generic")) {
		currentFrontendPtr = std::make_unique<SSHFrontend>();
	} else {
		// fallback to use the default
		fprintf(stderr, "Invalid workload manager argument %s provided in %s\n", wlmName.c_str(), CTI_WLM);
		currentFrontendPtr = std::make_unique<DefaultFrontend>();
	}
	if (currentFrontendPtr == nullptr) {
		fprintf(stderr, "Workload manager argument '%s' produced null frontend!\n", wlmName.c_str());
	}
	#endif
}

// Destructor function
void __attribute__ ((destructor))
_cti_fini(void) {
	// Ensure this is only called once
	if (currentFrontendPtr == nullptr) {
		return;
	}

	// call the transfer fini function
	_cti_transfer_fini();

	// reset wlm frontend to nullptr
	currentFrontendPtr.reset();
	
	return;
}

/*********************
** internal functions 
*********************/

Frontend& _cti_getCurrentFrontend() {
	if (currentFrontendPtr == nullptr) {
		_cti_init();
		if (currentFrontendPtr == nullptr) {
			throw std::runtime_error("frontend failed to initialize");
		}
	}

	return *currentFrontendPtr;
}

std::string const& _cti_getLdAuditPath(void) {
	return _cti_ld_audit_lib;
}

std::string const& _cti_getOverwatchPath(void) {
	return _cti_overwatch_bin;
}

std::string const& _cti_getDlaunchPath(void) {
	return _cti_dlaunch_bin;
}

std::string const& _cti_getCfgDir(void) {
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
			if ((dir_var != nullptr) && cti_conventions::hasDirPerms(dir_var, R_OK | W_OK | X_OK)) {
				cfgDir = std::string(dir_var);
				break;
			}
		}
	}

	// Create the directory name string - we default this to have the name cray_cti-<username>
	std::string cfgPath;
	if (!customCfgDir.empty()) {
		cfgPath = customCfgDir + "/cray_cti-" + username;
	} else if (!cfgDir.empty()) {
		cfgPath = cfgDir + "/cray_cti-" + username;
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
		if (!cti_conventions::hasDirPerms(cfgPath.c_str(), R_OK | W_OK | X_OK)) {
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
	return _cti_cfg_dir;
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
	if (currentFrontendPtr != nullptr) {
		return currentFrontendPtr->getWLMType();
	} else {
		return CTI_WLM_NONE;
	}
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
cti_getNumAppPEs(cti_app_id_t appId) {
	return runSafely("cti_getNumAppPEs", [&](){
		return getApp(appId).getNumPEs();
	}, -1);
}

int
cti_getNumAppNodes(cti_app_id_t appId) {
	return runSafely("cti_getNumAppNodes", [&](){
		return getApp(appId).getNumHosts();
	}, -1);
}

char**
cti_getAppHostsList(cti_app_id_t appId) {
	return runSafely("cti_getAppHostsList", [&](){
		auto const hostList = getApp(appId).getHostnameList();

		char **host_list = (char**)malloc(sizeof(char*) * (hostList.size() + 1));
		for (size_t i = 0; i < hostList.size(); i++) {
			host_list[i] = strdup(hostList[i].c_str());
		}
		host_list[hostList.size()] = nullptr;

		return host_list;
	}, (char**)nullptr);
}

cti_hostsList_t*
cti_getAppHostsPlacement(cti_app_id_t appId) {
	return runSafely("cti_getAppHostsPlacement", [&](){
		auto const hostPlacement = getApp(appId).getHostsPlacement();

		cti_hostsList_t *result = (cti_hostsList_t*)malloc(sizeof(cti_hostsList_t));
		result->hosts = (cti_host_t*)malloc(sizeof(cti_host_t) * hostPlacement.size());

		result->numHosts = hostPlacement.size();
		for (size_t i = 0; i < hostPlacement.size(); i++) {
			result->hosts[i].hostname = strdup(hostPlacement[i].hostname.c_str());
			result->hosts[i].numPEs   = hostPlacement[i].numPEs;
		}

		return result;
	}, (cti_hostsList_t*)nullptr);
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

char*
cti_getHostname() {
	return runSafely("cti_getHostname", [&](){
		return strdup(_cti_getCurrentFrontend().getHostname().c_str());
	}, (char*)nullptr);
}

char*
cti_getLauncherHostName(cti_app_id_t appId) {
	return runSafely("cti_getLauncherHostName", [&](){
		return strdup(getApp(appId).getLauncherHostname().c_str());
	}, (char*)nullptr);
}



/* WLM-specific function implementation */

// ALPS

#if !USE_CRAY_SLURM_ONLY
uint64_t cti_alps_getApid(pid_t aprunPid) {
	return runSafely("cti_alps_getApid", [&](){
		return downcastCurrentFE<ALPSFrontend>()->getApid(aprunPid);
	});
}

cti_app_id_t cti_alps_registerApid(uint64_t apid) {
	return runSafely("cti_alps_registerApid", [&](){
		return downcastCurrentFE<ALPSFrontend>()->registerApid(apid);
	});
}

cti_aprunProc_t * cti_alps_getAprunInfo(cti_app_id_t app_id) {
	return runSafely("cti_alps_getApid", [&](){
		auto alpsPtr = downcastCurrentFE<ALPSFrontend>();
		if (auto result = (cti_aprunProc_t*)malloc(sizeof(cti_aprunProc_t))) {
			*result = alpsPtr->getAprunInfo(app_id);
			return result;
		} else {
			throw std::runtime_error("malloc failed.");
		}
	});
}

int cti_alps_getAlpsOverlapOrdinal(cti_app_id_t app_id) {
	return runSafely("cti_alps_getAlpsOverlapOrdinal", [&](){
		return downcastCurrentFE<ALPSFrontend>()->getAlpsOverlapOrdinal(app_id);
	});
}
#endif

// Cray-SLURM

cti_srunProc_t*
cti_cray_slurm_getJobInfo(pid_t srunPid) {
	return runSafely("cti_cray_slurm_getJobInfo", [&](){
		auto craySlurmPtr = downcastCurrentFE<CraySLURMFrontend>();
		if (auto result = (cti_srunProc_t*)malloc(sizeof(cti_srunProc_t))) {
			*result = craySlurmPtr->getSrunInfo(srunPid);
			return result;
		} else {
			throw std::runtime_error("malloc failed.");
		}
	}, (cti_srunProc_t*)nullptr);
}

cti_app_id_t
cti_cray_slurm_registerJobStep(uint32_t job_id, uint32_t step_id) {
	return runSafely("cti_cray_slurm_registerJobStep", [&](){
		auto const appId = newAppId();
		appList.insert(std::make_pair(appId,
			downcastCurrentFE<CraySLURMFrontend>()->registerJobStep(job_id, step_id)));
		return appId;
	}, cti_app_id_t{0});
}

cti_srunProc_t*
cti_cray_slurm_getSrunInfo(cti_app_id_t appId) {
	return runSafely("cti_cray_slurm_getSrunInfo", [&](){
		auto craySlurmPtr = downcastCurrentFE<CraySLURMFrontend>();
		if (auto result = (cti_srunProc_t*)malloc(sizeof(cti_srunProc_t))) {
			*result = dynamic_cast<CraySLURMApp&>(getApp(appId)).getSrunInfo();
			return result;
		} else {
			throw std::runtime_error("malloc failed.");
		}
	}, (cti_srunProc_t*)nullptr);
}

// SLURM

cti_app_id_t
cti_slurm_registerJobStep(pid_t launcher_pid) {
#ifdef SLURMFrontend
	return runSafely("cti_slurm_registerJobStep", [&](){
		throw std::runtime_error("Not implemented for SLURM WLM");
	});
#else
	return cti_app_id_t{0};
#endif
}

// SSH

cti_app_id_t
cti_ssh_registerJob(pid_t launcher_pid) {
#ifdef SSHFrontend
	return runSafely("cti_ssh_registerJob", [&](){
		throw std::runtime_error("Not implemented for SSH WLM");
	});
#else
	return cti_app_id_t{0};
#endif
}



/* app launch / release implementations */

int
cti_appIsValid(cti_app_id_t appId) {
	return runSafely("cti_appIsValid", [&](){
		return appList.find(appId) != appList.end();
	}, false);
}

void
cti_deregisterApp(cti_app_id_t appId) {
	runSafely("cti_deregisterApp", [&](){
		appList.erase(appId);
		return true;
	}, false);
}

cti_app_id_t
cti_launchApp(const char * const launcher_argv[], int stdout_fd, int stderr_fd,
	const char *inputFile, const char *chdirPath, const char * const env_list[])
{
	return runSafely("cti_launchApp", [&](){
		// sanity check the app launch arguments
		cti_conventions::verifyLaunchArgs(stdout_fd, stderr_fd, inputFile, chdirPath);

		// delegate app launch and registration to launchAppBarrier
		auto const appId = cti_launchAppBarrier(launcher_argv, stdout_fd, stderr_fd, inputFile, chdirPath, env_list);

		// release barrier
		getApp(appId).releaseBarrier();

		return appId;
	}, cti_app_id_t{0});
}

cti_app_id_t
cti_launchAppBarrier(const char * const launcher_argv[], int stdout_fd, int stderr_fd,
	const char *inputFile, const char *chdirPath, const char * const env_list[])
{
	return runSafely("cti_launchAppBarrier", [&](){
		// sanity check the app launch arguments
		cti_conventions::verifyLaunchArgs(stdout_fd, stderr_fd, inputFile, chdirPath);

		// create app instance held at barrier
		auto const appId = newAppId();
		auto newApp = _cti_getCurrentFrontend().launchBarrier(launcher_argv, stdout_fd, stderr_fd,
			inputFile, chdirPath, env_list);

		// register in the app list
		appList.insert(std::make_pair(appId, std::move(newApp)));

		return appId;
	}, cti_app_id_t{0});
}

int
cti_releaseAppBarrier(cti_app_id_t appId) {
	return runSafely("cti_releaseAppBarrier", [&](){
		getApp(appId).releaseBarrier();
		return SUCCESS;
	}, FAILURE);
}

int
cti_killApp(cti_app_id_t appId, int signum) {
	return runSafely("cti_killApp", [&](){
		getApp(appId).kill(signum);
		return SUCCESS;
	}, FAILURE);
}



/* session implementations */

// create and add wlm basefiles to manifest. run this after creating a Session
static void shipWLMBaseFiles(Session& liveSession) {
	auto baseFileManifest = liveSession.createManifest();
	for (auto const& path : liveSession.m_activeApp.getExtraBinaries()) {
		baseFileManifest->addBinary(path);
	}
	for (auto const& path : liveSession.m_activeApp.getExtraLibraries()) {
		baseFileManifest->addLibrary(path);
	}
	for (auto const& path : liveSession.m_activeApp.getExtraLibDirs()) {
		baseFileManifest->addLibDir(path);
	}
	for (auto const& path : liveSession.m_activeApp.getExtraFiles()) {
		baseFileManifest->addFile(path);
	}

	// ship basefile manifest and run remote extraction
	baseFileManifest->finalizeAndShip().extract();
}

cti_session_id_t
cti_createSession(cti_app_id_t appId) {
	return runSafely("cti_createSession", [&](){
		auto const sid = newSessionId();

		// create session instance
		auto newSession = std::make_shared<Session>(_cti_getCurrentFrontend().getWLMType(), getApp(appId));
		shipWLMBaseFiles(*newSession);
		sessionList.insert(std::make_pair(sid, newSession));
		return sid;
	}, SESSION_ERROR);
}

int
cti_sessionIsValid(cti_session_id_t sid) {
	return runSafely("cti_sessionIsValid", [&](){
		return sessionList.find(sid) != sessionList.end();
	}, false);
}

char**
cti_getSessionLockFiles(cti_session_id_t sid) {
	return runSafely("cti_getSessionLockFiles", [&](){
		auto const& activeManifests = getSession(sid).getManifests();

		// ensure there's at least one manifest instance
		if (activeManifests.size() == 0) {
			throw std::runtime_error("backend not initialized for session id " + std::to_string(sid));
		}

		// create return array
		auto result = (char**)malloc(sizeof(char*) * (activeManifests.size() + 1));
		if (result == nullptr) {
			throw std::runtime_error("malloc failed for session id " + std::to_string(sid));
		}

		// create the strings
		for (size_t i = 0; i < activeManifests.size(); i++) {
			result[i] = strdup(activeManifests[i]->m_lockFilePath.c_str());
		}
		result[activeManifests.size()] = nullptr;
		return result;
	}, (char**)nullptr);
}

// fill in a heap string pointer to session root path plus subdirectory
static char* sessionPathAppend(std::string const& caller, cti_session_id_t sid, const std::string& str) {
	return runSafely(caller, [&](){
		// get session and construct string
		auto const& session = getSession(sid);
		std::stringstream ss;
		ss << session.m_toolPath << "/" << session.m_stageName << str;
		return strdup(ss.str().c_str());
	}, (char*)nullptr);
}

char*
cti_getSessionRootDir(cti_session_id_t sid) {
	return sessionPathAppend("cti_getSessionRootDir", sid, "");
}

char*
cti_getSessionBinDir(cti_session_id_t sid) {
	return sessionPathAppend("cti_getSessionBinDir", sid, "/bin");
}

char*
cti_getSessionLibDir(cti_session_id_t sid) {
	return sessionPathAppend("cti_getSessionLibDir", sid, "/lib");
}

char*
cti_getSessionFileDir(cti_session_id_t sid) {
	return sessionPathAppend("cti_getSessionFileDir", sid, "");
}

char*
cti_getSessionTmpDir(cti_session_id_t sid) {
	return sessionPathAppend("cti_getSessionTmpDir", sid, "/tmp");
}



/* manifest implementations */

cti_manifest_id_t
cti_createManifest(cti_session_id_t sid) {
	return runSafely("cti_createManifest", [&](){
		auto const mid = newManifestId();
		manifestList.insert({mid, getSession(sid).createManifest()});
		return mid;
	}, MANIFEST_ERROR);
}

int
cti_manifestIsValid(cti_manifest_id_t mid) {
	return runSafely("cti_manifestIsValid", [&](){
		return manifestList.find(mid) != manifestList.end();
	}, false);
}

int
cti_destroySession(cti_session_id_t sid) {
	return runSafely("cti_destroySession", [&](){
		getSession(sid).launchCleanup();
		sessionList.erase(sid);
		return SUCCESS;
	}, FAILURE);
}

int
cti_addManifestBinary(cti_manifest_id_t mid, const char * rawName) {
	return runSafely("cti_addManifestBinary", [&](){
		getManifest(mid).addBinary(rawName);
		return SUCCESS;
	}, FAILURE);
}

int
cti_addManifestLibrary(cti_manifest_id_t mid, const char * rawName) {
	return runSafely("cti_addManifestLibrary", [&](){
		getManifest(mid).addLibrary(rawName);
		return SUCCESS;
	}, FAILURE);
}

int
cti_addManifestLibDir(cti_manifest_id_t mid, const char * rawName) {
	return runSafely("cti_addManifestLibDir", [&](){
		getManifest(mid).addLibDir(rawName);
		return SUCCESS;
	}, FAILURE);
}

int
cti_addManifestFile(cti_manifest_id_t mid, const char * rawName) {
	return runSafely("cti_addManifestFile", [&](){
		getManifest(mid).addFile(rawName);
		return SUCCESS;
	}, FAILURE);
}

int
cti_sendManifest(cti_manifest_id_t mid) {
	return runSafely("cti_sendManifest", [&](){
		auto remotePackage = getManifest(mid).finalizeAndShip();
		remotePackage.extract();
		manifestList.erase(mid);
		return SUCCESS;
	}, FAILURE);
}

/* tool daemon prototypes */
int
cti_execToolDaemon(cti_manifest_id_t mid, const char *daemonPath,
	const char * const daemonArgs[], const char * const envVars[])
{
	return runSafely("cti_execToolDaemon", [&](){
		{ auto& manifest = getManifest(mid);
			manifest.addBinary(daemonPath);
			auto remotePackage = manifest.finalizeAndShip();
			remotePackage.extractAndRun(daemonPath, daemonArgs, envVars);
		}
		manifestList.erase(mid);
		return SUCCESS;
	}, FAILURE);
}

bool _cti_stage_deps = true; // extern defined in cti_transfer.h
void
_cti_setStageDeps(bool stageDeps) {
	_cti_stage_deps = stageDeps;
}

void
_cti_consumeSession(void* rawSidPtr) {
	if (rawSidPtr == nullptr) {
		return;
	}

	auto sidPtr = static_cast<cti_session_id_t*>(rawSidPtr);
	cti_destroySession(*sidPtr);
	delete sidPtr;
}

void _cti_transfer_init(void) { /* no-op */ }

void
_cti_transfer_fini(void) {
	sessionList.clear();
}