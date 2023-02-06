// Copyright 2023 Hewlett Packard Enterprise Development LP.

#include "cti_defs.h"
#include "cti_argv_defs.hpp"

#include "Localhost/Frontend.hpp"

#include "useful/cti_useful.h"
#include "useful/cti_argv.hpp"
#include "useful/cti_wrappers.hpp"
#include <filesystem>
#include <fstream>

int LocalhostApp::m_nextId = 0;

LocalhostApp::LocalhostApp(LocalhostFrontend& fe, CArgArray launcher_argv,
                           int stdout_fd, int stderr_fd,
                           CStr inputFile, CStr chdirPath, CArgArray env_list,
                           bool stopAtBarrier)
    : App(fe, 0)
    , m_id          { std::to_string(getpid()) + "." + std::to_string(m_nextId++) }
    , m_toolPath    { LOCALHOST_TOOL_DIR }
    , m_attribsPath { LOCALHOST_TOOL_DIR }
    , m_stagePath   { cti::cstr::mkdtemp(std::string{fe.getCfgDir() + "/" + SSH_STAGE_DIR}) }
    , m_cleanupFiles { 1, m_stagePath }
{
    auto numPEs = 1;
    auto appArgs = launcher_argv;
    for (auto argIt = launcher_argv; *argIt; ++argIt) {
        if (strcmp(*argIt, "-n") == 0 && *(argIt+1)) {
            numPEs = atoi(*++argIt);
            appArgs = ++argIt;
            break;
        }
    }

    auto lockFileEnv = std::string{};
    if (stopAtBarrier) {
        // This is meant to work with a faked mpi which implements a startup barrier by
        // waiting for the lock file to be delete in releaseBarrier.   Crude, but
        // hopefully effective.
        auto lockFile = m_toolPath + "/cti.lock." + m_id;
        if (auto lock = fopen(lockFile.c_str(), "w")) {
            lockFileEnv = std::string{"CTI_LOCALHOST_LOCK_FILE="} + lockFile.c_str();
            m_lockFile = lockFile;
            fclose(lock);
        }
    }

    // Pass the rank information and possibly barrier lock to the application via
    // the environment.   This will allow a very basic form of ersatz mpi to work.
    std::vector<char*> env;
    for (auto envIt = environ; *envIt; ++envIt) {
        env.push_back(*envIt);
    }
    if (!lockFileEnv.empty()) {
        env.push_back(const_cast<char*>(lockFileEnv.c_str()));
    }
    env.push_back(nullptr); // place holder for rank
    env.push_back(nullptr); // terminator
    auto rankLoc = env.end()-2;

    for (int i=0; i<numPEs; ++i) {
        auto rankEnv = std::string{"CTI_LOCALHOST_RANK="} + std::to_string(i);
        *rankLoc = const_cast<char*>(rankEnv.c_str());
        auto pid = fork();
        if (pid == 0) {
            ::execvpe(appArgs[0], const_cast<char* const*>(appArgs), &env[0]);
        } else if (pid > 0) {
            m_appPEs.push_back(pid);
        } else {
            throw std::runtime_error("fork failed");
        }
    }

    writeAppPEs();
}

LocalhostApp::LocalhostApp(LocalhostFrontend& fe, const std::vector<int>& appPEs)
    : App(fe, 0)
    , m_id          { std::to_string(getpid()) + "." + std::to_string(m_nextId++) }
    , m_appPEs      { appPEs }
    , m_toolPath    { LOCALHOST_TOOL_DIR }
    , m_attribsPath { LOCALHOST_TOOL_DIR }
    , m_stagePath   { cti::cstr::mkdtemp(std::string{fe.getCfgDir() + "/" + SSH_STAGE_DIR})}
    , m_cleanupFiles { 1, m_stagePath }
{
   writeAppPEs();
}

LocalhostApp::~LocalhostApp()
{
    for (auto&& file : m_cleanupFiles) {
        std::error_code ec;
        std::filesystem::remove_all(file, ec);
        
        if (auto loc = file.find("1.tar"); loc == file.size()-5) {
            // normally the tar file is expanded.   Assuming just one
            // tar file was sent
            file.erase(loc);
            std::filesystem::remove_all(file, ec);
        }
    }
}

void LocalhostApp::writeAppPEs()
{
    // write the application pes to a file for the back-end, like a mini-MPIR proc
    // table.
    auto pidPath = m_stagePath + "/" + LOCALHOST_PID_FILE;
    if (auto const pidFile = cti::file::open(pidPath, "w")) {
        fprintf(pidFile.get(), "%zu\n", m_appPEs.size());
        for (auto pid : m_appPEs) {
            fprintf(pidFile.get(), "%d\n", pid);
        }
    } else {
        throw std::runtime_error("failed to open PID file path " + pidPath);
    }

    m_extraFiles.push_back(pidPath);
}

/* running app info accessors */
std::string
LocalhostApp::getJobId() const
{
    // In the future, the jobId could include a reference to the pidFile
    // which could work like bone simple MPIR file, but you'd have to
    // launch via cti_launch.   It could be done, but at the moment there
    // isn't much call to work with a fake attach workflow.  So just
    // do enough so parallel launches get unique ids.
    return m_id;
}

std::string
LocalhostApp::getLauncherHostname() const
{
    throw std::runtime_error("not supported for WLM: getLauncherHostname");
}

bool
LocalhostApp::isRunning() const
{
    return !m_appPEs.empty();
}

std::vector<std::string>
LocalhostApp::getHostnameList() const
{
    std::vector<std::string> result(1, m_frontend.getHostname());
    return result;
}

std::map<std::string, std::vector<int>>
LocalhostApp::getBinaryRankMap() const
{
    auto n = m_appPEs.size();
    std::vector<int> ranks(n);
    for (size_t i=0; i<n; ++i) ranks[i] = i;
    return std::map<std::string, std::vector<int>>
        { { m_frontend.getHostname(), std::move(ranks)}};
}

std::vector<CTIHost>
LocalhostApp::getHostsPlacement() const
{
    return std::vector<CTIHost>(1, CTIHost{m_frontend.getHostname(), m_appPEs.size()});
}

void
LocalhostApp::releaseBarrier()
{
    if (!m_lockFile.empty()) {
        // the fake startup barrier in the fake mpi is watching for this file to
        // cease to exist.
        std::error_code ec;
        std::filesystem::remove(m_lockFile, ec);
        m_lockFile.clear();
    }
}

void
LocalhostApp::kill(int signal)
{
    for (auto id : m_appPEs) {
        ::kill(id, signal);
    }
}

void
LocalhostApp::shipPackage(std::string const& tarPath) const
{

    auto from = std::filesystem::path{tarPath};
    auto to = std::filesystem::path{m_toolPath};
    to /= from.filename();

    std::filesystem::rename(from, to);

    m_cleanupFiles.push_back(to);
}

void
LocalhostApp::startDaemon(const char* const args[])
{
    // sanity check
    if (args == nullptr) {
        throw std::runtime_error("args array is empty!");
    }

    // Send daemon if not already shipped
    if (!m_beDaemonSent) {
        // Get the location of the backend daemon
        if (m_frontend.getBEDaemonPath().empty()) {
            throw std::runtime_error("Unable to locate backend daemon binary. Try setting " + std::string(CTI_BASE_DIR_ENV_VAR) + " environment variable to the install location of CTI.");
        }

        // Copy the BE binary to its unique storage name
        auto const sourcePath = m_frontend.getBEDaemonPath();
        auto const daemonPath = m_frontend.getCfgDir() + "/" + getBEDaemonName();

        try {
            std::filesystem::create_symlink( sourcePath, daemonPath);
            m_cleanupFiles.push_back(daemonPath);
        } catch (std::exception& err) {
            throw std::runtime_error("failed to link " + sourcePath + " to " + daemonPath);
        }

        // Ship the unique backend daemon
        shipPackage(daemonPath);
        // set transfer to true
        m_beDaemonSent = true;
    }

    // Prepare the launcher arguments
    auto daemonPath = std::filesystem::path{m_toolPath};
    daemonPath /= getBEDaemonName();
    cti::ManagedArgv launcherArgv { daemonPath };

    // Copy provided launcher arguments
    launcherArgv.add(args);

    // Execute the launcher
    if ( auto pid = ::fork(); pid == 0) {
        ::execvp(launcherArgv.get()[0], launcherArgv.get());
        throw std::runtime_error(std::string{"executing "} + launcherArgv.get()[0] + " failed");
    } else if (pid < 0) {
        throw std::runtime_error("fork failed when starting daemon");
    }
}

LocalhostFrontend::LocalhostFrontend()
{
}

LocalhostFrontend::~LocalhostFrontend()
{
}

std::weak_ptr<App>
LocalhostFrontend::launch(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
    CStr inputFile, CStr chdirPath, CArgArray env_list)
{
    auto appPtr = std::make_shared<LocalhostApp>(*this, launcher_argv, stdout_fd, stderr_fd,
                                                 inputFile, chdirPath, env_list, false);

    // Register with frontend application set
    auto resultInsertedPair = m_apps.emplace(std::move(appPtr));
    if (!resultInsertedPair.second) {
        throw std::runtime_error("Failed to insert new App object.");
    }

    return *resultInsertedPair.first;
}

std::weak_ptr<App>
LocalhostFrontend::launchBarrier(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
        CStr inputFile, CStr chdirPath, CArgArray env_list)
{
    auto appPtr = std::make_shared<LocalhostApp>(*this, launcher_argv, stdout_fd, stderr_fd,
                                                 inputFile, chdirPath, env_list, true);

    // Register with frontend application set
    auto resultInsertedPair = m_apps.emplace(std::move(appPtr));
    if (!resultInsertedPair.second) {
        throw std::runtime_error("Failed to insert new App object.");
    }

    return *resultInsertedPair.first;
}

std::weak_ptr<App>
LocalhostFrontend::registerJob(size_t numIds, ...)
{
    if (numIds != 1) {
        throw std::logic_error("expecting single pid argument to register app");
    }


    auto jobId = std::string{};
    va_list idArgs;
    va_start(idArgs, numIds);
    jobId = va_arg(idArgs, const char*);
    va_end(idArgs);

    // expecting <parent-pid>.<executable>
    auto launcher = std::string{jobId};
    auto exe = std::string{"dbgsrv"};
    if (auto dot = launcher.find('.'); dot != std::string::npos) {
        exe = launcher.substr(dot+1);
        launcher = launcher.substr(0,dot);
    }

    std::vector<int> appIds;
    std::vector<std::string> pids{launcher};
    while (!pids.empty()) {
        auto pid = std::move(pids.back());
        pids.pop_back();

        auto pdir = std::filesystem::path{"/proc/" +  pid};
        auto comm = pdir / "comm";
        if (auto is = std::ifstream(comm.c_str())) {
            auto line = std::string{};
            std::getline(is, line);
            if (line == exe) {
                appIds.push_back(std::stoi(pid));
                continue;
            }
        }
        auto children = pdir / "task" / pid / "children";
        if (auto cis = std::ifstream{children}) {
            auto childPid = std::string{};
            while (cis >> childPid) {
                pids.push_back(childPid);
            }
        }
    }

    if (appIds.empty()) {
        throw std::runtime_error("Could not find processes in job");
    }

    auto appPtr = std::make_shared<LocalhostApp>(*this, appIds);

    // Register with frontend application set
    auto resultInsertedPair = m_apps.emplace(std::move(appPtr));
    if (!resultInsertedPair.second) {
        throw std::runtime_error("Failed to insert new App object.");
    }

    return *resultInsertedPair.first;
}

std::string
LocalhostFrontend::getHostname() const
{
    return cti::cstr::gethostname();
}


