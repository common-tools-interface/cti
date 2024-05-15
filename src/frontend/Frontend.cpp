/*********************************************************************************\
 * Frontend.cpp - define workload manager frontend interface and common base class
 *
 * Copyright 2014-2023 Hewlett Packard Enterprise Development LP.
 * SPDX-License-Identifier: Linux-OpenIB
 ******************************************************************************/

// This pulls in config.h
#include "cti_defs.h"
#include "cti_argv_defs.hpp"

#include <filesystem>
#include <algorithm>
#include <chrono>

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

// Set this library instance as original
pid_t Frontend::m_original_pid{getpid()};

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

static void createCfgDir(const std::string& path) {
    if (::mkdir(path.c_str(), S_IRWXU)) {
        switch (errno) {
            case EEXIST:
                // Something already exists at path. verifyCfgDir will make sure
                // that it's a directory with the right permissions. Our job here is done.
                return;
            default:
                throw std::runtime_error(std::string("mkdir(") + path + ") " + strerror(errno));
        }
    }
}

static std::string verifyAndExpandCfgDir(const std::string& path, uid_t uid) {
    // verify that path is a directory and that we can access it
    if (!cti::dirHasPerms(path.data(), R_OK | W_OK | X_OK)) {
        throw std::runtime_error(std::string{"Bad directory: "} + path + ": bad permissions (needs rwx)");
    }

    // verify that it has *no more* permissions than expected
    {
        struct stat st;
        if (stat(path.data(), &st)) {
            // could not stat the directory
            throw std::runtime_error(std::string("stat(") + path + ") " + strerror(errno));
        }

        if ((st.st_mode & (S_ISUID | S_ISGID | S_IRWXU | S_IRWXG | S_IRWXO)) & ~S_IRWXU) {
            // bits other than S_IRWXU are set
            throw std::runtime_error(std::string("Bad permissions (Only 0700 allowed) for ") + path);
        }
    }

    // expand to real path
    auto real_path = std::filesystem::canonical(path);

    // Ensure we have ownership of this directory, otherwise it is untrusted
    {
        struct stat st;
        memset(&st, 0, sizeof(st));
        if (stat(path.c_str(), &st)) {
            throw std::runtime_error(std::string("stat(") + path + ") " + strerror(errno));
        }
        if (st.st_uid != uid) {
            throw std::runtime_error(std::string("Directory already exists: ") + path);
        }
    }

    return real_path;
}

std::string
Frontend::setupCfgDir()
{
    // Create a config directory for this specific instance of the CTI frontend. The
    // config directory is used to store temporary files.
    //
    // It will be created at <top path>/<base path>/<config path>:
    //   <top path>: some generic, already existing, directory that the user has write access to (e.g. /tmp),
    //   <base path>: a special directory we create for CTI files (e.g. /tmp/cti-username),
    //   <config path>: the directory we create for *this* CTI instance (e.g. /tmp/cti-username/<pid>)
    //
    // CTI_CFG_DIR_ENV_VAR allows a user to specify the <top path>.

    // top path
    std::string top_path;
    {
        if (const char* cfg_dir_env = getenv(CTI_CFG_DIR_ENV_VAR)) {
            top_path = cfg_dir_env;
        } else {
            const auto strGetEnv = [](const char* key) -> std::string {
                const char* value = ::getenv(key);
                return value ? value : "";
            };

            // look in this order: $TMPDIR, /tmp, $HOME
            const auto search_dirs = std::vector<std::string> {
                strGetEnv("TMPDIR"),
                "/tmp",
                strGetEnv("HOME"),
            };

            for (auto&& dir_var : search_dirs) {
                if (!dir_var.empty() && cti::dirHasPerms(dir_var.c_str(), R_OK | W_OK | X_OK)) {
                    top_path = dir_var;
                    break;
                }
            }
        }

        if (top_path.empty()) {
            // We have no where to create a temporary directory...
            throw std::runtime_error(
                std::string("Cannot find suitable config directory. Try setting "
                            "the env variable ") + CTI_CFG_DIR_ENV_VAR);
        }
    }

    // base path
    std::string base_path;
    {
        // FIXME: How to ensure sane pwd?
        if (!m_pwd.pw_name)
            throw std::runtime_error("Unable to determine username");

        base_path = top_path + "/cti-" + std::string(m_pwd.pw_name);
        createCfgDir(base_path);
        // expands to full path
        base_path = verifyAndExpandCfgDir(base_path, m_pwd.pw_uid);
    }

    // config path
    auto cfg_dir = base_path + "/" + std::to_string(::getpid());
    createCfgDir(cfg_dir);
    return verifyAndExpandCfgDir(cfg_dir, m_pwd.pw_uid);
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

void
Frontend::removeApp(std::shared_ptr<App> app)
{
    // drop the shared_ptr
    m_apps.erase(app);
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
    , Eproxy
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
        case System::Eproxy:  return "Eproxy";
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
    , Localhost
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
        case WLM::Localhost: return "Localhost";
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

// Check if this is an elogin node with eproxy configured
static bool detect_Eproxy()
{
    // Check for Eproxy binary and configuration file
    try {
        char const* eproxyArgv[] = { "eproxy", "--check", nullptr };

        // Start eproxy check
        if (cti::Execvp::runExitStatus("eproxy", (char* const*)eproxyArgv)) {
            return false;
        }

        // Look for Eproxy configuration
        auto eproxy_keyfile = (::getenv("EPROXY_KEYFILE"))
            ? ::getenv("EPROXY_KEYFILE")
            : "/opt/cray/elogin/eproxy/etc/eproxy.ini";
        if (!cti::fileHasPerms(eproxy_keyfile, R_OK)) {
            return false;
        }

        // All Eproxy checks passed
        return true;

    } catch (...) {
        // eproxy not installed
        return false;
    }
}

// HPCM / Shasta
static bool detect_PALS(std::string const& /* unused */)
{
    try {

        // Check that PBS is installed (required for PALS)
        char const* rpm_argv[] =
            { "rpm", "-q"
            , "pbspro-server", "pbspro-client", "pbspro-execution"
            , "openpbs-server", "openpbs-client", "openpbs-execution"
            , nullptr
        };

        // PBS is configured if at least one of these packages exists
        // Return code of 6 means query of all 6 packages failed (not installed)
        auto const failed_packages = cti::Execvp::runExitStatus("rpm", (char* const*)rpm_argv);
        if (failed_packages == 6) {
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
        if (cti::Execvp::runExitStatus(launcher_name, fluxArgv.get())) {
            return false;
        }

        // Remove check for FLUX_URI, as this is only available in allocations
        // Still want to be able to present a diganostic to run in an allocation

        return true;

    } catch (std::exception const& ex) {
        return false;
    }
}

enum class MPIRSymbolStatus
    { Ok
    , LauncherNotFound
    , NotBinaryFile
    , NoMPIRBreakpoint
    , NoMPIRSymbols
};

static auto format_MPIRSymbolError(MPIRSymbolStatus const& mpirSymbolStatus,
    std::string const& launcherName, std::string const& launcherPath,
    System const& system, WLM const& wlm)
{
    auto result = std::stringstream{};

    if (mpirSymbolStatus == MPIRSymbolStatus::LauncherNotFound) {
        result << launcherName << " was not found in PATH (tried "
            << format_System_WLM(system, wlm) << "). If your system is "
            "not configured with this workload manager, try setting the environment "
            "variable " CTI_WLM_IMPL_ENV_VAR " to one of 'slurm', 'pals', 'flux', or "
            "'alps'. For more information, run `man cti` and review "
            CTI_WLM_IMPL_ENV_VAR ".";

    } else if (mpirSymbolStatus == MPIRSymbolStatus::NotBinaryFile) {
        result << launcherName << " was found at " << launcherPath
            << ", but it is not a binary file. Tool launch requires "
            "direct access to the " << launcherName << " binary. "
            "Ensure that the " << launcherName << " binary not wrapped by a script "
            "(tried " << format_System_WLM(system, wlm) << ")";

    } else if (mpirSymbolStatus == MPIRSymbolStatus::NoMPIRBreakpoint) {
        result << launcherName << " was found at " << launcherPath
            << ", but it does not appear to support MPIR launch "
            "(function MPIR_Breakpoint was not found). Tool launch is "
            "coordinated through setting a breakpoint at this function. "
            "Please contact your system administrator with a bug report "
            "(tried " << format_System_WLM(system, wlm) << ")";

    } else if (mpirSymbolStatus == MPIRSymbolStatus::NoMPIRSymbols) {
        result << launcherName << " was found at " << launcherPath
            << ", but it does not contain debug symbols. "
            "Tool launch is coordinated through reading information at these symbols. "
            "Please contact your system administrator with a bug report "
            "(tried " << format_System_WLM(system, wlm) << ")";
    }

    return result.str();
}

// Verify that the provided launcher is a binary and contains MPIR symbols
// Return tuple of {status, detected launcher path}
static auto verify_MPIR_symbols(System const& system, WLM const& wlm,
    std::string launcherName)
{
    assert(!launcherName.empty());

    // Check that the launcher is present in PATH
    auto launcherPath = std::string{};
    try {
        launcherPath = cti::findPath(launcherName);
    } catch (...) {
        return std::make_tuple(MPIRSymbolStatus::LauncherNotFound, std::string{});
    }

    // Check that the launcher is a binary and not a script
    { auto binaryTestArgv = cti::ManagedArgv{"sh", "-c",
        "file --mime -L " + launcherPath + " | grep -E 'application/x-(executable|sharedlib)'"};
        if (cti::Execvp::runExitStatus("sh", binaryTestArgv.get())) {
            return std::make_tuple(MPIRSymbolStatus::NotBinaryFile, launcherPath);
        }
    }

    // Check that the launcher binary supports MPIR launch
    { auto symbolTestArgv = cti::ManagedArgv{"sh", "-c",
        "nm -a " + launcherPath + " | grep MPIR_Breakpoint$"};
        if (cti::Execvp::runExitStatus("sh", symbolTestArgv.get())) {
            return std::make_tuple(MPIRSymbolStatus::NoMPIRBreakpoint, launcherPath);
        }
    }

    // Check that the launcher binary contains MPIR symbols
    { auto symbolTestArgv = cti::ManagedArgv{"sh", "-c",
        "nm -a " + launcherPath + " | grep MPIR_being_debugged$"};
        if (cti::Execvp::runExitStatus("sh", symbolTestArgv.get())) {
            return std::make_tuple(MPIRSymbolStatus::NoMPIRSymbols, launcherPath);
        }
    }

    return std::make_tuple(MPIRSymbolStatus::Ok, launcherPath);;
}

static bool verify_PALS_configured(System const& system, WLM const& wlm,
    std::string launcherName)
{
    // Default to `mpiexec`
    if (launcherName.empty()) {
        launcherName = "mpiexec";
    }

    // Check for MPIR symbols in launcher
    auto [status, launcherPath] = verify_MPIR_symbols(system, wlm, launcherName);

    // Throw error on failure
    if (status != MPIRSymbolStatus::Ok) {
        throw std::runtime_error(format_MPIRSymbolError(status, launcherName,
            launcherPath, system, wlm));
    }

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

static bool verify_XC_ALPS_configured(System const& system, WLM const& wlm,
    std::string launcherName)
{
    // Default to `aprun`
    if (launcherName.empty()) {
        launcherName = "aprun";
    }

    // Check for MPIR symbols in launcher
    auto [status, launcherPath] = verify_MPIR_symbols(system, wlm, launcherName);

    // Throw error on failure
    if (status != MPIRSymbolStatus::Ok) {
        throw std::runtime_error(format_MPIRSymbolError(status, launcherName,
            launcherPath, system, wlm));
    }

    return true;
}

// A Slurm cluster launch / attach can be in one of three situations:
// 1) One cluster (default) or multi-cluster where only one cluster has valid nodes
// 2) Multi-cluster running from a cluster-unique node (usually compute or partitioned login nodes)
// 3) Multi-cluster running from a node shared between multiple clusters or otherwise unassigned
// Case 2 is identical to the default case 1 from our perspective, as long as the user
//   is not attempting to attach to a job running on a different cluster. `sbcast` and `sattach`
//   will function normally in this case. If the user does attempt to attach between clusters,
//   Slurm will report the job ID as invalid. We can't detect this case without querying every
//   cluster in the system.
// Case 3 is not supported, as `sbcast` and `sattach` do not support selecting the target
// cluster for the command.
static bool detect_Slurm_shared_multicluster()
{
    try {
        char const* sacctmgrArgv[] = { SACCTMGR, "-P", "-n", "show", "cluster", "format=Cluster,ClusterNodes", nullptr };

        // Start sacctmgr
        auto sacctmgrOutput = cti::Execvp{SACCTMGR, (char* const*)sacctmgrArgv, cti::Execvp::stderr::Ignore};

        // Count number of clusters that contain nodes
        auto num_active_clusters = int{0};
        auto& sacctmgrStream = sacctmgrOutput.stream();
        auto clusterLine = std::string{};
        while (std::getline(sacctmgrStream, clusterLine)) {
            auto&& [cluster, nodes] = cti::split::string<2>(clusterLine, '|');
            if (!nodes.empty()) {
                num_active_clusters++;
            }
        }

        // Check return code
        if (sacctmgrOutput.getExitStatus() != 0) {
            return false;
        }

        // Multi-cluster systems where only one cluster has active nodes can be treated as
        // a normal single cluster system
        if (num_active_clusters <= 1) {
            return false;
        }

        // Detect running from shared node (no cluster name specified in Slurm configuration)
        { auto clusterNameArgv = cti::ManagedArgv{"sh", "-c",
            "scontrol show config | grep ClusterName"};
            if (cti::Execvp::runExitStatus("sh", clusterNameArgv.get())) {
                return true;
            }
        }

        // Running from a node within a defined cluster
        return false;

    } catch(...) {
        return false;
    }
}

static bool detect_Slurm_allocation()
{
    // Interactive allocations have job name of "interactive"
    // Additionally, when launched outside of an allocation,
    // this environment variable is not set in the environment.
    if (auto slurm_job_name = ::getenv(SLURM_JOB_NAME)) {
        return strcmp(slurm_job_name, "interactive") == 0;
    }

    return false;
}

static void verify_Slurm_configured(System const& system, WLM const& wlm,
    std::string launcherName)
{
    // Default to `srun`
    if (launcherName.empty()) {
        launcherName = "srun";
    }

    // Check for MPIR symbols in launcher
    auto [status, launcherPath] = verify_MPIR_symbols(system, wlm, launcherName);

    // Set launcher wrapper path if launcher was detected to be a wrapper script
    if (status == MPIRSymbolStatus::NotBinaryFile) {

        // Don't override user setting
        ::setenv(CTI_LAUNCHER_SCRIPT_ENV_VAR, launcherPath.c_str(), 0);

    // Throw error on other failure
    } else if (status != MPIRSymbolStatus::Ok) {
        throw std::runtime_error(format_MPIRSymbolError(status, launcherName,
            launcherPath, system, wlm));
    }

    // Check for multi-cluster system and allocation
    if (::getenv(SLURM_OVERRIDE_MC_ENV_VAR) == nullptr) {

        if (detect_Slurm_shared_multicluster()) {

            if (!detect_Slurm_allocation()) {
                throw std::runtime_error(
                    "CTI uses several Slurm utilities to set up job launches, some of which "
                    "do not support specifying the target cluster within a multi-cluster system.\n"
                    "To continue with launch, please start this tool inside a Slurm allocation "
                    "or on a node within the same cluster as your target job.\n"
                    "To bypass this check, set the environment variable "
                    SLURM_OVERRIDE_MC_ENV_VAR);
            }
        }
    }

    return;
}

// Check if this is an elogin node with eproxy configured
static void verify_Eproxy_Slurm_configured(System const& system, WLM const& wlm,
    std::string launcherName)
{
    // Skip check if disabled in environment
    if (::getenv(SLURM_OVERRIDE_EPROXY_ENV_VAR) != nullptr) {
        return;
    }

    try {
        char const* eproxyArgv[] = { "eproxy", "--check", nullptr };

        // Start eproxy
        auto eproxyOutput = cti::Execvp{"eproxy", (char* const*)eproxyArgv, cti::Execvp::stderr::Ignore};

        // Ensure Eproxy is satisfied with the state of the Slurm utility links
        auto& eproxyStream = eproxyOutput.stream();
        auto utilityNames = std::set<std::string> { "srun", "squeue", "scancel", "sbcast" };
        auto line = std::string{};
        while (std::getline(eproxyStream, line)) {

            // Looking for `<utility> is correct`
            if ((line.length() > 11) && (line.compare(line.length() - 11, 11, "is correct.") == 0)) {
                auto utility_start = line.rfind(' ', line.length() - 13) + 1;

                if (utility_start < std::string::npos) {
                    auto utility_end = line.find(' ', utility_start);
                    if (utility_end < std::string::npos) {

                        // Remove utility from required set
                        auto utility = line.substr(utility_start, utility_end - utility_start);
                        utilityNames.erase(utility);
                    }
                }
            }
        }

        // Ignore return code
        (void)eproxyOutput.getExitStatus();

        // All Eproxy utilities configured if seen
        if (!utilityNames.empty()) {
            throw std::runtime_error("Eproxy reported Slurm utilities not configured ("
                + cti::joinStr(utilityNames.begin(), utilityNames.end(), ", ")
                + ")");
        }

    } catch (std::exception const& ex) {

        throw std::runtime_error("Eproxy detected as not configured: "
            + std::string{ex.what()}
            + ". To disable this check, set " SLURM_OVERRIDE_EPROXY_ENV_VAR);
    }
}

static bool verify_SSH_configured(System const& system, WLM const& wlm,
    std::string launcherName)
{
    // Default to `mpiexec`
    if (launcherName.empty()) {
        launcherName = "mpiexec";
    }

    // Check for MPIR symbols in launcher
    auto [status, launcherPath] = verify_MPIR_symbols(system, wlm, launcherName);

    // Throw error on failure
    if (status != MPIRSymbolStatus::Ok) {
        throw std::runtime_error(format_MPIRSymbolError(status, launcherName,
            launcherPath, system, wlm));
    }

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
        } else if (systemSetting == "eproxy") {
            return System::Eproxy;
        } else {
            throw std::runtime_error("invalid system setting for " CTI_WLM_IMPL_ENV_VAR ": '"
                + systemSetting + "'");
        }
    }

    // Run available system detection heuristics
    if (detect_Eproxy()) {
        return System::Eproxy;
    } else if (detect_HPCM()) {
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
        } else if (wlmSetting == "localhost") {
            return WLM::Localhost;
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

    // Run XC WLM detection heuristics if detected XC
    if (system == System::XC) {
        if (detect_Slurm(launcherName)) {
            return WLM::Slurm;
        } else if (detect_XC_ALPS(launcherName)) {
            return WLM::ALPS;
        } else {
            return WLM::Unknown;
        }
    }

    // Run general WLM detection heuristics
    if (detect_Slurm(launcherName)) {
        return WLM::Slurm;

    } else if (detect_PALS(launcherName)) {
        return WLM::PALS;

    } else if (detect_Flux(launcherName)) {
        return WLM::Flux;

    }

    // Could not detect WLM, try SSH
    return WLM::SSH;
}

static void verify_System_WLM_configured(System const& system, WLM const& wlm, std::string const& launcherName)
{
    // Eproxy is only valid with Slurm WLM
    if ((system == System::Eproxy) && (wlm != WLM::Slurm)) {
        throw std::runtime_error("System was detected as Eproxy, but WLM was not detected as Slurm."
            "CTI only supports Eproxy mode on Slurm systems. Please run this tool directly on a login "
            "or compute node (tried " + format_System_WLM(system, wlm) + ")");
    }

    switch (wlm) {

    case WLM::PALS:
        verify_PALS_configured(system, wlm, launcherName);
        break;

    case WLM::Slurm:
        if (system == System::Eproxy) {
            verify_Eproxy_Slurm_configured(system, wlm, launcherName);
        } else {
            verify_Slurm_configured(system, wlm, launcherName);
        }
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

    case WLM::Localhost:
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
        } else if (system == System::Eproxy) {
            return new EproxySLURMFrontend{};
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
#if HAVE_PALS
        return new PALSFrontend{};
#else
        throw std::runtime_error("PALS support was not configured for this build of CTI \
(tried " + format_System_WLM(system, wlm) + ")");
#endif

    } else if (wlm == WLM::SSH) {
        return new GenericSSHFrontend{};

    } else if (wlm == WLM::Flux) {
#if HAVE_FLUX
        return new FluxFrontend{};
#else
        throw std::runtime_error("Flux support was not configured for this build of CTI \
(tried " + format_System_WLM(system, wlm) + ")");

#endif

    } else if (wlm == WLM::Localhost) {
        return new LocalhostFrontend{};

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
            // Create the cleanup handle if needed
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

        // Skip session cleanup if not running from original instance
        if (instance->isOriginalInstance()) {

            // clean up all App/Sessions before destructors are run
            for (auto&& app : instance->m_apps) {
                try {
                    app->finalize();
                } catch (std::exception const& ex) {
                    // Ignore cleanup exceptions
                }
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
    m_cfg_dir = setupCfgDir();
    m_base_dir = findBaseDir();
    // Following strings depend on m_base_dir
    m_ld_audit_path = cti::accessiblePath(m_base_dir + "/lib/" + LD_AUDIT_LIB_NAME);
    m_fe_daemon_path = cti::accessiblePath(m_base_dir + "/libexec/" + CTI_FE_DAEMON_BINARY);
    m_be_daemon_path = cti::accessiblePath(m_base_dir + "/libexec/" + CTI_BE_DAEMON_BINARY);
    // init the frontend daemon now that we have the path to the binary
    m_daemon.initialize(m_fe_daemon_path);
}

Frontend::~Frontend()
{
    if (!isOriginalInstance()) {
        writeLog("~Frontend: forked PID %d exiting without cleanup\n", getpid());
        return;
    }

    // Clean up temporary files.
    std::filesystem::remove_all(getCfgDir());

    // Sometimes, previous CTI frontends die and can't clean up. Try to clean up
    // leftover temporary directories that are no longer in use.
    try {
        const auto base_path = std::filesystem::path(getCfgDir()).parent_path();

        // Clean up an old directory if:
        // - It is older than 5 minutes
        // - Its name matches the format of a pid
        // - And there is no process running that we control which matches the pid
        auto to_remove = std::vector<std::filesystem::path>{};
        for (const auto& dir_entr : std::filesystem::directory_iterator(base_path)) {
            if (!dir_entr.is_directory()) continue;

            // verify that the directory is at least 5 minutes old
            using namespace std::chrono_literals;
            auto last_write_time = std::filesystem::last_write_time(dir_entr.path());
            auto age = std::filesystem::file_time_type::clock::now() - last_write_time;
            if (age < 5min) continue;

            // verify that the directory name could possibly be a pid
            const auto filename = dir_entr.path().filename().string();
            char* end = nullptr;
            auto pid = ::strtol(filename.c_str(), &end, 10);

            if (end != filename.c_str() + filename.size())
                // directory name not exclusively digits
                continue;

            if (pid <= 0 || pid > std::numeric_limits<pid_t>::max())
                // directory name is too small/too big to be a valid pid
                continue;

            // verify that the owning process is gone
            if (!::kill(pid, 0)) continue;

            to_remove.push_back(dir_entr.path());
        }

        for (const auto& path : to_remove) {
            std::filesystem::remove_all(path);
        }
    } catch (const std::exception& e) {
        writeLog("~Frontend: exception thrown while attempting to clean up old directories, skipping (%s).\n",
            e.what());
    } catch (...) {
        writeLog("~Frontend: unknown exception thrown while attempting to clean up old directories, skipping.\n");
    }
}

cti_symbol_result_t Frontend::containsSymbols(std::string const& binaryPath,
    std::unordered_set<std::string> const& symbols, cti_symbol_query_t query) const
{
    // Check file exists
    struct stat buf = {};
    if (::stat(binaryPath.c_str(), &buf) < 0) {
        throw std::runtime_error("no file found at " + binaryPath);
    }

    // Check file executable
    if (!(buf.st_mode & S_IXUSR)) {
        throw std::runtime_error(binaryPath + " is not executable");
    }

    // Use nm to list symbols
    auto result = CTI_SYMBOLS_NO;
    char const* nm_argv[] = {"nm", binaryPath.c_str(), nullptr};
    auto nmOutput = cti::Execvp{"nm", (char* const*)nm_argv, cti::Execvp::stderr::Ignore};
    auto& nmStream = nmOutput.stream();
    auto nmLine = std::string{};

    if (query == CTI_SYMBOLS_ANY) {

        // Determine if any symbols match provided
        while (std::getline(nmStream, nmLine)) {
            nmLine = cti::split::removeLeadingWhitespace(std::move(nmLine));
            auto [addr, label, symbol] = cti::split::string<3>(nmLine, ' ');

            // Undefined symbols won't have an address
            if (symbol.empty()) {
                symbol = label;
            }

            if (symbols.count(symbol) > 0) {
                result = CTI_SYMBOLS_YES;
                break;
            }
        }

    } else if (query == CTI_SYMBOLS_ALL) {
        auto remainingSymbols = symbols;

        // Determine if all symbols match provided
        while (std::getline(nmStream, nmLine)) {
            auto [addr, label, symbol] = cti::split::string<3>(nmLine, ' ');
            auto symbolIter = remainingSymbols.find(symbol);
            if (symbolIter != remainingSymbols.end()) {
                remainingSymbols.erase(symbolIter);
            }

            // Exit if found all
            if (remainingSymbols.empty()) {
                result = CTI_SYMBOLS_YES;
                break;
            }
        }
    }

    // Wait for nm exit
    nmStream.ignore(std::numeric_limits<std::streamsize>::max());
    (void)nmOutput.getExitStatus();

    return result;
}

App::App(Frontend& fe, FE_daemon::DaemonAppId daemonAppId)
    : m_frontend{fe}
    , m_daemonAppId{daemonAppId}
    , m_sessions{}
    , m_uniqueBEDaemonName{CTI_BE_DAEMON_BINARY}
{
    // Generate the unique BE daemon name
    for (size_t i = 0; i < 6; i++) {
        m_uniqueBEDaemonName.push_back(m_frontend.Prng().genChar());
    }
}

App::App(Frontend& fe)
    : App{fe, -1}
{
    // Create new daemon app ID
    m_daemonAppId = Frontend::inst().Daemon().request_RegisterApp();
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
