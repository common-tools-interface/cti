/*********************************************************************************\
 * Frontend.cpp - define workload manager frontend interface and common base class
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
#include "cti_argv_defs.hpp"

#include <sys/types.h>
#include <assert.h>
#include <unistd.h>
#include <signal.h>

// CTI Transfer includes
#include "transfer/Manifest.hpp"
#include "transfer/Session.hpp"

// CTI Frontend / App implementations
#include "Frontend.hpp"
#include "Frontend_impl.hpp"

// utility includes
#include "useful/cti_log.h"
#include "useful/cti_wrappers.hpp"
#include "useful/cti_split.hpp"
#include "useful/cti_dlopen.hpp"
#include "checksum/checksums.h"

// Static data objects
std::atomic<Frontend*>              Frontend::m_instance{nullptr};
std::mutex                          Frontend::m_mutex;
std::unique_ptr<cti::Logger>        Frontend::m_logger;  // must be destroyed after m_cleanup
std::unique_ptr<Frontend_cleanup>   Frontend::m_cleanup{nullptr};

// This ensures the singleton gets deleted
Frontend_cleanup::~Frontend_cleanup() {
    Frontend::destroy();
}

// PRNG initialization
FE_prng::FE_prng()
{
    // We need to generate a good seed to avoid collisions. Since this
    // library can be used by automated tests, it is vital to have a
    // good seed.
    struct timespec     tv;
    unsigned int        pval;
    unsigned int        seed;

    // get the current time from epoch with nanoseconds
    if (clock_gettime(CLOCK_REALTIME, &tv)) {
        throw std::runtime_error("clock_gettime failed.");
    }

    // generate an appropriate value from the pid, we shift this to
    // the upper 16 bits of the int. This should avoid problems with
    // collisions due to slight variations in nano time and adding in
    // pid offsets.
    pval = (unsigned int)getpid() << ((sizeof(unsigned int) * CHAR_BIT) - 16);

    // Generate the seed. This is not crypto safe, but should have enough
    // entropy to avoid the case where two procs are started at the same
    // time that use this interface.
    seed = (tv.tv_sec ^ tv.tv_nsec) + pval;

    // init the state
    memset((char*)&m_r_data, 0, sizeof(m_r_data));
    memset(m_r_state, 0, sizeof(m_r_state));
    initstate_r(seed, (char *)m_r_state, sizeof(m_r_state), &m_r_data);

    // set the PRNG state
    if (setstate_r((char *)m_r_state, &m_r_data)) {
        throw std::runtime_error("setstate failed.");
    }
}

// PRNG character generation
char FE_prng::genChar()
{
    // valid chars array used in seed generation
    static constexpr char _cti_valid_char[] {
        '0','1','2','3','4','5','6','7','8','9',
        'A','B','C','D','E','F','G','H','I','J','K','L','M',
        'N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
        'a','b','c','d','e','f','g','h','i','j','k','l','m',
        'n','o','p','q','r','s','t','u','v','w','x','y','z'
    };

    // Generate a random offset into the array. This is random() modded
    // with the number of elements in the array.
    int32_t oset;
    random_r(&m_r_data, &oset);
    oset %= (sizeof(_cti_valid_char)/sizeof(_cti_valid_char[0]));
    // assign this char
    return _cti_valid_char[oset];
}

// initialized helper for getLogger.
Frontend::LoggerInit::LoggerInit() {
    Frontend::m_logger.reset(new cti::Logger{Frontend::inst().m_debug, Frontend::inst().m_log_dir, cti::cstr::gethostname(), getpid()});
}

cti::Logger& Frontend::LoggerInit::get() { return *Frontend::m_logger; }

// Logger object that must be created after frontend instantiation, but also must be destroyed after
// frontend instantiation, hence the extra LoggerInit logic. Do not call inside Frontend
// instantiation, as it depends on Frontend::inst() state and will deadlock.
cti::Logger&
Frontend::getLogger(void)
{
    static auto _cti_init = LoggerInit{};
    return _cti_init.get();
}

std::string
Frontend::findCfgDir()
{
    // Get the pw info, this is used in the unique name part of cfg directories
    // and when doing the final ownership check
    std::string username;
    decltype(passwd::pw_uid) uid;

    // FIXME: How to ensure sane pwd?
    assert(m_pwd.pw_name != nullptr);

    username = std::string(m_pwd.pw_name);
    uid = m_pwd.pw_uid;

    // get the cfg dir settings
    std::string customCfgDir, cfgDir;
    if (const char* cfg_dir_env = getenv(CTI_CFG_DIR_ENV_VAR)) {
        customCfgDir = std::string(cfg_dir_env);
    }
    else {
        // look in this order: $TMPDIR, /tmp, $HOME
        for (auto&& dir_var : { const_cast<const char *>(getenv("TMPDIR")), "/tmp", const_cast<const char *>(getenv("HOME")) }) {
            if ((dir_var != nullptr) && cti::dirHasPerms(dir_var, R_OK | W_OK | X_OK)) {
                cfgDir = std::string(dir_var);
                break;
            }
        }
    }

    // Create the directory name string - we default this to have the name cti-<username>
    std::string cfgPath;
    if (!customCfgDir.empty()) {
        cfgPath = customCfgDir + "/cti-" + username;
    }
    else if (!cfgDir.empty()) {
        cfgPath = cfgDir + "/cti-" + username;
    }
    else {
        // We have no where to create a temporary directory...
        throw std::runtime_error(std::string("Cannot find suitable config directory. Try setting the env variable ") + CTI_CFG_DIR_ENV_VAR);
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
        // The user set CTI_CFG_DIR_ENV_VAR, we *ALWAYS* want to use that
        // custom cfgdir behavior: error if not exist or bad perms
        // Check to see if we can write to this directory
        if (!cti::dirHasPerms(cfgPath.c_str(), R_OK | W_OK | X_OK)) {
            throw std::runtime_error(std::string("Bad directory specified by environment variable ") + CTI_CFG_DIR_ENV_VAR);
        }
        // verify that it has the permissions we expect
        struct stat st;
        if (stat(cfgPath.c_str(), &st)) {
            // could not stat the directory
            throw std::runtime_error(std::string("stat() ") + strerror(errno));
        }
        if ((st.st_mode & (S_ISUID | S_ISGID | S_IRWXU | S_IRWXG | S_IRWXO)) & ~S_IRWXU) {
            // bits other than S_IRWXU are set
            throw std::runtime_error(std::string("Bad permissions (Only 0700 allowed) for directory specified by environment variable ") + CTI_CFG_DIR_ENV_VAR);
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
    auto findUnverifiedBaseDir = []() {
        const char * base_dir_env = getenv(CTI_BASE_DIR_ENV_VAR);
        // Check default install locations
        if (!cti::dirHasPerms(base_dir_env, R_OK | X_OK)) {
            for (const char* const* pathPtr = cti::default_dir_locs; *pathPtr != nullptr; pathPtr++) {
                if (cti::dirHasPerms(*pathPtr, R_OK | X_OK)) {
                    return std::string{*pathPtr};
                }
            }
        }
        else {
            // Honor the env var setting
            return std::string{base_dir_env};
        }

        throw std::runtime_error(std::string{"failed to find a CTI installation. Ensure "} + CTI_BASE_DIR_ENV_VAR + " is set properly.");
    };

    auto verifyBaseDir = [](std::string const& unverifiedBaseDir) {

        // Hash the file at the given path
        auto checkHash = [](std::string const& path, std::string const& hash) {
            if (!has_same_hash(path.c_str(), hash.c_str())) {
                throw std::runtime_error("hash mismatch: " + path);
            }
        };

        // Checksum important binaries in the detected directory
        checkHash(unverifiedBaseDir + "/libexec/" + CTI_BE_DAEMON_BINARY, CTI_BE_DAEMON_CHECKSUM);
    };

    // Find and verify base dir
    auto const baseDir = findUnverifiedBaseDir();
    verifyBaseDir(baseDir);

    return baseDir;
}

// BUG 819725:
// create the hidden name for the cleanup file. This will be checked by future
// runs to try assisting in cleanup if we get killed unexpectedly. This is cludge
// in an attempt to cleanup. The ideal situation is to be able to tell the kernel
// to remove a tarball if the process exits, but no mechanism exists today that
// I know about that allows us to share the file with other processes later on.
void
Frontend::addFileCleanup(std::string const& file)
{
    // track file itself
    m_cleanup_files.push_back(file);

    // track cleanup file that stores this app's PID
    std::string const cleanupFilePath{m_cfg_dir + "/." + file};
    m_cleanup_files.push_back(std::move(cleanupFilePath));
    auto cleanupFileHandle = cti::file::open(cleanupFilePath, "w");
    pid_t pid = getpid();
    cti::file::writeT<pid_t>(cleanupFileHandle.get(), pid);
}

void
Frontend::finalize()
{
    // tell all Apps to finalize transfer sessions
    for (auto&& app : m_apps) {
        app->finalize();
    }
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
        if ( name.compare(0, stage_name.size(), stage_name) == 0 ) {
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

namespace
{

enum class System : int
    { Unknown
    , Linux
    , HPCM
    , Shasta
    , XC
    , CS
};

static std::string System_to_string(System const& system)
{
    switch (system) {
        case System::Unknown: return "";
        case System::Linux:   return "Generic Linux";
        case System::HPCM:    return "HPCM";
        case System::Shasta:  return "Cray Shasta";
        case System::XC:      return "Cray XC";
        case System::CS:      return "Cray CS";
        default: assert(false);
    }
}

enum class WLM : int
    { Unknown
    , PALS
    , Slurm
    , ALPS
    , SSH
    , Flux
};

static std::string WLM_to_string(WLM const& wlm)
{
    switch (wlm) {
        case WLM::Unknown: return "Unknown WLM";
        case WLM::PALS:    return "PALS";
        case WLM::Slurm:   return "Slurm";
        case WLM::ALPS:    return "ALPS";
        case WLM::SSH:     return "SSH";
        case WLM::Flux:    return "Flux";
        default: assert(false);
    }
}

static auto format_System_WLM(System const& system, WLM const& wlm)
{
    if (system != System::Unknown) {
        return System_to_string(system) + " / " + WLM_to_string(wlm);
    } else {
        return WLM_to_string(wlm);
    }
}

// Running on an HPCM machine if the `cminfo` cluster info query program is
// installed, and it reports the current node type.
static bool detect_HPCM()
{
    try {
        char const* cminfoArgv[] = { "cminfo", "--name", nullptr };

        // Start cminfo
        auto cminfoOutput = cti::Execvp{"cminfo", (char* const*)cminfoArgv, cti::Execvp::stderr::Ignore};

        // Detect if running on HPCM login or compute node
        auto& cminfoStream = cminfoOutput.stream();
        std::string cmName;
        if (std::getline(cminfoStream, cmName)) {
            auto const hpcm_login_node = cmName == "admin";
            auto const hpcm_compute_node = cmName.substr(0, 7) == "service";
            return hpcm_login_node || hpcm_compute_node;
        } else {
            return false;
        }

        // Check return code
        if (cminfoOutput.getExitStatus() != 0) {
            return false;
        }

        // All HPCM checks passed
        return true;

    } catch(...) {
        // cminfo not installed
        return false;
    }
}

// Check if this is a CS cluster system
static bool detect_CS()
{
    // CS cluster file will be present on all CS systems
    { struct stat sb;
        if (stat(CLUSTER_FILE_TEST, &sb) == 0) {
            return true;
        }
    }

    return false;
}

// HPCM / Shasta
static bool detect_PALS(std::string const& /* unused */)
{
    // Check manual PALS debug mode flag
    if (::getenv(PALS_DEBUG)) {
        return true;
    }

    try {

        // Check that PBS is installed (required for PALS)
        char const* rpm_argv[] =
            { "rpm", "-q"
            , "pbspro-server", "pbspro-client", "pbspro-execution"
            , nullptr
        };

        // PBS is configured if at least one of these packages exists
        // Return code of 3 means query of all 3 packages failed (not installed)
        auto const failed_packages = cti::Execvp::runExitStatus("rpm", (char* const*)rpm_argv);
        if (failed_packages == 3) {
            return false;
        }

    } catch(...) {
        // craycli not installed
        return false;
    }

    return true;
}

static bool detect_Slurm(std::string const& launcherName)
{
    auto const launcher_name = !launcherName.empty() ? launcherName.c_str() : "srun";

    try {

        // Check that the srun version starts with "slurm "
        auto srunArgv = cti::ManagedArgv{launcher_name, "--version"};
        auto srunOutput = cti::Execvp{launcher_name, srunArgv.get(), cti::Execvp::stderr::Ignore};

        // Read output line
        auto versionLine = std::string{};
        if (std::getline(srunOutput.stream(), versionLine)) {
            if (versionLine.substr(0, 6) != "slurm ") {
                return false;
            }
        } else {
            return false;
        }

        // Ensure exited properly
        if (srunOutput.getExitStatus()) {
            return false;
        }

        // All Slurm checks passed
        return true;

    } catch(...) {
        // Slurm not installed
        return false;
    }
}

static bool detect_XC_ALPS(std::string const& launcherName)
{
    auto const launcher_name = !launcherName.empty() ? launcherName.c_str() : "aprun";

    try {

        // Check that aprun version returns expected content
        auto aprunTestArgv = cti::ManagedArgv{launcher_name, "--version"};
        auto aprunOutput = cti::Execvp{launcher_name, aprunTestArgv.get(), cti::Execvp::stderr::Ignore};

        // Read first line, ensure it is in format "aprun (ALPS) <version>"
        auto& aprunStream = aprunOutput.stream();
        auto versionLine = std::string{};
        if (std::getline(aprunStream, versionLine)) {

            // Split line into each word
            auto const [aprun, alps, version] = cti::split::string<3>(versionLine, ' ');
            if ((aprun == "aprun") && (alps == "(ALPS)")) {
                return true;
            }
        }

        // Wait for aprun to complete
        if (aprunOutput.getExitStatus()) {
            return false;
        }

        // All ALPS checks passed
        return true;

    } catch(...) {
        // ALPS not installed
        return false;
    }
}

static bool detect_Flux(std::string const& launcherName)
{
    auto const launcher_name = !launcherName.empty() ? launcherName.c_str() : "flux";

    try {
        // Check that flux version succeeds
        auto fluxArgv = cti::ManagedArgv{launcher_name, "--version"};
        auto fluxOutput = cti::Execvp{launcher_name, fluxArgv.get(), cti::Execvp::stderr::Ignore};

        // Wait for flux to complete
        if (fluxOutput.getExitStatus()) {
            return false;
        }

        // Look for Flux socket information in environment
        if (auto const flux_uri = ::getenv(FLUX_URI)) {
            return true;

        } else {
            return false;
        }
    } catch (...) {
        return false;
    }
}

// Verify that the provided launcher is a binary and contains MPIR symbols
static bool verify_MPIR_symbols(System const& system, WLM const& wlm, std::string const& launcherName)
{
    assert(!launcherName.empty());

    // Check that the launcher is present in PATH
    auto launcherPath = std::string{};
    try {
        launcherPath = cti::findPath(launcherName);
    } catch (...) {
        throw std::runtime_error(launcherName + " was not found in PATH. \
(tried " + format_System_WLM(system, wlm) + ")");
    }

    // Check that the launcher is a binary and not a script
    { auto binaryTestArgv = cti::ManagedArgv{"sh", "-c",
        "file --mime -L " + launcherPath + " | grep -E 'application/x-(executable|sharedlib)'"};
        if (cti::Execvp::runExitStatus("sh", binaryTestArgv.get())) {
            throw std::runtime_error(launcherName + " was found at " + launcherPath + ", but it is not a binary file. \
Tool launch requires direct access to the " + launcherName + " binary. \
Ensure that the " + launcherName + " binary not wrapped by a script \
(tried " + format_System_WLM(system, wlm) + ")");
        }
    }

    // Check that the launcher binary supports MPIR launch
    { auto symbolTestArgv = cti::ManagedArgv{"sh", "-c",
        "nm " + launcherPath + " | grep MPIR_Breakpoint$"};
        if (cti::Execvp::runExitStatus("sh", symbolTestArgv.get())) {
            throw std::runtime_error(launcherName + " was found at " + launcherPath + ", but it does not appear to support MPIR launch \
(function MPIR_Breakpoint was not found). Tool launch is \
coordinated through setting a breakpoint at this function. \
Please contact your system administrator with a bug report \
(tried " + format_System_WLM(system, wlm) + ")");
        }
    }

    // Check that the launcher binary contains MPIR symbols
    { auto symbolTestArgv = cti::ManagedArgv{"sh", "-c",
        "nm " + launcherPath + " | grep MPIR_being_debugged$"};
        if (cti::Execvp::runExitStatus("sh", symbolTestArgv.get())) {
            throw std::runtime_error(launcherName + " was found at " + launcherPath + ", but it does not contain debug symbols. \
Tool launch is coordinated through reading information at these symbols. \
Please contact your system administrator with a bug report \
(tried " + format_System_WLM(system, wlm) + ")");
        }
    }

    return true;
}

static bool verify_PALS_configured(System const& system, WLM const& wlm, std::string const& launcherName)
{
    // Check for MPIR symbols in launcher
    verify_MPIR_symbols(system, wlm, !launcherName.empty() ? launcherName : "mpiexec");

    // Check that the cray-pals software module is loaded
    try {
        auto palstatArgv = cti::ManagedArgv{"palstat", "--version"};
        auto palstatOutput = cti::Execvp{"palstat", palstatArgv.get(), cti::Execvp::stderr::Ignore};

        // Read output line
        auto versionLine = std::string{};

        // Ensure exited properly
        if (!std::getline(palstatOutput.stream(), versionLine) || palstatOutput.getExitStatus()) {
            throw std::runtime_error("`palstat --version` failed");
        }

        // Check version output
        if (versionLine.substr(0, 8) != "palstat ") {
            throw std::runtime_error("`palstat --version` returned " + versionLine);
        }

    } catch (std::exception const& ex) {
        auto const detail = (ex.what())
            ? " (" + std::string{ex.what()} + ")"
            : std::string{};
        throw std::runtime_error("The system was detected as "
            + format_System_WLM(system, wlm) + ", but checking the PALS utilities failed. \
You may need to run `module load cray-pals`" + detail);
    }

    return true;
}

static bool verify_XC_ALPS_configured(System const& system, WLM const& wlm, std::string const& launcherName)
{
    verify_MPIR_symbols(system, wlm, !launcherName.empty() ? launcherName : "aprun");

    return true;
}

static bool verify_Slurm_configured(System const& system, WLM const& wlm, std::string const& launcherName)
{
    verify_MPIR_symbols(system, wlm, !launcherName.empty() ? launcherName : "srun");

    return true;
}

static bool verify_SSH_configured(System const& system, WLM const& wlm, std::string const& launcherName)
{
    verify_MPIR_symbols(system, wlm, !launcherName.empty() ? launcherName : "mpiexec");

    // Passwordless SSH must also be configured, but there is no way to verify this
    // before extracting MPIR information and attempting to launch a command on a
    // compute node associated with the job.

    // If it is not configured correctly, then an error will be reported upon
    // attempting to launch or attach to a job on the node. This is the earliest
    // that a misconfiguration can be reported.

    return true;
}

static bool verify_Flux_configured(System const& system, WLM const& wlm, std::string const& launcherName)
{
#if HAVE_FLUX

    auto const launcher_name = !launcherName.empty() ? launcherName : "flux";

    // Look for Flux socket information in environment
    if (auto const flux_uri = ::getenv(FLUX_URI)) {

        auto fluxSocketPath = std::string{flux_uri};

        // Ensure socket is readable if local
        { auto const sep = fluxSocketPath.find("://");

            if (sep == std::string::npos) {
                throw std::runtime_error("Could not parse Flux API socket information. \
FLUX_URI contained '" + fluxSocketPath + "', expected format 'protocol://socket_path' \
(tried " + format_System_WLM(system, wlm) + ")");
            }

            auto const protocol = fluxSocketPath.substr(0, sep);
            fluxSocketPath = fluxSocketPath.substr(sep + 3);

            if (protocol == "local") {

                // Ensure socket exists and is readable
                if (!cti::socketHasPerms(fluxSocketPath.c_str(), R_OK | W_OK)) {
                    throw std::runtime_error("The Flux API socket at " + fluxSocketPath + " is \
inaccessible, or lacks permissions for reading and writing by the current user \
(tried " + format_System_WLM(system, wlm) + ")");
                }
            }
        }

    } else {
        throw std::runtime_error("No Flux API socket information was found in the environment \
(FLUX_URI was empty). Ensure that a Flux session has been started, and that tool launch was \
initiated inside the Flux session. \
(tried " + format_System_WLM(system, wlm) + ")");
    }

    // Find path to libflux
    auto const libFluxPath = FluxFrontend::findLibFluxPath(launcher_name);

    // Verify libflux is accessible
    if (!cti::fileHasPerms(libFluxPath.c_str(), R_OK)) {
        throw std::runtime_error("Could not access libflux at '" + libFluxPath + "'. Ensure that the path \
is accessible, or try setting the environment variable " LIBFLUX_PATH_ENV_VAR " to the libflux library path \
(tried " + format_System_WLM(system, wlm) + ")");
    }

    return true;

#else
    return false;
#endif
}

} // anonymous namespace

static auto detect_System(std::string const& systemSetting)
{
    // Check environment system setting, if provided
    if (!systemSetting.empty()) {
        if (systemSetting == "linux") {
            return System::Linux;
        } else if (systemSetting == "hpcm") {
            return System::HPCM;
        } else if (systemSetting == "shasta") {
            return System::Shasta;
        } else if (systemSetting == "xc") {
            return System::XC;
        } else if (systemSetting == "cs") {
            return System::CS;
        } else {
            throw std::runtime_error("invalid system setting for " CTI_WLM_IMPL_ENV_VAR ": '"
                + systemSetting + "'");
        }
    }

    // Run available system detection heuristics
    if (detect_HPCM()) {
        return System::HPCM;
    } else if (detect_CS()) {
        return System::CS;
    }

    // Other systems have combination system and WLM detection heuristics
    return System::Unknown;
}

static auto detect_WLM(System const& system, std::string const& wlmSetting, std::string const& launcherName)
{
    // Check environment WLM setting, if provided
    if (!wlmSetting.empty()) {
        if ((wlmSetting == "ssh") || (wlmSetting == "generic")) {
            return WLM::SSH;
        } else if (wlmSetting == "alps") {
            return WLM::ALPS;
        } else if (wlmSetting == "slurm") {
            return WLM::Slurm;
        } else if (wlmSetting == "pals") {
            return WLM::PALS;
        } else if (wlmSetting == "flux") {
            return WLM::Flux;
        } else {
            throw std::runtime_error("invalid WLM setting for " CTI_WLM_IMPL_ENV_VAR ": '"
                + wlmSetting + "'");
        }
    }

    // Run wlm_detect, if available
    try {
        // Define libwlm_detect function types
        using WlmDetectGetActiveType = char*(void);
        using WlmDetectGetDefaultType = char*(void);

        // Try to load libwlm_detect functions
        auto libWlmDetectHandle = cti::Dlopen::Handle{WLM_DETECT_LIB_NAME};
        auto wlm_detect_get_active = libWlmDetectHandle.load<WlmDetectGetActiveType>("wlm_detect_get_active");
        auto wlm_detect_get_default = libWlmDetectHandle.load<WlmDetectGetDefaultType>("wlm_detect_get_default");

        // Call libwlm_detect functions to determine WLM
        auto wlmName = std::string{};
        if (auto activeWlmName = cti::take_pointer_ownership(wlm_detect_get_active(), ::free)) {
            wlmName = activeWlmName.get();
        } else if (auto defaultWlmName = cti::take_pointer_ownership(wlm_detect_get_default(), ::free)) {
            wlmName = defaultWlmName.get();
        } else {
            throw std::runtime_error("no active or default WLM detected");
        }

        // Compare WLM name to determine Slurm or ALPS
        if (wlmName == "ALPS") {
            return WLM::ALPS;
        } else if (wlmName == "SLURM") {
            return WLM::Slurm;
        }

    } catch (...) {
        // Ignore wlm_detect errors and continue with heuristics. Logger cannot be called
        // during construction, as it depends on Frontend state and will deadlock.
    }

    // Run WLM detection heuristics that may depend on system type
    switch (system) {

    case System::Unknown:
    case System::Linux:
        if (detect_PALS(launcherName)) {
            return WLM::PALS;
        } else if (detect_Flux(launcherName)) {
            return WLM::Flux;
        } else {
            return WLM::SSH;
        }

    case System::HPCM:
        if (detect_PALS(launcherName)) {
            return WLM::PALS;
        } else if (detect_Slurm(launcherName)) {
            return WLM::Slurm;
        } else if (detect_Flux(launcherName)) {
            return WLM::Flux;
        } else {
            return WLM::Unknown;
        }

    case System::Shasta:
        if (detect_PALS(launcherName)) {
            return WLM::PALS;
        } else if (detect_Slurm(launcherName)) {
            return WLM::Slurm;
        } else if (detect_Flux(launcherName)) {
            return WLM::Flux;
        } else {
            return WLM::Unknown;
        }

    case System::XC:
        if (detect_Slurm(launcherName)) {
            return WLM::Slurm;
        } else if (detect_XC_ALPS(launcherName)) {
            return WLM::ALPS;
        } else {
            return WLM::Unknown;
        }

    case System::CS:
        if (detect_Slurm(launcherName)) {
            return WLM::Slurm;
        } else {
            return WLM::SSH;
        }

    default:
        break;
    }

    // Run WLM detection heuristics that do not depend on system type
    if (detect_PALS(launcherName)) {
        return WLM::PALS;
    } else if (detect_Slurm(launcherName)) {
        return WLM::Slurm;
    }

    return WLM::Unknown;
}

static void verify_System_WLM_configured(System const& system, WLM const& wlm, std::string const& launcherName)
{
    switch (wlm) {

    case WLM::PALS:
        verify_PALS_configured(system, wlm, launcherName);
        break;

    case WLM::Slurm:
        verify_Slurm_configured(system, wlm, launcherName);
        break;

    case WLM::ALPS:
        // XC systems have no detection heuristic, so will detect as Unknown
        if ((system == System::Unknown) || (system == System::XC)) {
            verify_XC_ALPS_configured(system, wlm, launcherName);
        } else {
            throw std::runtime_error("WLM was set to ALPS, but system was not detected as a Cray XC system \
(tried " + format_System_WLM(system, wlm) + ")");
        }
        break;

    case WLM::SSH:
        verify_SSH_configured(system, wlm, launcherName);
        break;

    case WLM::Flux:
        verify_Flux_configured(system, wlm, launcherName);
        break;

    default:
        // TODO: write instructions on how to use the CTI diagnostic utility
        throw std::runtime_error("Could not detect either a PALS, Slurm, ALPS, Flux, or generic MPIR-compliant WLM. Manually set " CTI_WLM_IMPL_ENV_VAR" env var \
(tried " + format_System_WLM(system, wlm) + ")");
    }
}

// Use combination of set / detected system and WLM to instantiate the proper
// Frontend variant
static Frontend* make_Frontend(System const& system, WLM const& wlm)
{
    // All invalid system / WLM combinations are caught and reported to the user
    // by verify_System_WLM_configured, so assert on invalid combinations.

    if (wlm == WLM::Slurm) {
        if (system == System::HPCM) {
            return new HPCMSLURMFrontend{};
        } else {
            return new SLURMFrontend{};
        }

    } else if (wlm == WLM::ALPS) {
#if HAVE_ALPS
        return new ALPSFrontend{};
#else
        throw std::runtime_error("ALPS support was not configured for this build of CTI \
(tried " + format_System_WLM(system, wlm) + ")");
#endif

    } else if (wlm == WLM::PALS) {
        return new HPCMPALSFrontend{};

    } else if (wlm == WLM::SSH) {
        return new GenericSSHFrontend{};

    } else if (wlm == WLM::Flux) {
#if HAVE_FLUX
        return new FluxFrontend{};
#else
        throw std::runtime_error("Flux support was not configured for this build of CTI \
(tried " + format_System_WLM(system, wlm) + ")");

#endif

    } else {
        assert(false);
    }
}

Frontend&
Frontend::inst()
{
    // Check if the singleton has been initialized
    auto inst = m_instance.load(std::memory_order_acquire);
    if (!inst) {
        // Grab the lock and double check the condition
        std::lock_guard<std::mutex> lock{m_mutex};
        inst = m_instance.load(std::memory_order_relaxed);

        if (!inst) {
            // We were the first one here, create the cleanup handle
            m_cleanup = std::make_unique<Frontend_cleanup>();

            // Read launcher name setting
            auto launcherName = std::string{};
            if (auto const launcher_name = ::getenv(CTI_LAUNCHER_NAME_ENV_VAR)) {
                launcherName = std::string{launcher_name};
            }

            // Determine which wlm to instantiate
            auto system = System::Unknown;
            auto wlm = WLM::Unknown;

            { auto systemSetting = std::string{};
              auto wlmSetting = std::string{};

                // Read and parse environment setting
                if (auto const system_wlm_setting = ::getenv(CTI_WLM_IMPL_ENV_VAR)) {

                    auto [firstSetting, secondSetting] = cti::split::string<2>(system_wlm_setting, '/');

                    // If only one of system / WLM provided, assume WLM
                    if (secondSetting.empty()) {
                        wlmSetting = std::move(firstSetting);

                    } else {
                        systemSetting = std::move(firstSetting);
                        wlmSetting = std::move(secondSetting);
                    }
                }

                // Run system and WLM detection
                system = detect_System(systemSetting);
                wlm = detect_WLM(system, wlmSetting, launcherName);
            }

            // Verify that detected / set system and WLM are configured correctly
            verify_System_WLM_configured(system, wlm, launcherName);

            // Instantiate frontend implementation
            inst = make_Frontend(system, wlm);

            // Store successfully constructed instance in class variable
            m_instance.store(inst, std::memory_order_release);
        }
    }
    return *inst;
}

void
Frontend::destroy() {
    // Use sequential consistency here
    if (auto instance = m_instance.exchange(nullptr)) {

        // clean up all App/Sessions before destructors are run
        for (auto&& app : instance->m_apps) {
            try {
                app->finalize();
            } catch (std::exception const& ex) {
                // Ignore cleanup exceptions
            }
        }

        delete instance;
    }
}

std::vector<std::string>
Frontend::getDefaultEnvVars() {
    std::vector<std::string> ret;
    // Check each attribute to see if it needs to be forwarded
    if (!m_log_dir.empty()) {
        ret.emplace_back(std::string{CTI_LOG_DIR_ENV_VAR} + "=" + m_log_dir);
    }
    if (m_pmi_fopen_timeout != PMI_ATTRIBS_DEFAULT_FOPEN_TIMEOUT) {
        ret.emplace_back(std::string{PMI_ATTRIBS_TIMEOUT_VAR} + "=" + std::to_string(m_pmi_fopen_timeout));
    }
    if (m_extra_sleep != 0) {
        ret.emplace_back(std::string{PMI_EXTRA_SLEEP_VAR} + "=" + std::to_string(m_extra_sleep));
    }
    return ret;
}

Frontend::Frontend()
: m_ld_preload{}
, m_stage_deps{true}
, m_log_dir{}
, m_debug{false}
, m_pmi_fopen_timeout{PMI_ATTRIBS_DEFAULT_FOPEN_TIMEOUT}
, m_extra_sleep{0}
{
    // Read initial environment variable overrides for default attrib values
    if (const char* env_var = getenv(CTI_LOG_DIR_ENV_VAR)) {
        if (!cti::dirHasPerms(env_var, R_OK | W_OK | X_OK)) {
            throw std::runtime_error(std::string{"Bad directory specified by environment variable "} + CTI_LOG_DIR_ENV_VAR);
        }
        m_log_dir = std::string{env_var};
    }
    if (getenv(CTI_DBG_ENV_VAR)) {
        m_debug = true;
    }
    // Unload any LD_PRELOAD values, this may muck up CTI daemons.
    // Make sure to save this to pass to the environment of any application
    // that gets launched.
    if (const char* env_var = getenv("LD_PRELOAD")) {
        m_ld_preload = std::string{env_var};
        unsetenv("LD_PRELOAD");
    }
    // Setup the password file entry. Other utilites need to use this
    std::tie(m_pwd, m_pwd_buf) = cti::getpwuid(geteuid());

    // Setup the directories. We break these out into private static methods
    // to avoid pollution in the constructor.
    m_cfg_dir = findCfgDir();
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

Frontend::~Frontend()
{
    // Unlink the cleanup files since we are exiting normally
    for (auto&& file : m_cleanup_files) {
        unlink(file.c_str());
    }
}

std::weak_ptr<Session>
App::createSession()
{
    auto ptrInsertedPair = m_sessions.emplace(Session::make_Session(shared_from_this()));
    if (!ptrInsertedPair.second) {
        throw std::runtime_error("Failed to create new Session object.");
    }
    return *ptrInsertedPair.first;
}

void
App::removeSession(std::shared_ptr<Session> sess)
{
    // tell session to launch cleanup
    sess->finalize();
    // drop the shared_ptr
    m_sessions.erase(sess);
}

void
App::finalize()
{
    // tell all sessions to launch cleanup
    for (auto&& session : m_sessions) {
        session->finalize();
    }
}
