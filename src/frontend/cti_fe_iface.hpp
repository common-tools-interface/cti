/*********************************************************************************\
 * cti_fe_iface.hpp - External C interface for the cti frontend.
 *
 * Copyright 2014-2019 Cray Inc.    All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
 *
 *********************************************************************************/

#pragma once

#include "cti_defs.h"

/*
** The following represents the c interface. These need to be exported as C routines.
*/
extern "C" {

/* API function prototypes */

// current frontend information query
const char *        cti_version(void);
const char *        cti_error_str(void);
int                 cti_error_str_r(char *buf, size_t buf_size);
cti_wlm_type        cti_current_wlm(void);
const char *        cti_wlm_type_toString(cti_wlm_type);
char *              cti_getHostname(void);
int                 cti_setAttribute(cti_attr_type, const char *);

// running app information query
char *              cti_getLauncherHostName(cti_app_id_t);
int                 cti_getNumAppPEs(cti_app_id_t);
int                 cti_getNumAppNodes(cti_app_id_t);
char **             cti_getAppHostsList(cti_app_id_t);
cti_hostsList_t *   cti_getAppHostsPlacement(cti_app_id_t);
void                cti_destroyHostsList(cti_hostsList_t *);

// app lifecycle management
int                 cti_appIsValid(cti_app_id_t);
void                cti_deregisterApp(cti_app_id_t);
cti_app_id_t        cti_launchApp(const char * const [], int, int, const char *, const char *, const char * const []);
cti_app_id_t        cti_launchAppBarrier(const char * const [], int, int, const char *, const char *, const char * const []);
int                 cti_releaseAppBarrier(cti_app_id_t);
int                 cti_killApp(cti_app_id_t, int);

// transfer session management
cti_session_id_t    cti_createSession(cti_app_id_t appId);
int                 cti_sessionIsValid(cti_session_id_t sid);
int                 cti_destroySession(cti_session_id_t sid);

// transfer session directory listings
char **             cti_getSessionLockFiles(cti_session_id_t sid);
char *              cti_getSessionRootDir(cti_session_id_t sid);
char *              cti_getSessionBinDir(cti_session_id_t sid);
char *              cti_getSessionLibDir(cti_session_id_t sid);
char *              cti_getSessionFileDir(cti_session_id_t sid);
char *              cti_getSessionTmpDir(cti_session_id_t sid);

// transfer manifest management
cti_manifest_id_t   cti_createManifest(cti_session_id_t sid);
int                 cti_manifestIsValid(cti_manifest_id_t mid);
int                 cti_addManifestBinary(cti_manifest_id_t mid, const char * rawName);
int                 cti_addManifestLibrary(cti_manifest_id_t mid, const char * rawName);
int                 cti_addManifestLibDir(cti_manifest_id_t mid, const char * rawName);
int                 cti_addManifestFile(cti_manifest_id_t mid, const char * rawName);
int                 cti_sendManifest(cti_manifest_id_t mid);

// tool daemon management
int                 cti_execToolDaemon(cti_manifest_id_t mid, const char *daemonPath, const char * const daemonArgs[], const char * const envVars[]);

/* WLM-specific functions */

// Cray-SLURM
cti_srunProc_t *    cti_cray_slurm_getJobInfo(pid_t srunPid);
cti_app_id_t        cti_cray_slurm_registerJobStep(uint32_t job_id, uint32_t step_id);
cti_srunProc_t *    cti_cray_slurm_getSrunInfo(cti_app_id_t appId);

// SSH
cti_app_id_t        cti_ssh_registerJob(pid_t launcher_pid);

} /* extern "C" */

#include <memory>
#include <string>
#include <unordered_map>

// Forward declarations
class App;
class Session;
class Manifest;

class FE_iface final {
private:
    // store and associate an arbitrary C++ object with an id (to make it accessible to C clients)
    template <typename IdType, typename T>
    class Registry {
    private: // variables
        std::unordered_map<IdType, std::weak_ptr<T>> m_list;
        IdType m_id = IdType{};

    public: // interface
        // Remove an id from the list
        void erase(IdType const id) { m_list.erase(id); }
        // Test if an id is still valid
        bool isValid(IdType const id)
        {
            // Ensure the id is valid
            auto itr = m_list.find(id);
            if (itr == m_list.end())  return false;
            // Check if the pointer is valid
            if (auto sp = itr->second.lock()) { return true; }
            // Pointer is no longer valid
            // Cleanup the id
            erase(id);
            return false;
        }
        // Get a handle to the object.
        std::shared_ptr<T> get_handle(IdType const id)
        {
            auto wp = m_list.at(id);
            if (auto sp = wp.lock()) { return sp; }
            // Cleanup the id
            erase(id);
            throw std::runtime_error("Provided id is no longer valid.");
        }
        // Add a weak_ptr to the registry
        IdType add(std::weak_ptr<T> wp)
        {
            auto const newId = ++m_id;
            m_list.insert(std::make_pair(newId, wp));
            return newId;
        }
    };

private: // Static internal data
    // Error string we export to callers - we want this to leak!
    static char *       _cti_err_str;
    static std::string  m_err_str;

private: // Internal data
    // Internal associations between iterface ids and internal objects
    Registry<cti_app_id_t,App> m_app_registry;
    Registry<cti_session_id_t,Session> m_session_registry;
    Registry<cti_manifest_id_t,Manifest> m_manifest_registry;

private:
    // Used to set the external facing error string
    static void set_error_str(std::string str);

public:
    // Used to obtain a pointer to the internal error string.
    // This is for external consumption.
    static const char *get_error_str();

    // Return codes
    static constexpr auto SUCCESS = int{0};
    static constexpr auto FAILURE = int{1};

    static constexpr auto APP_ERROR      = cti_app_id_t{0};
    static constexpr auto SESSION_ERROR  = cti_session_id_t{0};
    static constexpr auto MANIFEST_ERROR = cti_manifest_id_t{0};

    // Safely run code that can throw and use it to set cti error instead.
    // A C api should never allow an exception to escape the runtime.
    template <typename FuncType, typename ReturnType = decltype(std::declval<FuncType>()())>
    static ReturnType
    runSafely(std::string const& caller, FuncType&& func, ReturnType const onError) {
        try {
            return std::forward<FuncType>(func)();
        } catch (std::exception const& ex) {
            set_error_str(caller + ": " + ex.what());
            return onError;
        }
    }

    // App accessors/mutators
    cti_app_id_t
    trackApp(std::weak_ptr<App> wp) { return m_app_registry.add(wp); }
    std::shared_ptr<App>
    getApp(cti_app_id_t id) { return m_app_registry.get_handle(id); }
    bool
    validApp(cti_app_id_t id) { return m_app_registry.isValid(id); }
    void
    removeApp(cti_app_id_t id) { m_app_registry.erase(id); }
    // Session accessors/mutators
    cti_session_id_t
    trackSession(std::weak_ptr<Session> wp) { return m_session_registry.add(wp); }
    std::shared_ptr<Session>
    getSession(cti_session_id_t id) { return m_session_registry.get_handle(id); }
    bool
    validSession(cti_session_id_t id) { return m_session_registry.isValid(id); }
    void
    removeSession(cti_session_id_t id) { m_session_registry.erase(id); }
    // Manifest accessors/mutators
    cti_manifest_id_t
    trackManifest(std::weak_ptr<Manifest> wp) { return m_manifest_registry.add(wp); }
    std::shared_ptr<Manifest>
    getManifest(cti_manifest_id_t id) { return m_manifest_registry.get_handle(id); }
    bool
    validManifest(cti_manifest_id_t id) { return m_manifest_registry.isValid(id); }
    void
    removeManifest(cti_manifest_id_t id) { m_manifest_registry.erase(id); }

public: // Constructor/destructors
    FE_iface();
    ~FE_iface() = default;
    FE_iface(const FE_iface&) = delete;
    FE_iface& operator=(const FE_iface&) = delete;
    FE_iface(FE_iface&&) = delete;
    FE_iface& operator=(FE_iface&&) = delete;
};
