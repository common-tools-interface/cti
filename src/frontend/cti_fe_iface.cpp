/******************************************************************************\
 * cti_fe_iface.cpp - C interface layer for the cti frontend.
 *
 * Copyright 2014-2019 Cray Inc.  All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
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
#include <typeinfo>

// CTI definition includes
#include "cti_fe_iface.h"
#include "cti_defs.h"

// CTI Transfer includes
#include "cti_transfer/Manifest.hpp"
#include "cti_transfer/Session.hpp"

// CTI Frontend / App implementations
#include "Frontend.hpp"
#include "Frontend_impl.hpp"

// WLM detection library interface
#include "wlm_detect.h"

// utility includes
#include "useful/cti_useful.h"
#include "useful/Dlopen.hpp"

/* helper functions */

namespace cti_conventions
{
	constexpr static const char* const _cti_default_dir_locs[] = {
		"/opt/cray/cti/"    CTI_RELEASE_VERSION,
		"/opt/cray/pe/cti/" CTI_RELEASE_VERSION,
		nullptr
	};

	namespace return_code
	{
		static constexpr auto SUCCESS = int{0};
		static constexpr auto FAILURE = int{1};

		static constexpr auto APP_ERROR      = cti_app_id_t{0};
		static constexpr auto SESSION_ERROR  = cti_session_id_t{0};
		static constexpr auto MANIFEST_ERROR = cti_manifest_id_t{0};
	}

	/* function definitions */

	// return true if running on cluster system
	static bool isClusterSystem();

	// return true if running on backend
	static bool isRunningOnBackend();

	// return true if path exists, is a directory, and has the given permissions
	static bool dirHasPerms(char const* dirPath, int const perms);

	// verify that FDs are writable, input file path is readable, and chdir path is
	// read/write/executable. if not, throw an exception with the corresponding error message
	static void verifyLaunchArgs(int const stdoutFd, int const stderrFd, const char *inputFilePath,
		const char *chdirPath);

	// use user info to build unique staging path; optionally create the staging direcotry
	static std::string setupCfgDir();

	// verify read/execute permissions of the given path, throw if inaccessible
	static std::string accessiblePath(std::string const& path);

	// get the base CTI directory from the environment and verify its permissions
	static std::string setupBaseDir();

	// use environment info to determine and instantiate the correct WLM Frontend implementation
	static std::unique_ptr<Frontend> make_Frontend();

	/* function implementations */

	static bool
	isClusterSystem()
	{
		struct stat sb;
		return (stat(CLUSTER_FILE_TEST, &sb) == 0);
	}

	static bool
	isRunningOnBackend()
	{
		return (getenv(BE_GUARD_ENV_VAR) != nullptr);
	}

	static bool
	dirHasPerms(char const* dirPath, int const perms)
	{
		struct stat st;
		return !stat(dirPath, &st) // make sure this directory exists
		    && S_ISDIR(st.st_mode) // make sure it is a directory
		    && !access(dirPath, perms); // check if we can access the directory
	}

	static void
	verifyLaunchArgs(int const stdoutFd, int const stderrFd, char const *inputFilePath, const char *chdirPath)
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
		if (!canWriteFd(stdoutFd)) {
			throw std::runtime_error("Invalid stdout_fd argument. No write access.");
		}
		if (!canWriteFd(stderrFd)) {
			throw std::runtime_error("Invalid stderr_fd argument. No write access.");
		}

		// verify inputFile is a file that can be read
		if (inputFilePath != nullptr) {
			struct stat st;
			if (stat(inputFilePath, &st)) { // make sure inputfile exists
				throw std::runtime_error("Invalid inputFile argument. File does not exist.");
			}
			if (!S_ISREG(st.st_mode)) { // make sure it is a regular file
				throw std::runtime_error("Invalid inputFile argument. The file is not a regular file.");
			}
			if (access(inputFilePath, R_OK)) { // make sure we can access it
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

	static std::string
	setupCfgDir()
	{
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
				if ((dir_var != nullptr) && cti_conventions::dirHasPerms(dir_var, R_OK | W_OK | X_OK)) {
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
			if (!cti_conventions::dirHasPerms(cfgPath.c_str(), R_OK | W_OK | X_OK)) {
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

		return cfgPath;
	}

	static std::string accessiblePath(std::string const& path) {
		if (!access(path.c_str(), R_OK | X_OK)) {
			return path;
		}
		throw std::runtime_error("path inacessible: " + path);
	};

	static std::string
	setupBaseDir(void)
	{
		const char * base_dir_env = getenv(BASE_DIR_ENV_VAR);
		if ((base_dir_env == nullptr) || !cti_conventions::dirHasPerms(base_dir_env, R_OK | X_OK)) {
			for (const char* const* pathPtr = cti_conventions::_cti_default_dir_locs; *pathPtr != nullptr; pathPtr++) {
				if (cti_conventions::dirHasPerms(*pathPtr, R_OK | X_OK)) {
					return std::string{*pathPtr};
				}
			}
		} else {
			return std::string{base_dir_env};
		}

		throw std::runtime_error(std::string{"failed to find a CTI installation. Ensure "} + BASE_DIR_ENV_VAR + " is set properly.");
	}

	/*
	 * This routine automatically determines the active Workload Manager. The
	 * user can force SSH as the "WLM" by setting the environment variable CTI_LAUNCHER_NAME.
	 * In the case of complete failure to determine the WLM, the default value of _cti_nonenessProto
	 * is used.
	*/
	static std::unique_ptr<Frontend>
	make_Frontend()
	{
		using DefaultFrontend = CraySLURMFrontend;

		// We do not want to call init if we are running on the backend inside of
		// a tool daemon! It is possible for BE libraries to link against both the
		// CTI fe and be libs (e.g. MRNet) and we do not want to call the FE init
		// in that case.
		if (cti_conventions::isRunningOnBackend()) {
			throw std::runtime_error("tried to create a Frontend implementation on the backend!");
		}

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
					return std::make_unique<DefaultFrontend>();
				}
			}
		}

		// parse the returned result
		if (!wlmName.compare("SLURM") || !wlmName.compare("slurm")) {
			return std::make_unique<CraySLURMFrontend>();
		} else if (!wlmName.compare("generic")) {
			return  std::make_unique<GenericSSHFrontend>();
		} else {
			// fallback to use the default
			fprintf(stderr, "Invalid workload manager argument %s provided in %s\n", wlmName.c_str(), CTI_WLM);
			return std::make_unique<DefaultFrontend>();
		}
	}

	// run code that can throw and use it to set cti error instead
	template <typename FuncType, typename ReturnType = decltype(std::declval<FuncType>()())>
	static ReturnType
	runSafely(std::string const& caller, FuncType&& func, ReturnType const onError) {
		try {
			return std::forward<FuncType>(func)();
		} catch (std::exception const& ex) {
			_cti_set_error((caller + ": " + ex.what()).c_str());
			return onError;
		}
	}
}
constexpr auto SUCCESS = cti_conventions::return_code::SUCCESS;
constexpr auto FAILURE = cti_conventions::return_code::FAILURE;

constexpr auto APP_ERROR      = cti_conventions::return_code::APP_ERROR;
constexpr auto SESSION_ERROR  = cti_conventions::return_code::SESSION_ERROR;
constexpr auto MANIFEST_ERROR = cti_conventions::return_code::MANIFEST_ERROR;

/*********************
** internal functions
*********************/

// store and associate an arbitrary C++ object with an id (to make it accessible to C clients)
template <typename IdType, typename T>
class Registry {

private: // variables
	std::unordered_map<IdType, T> m_list;
	IdType m_id = IdType{};

public: // interface

	bool isValid(IdType const id) const { return m_list.find(id) != m_list.end(); }
	void erase(IdType const id)         { m_list.erase(id); }
	T&   get(IdType const id)           { return m_list.at(id); }

	// take ownership of an object and assign it an id
	IdType own(T&& expiring) {
		// preincrement as cti_app/session/manifest_id_t represent error as 0
		auto const newId = ++m_id;

		m_list.insert(std::make_pair(newId, std::move(expiring)));
		return newId;
	}
};

/* global interface state objects */

static auto appRegistry      = Registry<cti_app_id_t,      std::unique_ptr<App>>{};
static auto sessionRegistry  = Registry<cti_session_id_t,  std::shared_ptr<Session>>{};
static auto manifestRegistry = Registry<cti_manifest_id_t, std::shared_ptr<Manifest>>{};

std::string const&
_cti_getCfgDir() {
	static std::string _cti_cfg_dir = cti_conventions::setupCfgDir();
	return _cti_cfg_dir;
}

std::string const&
_cti_getBaseDir() {
	static std::string _cti_base_dir = cti_conventions::setupBaseDir();
	return _cti_base_dir;
}

std::string const&
_cti_getLdAuditPath() {
	static std::string _cti_ld_audit_lib = cti_conventions::accessiblePath(_cti_getBaseDir() + "/lib/" + LD_AUDIT_LIB_NAME);
	return _cti_ld_audit_lib;
}

std::string const&
_cti_getOverwatchPath() {
	static std::string _cti_overwatch_bin = cti_conventions::accessiblePath(_cti_getBaseDir() + "/libexec/" + CTI_OVERWATCH_BINARY);
	return _cti_overwatch_bin;
}

std::string const&
_cti_getDlaunchPath() {
	static std::string _cti_dlaunch_bin = cti_conventions::accessiblePath(_cti_getBaseDir() + "/libexec/" + CTI_DLAUNCH_BINARY);
	return _cti_dlaunch_bin;
}

Frontend&
_cti_getCurrentFrontend() {
	static auto _cti_currentFrontendPtr = cti_conventions::make_Frontend();
	return *_cti_currentFrontendPtr;
}

Logger&
_cti_getLogger() {
	static auto _cti_logger = Logger{_cti_getCurrentFrontend().getHostname().c_str(), getpid()};
	return _cti_logger;
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
	try {
		return _cti_getCurrentFrontend().getWLMType();
	} catch (std::exception& ex) {
		return CTI_WLM_NONE;
	}
}

const char *
cti_wlm_type_toString(cti_wlm_type wlm_type) {
	switch (wlm_type) {

		case CTI_WLM_CRAY_SLURM:
			return "Cray based SLURM";

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
	return cti_conventions::runSafely(__func__, [&](){
		return appRegistry.get(appId)->getNumPEs();
	}, -1);
}

int
cti_getNumAppNodes(cti_app_id_t appId) {
	return cti_conventions::runSafely(__func__, [&](){
		return appRegistry.get(appId)->getNumHosts();
	}, -1);
}

char**
cti_getAppHostsList(cti_app_id_t appId) {
	return cti_conventions::runSafely(__func__, [&](){
		auto const hostList = appRegistry.get(appId)->getHostnameList();

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
	return cti_conventions::runSafely(__func__, [&](){
		auto const hostPlacement = appRegistry.get(appId)->getHostsPlacement();

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
	return cti_conventions::runSafely(__func__, [&](){
		return strdup(_cti_getCurrentFrontend().getHostname().c_str());
	}, (char*)nullptr);
}

char*
cti_getLauncherHostName(cti_app_id_t appId) {
	return cti_conventions::runSafely(__func__, [&](){
		return strdup(appRegistry.get(appId)->getLauncherHostname().c_str());
	}, (char*)nullptr);
}



/* WLM-specific function implementation */

template <typename WLMType>
static WLMType& downcastCurrentFE() {
	try {
		return dynamic_cast<WLMType&>(_cti_getCurrentFrontend());
	} catch (std::bad_cast& bc) {
		std::string const wlmName(cti_wlm_type_toString(_cti_getCurrentFrontend().getWLMType()));
		throw std::runtime_error("Invalid call. " + wlmName + " not in use.");
	}
}

// Cray-SLURM

cti_srunProc_t*
cti_cray_slurm_getJobInfo(pid_t srunPid) {
	return cti_conventions::runSafely(__func__, [&](){
		auto& craySlurm = downcastCurrentFE<CraySLURMFrontend>();
		if (auto result = (cti_srunProc_t*)malloc(sizeof(cti_srunProc_t))) {
			*result = craySlurm.getSrunInfo(srunPid);
			return result;
		} else {
			throw std::runtime_error("malloc failed.");
		}
	}, (cti_srunProc_t*)nullptr);
}

cti_app_id_t
cti_cray_slurm_registerJobStep(uint32_t job_id, uint32_t step_id) {
	return cti_conventions::runSafely(__func__, [&](){
		auto& craySlurm = downcastCurrentFE<CraySLURMFrontend>();
		return appRegistry.own(craySlurm.registerJob(2, job_id, step_id));
	}, APP_ERROR);
}

cti_srunProc_t*
cti_cray_slurm_getSrunInfo(cti_app_id_t appId) {
	return cti_conventions::runSafely(__func__, [&](){
		if (auto result = (cti_srunProc_t*)malloc(sizeof(cti_srunProc_t))) {
			*result = dynamic_cast<CraySLURMApp&>(*appRegistry.get(appId)).getSrunInfo();
			return result;
		} else {
			throw std::runtime_error("malloc failed.");
		}
	}, (cti_srunProc_t*)nullptr);
}



/* app launch / release implementations */

int
cti_appIsValid(cti_app_id_t appId) {
	return cti_conventions::runSafely(__func__, [&](){
		return appRegistry.isValid(appId);
	}, false);
}

void
cti_deregisterApp(cti_app_id_t appId) {
	cti_conventions::runSafely(__func__, [&](){
		appRegistry.erase(appId);
		return true;
	}, false);
}

cti_app_id_t
cti_launchApp(const char * const launcher_argv[], int stdout_fd, int stderr_fd,
	const char *inputFile, const char *chdirPath, const char * const env_list[])
{
	return cti_conventions::runSafely(__func__, [&](){
		// sanity check the app launch arguments
		cti_conventions::verifyLaunchArgs(stdout_fd, stderr_fd, inputFile, chdirPath);

		// delegate app launch and registration to launchAppBarrier
		auto const appId = cti_launchAppBarrier(launcher_argv, stdout_fd, stderr_fd, inputFile, chdirPath, env_list);

		// release barrier
		appRegistry.get(appId)->releaseBarrier();

		return appId;
	}, APP_ERROR);
}

cti_app_id_t
cti_launchAppBarrier(const char * const launcher_argv[], int stdout_fd, int stderr_fd,
	const char *inputFile, const char *chdirPath, const char * const env_list[])
{
	return cti_conventions::runSafely(__func__, [&](){
		// sanity check the app launch arguments
		cti_conventions::verifyLaunchArgs(stdout_fd, stderr_fd, inputFile, chdirPath);

		// register new app instance held at barrier
		return appRegistry.own(_cti_getCurrentFrontend().launchBarrier(launcher_argv, stdout_fd, stderr_fd,
			inputFile, chdirPath, env_list));
	}, APP_ERROR);
}

int
cti_releaseAppBarrier(cti_app_id_t appId) {
	return cti_conventions::runSafely(__func__, [&](){
		appRegistry.get(appId)->releaseBarrier();
		return SUCCESS;
	}, FAILURE);
}

int
cti_killApp(cti_app_id_t appId, int signum) {
	return cti_conventions::runSafely(__func__, [&](){
		appRegistry.get(appId)->kill(signum);
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
	return cti_conventions::runSafely(__func__, [&](){
		// register new session instance and ship the WLM-specific base files
		auto const sid = sessionRegistry.own(
			std::make_shared<Session>(_cti_getCurrentFrontend().getWLMType(), *appRegistry.get(appId)));
		shipWLMBaseFiles(*sessionRegistry.get(sid));
		return sid;
	}, SESSION_ERROR);
}

int
cti_sessionIsValid(cti_session_id_t sid) {
	return cti_conventions::runSafely(__func__, [&](){
		return sessionRegistry.isValid(sid);
	}, false);
}

char**
cti_getSessionLockFiles(cti_session_id_t sid) {
	return cti_conventions::runSafely(__func__, [&](){
		auto const& activeManifests = sessionRegistry.get(sid)->getManifests();

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
	return cti_conventions::runSafely(caller, [&](){
		// get session and construct string
		auto const& session = *sessionRegistry.get(sid);
		std::stringstream ss;
		ss << session.m_toolPath << "/" << session.m_stageName << str;
		return strdup(ss.str().c_str());
	}, (char*)nullptr);
}

char*
cti_getSessionRootDir(cti_session_id_t sid) {
	return sessionPathAppend(__func__, sid, "");
}

char*
cti_getSessionBinDir(cti_session_id_t sid) {
	return sessionPathAppend(__func__, sid, "/bin");
}

char*
cti_getSessionLibDir(cti_session_id_t sid) {
	return sessionPathAppend(__func__, sid, "/lib");
}

char*
cti_getSessionFileDir(cti_session_id_t sid) {
	return sessionPathAppend(__func__, sid, "");
}

char*
cti_getSessionTmpDir(cti_session_id_t sid) {
	return sessionPathAppend(__func__, sid, "/tmp");
}



/* manifest implementations */

cti_manifest_id_t
cti_createManifest(cti_session_id_t sid) {
	return cti_conventions::runSafely(__func__, [&](){
		return manifestRegistry.own(sessionRegistry.get(sid)->createManifest());
	}, MANIFEST_ERROR);
}

int
cti_manifestIsValid(cti_manifest_id_t mid) {
	return cti_conventions::runSafely(__func__, [&](){
		return manifestRegistry.isValid(mid);
	}, false);
}

int
cti_destroySession(cti_session_id_t sid) {
	return cti_conventions::runSafely(__func__, [&](){
		sessionRegistry.get(sid)->launchCleanup();
		sessionRegistry.erase(sid);
		return SUCCESS;
	}, FAILURE);
}

int
cti_addManifestBinary(cti_manifest_id_t mid, const char * rawName) {
	return cti_conventions::runSafely(__func__, [&](){
		manifestRegistry.get(mid)->addBinary(rawName);
		return SUCCESS;
	}, FAILURE);
}

int
cti_addManifestLibrary(cti_manifest_id_t mid, const char * rawName) {
	return cti_conventions::runSafely(__func__, [&](){
		manifestRegistry.get(mid)->addLibrary(rawName);
		return SUCCESS;
	}, FAILURE);
}

int
cti_addManifestLibDir(cti_manifest_id_t mid, const char * rawName) {
	return cti_conventions::runSafely(__func__, [&](){
		manifestRegistry.get(mid)->addLibDir(rawName);
		return SUCCESS;
	}, FAILURE);
}

int
cti_addManifestFile(cti_manifest_id_t mid, const char * rawName) {
	return cti_conventions::runSafely(__func__, [&](){
		manifestRegistry.get(mid)->addFile(rawName);
		return SUCCESS;
	}, FAILURE);
}

int
cti_sendManifest(cti_manifest_id_t mid) {
	return cti_conventions::runSafely(__func__, [&](){
		auto remotePackage = manifestRegistry.get(mid)->finalizeAndShip();
		remotePackage.extract();
		manifestRegistry.erase(mid);
		return SUCCESS;
	}, FAILURE);
}

/* tool daemon prototypes */
int
cti_execToolDaemon(cti_manifest_id_t mid, const char *daemonPath,
	const char * const daemonArgs[], const char * const envVars[])
{
	return cti_conventions::runSafely(__func__, [&](){
		{ auto& manifest = *manifestRegistry.get(mid);
			manifest.addBinary(daemonPath);
			auto remotePackage = manifest.finalizeAndShip();
			remotePackage.extractAndRun(daemonPath, daemonArgs, envVars);
		}
		manifestRegistry.erase(mid);
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
void _cti_transfer_fini(void) { /* no-op */ }
