/*********************************************************************************\
 * Frontend.hpp - define workload manager frontend interface and common base class
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

#include <cstdarg>
#include <unistd.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

#include <pwd.h>

#include "cti_fe_iface.hpp"
#include "daemon/cti_fe_daemon_iface.hpp"

#include "useful/cti_log.h"
#include "useful/cti_useful.h"

struct CTIHost {
    std::string hostname;
    size_t      numPEs;
};

using CStr      = const char*;
using CArgArray = const char* const[];

// pseudorandom character generator for unique filenames / directories
class FE_prng {
    char m_r_state[256];

public:
    FE_prng();

    char genChar();
};

/* CTI Frontend object interfaces */

// This is used to ensure the static global pointers get cleaned up upon exit
class Frontend_cleanup final {
public:
    Frontend_cleanup() = default;
    ~Frontend_cleanup();
};

// Forward declarations
class App;

/*
**
** The Frontend object is defined below. It defines the generic WLM interface that
** all implementations must implement. It is an abstract base class, so we are
** never able to instantiate a Frontend object without the specialization that
** implements the generic WLM interface. Anything that is frontend related, but
** not WLM specific should be implemented directly in the base Frontend.
*/
class Frontend {
public: // impl.-specific interface that derived type must implement


    // Frontend implementations must implement the following static functions:

    // static char const* getName()
    //   return the short name for the WLM that can be set in the CRAY_CTI_WLM environment variable

    // static char const* getDescription()
    //   return the long description for the WLM used on call to cti_wlm_type_toString

    // static bool isSupported()
    //   determines in an implementation-specific manner if the Frontend is supported
    //   on the current system.


    // Frontend implementations must implement the following virtual functions:

    // wlm type
    virtual cti_wlm_type_t
    getWLMType() const = 0;

    // launch application with barrier
    virtual std::weak_ptr<App>
    launchBarrier(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
                  CStr inputFile, CStr chdirPath, CArgArray env_list) = 0;

    // create an application instance from an already-running job (the number of IDs used to
    // represent a job is implementation-defined)
    virtual std::weak_ptr<App>
    registerJob(size_t numIds, ...) = 0;

    // get hostname of current node
    virtual std::string
    getHostname(void) const = 0;

protected: // Protected and Private static data members that are accessed only via base frontend
    // This is protected for mock testing purposes only - it should be treated as private
    static std::atomic<Frontend*>               m_instance;
private:
    static std::mutex                           m_mutex;
    static std::unique_ptr<Frontend_cleanup>    m_cleanup;

private: // Private data members usable only by the base Frontend
    FE_iface            m_iface;
    FE_daemon           m_daemon;
    FE_prng             m_prng;
    // Directory paths
    std::string         m_cfg_dir;
    std::string         m_base_dir;
    std::string         m_ld_audit_path;
    std::string         m_fe_daemon_path;
    std::string         m_be_daemon_path;
    // Cleanup files
    std::vector<std::string>    m_cleanup_files;

protected: // Protected data members that belong to any frontend
    struct passwd       m_pwd;
    std::vector<char>   m_pwd_buf;
    // Frontends have direct ownership of all App objects
    std::unordered_set<std::shared_ptr<App>>    m_apps;

public: // Values set by cti_setAttribute
    bool                m_stage_deps;
    std::string         m_log_dir;
    bool                m_debug;
    unsigned long       m_pmi_fopen_timeout;
    unsigned long       m_extra_sleep;

private: // Private static utility methods used by the generic frontend
    // get the logger associated with the frontend - can only construct logger
    // after fe instantiation!
    static cti::Logger& getLogger();
    // get the frontend type for this system
    static cti_wlm_type_t detect_Frontend();

public: // Public static utility methods - Try to keep these to a minimum
    // Get the singleton instance to the Frontend
    static Frontend& inst();
    // Used to destroy the singleton
    static void destroy();

private: // Private utility methods used by the generic frontend
    static bool isRunningOnBackend() { return (getenv(BE_GUARD_ENV_VAR) != nullptr); }
    // use user info to build unique staging path; optionally create the staging direcotry
    std::string findCfgDir();
    // find the base CTI directory from the environment and verify its permissions
    std::string findBaseDir();
    // Try to cleanup old files left in the cfg dir during the ctor.
    void doFileCleanup();

public: // Public interface to generic WLM-agnostic capabilities
    // Write to the log file associated with the Frontend
    template <typename... Args>
    void writeLog(char const* fmt, Args&&... args)
    {
        getLogger().write(fmt, std::forward<Args>(args)...);
    }
    // Remove an app object
    void removeApp(std::shared_ptr<App> app)
    {
        // drop the shared_ptr
        m_apps.erase(app);
    }
    // Interface accessor - guarantees access via singleton object
    FE_iface& Iface() { return m_iface; }
    // Daemon accessor - guarantees access via singleton object
    FE_daemon& Daemon() { return m_daemon; }
    // PRNG accessor
    FE_prng& Prng() { return m_prng; }
    // Register a cleanup file
    void addFileCleanup(std::string const& file);
    // Get a list of default env vars to forward to BE daemon
    std::vector<std::string> getDefaultEnvVars();
    // Accessors
    std::string getCfgDir() { return m_cfg_dir; }
    std::string getBaseDir() { return m_base_dir; }
    std::string getLdAuditPath() { return m_ld_audit_path; }
    std::string getBEDaemonPath() { return m_be_daemon_path; }
    const struct passwd& getPwd() { return m_pwd; }

    // tell all Apps to finalize their transfer Sessions
    void finalize();

protected: // Constructor/destructors
    Frontend();
public:
    virtual ~Frontend();
    Frontend(const Frontend&) = delete;
    Frontend& operator=(const Frontend&) = delete;
    Frontend(Frontend&&) = delete;
    Frontend& operator=(Frontend&&) = delete;
};

// Forward declarations
class Session;

// This is the app instance interface that all wlms should implement.
// We only create weak_ptr to the base, not the derived.
// XXX: This takes a reference to the fe object. Once we move to C++20 we can
// use std:atomic on shared_ptr and weak_ptr, so rethink the design then.
class App : public std::enable_shared_from_this<App> {
public: // impl.-specific interface that derived type must implement
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

protected: // Protected data members that belong to any App
    // Reference to Frontend associated with App
    Frontend& m_frontend;
private:
    // Apps have direct ownership of all Session objects underneath it
    std::unordered_set<std::shared_ptr<Session>> m_sessions;

public:
    // App specific logger
    template <typename... Args>
    void writeLog(char const* fmt, Args&&... args) const {
        m_frontend.writeLog((getJobId() + ":" + fmt).c_str(), std::forward<Args>(args)...);
    }

public: // Public interface to generic WLM-agnostic capabilities
    // Create a new session associated with this app
    std::weak_ptr<Session> createSession();
    // Remove a session object
    void removeSession(std::shared_ptr<Session> sess);
    // Frontend acessor
    // TODO: When we switch to std::atomic on shared_ptr with C++20,
    // this can return a shared_ptr handle instead.
    Frontend& getFrontend() { return m_frontend; }

    // tell all Sessions to initialize cleanup
    void finalize();

public: // Constructor/destructors
    App(Frontend& fe)
    : m_frontend{fe}, m_sessions{}
    { }
    virtual ~App() = default;
    App(const App&) = delete;
    App& operator=(const App&) = delete;
    App(App&&) = delete;
    App& operator=(App&&) = delete;
};
