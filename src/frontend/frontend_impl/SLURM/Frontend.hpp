/******************************************************************************\
 * Frontend.hpp - A header file for the SLURM specific frontend interface.
 *
 * Copyright 2014-2019 Cray Inc. All Rights Reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
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

#pragma once

#include <stdint.h>
#include <sys/types.h>

#include <stdexcept>

#include "frontend/Frontend.hpp"

#include "useful/cti_wrappers.hpp"

// cti_srunProc_t extended to performs sanity checking upon construction
struct SrunInfo : public cti_srunProc_t {
    SrunInfo(uint32_t jobid, uint32_t stepid)
        : cti_srunProc_t{jobid, stepid}
    {
        // sanity check - Note that 0 is a valid step id.
        if (jobid == 0) {
            throw std::runtime_error("Invalid jobid " + std::to_string(jobid));
        }
    }
};

class SLURMFrontend final : public Frontend
{
public: // inherited interface
    static char const* getName()        { return CTI_WLM_TYPE_SLURM_STR; }
    static bool isSupported();

    cti_wlm_type_t getWLMType() const override { return CTI_WLM_SLURM; }

    std::weak_ptr<App> launchBarrier(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
        CStr inputFile, CStr chdirPath, CArgArray env_list) override;

    std::weak_ptr<App> registerJob(size_t numIds, ...) override;

    std::string getHostname() const override;

private: // slurm specific members
    // Arguments specified by CTI_SLURM_OVERRIDE / _APPEND for SRUN launches
    std::vector<std::string> m_srunAppArgs;
    // Also contains version-specific SRUN arguments
    std::vector<std::string> m_srunDaemonArgs;

public: // slurm specific types
    struct NodeLayout {
        std::string hostname;
        size_t numPEs; // number of PEs running on node
        size_t firstPE; // first PE number on this node
    };

    struct StepLayout {
        size_t numPEs; // number of PEs associated with job step
        std::vector<NodeLayout> nodes; // array of hosts
    };

public: // slurm specific interface
    auto const& getSrunAppArgs()    const { return m_srunAppArgs;    }
    auto const& getSrunDaemonArgs() const { return m_srunDaemonArgs; }

    // Get the default launcher binary name, or, if provided, from the environment.
    std::string getLauncherName();

    // use sattach to retrieve node / host information about a SLURM job
    /* sattach layout format:
    Job step layout:
      {numPEs} tasks, {numNodes} nodes ({hostname}...)
      <newline>
      Node {nodeNum} ({hostname}), {numPEs} task(s): PE_0 {PE_i }...
    */
    StepLayout fetchStepLayout(uint32_t job_id, uint32_t step_id);

    // Use a Slurm Step Layout to create the SLURM Node Layout file inside the staging directory, return the new path.
    std::string createNodeLayoutFile(StepLayout const& stepLayout, std::string const& stagePath);

    // Use an MPIR ProcTable to create the SLURM PID List file inside the staging directory, return the new path.
    std::string createPIDListFile(MPIRProctable const& procTable, std::string const& stagePath);

    // Launch a SLURM app under MPIR control and hold at SRUN barrier.
    FE_daemon::MPIRResult launchApp(const char * const launcher_argv[],
        const char *inputFile, int stdoutFd, int stderrFd, const char *chdirPath,
        const char * const env_list[]);

    // attach and read srun info
    SrunInfo getSrunInfo(pid_t srunPid);

public: // constructor / destructor interface
    SLURMFrontend();
    ~SLURMFrontend() = default;
    SLURMFrontend(const SLURMFrontend&) = delete;
    SLURMFrontend& operator=(const SLURMFrontend&) = delete;
    SLURMFrontend(SLURMFrontend&&) = delete;
    SLURMFrontend& operator=(SLURMFrontend&&) = delete;
};

class SLURMApp final : public App
{
private: // variables
    FE_daemon::DaemonAppId m_daemonAppId; // used for util registry and MPIR release
    uint32_t m_jobId;
    uint32_t m_stepId;
    SLURMFrontend::StepLayout m_stepLayout; // SLURM Layout of job step
    int      m_queuedOutFd; // Where to redirect stdout after barrier release
    int      m_queuedErrFd; // Where to redirect stderr after barrier release
    bool     m_beDaemonSent; // Have we already shipped over the backend daemon?

    std::string m_toolPath;    // Backend path where files are unpacked
    std::string m_attribsPath; // Backend Cray-specific directory
    std::string m_stagePath;   // Local directory where files are staged before transfer to BE
    std::vector<std::string> m_extraFiles; // List of extra support files to transfer to BE

private: // member helpers
    void redirectOutput(int stdoutFd, int stderrFd);

public: // app interaction interface
    std::string getJobId()            const override;
    std::string getLauncherHostname() const override;
    std::string getToolPath()         const override { return m_toolPath;    }
    std::string getAttribsPath()      const override { return m_attribsPath; }

    std::vector<std::string> getExtraFiles() const override { return m_extraFiles; }

    size_t getNumPEs()       const override { return m_stepLayout.numPEs;       }
    size_t getNumHosts()     const override { return m_stepLayout.nodes.size(); }
    std::vector<std::string> getHostnameList()   const override;
    std::vector<CTIHost>     getHostsPlacement() const override;

    void releaseBarrier() override;
    void kill(int signal) override;
    void shipPackage(std::string const& tarPath) const override;
    void startDaemon(const char* const args[]) override;

public: // slurm specific interface
    uint64_t getApid() const { return SLURM_APID(m_jobId, m_stepId); }
    SrunInfo getSrunInfo() const { return SrunInfo { m_jobId, m_stepId }; }

private: // delegated constructor
    SLURMApp(SLURMFrontend& fe, FE_daemon::MPIRResult&& mpirData);
public: // constructor / destructor interface
    // attach case
    SLURMApp(SLURMFrontend& fe, uint32_t jobid, uint32_t stepid);
    // launch case
    SLURMApp(SLURMFrontend& fe, const char * const launcher_argv[], int stdout_fd,
        int stderr_fd, const char *inputFile, const char *chdirPath, const char * const env_list[]);
    ~SLURMApp();
    SLURMApp(const SLURMApp&) = delete;
    SLURMApp& operator=(const SLURMApp&) = delete;
    SLURMApp(SLURMApp&&) = delete;
    SLURMApp& operator=(SLURMApp&&) = delete;
};
