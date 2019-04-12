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

#include <set>
#include <unordered_map>
#include <memory>
#include <sstream>
#include <typeinfo>

// CTI definition includes
#include "cti_fe_iface.h"

// CTI Transfer includes
#include "cti_transfer/Manifest.hpp"
#include "cti_transfer/Session.hpp"

// CTI Frontend / App implementations
#include "Frontend.hpp"
#include "Frontend_impl.hpp"

// utility includes
#include "useful/cti_useful.h"
#include "useful/Dlopen.hpp"
#include "useful/ExecvpOutput.hpp"
#include "useful/cti_argv.hpp"
#include "useful/cti_overwatch.hpp"
#include "useful/MsgQueue.hpp"

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

	// use user info to build unique staging path; optionally create the staging direcotry
	static std::string setupCfgDir();

	// verify read/execute permissions of the given path, throw if inaccessible
	static std::string accessiblePath(std::string const& path);

	// get the base CTI directory from the environment and verify its permissions
	static std::string setupBaseDir();

	// use environment info to determine and instantiate the correct WLM Frontend implementation
	static std::unique_ptr<Frontend> detect_Frontend();

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
	fileHasPerms(char const* filePath, int const perms)
	{
		struct stat st;
		return !stat(filePath, &st) // make sure this directory exists
			&& S_ISREG(st.st_mode)  // make sure it is a regular file
			&& !access(filePath, perms); // check that the file has the desired permissions
	}

	static bool
	dirHasPerms(char const* dirPath, int const perms)
	{
		struct stat st;
		return !stat(dirPath, &st) // make sure this directory exists
			&& S_ISDIR(st.st_mode) // make sure it is a directory
			&& !access(dirPath, perms); // check that the directory has the desired permissions
	}

	static bool
	canWriteFd(int const fd)
	{
		errno = 0;
		int accessFlags = fcntl(fd, F_GETFL) & O_ACCMODE;
		if (errno != 0) {
			return false;
		}
		return (accessFlags & O_RDWR) || (accessFlags & O_WRONLY);
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
	detect_Frontend()
	{
		using DefaultFrontend = CraySLURMFrontend;

		// We do not want to call init if we are running on the backend inside of
		// a tool daemon! It is possible for BE libraries to link against both the
		// CTI fe and be libs (e.g. MRNet) and we do not want to call the FE init
		// in that case.
		if (cti_conventions::isRunningOnBackend()) {
			return nullptr;
		}

		std::string wlmName;

		// Use the workload manager in the environment variable if it is set
		if (const char* wlm_name_env = getenv(CTI_WLM)) {
			wlmName = std::string(wlm_name_env);

			// parse the env string
			if (!wlmName.compare("SLURM") || !wlmName.compare("slurm")) {
				return std::make_unique<CraySLURMFrontend>();
			}
			else if (!wlmName.compare("generic")) {
				return std::make_unique<GenericSSHFrontend>();
			}
			else {
				// fallback to use the default
				fprintf(stderr, "Invalid workload manager argument %s provided in %s\n", wlmName.c_str(), CTI_WLM);
				return std::make_unique<DefaultFrontend>();
			}
		}
		else {
			// Query for the slurm package using rpm
			// FIXME: This is a hack. This should be addressed by PE-25088
			auto rpmArgv = cti_argv::ManagedArgv { "rpm", "-q", "slurm" };
			ExecvpOutput rpmOutput("rpm", rpmArgv.get());
			auto res = rpmOutput.getExitStatus();
			if (res == 0) {
				// The slurm package is installed. This is a naive check.
				return std::make_unique<CraySLURMFrontend>();
			}
		}

		// Unknown WLM
		fprintf(stderr, "Unable to determine wlm in use. Falling back to default.");
		return std::make_unique<DefaultFrontend>();
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

class CTIFEIface
{
public: // types
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

	using AppRegistry = Registry<cti_app_id_t, std::unique_ptr<App>>;
	using SessionRegistry = Registry<cti_session_id_t, std::shared_ptr<Session>>;
	using ManifestRegistry = Registry<cti_manifest_id_t, std::shared_ptr<Manifest>>;

public: // variables
	AppRegistry appRegistry;
	SessionRegistry sessionRegistry;
	ManifestRegistry manifestRegistry;

	// associate app with session IDs so that when an app is deregistered its sessions are invalidated
	std::unordered_map<cti_app_id_t, std::set<cti_session_id_t>> appSessions;

	std::string const cfg_dir;
	std::string const base_dir;
	std::string const ld_audit_lib;
	std::string const overwatch_bin;
	std::string const dlaunch_bin;

	std::unique_ptr<Frontend> currentFrontendPtr;

	Logger logger;
	OverwatchQueue overwatchQueue;

public: // interface
	CTIFEIface()
		: appRegistry{}
		, sessionRegistry{}
		, manifestRegistry{}
		, appSessions{}

		, cfg_dir{cti_conventions::setupCfgDir()}
		, base_dir{cti_conventions::setupBaseDir()}
		, ld_audit_lib{cti_conventions::accessiblePath(base_dir + "/lib/" + LD_AUDIT_LIB_NAME)}
		, overwatch_bin{cti_conventions::accessiblePath(base_dir + "/libexec/" + CTI_OVERWATCH_BINARY)}
		, dlaunch_bin{cti_conventions::accessiblePath(base_dir + "/libexec/" + CTI_DLAUNCH_BINARY)}

		, currentFrontendPtr{cti_conventions::detect_Frontend()}

		, logger{currentFrontendPtr ? currentFrontendPtr->getHostname().c_str() : "(NULL frontend)", getpid()}
	{
		key_t overwatchQueueKey = rand();
		if (auto const forkedPid = fork()) {
			// parent case

			// close fds
			dup2(open("/dev/null", O_RDONLY), STDIN_FILENO);
			dup2(open("/dev/null", O_WRONLY), STDOUT_FILENO);
			// dup2(open("/dev/null", O_WRONLY), STDERR_FILENO);

			// setup args
			using OWA = CTIOverwatchArgv;
			cti_argv::OutgoingArgv<OWA> overwatchArgv{overwatch_bin};
			overwatchArgv.add(OWA::ClientPID, std::to_string(forkedPid));
			overwatchArgv.add(OWA::QueueKey,  std::to_string(overwatchQueueKey));

			// exec
			execvp(overwatch_bin.c_str(), overwatchArgv.get());
			throw std::runtime_error("returned from execvp: " + std::string{strerror(errno)});
		} else {
			// child case
			overwatchQueue = OverwatchQueue{overwatchQueueKey};
		}
	}

	~CTIFEIface()
	{
		overwatchQueue.send(OverwatchMsgType::Shutdown, OverwatchData{});
	}
};

/* global state accessors */

static CTIFEIface& _cti_getState()
{
	static CTIFEIface feIfaceState{};
	return feIfaceState;
}

std::string const&
_cti_getCfgDir() {
	return _cti_getState().cfg_dir;
}

std::string const&
_cti_getBaseDir() {
	return _cti_getState().base_dir;
}

std::string const&
_cti_getLdAuditPath() {
	return _cti_getState().ld_audit_lib;
}

std::string const&
_cti_getOverwatchPath() {
	return _cti_getState().overwatch_bin;
}

std::string const&
_cti_getDlaunchPath() {
	return _cti_getState().dlaunch_bin;
}

Frontend&
_cti_getCurrentFrontend()
{
	if (_cti_getState().currentFrontendPtr) {
		return *_cti_getState().currentFrontendPtr;
	} else {
		throw std::runtime_error("tried to use an uninitialized frontend");
	}
}

Logger&
_cti_getLogger() {
	return _cti_getState().logger;
}

/* overwatch interface */

pid_t
_cti_overwatchApp(pid_t const appPid)
{
	if (appPid) {
		_cti_getState().overwatchQueue.send(OverwatchMsgType::AppRegister,
			OverwatchData { .appPid = appPid }
		);
	}
	return appPid;
}

pid_t
_cti_overwatchUtil(pid_t const appPid, pid_t const utilPid)
{
	if (utilPid) {
		_cti_getState().overwatchQueue.send(OverwatchMsgType::UtilityRegister,
			OverwatchData { .appPid = appPid, .utilPid = utilPid }
		);
	}
	return utilPid;
}

void
_cti_endOverwatchApp(pid_t const appPid)
{
	_cti_getState().overwatchQueue.send(OverwatchMsgType::AppDeregister,
		OverwatchData { .appPid = appPid }
	);
}

/* internal testing functions */

// this function is used only during testing to manually add Mock App instances
cti_app_id_t
_cti_registerApp(std::unique_ptr<App>&& expiring)
{
	return _cti_getState().appRegistry.own(std::move(expiring));
}

// this function is used only during testing to manually get a Mock App reference
App&
_cti_getApp(cti_app_id_t const appId)
{
	return *_cti_getState().appRegistry.get(appId);
}

// this function is used only during testing to manually set a custom CTI Frontend
void
_cti_setFrontend(std::unique_ptr<Frontend>&& expiring)
{
	_cti_getState().currentFrontendPtr = std::move(expiring);
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

		case CTI_WLM_MOCK:
			return "Test WLM frontend";

		case CTI_WLM_NONE:
			return "No WLM detected";
	}

	// Shouldn't get here
	return "Invalid WLM.";
}

int
cti_getNumAppPEs(cti_app_id_t appId) {
	return cti_conventions::runSafely(__func__, [&](){
		return _cti_getState().appRegistry.get(appId)->getNumPEs();
	}, -1);
}

int
cti_getNumAppNodes(cti_app_id_t appId) {
	return cti_conventions::runSafely(__func__, [&](){
		return _cti_getState().appRegistry.get(appId)->getNumHosts();
	}, -1);
}

char**
cti_getAppHostsList(cti_app_id_t appId) {
	return cti_conventions::runSafely(__func__, [&](){
		auto const hostList = _cti_getState().appRegistry.get(appId)->getHostnameList();

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
		auto const hostPlacement = _cti_getState().appRegistry.get(appId)->getHostsPlacement();

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
		return strdup(_cti_getState().appRegistry.get(appId)->getLauncherHostname().c_str());
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
		return _cti_getState().appRegistry.own(craySlurm.registerJob(2, job_id, step_id));
	}, APP_ERROR);
}

cti_srunProc_t*
cti_cray_slurm_getSrunInfo(cti_app_id_t appId) {
	return cti_conventions::runSafely(__func__, [&](){
		if (auto result = (cti_srunProc_t*)malloc(sizeof(cti_srunProc_t))) {
			*result = dynamic_cast<CraySLURMApp&>(*_cti_getState().appRegistry.get(appId)).getSrunInfo();
			return result;
		} else {
			throw std::runtime_error("malloc failed.");
		}
	}, (cti_srunProc_t*)nullptr);
}

// SSH

cti_app_id_t
cti_ssh_registerJob(pid_t launcher_pid)
{
	return cti_conventions::runSafely(__func__, [&](){
		auto& genericSSH = downcastCurrentFE<GenericSSHFrontend>();
		return _cti_getState().appRegistry.own(genericSSH.registerJob(1, launcher_pid));
	}, APP_ERROR);
}

/* app launch / release implementations */

int
cti_appIsValid(cti_app_id_t appId) {
	return cti_conventions::runSafely(__func__, [&](){
		return _cti_getState().appRegistry.isValid(appId);
	}, false);
}

void
cti_deregisterApp(cti_app_id_t appId) {
	cti_conventions::runSafely(__func__, [&](){
		auto const& idSessionsPair = _cti_getState().appSessions.find(appId);
		if (idSessionsPair != _cti_getState().appSessions.end()) {
			// invalidate the app's transfer sessions
			for (auto&& sessionId : idSessionsPair->second) {
				_cti_getState().sessionRegistry.erase(sessionId);
			}
			_cti_getState().appSessions.erase(idSessionsPair);
		}

		// invalidate the app ID
		_cti_getState().appRegistry.erase(appId);

		return true;
	}, false);
}

cti_app_id_t
cti_launchApp(const char * const launcher_argv[], int stdout_fd, int stderr_fd,
	const char *inputFile, const char *chdirPath, const char * const env_list[])
{
	return cti_conventions::runSafely(__func__, [&](){
		// delegate app launch and registration to launchAppBarrier
		auto const appId = cti_launchAppBarrier(launcher_argv, stdout_fd, stderr_fd, inputFile, chdirPath, env_list);

		// release barrier
		_cti_getState().appRegistry.get(appId)->releaseBarrier();

		return appId;
	}, APP_ERROR);
}

cti_app_id_t
cti_launchAppBarrier(const char * const launcher_argv[], int stdoutFd, int stderrFd,
	const char *inputFile, const char *chdirPath, const char * const env_list[])
{
	return cti_conventions::runSafely(__func__, [&](){
		// verify that FDs are writable, input file path is readable, and chdir path is
		// read/write/executable. if not, throw an exception with the corresponding error message

		// ensure stdout, stderr can be written to (fd is -1, then ignore)
		if ((stdoutFd > 0) && !cti_conventions::canWriteFd(stdoutFd)) {
			throw std::runtime_error("Invalid stdoutFd argument. No write access.");
		}
		if ((stderrFd > 0) && !cti_conventions::canWriteFd(stderrFd)) {
			throw std::runtime_error("Invalid stderr_fd argument. No write access.");
		}

		// verify inputFile is a file that can be read
		if ((inputFile != nullptr) && !cti_conventions::fileHasPerms(inputFile, R_OK)) {
			throw std::runtime_error("Invalid inputFile argument. No read access.");
		}
		// verify chdirPath is a directory that can be read, written, and executed
		if ((chdirPath != nullptr) && !cti_conventions::dirHasPerms(chdirPath, R_OK | W_OK | X_OK)) {
			throw std::runtime_error("Invalid chdirPath argument. No RWX access.");
		}

		// register new app instance held at barrier
		return _cti_getState().appRegistry.own(_cti_getCurrentFrontend().launchBarrier(launcher_argv, stdoutFd, stderrFd,
			inputFile, chdirPath, env_list));
	}, APP_ERROR);
}

int
cti_releaseAppBarrier(cti_app_id_t appId) {
	return cti_conventions::runSafely(__func__, [&](){
		_cti_getState().appRegistry.get(appId)->releaseBarrier();
		return SUCCESS;
	}, FAILURE);
}

int
cti_killApp(cti_app_id_t appId, int signum) {
	return cti_conventions::runSafely(__func__, [&](){
		_cti_getState().appRegistry.get(appId)->kill(signum);
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
		auto const sid = _cti_getState().sessionRegistry.own(
			std::make_shared<Session>(_cti_getCurrentFrontend().getWLMType(), *_cti_getState().appRegistry.get(appId)));
		shipWLMBaseFiles(*_cti_getState().sessionRegistry.get(sid));

		// associate owning app ID with the new session ID
		_cti_getState().appSessions[appId].insert(sid);

		return sid;
	}, SESSION_ERROR);
}

int
cti_sessionIsValid(cti_session_id_t sid) {
	return cti_conventions::runSafely(__func__, [&](){
		return _cti_getState().sessionRegistry.isValid(sid);
	}, false);
}

char**
cti_getSessionLockFiles(cti_session_id_t sid) {
	return cti_conventions::runSafely(__func__, [&](){
		auto const& activeManifests = _cti_getState().sessionRegistry.get(sid)->getManifests();

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
		auto const& session = *_cti_getState().sessionRegistry.get(sid);
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
		return _cti_getState().manifestRegistry.own(_cti_getState().sessionRegistry.get(sid)->createManifest());
	}, MANIFEST_ERROR);
}

int
cti_manifestIsValid(cti_manifest_id_t mid) {
	return cti_conventions::runSafely(__func__, [&](){
		return _cti_getState().manifestRegistry.isValid(mid);
	}, false);
}

int
cti_destroySession(cti_session_id_t sid) {
	return cti_conventions::runSafely(__func__, [&](){
		_cti_getState().sessionRegistry.get(sid)->launchCleanup();
		_cti_getState().sessionRegistry.erase(sid);
		return SUCCESS;
	}, FAILURE);
}

int
cti_addManifestBinary(cti_manifest_id_t mid, const char * rawName) {
	return cti_conventions::runSafely(__func__, [&](){
		_cti_getState().manifestRegistry.get(mid)->addBinary(rawName);
		return SUCCESS;
	}, FAILURE);
}

int
cti_addManifestLibrary(cti_manifest_id_t mid, const char * rawName) {
	return cti_conventions::runSafely(__func__, [&](){
		_cti_getState().manifestRegistry.get(mid)->addLibrary(rawName);
		return SUCCESS;
	}, FAILURE);
}

int
cti_addManifestLibDir(cti_manifest_id_t mid, const char * rawName) {
	return cti_conventions::runSafely(__func__, [&](){
		_cti_getState().manifestRegistry.get(mid)->addLibDir(rawName);
		return SUCCESS;
	}, FAILURE);
}

int
cti_addManifestFile(cti_manifest_id_t mid, const char * rawName) {
	return cti_conventions::runSafely(__func__, [&](){
		_cti_getState().manifestRegistry.get(mid)->addFile(rawName);
		return SUCCESS;
	}, FAILURE);
}

int
cti_sendManifest(cti_manifest_id_t mid) {
	return cti_conventions::runSafely(__func__, [&](){
		auto remotePackage = _cti_getState().manifestRegistry.get(mid)->finalizeAndShip();
		remotePackage.extract();
		_cti_getState().manifestRegistry.erase(mid);
		return SUCCESS;
	}, FAILURE);
}

/* tool daemon prototypes */
int
cti_execToolDaemon(cti_manifest_id_t mid, const char *daemonPath,
	const char * const daemonArgs[], const char * const envVars[])
{
	return cti_conventions::runSafely(__func__, [&](){
		{ auto& manifest = *_cti_getState().manifestRegistry.get(mid);
			manifest.addBinary(daemonPath);
			auto remotePackage = manifest.finalizeAndShip();
			remotePackage.extractAndRun(daemonPath, daemonArgs, envVars);
		}
		_cti_getState().manifestRegistry.erase(mid);
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

int
cti_setAttribute(cti_attr_type attrib, const char *value)
{
	return cti_conventions::runSafely(__func__, [&](){
		switch (attrib) {
			case CTI_ATTR_STAGE_DEPENDENCIES:
				if (value == nullptr) {
					throw std::runtime_error("CTI_ATTR_STAGE_DEPENDENCIES: NULL pointer for 'value'.");
				} else if (value[0] == '0') {
					_cti_setStageDeps(false);
					return SUCCESS;
				} else if (value[0] == '1') {
					_cti_setStageDeps(true);
					return SUCCESS;
				} else {
					throw std::runtime_error("CTI_ATTR_STAGE_DEPENDENCIES: Unsupported value '" + std::to_string(value[0]) + "'");
				}
			default:
				throw std::runtime_error("Invalid cti_attr_type " + std::to_string((int)attrib));
		}
	}, FAILURE);
}
