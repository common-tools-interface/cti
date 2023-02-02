// Copyright 2023 Hewlett Packard Enterprise Development LP.

#include "cti_defs.h"
#include "cti_argv_defs.hpp"

#include "Localhost/Frontend.hpp"

#include "useful/cti_useful.h"
#include "useful/cti_argv.hpp"
#include "useful/cti_wrappers.hpp"
#include <filesystem>

int LocalhostApp::m_nextId = 0;

LocalhostApp::LocalhostApp(LocalhostFrontend& fe, CArgArray launcher_argv,
                           int stdout_fd, int stderr_fd,
                           CStr inputFile, CStr chdirPath, CArgArray env_list,
                           bool stopAtBarrier)
    : App(fe, 0)
    , m_id(m_nextId++)
    , m_toolPath    { LOCALHOST_TOOL_DIR }
    , m_attribsPath { LOCALHOST_TOOL_DIR }
    , m_stagePath   { cti::cstr::mkdtemp(std::string{fe.getCfgDir() + "/" + SSH_STAGE_DIR}) }
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

    for (int i=0; i<numPEs; ++i) {
        auto pid = fork();
        if (pid == 0) {
            ::execvp(appArgs[0], const_cast<char* const*>(appArgs));
        } else if (pid > 0) {
            if (false && stopAtBarrier) {
                ::kill(pid, SIGSTOP);
            }
            m_appPEs.push_back(pid);
        } else {
            throw std::runtime_error("fork failed");
        }
    }

    // write the application pes to a file for the back-end
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

LocalhostApp::~LocalhostApp()
{
    fprintf(stderr, "don't forget to clean up the temp directory!\n");
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
    auto id = std::to_string(getpid()) + "." + std::to_string(m_id);
    return id;
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
    fprintf(stderr, "appBarrier is disabled\n");
    return;
    fprintf(stderr, "releaseBarrier\n");
    kill(SIGCONT);
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

    fprintf(stderr, "shipPackage %s to %s\n", from.c_str(), to.c_str());
    std::filesystem::rename(from, to);
}

void
LocalhostApp::startDaemon(const char* const args[])
{
    // sanity check
    if (args == nullptr) {
        throw std::runtime_error("args array is empty!");
    }

        // need to set env FILE_DIR_VAR
    fprintf(stderr, "in LocalhostApp::startDaemon\n");
    for (auto arg = args; *arg; ++arg) {
        fprintf(stderr, "startDaemon: %s\n", *arg);
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
        } catch (std::exception& err) {
            fprintf(stderr, "failed to line: %s\n", err.what());
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

    for (auto arg = launcherArgv.get(); *arg; ++arg) {
        fprintf(stderr, "launcher argv: %s\n", *arg);
    }

    fprintf(stderr, "trying to launch\n");

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
                                                 inputFile, chdirPath, env_list, false);

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
    throw std::runtime_error("Localhost register job is not implemented");
}

std::string
LocalhostFrontend::getHostname() const
{
    return cti::cstr::gethostname();
}


