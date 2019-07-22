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

// This pulls in config.h
#include "cti_defs.h"

// CTI definition includes
#include "cti_fe_iface.hpp"

// CTI Transfer includes
#include "cti_transfer/Manifest.hpp"
#include "cti_transfer/Session.hpp"

// CTI Frontend / App implementations
#include "Frontend.hpp"
#include "Frontend_impl.hpp"

// utility includes
#include "useful/cti_useful.h"
#include "useful/cti_wrappers.hpp"
#include "useful/cti_execvp.hpp"
#include "useful/cti_argv.hpp"

/*********************
** Internal data
*********************/
char *      FE_iface::_cti_err_str = nullptr;
std::string FE_iface::m_err_str = DEFAULT_ERR_STR;
char *      FE_iface::_cti_attr_str = nullptr;

constexpr auto SUCCESS = FE_iface::SUCCESS;
constexpr auto FAILURE = FE_iface::FAILURE;

constexpr auto APP_ERROR      = FE_iface::APP_ERROR;
constexpr auto SESSION_ERROR  = FE_iface::SESSION_ERROR;
constexpr auto MANIFEST_ERROR = FE_iface::MANIFEST_ERROR;

/* Frontend utility functions */

// Cast the FE pointer to the expected type
template <typename WLMType>
static WLMType& downcastFE() {
    auto&& fe = Frontend::inst();
    try {
        return dynamic_cast<WLMType&>(fe);
    } catch (std::bad_cast& bc) {
        std::string const wlmName(cti_wlm_type_toString(fe.getWLMType()));
        throw std::runtime_error("Invalid call. " + wlmName + " not in use.");
    }
}

template <typename To, typename From>
static std::shared_ptr<To> downcastApp(std::shared_ptr<From> app) {
    auto sp = std::dynamic_pointer_cast<To>(app);
    if (!sp) {
        throw std::runtime_error("Provided appId does not belong to wlm specific funciton.");
    }
    return sp;
}

/*********************
** internal functions
*********************/

void
FE_iface::set_error_str(std::string str)
{
    m_err_str = std::move(str);
}

// Note that we want to leak by design! Do not free the buffer!
// Since we pass this out via the c interface, we cannot safely
// reclaim the space.
const char *
FE_iface::get_error_str()
{
    if (_cti_err_str == nullptr) {
        // Allocate space for the external string
        _cti_err_str = (char *)std::malloc(CTI_ERR_STR_SIZE);
        memset(_cti_err_str, '\0', CTI_ERR_STR_SIZE);
    }
    // Copy the internal error string to the external buffer
    strncpy(_cti_err_str, m_err_str.c_str(), CTI_ERR_STR_SIZE);
    // Enforce null termination
    _cti_err_str[CTI_ERR_STR_SIZE] = '\0';
    // Return the pointer to the external buffer
    return const_cast<const char *>(_cti_err_str);
}

// Note that we want to leak by design! Do not free the buffer!
// Since we pass this out via the c interface, we cannot safely
// reclaim the space.
const char *
FE_iface::get_attr_str(const char *value)
{
    if (_cti_attr_str == nullptr) {
        // Allocate space for the external string
        _cti_attr_str = (char *)std::malloc(CTI_BUF_SIZE);
        memset(_cti_attr_str, '\0', CTI_BUF_SIZE);
    }
    // Copy the value string to the buffer
    strncpy(_cti_attr_str, value, CTI_BUF_SIZE);
    // Enforce null termination
    _cti_attr_str[CTI_BUF_SIZE] = '\0';
    // Return the pointer to the external buffer
    return const_cast<const char *>(_cti_attr_str);
}

FE_iface::FE_iface()
: m_app_registry{}
, m_session_registry{}
, m_manifest_registry{}
{ }

/*******************************
* C API defined functions below
*******************************/

const char *
cti_version(void) {
    return CTI_FE_VERSION;
}

const char *
cti_error_str(void) {
    return FE_iface::get_error_str();
}

int
cti_error_str_r(char *buf, size_t buf_len) {
    if (buf_len < 1) {
        return ERANGE;
    }

    // fill buf from error string
    auto const error_str = FE_iface::get_error_str();
    std::strncpy(buf, error_str, buf_len - 1);
    buf[buf_len - 1] = '\0';

    return 0;
}

cti_wlm_type_t
cti_current_wlm(void) {
    return FE_iface::runSafely(__func__, [&](){
        auto&& fe = Frontend::inst();
        return fe.getWLMType();
    }, CTI_WLM_NONE);
}

const char *
cti_wlm_type_toString(cti_wlm_type_t wlm_type) {
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
    return FE_iface::runSafely(__func__, [&](){
        auto&& fe = Frontend::inst();
        auto const sp = fe.Iface().getApp(appId);
        return sp->getNumPEs();
    }, -1);
}

int
cti_getNumAppNodes(cti_app_id_t appId) {
    return FE_iface::runSafely(__func__, [&](){
        auto&& fe = Frontend::inst();
        auto const sp = fe.Iface().getApp(appId);
        return sp->getNumHosts();
    }, -1);
}

char**
cti_getAppHostsList(cti_app_id_t appId) {
    return FE_iface::runSafely(__func__, [&](){
        auto&& fe = Frontend::inst();
        auto sp = fe.Iface().getApp(appId);
        auto const hostList = sp->getHostnameList();

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
    return FE_iface::runSafely(__func__, [&](){
        auto&& fe = Frontend::inst();
        auto sp = fe.Iface().getApp(appId);
        auto const hostPlacement = sp->getHostsPlacement();

        cti_hostsList_t *result = (cti_hostsList_t*)malloc(sizeof(cti_hostsList_t));
        result->hosts = (cti_host_t*)malloc(sizeof(cti_host_t) * hostPlacement.size());

        result->numHosts = hostPlacement.size();
        for (size_t i = 0; i < hostPlacement.size(); i++) {
            result->hosts[i].hostname = strdup(hostPlacement[i].hostname.c_str());
            result->hosts[i].numPes   = hostPlacement[i].numPEs;
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
    return FE_iface::runSafely(__func__, [&](){
        auto&& fe = Frontend::inst();
        return strdup(fe.getHostname().c_str());
    }, (char*)nullptr);
}

char*
cti_getLauncherHostName(cti_app_id_t appId) {
    return FE_iface::runSafely(__func__, [&](){
        auto&& fe = Frontend::inst();
        auto sp = fe.Iface().getApp(appId);
        return strdup(sp->getLauncherHostname().c_str());
    }, (char*)nullptr);
}

// Cray-SLURM WLM extensions

static cti_srunProc_t*
_cti_cray_slurm_getJobInfo(pid_t srunPid) {
    return FE_iface::runSafely(__func__, [&](){
        auto&& fe = downcastFE<CraySLURMFrontend>();
        if (auto result = (cti_srunProc_t*)malloc(sizeof(cti_srunProc_t))) {
            *result = fe.getSrunInfo(srunPid);
            return result;
        } else {
            throw std::runtime_error("malloc failed.");
        }
    }, (cti_srunProc_t*)nullptr);
}

static cti_app_id_t
_cti_cray_slurm_registerJobStep(uint32_t job_id, uint32_t step_id) {
    return FE_iface::runSafely(__func__, [&](){
        auto&& fe = downcastFE<CraySLURMFrontend>();
        auto wp = fe.registerJob(2, job_id, step_id);
        return fe.Iface().trackApp(wp);
    }, APP_ERROR);
}

static cti_srunProc_t*
_cti_cray_slurm_getSrunInfo(cti_app_id_t appId) {
    return FE_iface::runSafely(__func__, [&](){
        auto&& fe = Frontend::inst();
        auto ap = downcastApp<CraySLURMApp>(fe.Iface().getApp(appId));
        if (auto result = (cti_srunProc_t*)malloc(sizeof(cti_srunProc_t))) {
            *result = ap->getSrunInfo();
            return result;
        } else {
            throw std::runtime_error("malloc failed.");
        }
    }, (cti_srunProc_t*)nullptr);
}

static cti_cray_slurm_ops_t _cti_cray_slurm_ops = {
    .getJobInfo         = _cti_cray_slurm_getJobInfo,
    .registerJobStep    = _cti_cray_slurm_registerJobStep,
    .getSrunInfo        = _cti_cray_slurm_getSrunInfo
};

// SSH WLM extensions

static cti_app_id_t
_cti_ssh_registerJob(pid_t launcher_pid) {
    return FE_iface::runSafely(__func__, [&](){
        auto&& fe = downcastFE<GenericSSHFrontend>();
        auto wp = fe.registerJob(1, launcher_pid);
        return fe.Iface().trackApp(wp);
    }, APP_ERROR);
}

static cti_ssh_ops_t _cti_ssh_ops = {
    .registerJob    = _cti_ssh_registerJob
};

// WLM specific extension ops accessor
cti_wlm_type_t
cti_open_ops(void **ops) {
    return FE_iface::runSafely(__func__, [&](){
        if (ops == nullptr) {
            throw std::runtime_error("NULL pointer for 'ops' argument.");
        }
        *ops = nullptr;
        auto&& fe = Frontend::inst();
        auto wlm_type = fe.getWLMType();
        switch (wlm_type) {
            case CTI_WLM_CRAY_SLURM:
                *ops = reinterpret_cast<void *>(&_cti_cray_slurm_ops);
                break;
            case CTI_WLM_SSH:
                *ops = reinterpret_cast<void *>(&_cti_ssh_ops);
                break;
            case CTI_WLM_NONE:
            case CTI_WLM_MOCK:
                *ops = nullptr;
                break;
        }
        return wlm_type;
    }, CTI_WLM_NONE);
}

/* app launch / release implementations */

int
cti_appIsValid(cti_app_id_t appId) {
    return FE_iface::runSafely(__func__, [&](){
        auto&& fe = Frontend::inst();
        return fe.Iface().validApp(appId);
    }, false);
}

void
cti_deregisterApp(cti_app_id_t appId) {
    FE_iface::runSafely(__func__, [&](){
        auto&& fe = Frontend::inst();
        auto sp = fe.Iface().getApp(appId);
        // Remove the app
        fe.removeApp(sp);
        fe.Iface().removeApp(appId);
        return true;
    }, false);
}

cti_app_id_t
cti_launchApp(const char * const launcher_argv[], int stdout_fd, int stderr_fd,
    const char *inputFile, const char *chdirPath, const char * const env_list[])
{
    return FE_iface::runSafely(__func__, [&](){
        // delegate app launch and registration to launchAppBarrier
        auto const appId = cti_launchAppBarrier(launcher_argv, stdout_fd, stderr_fd, inputFile, chdirPath, env_list);

        // release barrier
        auto&& fe = Frontend::inst();
        auto sp = fe.Iface().getApp(appId);
        sp->releaseBarrier();

        return appId;
    }, APP_ERROR);
}

cti_app_id_t
cti_launchAppBarrier(const char * const launcher_argv[], int stdoutFd, int stderrFd,
    const char *inputFile, const char *chdirPath, const char * const env_list[])
{
    return FE_iface::runSafely(__func__, [&](){
        // verify that FDs are writable, input file path is readable, and chdir path is
        // read/write/executable. if not, throw an exception with the corresponding error message

        // ensure stdout, stderr can be written to (fd is -1, then ignore)
        if ((stdoutFd > 0) && !cti::canWriteFd(stdoutFd)) {
            throw std::runtime_error("Invalid stdoutFd argument. No write access.");
        }
        if ((stderrFd > 0) && !cti::canWriteFd(stderrFd)) {
            throw std::runtime_error("Invalid stderr_fd argument. No write access.");
        }

        // verify inputFile is a file that can be read
        if ((inputFile != nullptr) && !cti::fileHasPerms(inputFile, R_OK)) {
            throw std::runtime_error("Invalid inputFile argument. No read access.");
        }
        // verify chdirPath is a directory that can be read, written, and executed
        if ((chdirPath != nullptr) && !cti::dirHasPerms(chdirPath, R_OK | W_OK | X_OK)) {
            throw std::runtime_error("Invalid chdirPath argument. No RWX access.");
        }

        // register new app instance held at barrier
        auto&& fe = Frontend::inst();
        auto wp = fe.launchBarrier(launcher_argv, stdoutFd, stderrFd, inputFile, chdirPath, env_list);

        return fe.Iface().trackApp(wp);
    }, APP_ERROR);
}

int
cti_releaseAppBarrier(cti_app_id_t appId) {
    return FE_iface::runSafely(__func__, [&](){
        // release barrier
        auto&& fe = Frontend::inst();
        auto sp = fe.Iface().getApp(appId);
        sp->releaseBarrier();
        return SUCCESS;
    }, FAILURE);
}

int
cti_killApp(cti_app_id_t appId, int signum) {
    return FE_iface::runSafely(__func__, [&](){
        auto&& fe = Frontend::inst();
        auto sp = fe.Iface().getApp(appId);
        sp->kill(signum);
        return SUCCESS;
    }, FAILURE);
}

/* session implementations */

cti_session_id_t
cti_createSession(cti_app_id_t appId) {
    return FE_iface::runSafely(__func__, [&](){
        // register new session instance and ship the WLM-specific base files
        auto&& fe = Frontend::inst();
        auto sp = fe.Iface().getApp(appId);
        auto wp = sp->createSession();
        return fe.Iface().trackSession(wp);
    }, SESSION_ERROR);
}

int
cti_destroySession(cti_session_id_t sid) {
    return FE_iface::runSafely(__func__, [&](){
        auto&& fe = Frontend::inst();
        auto sp = fe.Iface().getSession(sid);
        auto app_sp = sp->getOwningApp();
        app_sp->removeSession(sp);
        fe.Iface().removeSession(sid);
        return SUCCESS;
    }, FAILURE);
}

int
cti_sessionIsValid(cti_session_id_t sid) {
    return FE_iface::runSafely(__func__, [&](){
        auto&& fe = Frontend::inst();
        return fe.Iface().validSession(sid);
    }, false);
}

char**
cti_getSessionLockFiles(cti_session_id_t sid) {
    return FE_iface::runSafely(__func__, [&](){
        auto&& fe = Frontend::inst();
        auto sp = fe.Iface().getSession(sid);
        auto lock_files = sp->getSessionLockFiles();
        // ensure there's at least one manifest instance
        if (lock_files.size() == 0) {
            throw std::runtime_error("backend not initialized for session id " + std::to_string(sid));
        }
        // create return array
        auto result = (char**)malloc(sizeof(char*) * (lock_files.size() + 1));
        if (result == nullptr) {
            throw std::runtime_error("malloc failed for session id " + std::to_string(sid));
        }
        // create the strings
        for (size_t i = 0; i < lock_files.size(); i++) {
            result[i] = strdup(lock_files[i].c_str());
        }
        // Ensure the list is null terminated
        result[lock_files.size()] = nullptr;
        return result;
    }, (char**)nullptr);
}

// fill in a heap string pointer to session root path plus subdirectory
static char* sessionPathAppend(std::string const& caller, cti_session_id_t sid, const std::string& str) {
    return FE_iface::runSafely(caller, [&](){
        // get session and construct string
        auto&& fe = Frontend::inst();
        auto sp = fe.Iface().getSession(sid);
        std::stringstream ss;
        ss << sp->getStagePath() << str;
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
    return FE_iface::runSafely(__func__, [&](){
        auto&& fe = Frontend::inst();
        auto sp = fe.Iface().getSession(sid);
        auto wp = sp->createManifest();
        return fe.Iface().trackManifest(wp);
    }, MANIFEST_ERROR);
}

int
cti_manifestIsValid(cti_manifest_id_t mid) {
    return FE_iface::runSafely(__func__, [&](){
        auto&& fe = Frontend::inst();
        return fe.Iface().validManifest(mid);
    }, false);
}

int
cti_addManifestBinary(cti_manifest_id_t mid, const char * rawName) {
    return FE_iface::runSafely(__func__, [&](){
        auto&& fe = Frontend::inst();
        // Check if we should bypass dependencies
        auto deps = fe.m_stage_deps ?
            Manifest::DepsPolicy::Stage : Manifest::DepsPolicy::Ignore;
        auto mp = fe.Iface().getManifest(mid);
        mp->addBinary(rawName, deps);
        return SUCCESS;
    }, FAILURE);
}

int
cti_addManifestLibrary(cti_manifest_id_t mid, const char * rawName) {
    return FE_iface::runSafely(__func__, [&](){
        auto&& fe = Frontend::inst();
        // Check if we should bypass dependencies
        auto deps = fe.m_stage_deps ?
            Manifest::DepsPolicy::Stage : Manifest::DepsPolicy::Ignore;
        auto mp = fe.Iface().getManifest(mid);
        mp->addLibrary(rawName, deps);
        return SUCCESS;
    }, FAILURE);
}

int
cti_addManifestLibDir(cti_manifest_id_t mid, const char * rawName) {
    return FE_iface::runSafely(__func__, [&](){
        auto&& fe = Frontend::inst();
        auto mp = fe.Iface().getManifest(mid);
        mp->addLibDir(rawName);
        return SUCCESS;
    }, FAILURE);
}

int
cti_addManifestFile(cti_manifest_id_t mid, const char * rawName) {
    return FE_iface::runSafely(__func__, [&](){
        auto&& fe = Frontend::inst();
        auto mp = fe.Iface().getManifest(mid);
        mp->addFile(rawName);
        return SUCCESS;
    }, FAILURE);
}

int
cti_sendManifest(cti_manifest_id_t mid) {
    return FE_iface::runSafely(__func__, [&](){
        auto&& fe = Frontend::inst();
        auto mp = fe.Iface().getManifest(mid);
        mp->sendManifest();
        fe.Iface().removeManifest(mid);
        return SUCCESS;
    }, FAILURE);
}

/* tool daemon prototypes */
int
cti_execToolDaemon(cti_manifest_id_t mid, const char *daemonPath,
    const char * const daemonArgs[], const char * const envVars[])
{
    return FE_iface::runSafely(__func__, [&](){
        auto&& fe = Frontend::inst();
        auto mp = fe.Iface().getManifest(mid);
        mp->execManifest(daemonPath, daemonArgs, envVars);
        fe.Iface().removeManifest(mid);
        return SUCCESS;
    }, FAILURE);
}

int
cti_setAttribute(cti_attr_type_t attrib, const char *value)
{
    return FE_iface::runSafely(__func__, [&](){
        auto&& fe = Frontend::inst();
        switch (attrib) {
            case CTI_ATTR_STAGE_DEPENDENCIES:
                if (value == nullptr) {
                    throw std::runtime_error("CTI_ATTR_STAGE_DEPENDENCIES: NULL pointer for value.");
                } else if (value[0] == '0') {
                    fe.m_stage_deps = false;
                } else if (value[0] == '1') {
                    fe.m_stage_deps = true;
                } else {
                    throw std::runtime_error("CTI_ATTR_STAGE_DEPENDENCIES: Unsupported value " + std::to_string(value[0]));
                }
                break;
            case CTI_LOG_DIR:
                if (value == nullptr) {
                    throw std::runtime_error("CTI_LOG_DIR: NULL pointer for value.");
                } else if (!cti::dirHasPerms(value, R_OK | W_OK | X_OK)) {
                    throw std::runtime_error(std::string{"CTI_LOG_DIR: Bad directory specified by value "} + value);
                } else {
                    fe.m_log_dir = std::string{value};
                }
                break;
            case CTI_DEBUG:
                if (value == nullptr) {
                    throw std::runtime_error("CTI_DEBUG: NULL pointer for value.");
                } else if (value[0] == '0') {
                    fe.m_debug = false;
                } else if (value[0] == '1') {
                    fe.m_debug = true;
                } else {
                    throw std::runtime_error("CTI_DEBUG: Unsupported value " + std::to_string(value[0]));
                }
                break;
            case CTI_PMI_FOPEN_TIMEOUT:
                if (value == nullptr) {
                    throw std::runtime_error("CTI_PMI_FOPEN_TIMEOUT: NULL pointer for value.");
                } else {
                    fe.m_pmi_fopen_timeout = std::stoul(std::string{value});
                }
                break;
            case CTI_EXTRA_SLEEP:
                if (value == nullptr) {
                    throw std::runtime_error("CTI_EXTRA_SLEEP: NULL pointer for value.");
                } else {
                    fe.m_extra_sleep = std::stoul(std::string{value});
                }
                break;
            default:
                throw std::runtime_error("Invalid cti_attr_type_t " + std::to_string((int)attrib));
        }
        return SUCCESS;
    }, FAILURE);
}

const char *
cti_getAttribute(cti_attr_type_t attrib)
{
    return FE_iface::runSafely(__func__, [&](){
        auto&& fe = Frontend::inst();
        const char *ret = nullptr;
        switch (attrib) {
            case CTI_ATTR_STAGE_DEPENDENCIES:
                if (fe.m_stage_deps) {
                    ret = get_attr_str("1");
                } else {
                    ret = get_attr_str("0");
                }
                break;
            case CTI_LOG_DIR:
                ret = get_attr_str(fe.m_log_dir.c_str());
                break;
            case CTI_DEBUG:
                if (fe.m_debug) {
                    ret = get_attr_str("1");
                } else {
                    ret = get_attr_str("0");
                }
                break;
            case CTI_PMI_FOPEN_TIMEOUT:
            {
                auto str = std::to_string(fe.m_pmi_fopen_timeout);
                ret = get_attr_str(str.c_str());
            }
                break;
            case CTI_EXTRA_SLEEP:
            {
                auto str = std::to_string(fe.m_extra_sleep);
                ret = get_attr_str(str.c_str());
            }
                break;
            default:
                throw std::runtime_error("Invalid cti_attr_type_t " + std::to_string((int)attrib));
        }
        return ret;
    }, (char*)nullptr);
}
