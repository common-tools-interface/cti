/******************************************************************************\
 * cti_fe_iface.cpp - C interface layer for the cti frontend.
 *
 * Copyright 2014-2020 Hewlett Packard Enterprise Development LP.
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 ******************************************************************************/

// This pulls in config.h
#include "cti_defs.h"

// CTI definition includes
#include "cti_fe_iface.hpp"

// CTI Transfer includes
#include "transfer/Manifest.hpp"
#include "transfer/Session.hpp"

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
    _cti_err_str[CTI_ERR_STR_SIZE - 1] = '\0';
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
        // WLM Frontend implementations
        case CTI_WLM_ALPS:
#if HAVE_ALPS
            return ALPSFrontend::getName();
#else
            return "ALPS support was not configured for this build of CTI.";
#endif
        case CTI_WLM_SLURM:
            return SLURMFrontend::getName();
#if HAVE_PALS
        case CTI_WLM_PALS:
            return PALSFrontend::getName();
#else
            return "PALS support was not configured for this build of CTI.";
#endif
        case CTI_WLM_SSH:
            return GenericSSHFrontend::getName();
        case CTI_WLM_FLUX:
#if HAVE_FLUX
            return FluxFrontend::getName();
#else
            return "Flux support was not configured for this build of CTI.";
#endif

        // Internal / testing types
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

cti_binaryList_t*
cti_getAppBinaryList(cti_app_id_t appId) {
    return FE_iface::runSafely(__func__, [&](){
        auto&& fe = Frontend::inst();
        auto sp = fe.Iface().getApp(appId);
        auto const binaryRankMap = sp->getBinaryRankMap();
        auto const numPEs = sp->getNumPEs();

        // Allocate the result structures
        cti_binaryList_t *result = (cti_binaryList_t*)malloc(sizeof(cti_binaryList_t));
        result->binaries = (char**)malloc(sizeof(char*) * (binaryRankMap.size() + 1));
        result->rankMap  = (int*)malloc(sizeof(int) * (numPEs + 1));

        // Teminate the lists
        result->binaries[binaryRankMap.size()] = nullptr;
        result->rankMap[numPEs] = -1;

        { size_t i = 0;
            for (auto&& [binary, ranks] : binaryRankMap) {
                // Copy binary path
                result->binaries[i] = strdup(binary.c_str());

                // Set each result rank in rankIndices to the binary path index
                for (auto&& rank : ranks) {
                    result->rankMap[rank] = i;
                }

                i++;
            }
        }

        return result;
    }, (cti_binaryList_t*)nullptr);
}

void
cti_destroyBinaryList(cti_binaryList_t *binary_list) {
    if (binary_list == nullptr) {
        return;
    }

    if (binary_list->binaries) {
        for (char **binary_ptr = binary_list->binaries; *binary_ptr != nullptr; binary_ptr++) {
            free(*binary_ptr);
        }
        free(binary_list->binaries);
        binary_list->binaries = nullptr;
    }

    if (binary_list->rankMap) {
        free(binary_list->rankMap);
        binary_list->rankMap = nullptr;
    }

    free(binary_list);
}

char*
cti_getHostname() {
    return FE_iface::runSafely(__func__, [&](){
        // Use user setting if provided
        if (auto const host_address = ::getenv(CTI_HOST_ADDRESS_ENV_VAR)) {
            return strdup(host_address);
        }

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

// ALPS WLM extensions

#if HAVE_ALPS
#define CHECK_ALPS(...) __VA_ARGS__
#else
#define CHECK_ALPS(...) \
    throw std::runtime_error("ALPS support was not compiled into this version of CTI");
#endif

static cti_app_id_t
_cti_alps_registerApid(uint64_t apid) {
    return FE_iface::runSafely(__func__, [&](){
        CHECK_ALPS(
        auto&& fe = downcastFE<ALPSFrontend>();
        auto wp = fe.registerJob(1, apid);
        return fe.Iface().trackApp(wp);
        )
    }, APP_ERROR);
}

static uint64_t
_cti_alps_getApid(pid_t aprunPid) {
    return FE_iface::runSafely(__func__, [&](){
        CHECK_ALPS(
        auto&& fe = downcastFE<ALPSFrontend>();
        return fe.getApid(aprunPid);
        )
    }, uint64_t{0});
}

static cti_aprunProc_t*
_cti_alps_getAprunInfo(cti_app_id_t appId) {
    return FE_iface::runSafely(__func__, [&](){
        CHECK_ALPS(
        auto&& fe = Frontend::inst();
        auto ap = downcastApp<ALPSApp>(fe.Iface().getApp(appId));
        if (auto result = (cti_aprunProc_t*)malloc(sizeof(cti_aprunProc_t))) {
            *result = ap->get_cti_aprunProc_t();
            return result;
        } else {
            throw std::runtime_error("malloc failed.");
        }
        )
    }, (cti_aprunProc_t*)nullptr);
}

static int
_cti_alps_getAlpsOverlapOrdinal(cti_app_id_t appId) {
    return FE_iface::runSafely(__func__, [&](){
        CHECK_ALPS(
        auto&& fe = Frontend::inst();
        auto ap = downcastApp<ALPSApp>(fe.Iface().getApp(appId));
        return ap->getAlpsOverlapOrdinal();
        )
    }, int{-1});
}

static cti_alps_ops_t _cti_alps_ops = {
    .registerApid          = _cti_alps_registerApid,
    .getApid               = _cti_alps_getApid,
    .getAprunInfo          = _cti_alps_getAprunInfo,
    .getAlpsOverlapOrdinal = _cti_alps_getAlpsOverlapOrdinal
};

// SLURM WLM extensions

static cti_srunProc_t*
_cti_slurm_getJobInfo(pid_t srunPid) {
    return FE_iface::runSafely(__func__, [&](){
        auto&& fe = downcastFE<SLURMFrontend>();
        if (auto result = (cti_srunProc_t*)malloc(sizeof(cti_srunProc_t))) {
            *result = fe.getSrunInfo(srunPid);
            return result;
        } else {
            throw std::runtime_error("malloc failed.");
        }
    }, (cti_srunProc_t*)nullptr);
}

static cti_app_id_t
_cti_slurm_registerJobStep(uint32_t job_id, uint32_t step_id) {
    return FE_iface::runSafely(__func__, [&](){
        auto&& fe = downcastFE<SLURMFrontend>();
        auto wp = fe.registerJob(2, job_id, step_id);
        return fe.Iface().trackApp(wp);
    }, APP_ERROR);
}

static cti_srunProc_t*
_cti_slurm_getSrunInfo(cti_app_id_t appId) {
    return FE_iface::runSafely(__func__, [&](){
        auto&& fe = Frontend::inst();
        auto ap = downcastApp<SLURMApp>(fe.Iface().getApp(appId));
        if (auto result = (cti_srunProc_t*)malloc(sizeof(cti_srunProc_t))) {
            *result = ap->getSrunInfo();
            return result;
        } else {
            throw std::runtime_error("malloc failed.");
        }
    }, (cti_srunProc_t*)nullptr);
}

static cti_slurm_ops_t _cti_slurm_ops = {
    .getJobInfo         = _cti_slurm_getJobInfo,
    .registerJobStep    = _cti_slurm_registerJobStep,
    .getSrunInfo        = _cti_slurm_getSrunInfo
};

// PALS WLM extensions

#if HAVE_PALS
#define CHECK_PALS(...) __VA_ARGS__
#else
#define CHECK_PALS(...) \
    throw std::runtime_error("PALS support was not compiled into this version of CTI");
#endif

static char*
_cti_pals_getApid(pid_t launcherPid) {
    return FE_iface::runSafely(__func__, [&](){
        CHECK_PALS(
        auto&& fe = downcastFE<PALSFrontend>();
        strdup(fe.getApid(launcherPid).c_str());
        )
    }, (char*)nullptr);
}

static cti_app_id_t
_cti_pals_registerApid(char const* apid) {
    return FE_iface::runSafely(__func__, [&](){
        CHECK_PALS(
        auto&& fe = downcastFE<PALSFrontend>();
        auto wp = fe.registerJob(1, apid);
        return fe.Iface().trackApp(wp);
        )
    }, APP_ERROR);
}

static cti_pals_ops_t _cti_pals_ops = {
    .getApid      = _cti_pals_getApid,
    .registerApid = _cti_pals_registerApid
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

static cti_app_id_t
_cti_ssh_registerRemoteJob(char const* hostname, pid_t launcher_pid) {
    return FE_iface::runSafely(__func__, [&](){
        auto&& fe = downcastFE<GenericSSHFrontend>();
        auto wp = fe.registerRemoteJob(hostname, launcher_pid);
        return fe.Iface().trackApp(wp);
    }, APP_ERROR);
}

static cti_app_id_t
_cti_ssh_registerLauncherPid(pid_t launcher_pid) {
    return FE_iface::runSafely(__func__, [&](){
        auto&& fe = downcastFE<GenericSSHFrontend>();
        auto wp = fe.registerJob(1, launcher_pid);
        return fe.Iface().trackApp(wp);
    }, APP_ERROR);
}

static cti_ssh_ops_t _cti_ssh_ops = {
    .registerJob         = _cti_ssh_registerJob,
    .registerRemoteJob   = _cti_ssh_registerRemoteJob,
    .registerLauncherPid = _cti_ssh_registerLauncherPid
};

// Flux WLM extensions

#if HAVE_FLUX
#define CHECK_FLUX(...) __VA_ARGS__
#else
#define CHECK_FLUX(...) \
    throw std::runtime_error("Flux support was not compiled into this version of CTI");
#endif

static cti_app_id_t
_cti_flux_registerJob(char const* job_id) {
    return FE_iface::runSafely(__func__, [&](){
        CHECK_FLUX(
        auto&& fe = downcastFE<FluxFrontend>();
        auto wp = fe.registerJob(1, job_id);
        return fe.Iface().trackApp(wp);
        )
    }, APP_ERROR);
}

static cti_flux_ops_t _cti_flux_ops = {
    .registerJob = _cti_flux_registerJob,
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
            case CTI_WLM_ALPS:
                *ops = reinterpret_cast<void *>(&_cti_alps_ops);
                break;
            case CTI_WLM_SLURM:
                *ops = reinterpret_cast<void *>(&_cti_slurm_ops);
                break;
            case CTI_WLM_PALS:
                *ops = reinterpret_cast<void *>(&_cti_pals_ops);
                break;
            case CTI_WLM_SSH:
                *ops = reinterpret_cast<void *>(&_cti_ssh_ops);
                break;
            case CTI_WLM_FLUX:
                *ops = reinterpret_cast<void *>(&_cti_flux_ops);
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

        // Check if app registered
        if (!fe.Iface().validApp(appId)) {
            return false;
        }

        // Get app instance
        auto sp = fe.Iface().getApp(appId);

        // Check if app running
        if (!sp->isRunning()) {
            // Remove the app if not running anymore
            fe.removeApp(sp);
            fe.Iface().removeApp(appId);

            return false;
        }

        // App is valid and running
        return true;
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

namespace
{
    enum class LaunchBarrierMode
        { Disabled = 0
        , Enabled  = 1
    };
}

static cti_app_id_t
launchAppImplementation(const char * const launcher_argv[], int stdout_fd, int stderr_fd,
    const char *input_file, const char *chdir_path, const char * const env_list[],
    LaunchBarrierMode const launchBarrierMode)
{
    auto&& fe = Frontend::inst();

    // If stdout or stderr FDs are provided, ensure that they are writable
    if ((stdout_fd > 0) && !cti::canWriteFd(stdout_fd)) {
        throw std::runtime_error("Invalid stdoutFd argument. No write access.");
    }
    if ((stderr_fd > 0) && !cti::canWriteFd(stderr_fd)) {
        throw std::runtime_error("Invalid stderr_fd argument. No write access.");
    }

    // If an input file is provided, ensure that it is readable
    if ((input_file != nullptr) && !cti::fileHasPerms(input_file, R_OK)) {
        throw std::runtime_error("Invalid inputFile argument. No read access.");
    }
    // If a working directory is provided, ensure that is a readable / writable / executable directory
    if ((chdir_path != nullptr) && !cti::dirHasPerms(chdir_path, R_OK | W_OK | X_OK)) {
        throw std::runtime_error("Invalid chdirPath argument. No RWX access.");
    }

    // If LD_PRELOAD was set globally, ensure that the global value gets added to the job environment
    auto fixedEnvVars = cti::ManagedArgv{};
    auto const globalLdPreload = fe.getGlobalLdPreload();
    if (!globalLdPreload.empty()) {

        // If LD_PRELOAD is set in the job environment, the global LD_PRELOAD will be prepended
        if (env_list != nullptr) {
            auto ldPreloadAdded = false;
            for (int i=0; env_list[i] != nullptr; ++i) {

                if (strcmp(env_list[i], "LD_PRELOAD") == 0) {
                    // Find separator '='
                    const char * sub_ptr = strrchr(env_list[i], '=');
                    if (sub_ptr == nullptr) {
                        throw std::runtime_error("Invalid environment variable set: '" +
                            std::string{env_list[i]} + "'");
                    }

                    // Advance past '='
                    ++sub_ptr;

                    // Remove beginning / trailing quotation marks

                    // Conditionally advance past beginning quotation mark
                    if (*sub_ptr == '"') {
                        ++sub_ptr;
                    }

                    // Determine if trailing quote is present
                    auto trailingQuotePresent = false;
                    for (char const* cursor = sub_ptr; *cursor != '\0'; cursor++) {
                        if (cursor[0] == '"') {

                            // Ensure that the quote is trailing
                            if (cursor[1] != '\0') {
                                throw std::runtime_error("Invalid environment variable set: '" +
                                    std::string{env_list[i]} + "'");
                            }

                            trailingQuotePresent = true;
                        }
                    }

                    // Prepend global LD_PRELOAD value to job environment LD_PRELOAD
                    fixedEnvVars.add(std::string{"LD_PRELOAD=\""}
                        + globalLdPreload + ":" + sub_ptr
                        + (trailingQuotePresent ? "" : "\""));

                    ldPreloadAdded = true;

                } else {
                    // Environment variable is not LD_PRELOAD, add unchanged to job environment
                    fixedEnvVars.add(env_list[i]);
                }
            }

            // If LD_PRELOAD was not set in the job environment, add the global LD_PRELOAD value
            if (!ldPreloadAdded) {
                fixedEnvVars.add(std::string{"LD_PRELOAD=\""} + globalLdPreload + "\"");
            }

        } else {
            // No job-specific environment provided
            fixedEnvVars.add(std::string{"LD_PRELOAD=\""} + globalLdPreload + "\"");
        }

        // Set job environment list to the fixed list containing the global LD_PRELOAD value
        env_list = (const char * const *)fixedEnvVars.get();
    }

    // If using barrier mode, call the barrier launch implementation
    auto wp = (launchBarrierMode == LaunchBarrierMode::Disabled)
        ? fe.launch(launcher_argv, stdout_fd, stderr_fd, input_file, chdir_path, env_list)
        : fe.launchBarrier(launcher_argv, stdout_fd, stderr_fd, input_file, chdir_path, env_list);

    // Assign a CTI application ID to the newly launched application
    return fe.Iface().trackApp(wp);
}

cti_app_id_t
cti_launchApp(const char * const launcher_argv[], int stdout_fd, int stderr_fd,
    const char *input_file, const char *chdir_path, const char * const env_list[])
{
    return FE_iface::runSafely(__func__, [&](){

        // Delegate to common launch implementation
        return launchAppImplementation(launcher_argv, stdout_fd, stderr_fd, input_file, chdir_path, env_list,
            LaunchBarrierMode::Disabled);

    }, APP_ERROR);
}

cti_app_id_t
cti_launchApp_fd(const char * const launcher_argv[], int stdout_fd, int stderr_fd,
    int stdin_fd, const char *chdir_path, const char * const env_list[])
{
    return FE_iface::runSafely(__func__, [&](){

        // Build path to file descriptor, if provided
        auto inputFile = std::string{};
        if (stdin_fd >= 0) {
            inputFile = "/proc/self/fd/" + std::to_string(stdin_fd);
        }

        // Delegate to common launch implementation
        return launchAppImplementation(launcher_argv, stdout_fd, stderr_fd,
            (!inputFile.empty()) ? inputFile.c_str() : nullptr,
            chdir_path, env_list,
            LaunchBarrierMode::Disabled);

    }, APP_ERROR);
}

cti_app_id_t
cti_launchAppBarrier(const char * const launcher_argv[], int stdout_fd, int stderr_fd,
    const char *input_file, const char *chdir_path, const char * const env_list[])
{
    return FE_iface::runSafely(__func__, [&](){

        // Delegate to common launch implementation
        return launchAppImplementation(launcher_argv, stdout_fd, stderr_fd, input_file, chdir_path, env_list,
            LaunchBarrierMode::Enabled);

    }, APP_ERROR);
}

cti_app_id_t
cti_launchAppBarrier_fd(const char * const launcher_argv[], int stdout_fd, int stderr_fd,
    int stdin_fd, const char *chdir_path, const char * const env_list[])
{
    return FE_iface::runSafely(__func__, [&](){

        // Build path to file descriptor, if provided
        auto inputFile = std::string{};
        if (stdin_fd >= 0) {
            inputFile = "/proc/self/fd/" + std::to_string(stdin_fd);
        }

        // Delegate to common launch implementation
        return launchAppImplementation(launcher_argv, stdout_fd, stderr_fd,
            (!inputFile.empty()) ? inputFile.c_str() : nullptr,
            chdir_path, env_list,
            LaunchBarrierMode::Enabled);

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
        if (value == nullptr) {
            throw std::runtime_error("NULL pointer pass as value argument.");
        }
        auto&& fe = Frontend::inst();
        switch (attrib) {
            case CTI_ATTR_STAGE_DEPENDENCIES:
                if (value[0] == '0') {
                    fe.m_stage_deps = false;
                } else if (value[0] == '1') {
                    fe.m_stage_deps = true;
                } else {
                    throw std::runtime_error("CTI_ATTR_STAGE_DEPENDENCIES: Unsupported value " + std::to_string(value[0]));
                }
                break;
            case CTI_LOG_DIR:
                if (!cti::dirHasPerms(value, R_OK | W_OK | X_OK)) {
                    throw std::runtime_error(std::string{"CTI_LOG_DIR: Bad directory specified by value "} + value);
                } else {
                    fe.m_log_dir = std::string{value};
                }
                break;
            case CTI_DEBUG:
                if (value[0] == '0') {
                    fe.m_debug = false;
                } else if (value[0] == '1') {
                    fe.m_debug = true;
                } else {
                    throw std::runtime_error("CTI_DEBUG: Unsupported value " + std::to_string(value[0]));
                }
                break;
            case CTI_PMI_FOPEN_TIMEOUT:
                fe.m_pmi_fopen_timeout = std::stoul(std::string{value});
                break;
            case CTI_EXTRA_SLEEP:
                fe.m_extra_sleep = std::stoul(std::string{value});
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
        switch (attrib) {
            case CTI_ATTR_STAGE_DEPENDENCIES:
                if (fe.m_stage_deps) {
                    return FE_iface::get_attr_str("1");
                }
                return FE_iface::get_attr_str("0");
            case CTI_LOG_DIR:
                return FE_iface::get_attr_str(fe.m_log_dir.c_str());
            case CTI_DEBUG:
                if (fe.m_debug) {
                    return FE_iface::get_attr_str("1");
                }
                return FE_iface::get_attr_str("0");
            case CTI_PMI_FOPEN_TIMEOUT:
            {
                auto str = std::to_string(fe.m_pmi_fopen_timeout);
                return FE_iface::get_attr_str(str.c_str());
            }
            case CTI_EXTRA_SLEEP:
            {
                auto str = std::to_string(fe.m_extra_sleep);
                return FE_iface::get_attr_str(str.c_str());
            }
            default:
                throw std::runtime_error("Invalid cti_attr_type_t " + std::to_string((int)attrib));
        }
        // Shouldn't get here
        return (const char *)nullptr;
    }, (const char*)nullptr);
}
