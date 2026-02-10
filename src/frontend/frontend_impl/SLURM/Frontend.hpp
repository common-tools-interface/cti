/******************************************************************************\
 * Frontend.hpp - A header file for the SLURM specific frontend interface.
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
 * SPDX-License-Identifier: Linux-OpenIB
 ******************************************************************************/

#pragma once

#include <stdint.h>
#include <sys/types.h>

#include <stdexcept>
#include <optional>

#include "frontend/Frontend.hpp"

#include "useful/cti_wrappers.hpp"
#include "useful/cti_argv.hpp"

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

class SLURMFrontend : public Frontend
{
public: // inherited interface
    static char const* getName()        { return CTI_WLM_TYPE_SLURM_STR; }

    cti_wlm_type_t getWLMType() const override { return CTI_WLM_SLURM; }

    std::weak_ptr<App> launch(CArgArray launcher_args, int stdout_fd, int stderr_fd,
        CStr inputFile, CStr chdirPath, CArgArray env_list) override;

    std::weak_ptr<App> launchBarrier(CArgArray launcher_args, int stdout_fd, int stderr_fd,
        CStr inputFile, CStr chdirPath, CArgArray env_list) override;

    std::weak_ptr<App> registerJob(size_t numIds, ...) override;

    std::string getHostname() const override;

private: // slurm specific members
    // Arguments specified by CTI_SLURM_OVERRIDE / _APPEND for SRUN launches
    std::vector<std::string> m_srunAppArgs;
    // Also contains version-specific SRUN arguments
    std::vector<std::string> m_srunDaemonArgs;
    // Whether scancel needs output parsed to check for successful signal (PE-45772)
    bool m_checkScancelOutput;
    // Whether job status should be queried after launch (CPE-14234)
    bool m_checkTaskPrologStatus;

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

    struct HetJobId {
        uint32_t job_id;
        uint32_t step_id;
        std::optional<uint32_t> het_offset;
        bool in_salloc;
        bool is_batch_step, is_extern_step;

        // sattach requires appending the hetjob offset to the base job ID
        auto get_sattach_id() const {
            if (het_offset) {
                if (in_salloc) {
                    return std::to_string(job_id) + "." + std::to_string(step_id) + "+" + std::to_string(*het_offset);
                } else {
                    return std::to_string(job_id + *het_offset) + "." + std::to_string(step_id);
                }
            } else {
                return std::to_string(job_id) + "." + std::to_string(step_id);
            }
        }

        // srun for tool daemon launch requires manually specifying the hetjob offset
        auto get_srun_id() const {
            return (het_offset)
                ? std::to_string(job_id) + "+" + std::to_string(*het_offset) + "." + std::to_string(step_id)
                : std::to_string(job_id) + "." + std::to_string(step_id);
        }

        // sbcast and scancel will handle hetjob topology when passed the base job ID
        auto get_sbcast_scancel_id() const {
            return std::to_string(job_id) + "." + std::to_string(step_id);
        }

        auto strs() const {
            return std::make_pair(std::to_string(job_id), std::to_string(step_id));
        }
    };

public: // slurm specific interface
    auto const& getSrunAppArgs()    const { return m_srunAppArgs;    }
    auto const& getSrunDaemonArgs() const { return m_srunDaemonArgs; }
    std::vector<std::string> const& getSrunEnvBlacklist() const;
    auto const& getCheckScancelOutput() const { return m_checkScancelOutput; }
    auto const& getCheckTaskPrologStatus() const { return m_checkTaskPrologStatus; }

    // Get the default launcher binary name, or, if provided, from the environment.
    std::string getLauncherName();

    // use sattach to retrieve node / host information about a SLURM job
    /* sattach layout format:
    Job step layout:
      {numPEs} tasks, {numNodes} nodes ({hostname}...)
      <newline>
      Node {nodeNum} ({hostname}), {numPEs} task(s): PE_0 {PE_i }...
    */
    std::map<std::string, StepLayout> fetchStepLayout(std::vector<HetJobId> const& jobIds);

    // Use a Slurm Step Layout to create the SLURM Node Layout file inside the staging directory, return the new path.
    std::string createNodeLayoutFile(std::map<std::string, StepLayout> const& idLayout,
        std::string const& stagePath);

    // Use an MPIR ProcTable to create the SLURM PID List file inside the staging directory, return the new path.
    std::string createPIDListFile(MPIRProctable const& procTable, std::string const& stagePath);

    // Launch a SLURM app under MPIR control and hold at SRUN barrier.
    FE_daemon::MPIRResult launchApp(const char * const launcher_args[],
        const char *inputFile, int stdoutFd, int stderrFd, const char *chdirPath,
        const char * const env_list[]);

    // attach and read srun info
    SrunInfo getSrunInfo(pid_t srunPid);

    // Submit batch script with sbatch
    SrunInfo submitBatchScript(std::string const& scriptPath,
        char const* const* sbatch_args, char const* const* env_list);

public: // constructor / destructor interface
    SLURMFrontend();
    ~SLURMFrontend() = default;
    SLURMFrontend(const SLURMFrontend&) = delete;
    SLURMFrontend& operator=(const SLURMFrontend&) = delete;
    SLURMFrontend(SLURMFrontend&&) = delete;
    SLURMFrontend& operator=(SLURMFrontend&&) = delete;

public: // unit testable
struct detail
{
    static std::string get_gres_setting(char const* const* launcher_argv);
    static void add_quoted_args(cti::ManagedArgv& args, std::string const& quotedArgs);
};

};

class SLURMApp : public App
{
private: // variables
    uint32_t m_jobId;
    uint32_t m_stepId;
    std::map<std::string, std::vector<int>> m_binaryRankMap; // Binary to rank ID map
    std::vector<SLURMFrontend::HetJobId> m_allJobIds; // Extended list of job IDs including hetjob offsets
    std::map<std::string, SLURMFrontend::StepLayout> m_stepLayout; // Map job ID to layout
    int      m_queuedOutFd; // Where to redirect stdout after barrier release
    int      m_queuedErrFd; // Where to redirect stderr after barrier release

    bool     m_beDaemonSent; // Have we already shipped over the backend daemon?
    std::string m_gresSetting; // Tool daemon GRES

    std::string m_toolPath;    // Backend path where files are unpacked
    std::string m_attribsPath; // Backend Cray-specific directory
    std::string m_stagePath;   // Local directory where files are staged before transfer to BE
    std::vector<std::string> m_extraFiles; // List of extra support files to transfer to BE

private: // member helpers
    void shipDaemon();
    cti::ManagedArgv generateDaemonLauncherArgv(char const* const* launcher_args);

public: // app interaction interface
    std::string getJobId()            const override;
    std::string getLauncherHostname() const override;
    std::string getToolPath()         const override { return m_toolPath;    }
    std::string getAttribsPath()      const override { return m_attribsPath; }

    std::vector<std::string> getExtraFiles() const override { return m_extraFiles; }

    bool   isRunning()       const override;
    size_t getNumPEs()       const override;
    size_t getNumHosts()     const override;
    std::vector<std::string> getHostnameList()   const override;
    std::vector<CTIHost>     getHostsPlacement() const override;
    std::map<std::string, std::vector<int>> getBinaryRankMap() const override;

    void releaseBarrier() override;
    void kill(int signal) override;
    void shipPackage(std::string const& tarPath) const override;
    void startDaemon(const char* const args[], bool synchronous) override;
    std::set<std::string> checkFilesExist(std::set<std::string> const& paths) override;

public: // slurm specific interface
    uint64_t getApid() const { return SLURM_APID(m_jobId, m_stepId); }
    SrunInfo getSrunInfo() const { return SrunInfo { m_jobId, m_stepId }; }

    // Regenerate MPIR proctable where each job binary is running under the provided
    // wrapper binary (e.g. running inside Singulary container)
    MPIRProctable reparentProctable(MPIRProctable const& procTable, std::string const& wrapperBinary);

    // Set GRES for subsequent tool daemon launches
    void setGres(std::string const& gresSetting) { m_gresSetting = gresSetting; }

public: // constructor / destructor interface
    SLURMApp(SLURMFrontend& fe, FE_daemon::MPIRResult&& mpirData,
        std::string jobId = {}, std::string stepId = {});
    ~SLURMApp();
    SLURMApp(const SLURMApp&) = delete;
    SLURMApp& operator=(const SLURMApp&) = delete;
    SLURMApp(SLURMApp&&) = delete;
    SLURMApp& operator=(SLURMApp&&) = delete;
};

class HPCMSLURMFrontend : public SLURMFrontend
{
public: // interface
    std::string getHostname() const override;
};

// Forward declare from SSHSession
class RemoteDaemon;
class EproxySLURMApp;

class EproxySLURMFrontend : public SLURMFrontend
{
public: // types
friend EproxySLURMApp;

struct EproxyEnvSpec
{
    std::set<std::string> m_includeVars, m_includePrefixes;
    std::set<std::string> m_excludeVars, m_excludePrefixes;
    bool m_includeAll;

    EproxyEnvSpec();
    EproxyEnvSpec(std::string const& path);
    void readFrom(std::istream& envStream);
    bool included(std::string const& var);
};

private: // members
    std::string m_eproxyLogin, m_eproxyKeyfile, m_eproxyEnvfile, m_eproxyUser, m_eproxyPrefix;
    std::string m_homeDir;
    EproxyEnvSpec m_envSpec;

private: // helpers
    std::tuple<std::unique_ptr<RemoteDaemon>, FE_daemon::MPIRResult>
    launchApp(const char * const launcher_args[],
        const char *inputFile, int stdoutFd, int stderrFd, const char *chdirPath,
        const char * const env_list[]);

public: // interface
    EproxySLURMFrontend();
    ~EproxySLURMFrontend();

    std::weak_ptr<App> launch(CArgArray launcher_args, int stdout_fd, int stderr_fd,
        CStr inputFile, CStr chdirPath, CArgArray env_list) override;

    std::weak_ptr<App> launchBarrier(CArgArray launcher_args, int stdout_fd, int stderr_fd,
        CStr inputFile, CStr chdirPath, CArgArray env_list) override;

    std::weak_ptr<App> registerJob(size_t numIds, ...) override;
};

class EproxySLURMApp : public SLURMApp
{
private: // members
    std::unique_ptr<RemoteDaemon> m_remoteDaemon;
    int m_remoteMpirId;
    std::string m_eproxyLogin;
    std::string m_eproxyUser;
    std::string m_homeDir;

public: // interface
    void releaseBarrier() override;
    bool isRunning() const override;
    void shipPackage(std::string const& tarPath) const override;

public: // constructor / destructor interface
    EproxySLURMApp(EproxySLURMFrontend& fe,
        std::unique_ptr<RemoteDaemon>&& remoteDaemon, FE_daemon::MPIRResult&& mpirData,
        std::string const& jobId, std::string const& stepId);
    ~EproxySLURMApp();
    EproxySLURMApp(const EproxySLURMApp&) = delete;
    EproxySLURMApp& operator=(const EproxySLURMApp&) = delete;
    EproxySLURMApp(EproxySLURMApp&&) = delete;
    EproxySLURMApp& operator=(EproxySLURMApp&&) = delete;
};
