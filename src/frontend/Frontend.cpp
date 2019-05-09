/*********************************************************************************\
 * Frontend.cpp - define workload manager frontend interface and common base class
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

// This pulls in config.h
#include "cti_defs.h"
#include "cti_argv_defs.hpp"

#include <sys/types.h>
#include <assert.h>
#include <unistd.h>
#include <signal.h>

// CTI Transfer includes
#include "cti_transfer/Manifest.hpp"
#include "cti_transfer/Session.hpp"

// CTI Frontend / App implementations
#include "Frontend.hpp"
#include "Frontend_impl.hpp"

// utility includes
#include "useful/cti_log.h"
#include "useful/cti_wrappers.hpp"

// Static data objects
std::atomic<Frontend*>              Frontend::m_instance{nullptr};
std::mutex                          Frontend::m_mutex;
std::unique_ptr<Frontend_cleanup>   Frontend::m_cleanup{nullptr};

// This ensures the singleton gets deleted
Frontend_cleanup::~Frontend_cleanup() {
    Frontend::destroy();
}

// Logger object that must be created after frontend instantiation
cti::Logger&
Frontend::getLogger(void)
{
    static auto _cti_logger = cti::Logger{Frontend::inst().getHostname().c_str(), getpid()};
    return _cti_logger;
}

std::string
Frontend::findCfgDir(struct passwd& pwd)
{
    // Get the pw info, this is used in the unique name part of cfg directories
    // and when doing the final ownership check
    std::string username;
    decltype(passwd::pw_uid) uid;
    if (struct passwd *pw = getpwuid(getuid())) {
        username = std::string(pw->pw_name);
        uid = pw->pw_uid;
    }
    else {
        throw std::runtime_error(std::string("getpwuid() ") + strerror(errno));
    }

    // FIXME: How to ensure sane pwd?
    assert(pwd.pw_name != nullptr);

    // get the cfg dir settings
    std::string customCfgDir, cfgDir;
    if (const char* cfg_dir_env = getenv(CFG_DIR_VAR)) {
        customCfgDir = std::string(cfg_dir_env);
    }
    else {
        // look in this order: $TMPDIR, /tmp, $HOME
        std::vector<const char*> defaultDirs {getenv("TMPDIR"), "/tmp", getenv("HOME")};
        for (const char* dir_var : defaultDirs) {
            if ((dir_var != nullptr) && cti::dirHasPerms(dir_var, R_OK | W_OK | X_OK)) {
                cfgDir = std::string(dir_var);
                break;
            }
        }
    }

    // Create the directory name string - we default this to have the name cray_cti-<username>
    std::string cfgPath;
    if (!customCfgDir.empty()) {
        cfgPath = customCfgDir + "/cray_cti-" + username;
    }
    else if (!cfgDir.empty()) {
        cfgPath = cfgDir + "/cray_cti-" + username;
    }
    else {
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
                throw std::runtime_error(std::string("mkdir() ") + strerror(errno));
            }
        }
        else {
            // directory already exists, so chmod it if has bad permissions.
            // We created this directory previously.
            if ((st.st_mode & (S_ISUID | S_ISGID | S_IRWXU | S_IRWXG | S_IRWXO)) & ~S_IRWXU) {
                if (chmod(cfgPath.c_str(), S_IRWXU)) {
                    throw std::runtime_error(std::string("chmod() ") + strerror(errno));
                }
            }
        }
    }
    else {
        // The user set CFG_DIR_VAR, we *ALWAYS* want to use that
        // custom cfgdir behavior: error if not exist or bad perms
        // Check to see if we can write to this directory
        if (!cti::dirHasPerms(cfgPath.c_str(), R_OK | W_OK | X_OK)) {
            throw std::runtime_error(std::string("Bad directory specified by environment variable ") + CFG_DIR_VAR);
        }
        // verify that it has the permissions we expect
        struct stat st;
        if (stat(cfgPath.c_str(), &st)) {
            // could not stat the directory
            throw std::runtime_error(std::string("stat() ") + strerror(errno));
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
    }
    else {
        throw std::runtime_error(std::string("realpath() ") + strerror(errno));
    }

    // Ensure we have ownership of this directory, otherwise it is untrusted
    struct stat st;
    memset(&st, 0, sizeof(st));
    if (stat(cfgPath.c_str(), &st)) {
        throw std::runtime_error(std::string("stat() ") + strerror(errno));
    }
    if (st.st_uid != uid) {
        throw std::runtime_error(std::string("Directory already exists: ") + cfgPath);
    }

    return cfgPath;
}

std::string
Frontend::findBaseDir(void)
{
    // Check if env var is defined
    // TODO: Rethink this. It is probably dangerous...
    const char * base_dir_env = getenv(BASE_DIR_ENV_VAR);
    if (base_dir_env != nullptr) {
        // Honor the env var setting
        return std::string{base_dir_env};
    }
    // Check default install locations
    if (!cti::dirHasPerms(base_dir_env, R_OK | X_OK)) {
        for (const char* const* pathPtr = cti::default_dir_locs; *pathPtr != nullptr; pathPtr++) {
            if (cti::dirHasPerms(*pathPtr, R_OK | X_OK)) {
                return std::string{*pathPtr};
            }
        }
    }

    throw std::runtime_error(std::string{"failed to find a CTI installation. Ensure "} + BASE_DIR_ENV_VAR + " is set properly.");
}

// BUG 819725:
// create the hidden name for the cleanup file. This will be checked by future
// runs to try assisting in cleanup if we get killed unexpectedly. This is cludge
// in an attempt to cleanup. The ideal situation is to be able to tell the kernel
// to remove a tarball if the process exits, but no mechanism exists today that
// I know about that allows us to share the file with other processes later on.
void
Frontend::addFileCleanup(std::string file)
{
    const std::string cleanupFilePath(m_cfg_dir + "/." + file);
    auto cleanupFileHandle = cti::file::open(cleanupFilePath, "w");
    pid_t pid = getpid();
    cti::file::writeT(cleanupFileHandle.get(), pid);
}

// BUG 819725:
// Attempt to cleanup old files in the cfg dir
void
Frontend::doFileCleanup()
{
    // Create stage name
    std::string stage_name{"." + std::string{DEFAULT_STAGE_DIR}};
    // get the position of the first 'X'
    auto found = stage_name.find('X');
    if (found != std::string::npos) {
        // Cut the "X" portion out
        stage_name.erase(found);
    }
    // Open cfg dir
    auto cfgDir = cti::dir::open(m_cfg_dir);
    // Recurse through each file in the directory
    struct dirent *d;
    while ((d = readdir(cfgDir.get())) != nullptr) {
        std::string name{d->d_name};
        // Skip the . and .. files
        if ( name.size() == 1 && name.compare(".") == 0 ) {
            continue;
        }
        if ( name.size() == 2 && name.compare("..") == 0 ) {
            continue;
        }
        // Check this name against the stage_name
        if ( name.compare(stage_name) == 0 ) {
            // pattern matches, check to see if we need to remove
            std::string file{m_cfg_dir + "/" + d->d_name};
            auto fileHandle = cti::file::open(file, "r");
            // read the pid from the file
            pid_t pid = cti::file::readT<pid_t>(fileHandle.get());
            // ping the process
            if (kill(pid,0) == 0) {
                // process is still alive
                continue;
            }
            // process is dead we need to remove the tarball
            std::string tarball_name{m_cfg_dir + "/" + (d->d_name+1)};
            // unlink the files
            unlink(tarball_name.c_str());
            unlink(file.c_str());
        }
    }
}

/*
* This routine automatically determines the active Workload Manager. The
* user can force SSH as the "WLM" by setting the environment variable CTI_LAUNCHER_NAME.
*/
cti_wlm_type
Frontend::detect_Frontend()
{
    // We do not want to call init if we are running on the backend inside of
    // a tool daemon! It is possible for BE libraries to link against both the
    // CTI fe and be libs (e.g. MRNet) and we do not want to call the FE init
    // in that case.
    if (isRunningOnBackend()) {
        throw std::runtime_error("Unable to instantiate Frontend from compute node!");
    }
    std::string wlmName;
    // Use the workload manager in the environment variable if it is set
    if (const char* wlm_name_env = getenv(CTI_WLM)) {
        wlmName = std::string(wlm_name_env);
        // parse the env string
        if (!wlmName.compare("SLURM") || !wlmName.compare("slurm")) {
            return CTI_WLM_CRAY_SLURM;
        }
        else if (!wlmName.compare("generic")) {
            return CTI_WLM_SSH;
        }
        else {
            throw std::runtime_error("Invalid workload manager argument " + wlmName + " provided in " + CTI_WLM);
        }
    }
    else {
        // Check if this is a cluster system
        // FIXME: This is a hack.
        struct stat sb;
        if (stat(CLUSTER_FILE_TEST, &sb) == 0) {
            return CTI_WLM_SSH;
        }
        // Query for the slurm package using rpm
        // FIXME: This is a hack. This should be addressed by PE-25088
        auto rpmArgv = cti::ManagedArgv { "rpm", "-q", "slurm" };
        cti::Execvp rpmOutput("rpm", rpmArgv.get());
        auto res = rpmOutput.getExitStatus();
        if (res == 0) {
            // The slurm package is installed. This is a naive check.
            return CTI_WLM_CRAY_SLURM;
        }
    }
    // Unknown WLM
    throw std::runtime_error("Unable to determine wlm in use. Manually set " + std::string{CTI_WLM} + " env var.");
}

Frontend&
Frontend::inst() {
    // Check if the singleton has been initialized
    auto inst = m_instance.load(std::memory_order_acquire);
    if (!inst) {
        // grab the lock
        std::lock_guard<std::mutex> lock{m_mutex};
        // Double check the condition
        inst = m_instance.load(std::memory_order_relaxed);
        if (!inst) {
            // We were the first one here
            // Setup the cleanup object
            m_cleanup = std::make_unique<Frontend_cleanup>();
            // Determine which wlm to instantiate
            switch(detect_Frontend()) {
                case CTI_WLM_CRAY_SLURM:
                    inst = new CraySLURMFrontend{};
                    break;
                case CTI_WLM_SSH:
                    inst = new GenericSSHFrontend{};
                    break;
                case CTI_WLM_NONE:
                case CTI_WLM_MOCK:
                    throw std::runtime_error("Unable to determine wlm in use. Manually set " + std::string{CTI_WLM} + " env var.");
            }
            // Only store after fully constructing FE object. Otherwise we could run into
            // partial construction ordering issues.
            m_instance.store(inst,std::memory_order_release);
        }
    }
    return *inst;
}

void
Frontend::destroy() {
    // Use sequential consistency here
    delete m_instance.exchange(nullptr);
}

Frontend::Frontend()
: m_stage_deps{true}
{
    // Setup the password file entry. Other utilites need to use this
    size_t buf_len = 4096;
    long rl = sysconf(_SC_GETPW_R_SIZE_MAX);
    if (rl != -1) buf_len = static_cast<size_t>(rl);
    // resize the vector
    m_pwd_buf.resize(buf_len);
    // Get the password file
    struct passwd *result = nullptr;
    if (getpwuid_r( geteuid(),
                    &m_pwd,
                    m_pwd_buf.data(),
                    m_pwd_buf.size(),
                    &result)) {
        throw std::runtime_error("getpwuid_r failed: " + std::string{strerror(errno)});
    }
    // Ensure we obtained a result
    if (result == nullptr) {
        throw std::runtime_error("password file entry not found for euid " + std::to_string(geteuid()));
    }
    // Setup the directories. We break these out into private static methods
    // to avoid pollution in the constructor.
    m_cfg_dir = findCfgDir(m_pwd);
    m_base_dir = findBaseDir();
    // Following strings depend on m_base_dir
    m_ld_audit_path = cti::accessiblePath(m_base_dir + "/lib/" + LD_AUDIT_LIB_NAME);
    m_fe_daemon_path = cti::accessiblePath(m_base_dir + "/libexec/" + CTI_FE_DAEMON_BINARY);
    m_be_daemon_path = cti::accessiblePath(m_base_dir + "/libexec/" + CTI_BE_DAEMON_BINARY);
    // init the frontend daemon now that we have the path to the binary
    m_daemon.initialize(m_fe_daemon_path);
    // Try to conduct cleanup of the cfg dir to prevent forest fires
    doFileCleanup();
}

std::weak_ptr<Session>
App::createSession() {
    auto ret = m_sessions.emplace(std::make_shared<Session>(*this));
    if (!ret.second) {
        throw std::runtime_error("Failed to create new Session object.");
    }
    return *ret.first;
}

void
App::removeSession(std::shared_ptr<Session>& sess) {
    // drop the shared_ptr
    m_sessions.erase(sess);
}
