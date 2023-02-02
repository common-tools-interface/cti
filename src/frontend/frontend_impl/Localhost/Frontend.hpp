
// Copyright 2023 Hewlett Packard Enterprise Development LP.

#pragma once

#include "frontend/Frontend.hpp"

class LocalhostFrontend : public Frontend
{
public:
    static char const* getName()        { return CTI_WLM_TYPE_LOCALHOST_STR; }

    // wlm type
    cti_wlm_type_t getWLMType() const override { return CTI_WLM_LOCALHOST; }

    // launch application
    std::weak_ptr<App>
    launch(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
        CStr inputFile, CStr chdirPath, CArgArray env_list) override;

    // launch application with barrier
    std::weak_ptr<App>
    launchBarrier(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
                  CStr inputFile, CStr chdirPath, CArgArray env_list);

    // create an application instance from an already-running job (the number of IDs used to
    // represent a job is implementation-defined)
    std::weak_ptr<App>
    registerJob(size_t numIds, ...) override;

    // get hostname of current node
    std::string
    getHostname(void) const override;

    LocalhostFrontend();
    ~LocalhostFrontend() override;
};

class LocalhostApp : public App
{
private:
    std::vector<int> m_appPEs;
    std::vector<int> m_toolPEs;
    std::string m_toolPath;    // Backend path where files are unpacked
    std::string m_attribsPath; // Backend Cray-specific directory
    std::string m_stagePath;   // Local directory where files are staged before transfer to BE
    std::vector<std::string> m_extraFiles; // List of extra support files to transfer to BE
    bool m_beDaemonSent = false;

public: // app interaction interface
    std::string getJobId()            const override;
    std::string getLauncherHostname() const override;
    std::string getToolPath()         const override { return m_toolPath; }
    std::string getAttribsPath()      const override { return m_attribsPath; }

    std::vector<std::string> getExtraFiles() const override { return m_extraFiles; }

    bool   isRunning()       const override;
    size_t getNumPEs()       const override { return m_appPEs.size(); }
    size_t getNumHosts()     const override { return 1; }
    std::vector<std::string> getHostnameList()   const override;
    std::vector<CTIHost>     getHostsPlacement() const override;
    std::map<std::string, std::vector<int>> getBinaryRankMap() const override;

    void releaseBarrier() override;
    void kill(int signal) override;
    void shipPackage(std::string const& tarPath) const override;
    void startDaemon(const char* const args[]) override;

public: // constructor / destructor interface
    LocalhostApp(LocalhostFrontend& fe, CArgArray launcher_argv, int stdout_fd, int stderr_fd,
                 CStr inputFile, CStr chdirPath, CArgArray env_list, bool stopAtBarrier);
    ~LocalhostApp();
    LocalhostApp(const LocalhostApp&) = delete;
    LocalhostApp& operator=(const LocalhostApp&) = delete;
    LocalhostApp(LocalhostApp&&) = delete;
    LocalhostApp& operator=(LocalhostApp&&) = delete;
};

