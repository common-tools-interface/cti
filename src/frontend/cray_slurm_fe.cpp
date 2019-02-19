/******************************************************************************\
 * cray_slurm_fe.c - Cray SLURM specific frontend library functions.
 *
 * Copyright 2014-2019 Cray Inc.  All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
 *
 * $HeadURL$
 * $Date$
 * $Rev$
 * $Author$
 *
 ******************************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>


#include "cti_defs.h"
#include "cti_fe.h"

#include "frontend/Frontend.hpp"
#include "cray_slurm_fe.hpp"

#include "useful/make_unique.hpp"
#include "useful/strong_argv.hpp"
#include "useful/Dlopen.hpp"
#include "useful/cti_wrappers.hpp"

#include "mpir_iface/mpir_iface.h"

/* Types used here */

slurm_util::StepLayout::StepLayout(uint32_t jobid, uint32_t stepid)
{
	auto raw_layout = UniquePtrDestr<slurmStepLayout_t>
		{ _cti_cray_slurm_getLayout(jobid, stepid) // fetch from slurm_util helper
		, _cti_cray_slurm_freeLayout               // raw layout destructor
	};

	// extract PE and node information from raw layout
	numPEs = (size_t)raw_layout->numPEs;
	for (int i = 0; i < raw_layout->numNodes; ++i) {
		nodes.push_back(NodeLayout
			{ .hostname = std::string{raw_layout->hosts[i].host}
			, .numPEs   = (size_t)raw_layout->hosts[i].PEsHere
			, .firstPE  = (size_t)raw_layout->hosts[i].firstPE
		});
	}
}

/* functions and data that are present / expected in a slurm environment */
namespace slurm_conventions
{
	// Environment variables that are unset before exec'ing SLURM launcher
	auto const static blacklist_env_vars = std::vector<char const*>{
		"SLURM_CHECKPOINT",
		"SLURM_CONN_TYPE",
		"SLURM_CPUS_PER_TASK",
		"SLURM_DEPENDENCY",
		"SLURM_DIST_PLANESIZE",
		"SLURM_DISTRIBUTION",
		"SLURM_EPILOG",
		"SLURM_GEOMETRY",
		"SLURM_NETWORK",
		"SLURM_NPROCS",
		"SLURM_NTASKS",
		"SLURM_NTASKS_PER_CORE",
		"SLURM_NTASKS_PER_NODE",
		"SLURM_NTASKS_PER_SOCKET",
		"SLURM_PARTITION",
		"SLURM_PROLOG",
		"SLURM_REMOTE_CWD",
		"SLURM_REQ_SWITCH",
		"SLURM_RESV_PORTS",
		"SLURM_TASK_EPILOG",
		"SLURM_TASK_PROLOG",
		"SLURM_WORKING_DIR"
	};

	// Use a slurm_util Step Layout to create the SLURM Node Layout file inside the staging directory, return the new path.
	static std::string
	createNodeLayoutFile(slurm_util::StepLayout const& stepLayout, std::string const& stagePath)
	{
		// How a SLURM Node Layout File entry is created from a slurm_util Node Layout entry:
		auto make_layoutFileEntry = [](slurm_util::NodeLayout const& node) {
			// Ensure we have good hostname information.
			auto const hostname_len = node.hostname.size() + 1;
			if (hostname_len > sizeof(slurmLayoutFile_t::host)) {
				throw std::runtime_error("hostname too large for layout buffer");
			}

			// Extract PE and node information from Node Layout.
			auto layout_entry    = slurmLayoutFile_t{};
			layout_entry.PEsHere = node.numPEs;
			layout_entry.firstPE = node.firstPE;
			memcpy(layout_entry.host, node.hostname.c_str(), hostname_len);

			return layout_entry;
		};

		// Create the file path, write the file using the Step Layout
		auto const layoutPath = std::string{stagePath + "/" + SLURM_LAYOUT_FILE};
		if (auto const layoutFile = file::open(layoutPath, "wb")) {

			// Write the Layout header.
			file::writeT(layoutFile.get(), slurmLayoutFileHeader_t
				{ .numNodes = (int)stepLayout.nodes.size()
			});

			// Write a Layout entry using node information from each slurm_util Node Layout entry.
			for (auto const& node : stepLayout.nodes) {
				file::writeT(layoutFile.get(), make_layoutFileEntry(node));
			}

			return layoutPath;
		} else {
			throw std::runtime_error("failed to open layout file path " + layoutPath);
		}
	}

	// Use an MPIR ProcTable to create the SLURM PID List file inside the staging directory, return the new path.
	static std::string
	createPIDListFile(cti_mpir_procTable_t const& procTable, std::string const& stagePath)
	{
		auto const pidPath = std::string{stagePath + "/" + SLURM_PID_FILE};
		if (auto const pidFile = file::open(pidPath, "wb")) {

			// Write the PID List header.
			file::writeT(pidFile.get(), slurmPidFileHeader_t
				{ .numPids = (int)procTable.num_pids
			});

			// Write a PID entry using information from each MPIR ProcTable entry.
			for (size_t i = 0; i < procTable.num_pids; ++i) {
				file::writeT(pidFile.get(), slurmPidFile_t
					{ .pid = procTable.pids[i]
				});
			}

			return pidPath;
		} else {
			throw std::runtime_error("failed to open PID file path " + pidPath);
		}
	}

	// Get the default launcher binary name, or, if provided, from the environment.
	static std::string
	getLauncherName()
	{
		auto getenvOrDefault = [](char const* envVar, char const* defaultValue) {
			if (char const* envValue = getenv(envVar)) {
				return envValue;
			}
			return defaultValue;
		};

		// Cache the launcher name result.
		auto static launcherName = std::string{getenvOrDefault(CTI_LAUNCHER_NAME, SRUN)};
		return launcherName;
	}

	// Extract the SLURM Job ID from launcher memory using an existing MPIR control session. Required to exist.
	static uint32_t
	fetchJobId(mpir_id_t mpir_id)
	{
		if (auto const jobid_str = cstr::handle{_cti_mpir_getStringAt(mpir_id, "totalview_jobid")}) {
			return std::stoul(jobid_str.get());
		} else {
			throw std::runtime_error("failed to get jobid string via MPIR.");
		}
	}

	// Extract the SLURM Step ID from launcher memory using an existing MPIR control session. Optional, returns 0 on failure.
	static uint32_t
	fetchStepId(mpir_id_t mpir_id)
	{
		if (auto const stepid_str = cstr::handle{_cti_mpir_getStringAt(mpir_id, "totalview_stepid")}) {
			return std::stoul(stepid_str.get());
		} else {
			fprintf(stderr, "cti_fe: Warning: stepid not found! Defaulting to 0.\n");
			return 0;
		}
	}

	// Start a new MPIR attach session on a launcher PID; extract and return an SrunInfo containing Job and Step IDs.
	static SrunInfo
	attachAndFetchJobInfo(pid_t srunPid)
	{
		// sanity check
		if (srunPid <= 0) {
			throw std::runtime_error("Invalid srunPid " + std::to_string(srunPid));
		}

		// Find the launcher path from the launcher name using helper _cti_pathFind.
		if (auto const launcherPath = cstr::handle{_cti_pathFind(slurm_conventions::getLauncherName().c_str(), nullptr)}) {

			// Start a new MPIR attach session on the provided PID using symbols from the launcher.
			auto const mpir_id = _cti_mpir_newAttachInstance(launcherPath.get(), srunPid);

			// Validate MPIR session, extract Job and Step IDs.
			if (mpir_id >= 0) {
				return SrunInfo
					{ .jobid  = fetchJobId(mpir_id)
					, .stepid = fetchStepId(mpir_id)
				};
			} else {
				throw std::runtime_error("Failed to create MPIR attach instance.");
			}

		} else {
			throw std::runtime_error("Failed to find launcher in path: " + slurm_conventions::getLauncherName());
		}
	}

	// Look in ALPS_XT_NID for the current node's hostname, otherwise fall back to gethostname().
	static std::string
	getHostname(void)
	{
		auto tryParseHostnameFile = [](char const* filePath) {
			if (auto nidFile = file::try_open(filePath, "r")) {
				int nid;
				{ // We expect this file to have a numeric value giving our current Node ID.
					char buf[BUFSIZ];
					if (fgets(buf, BUFSIZ, nidFile.get()) == nullptr) {
						throw std::runtime_error("_cti_cray_slurm_getHostname fgets failed.");
					}
					nid = std::stoi(std::string{buf});
				}

				// Use the NID to create the standard hostname format.
				return cstr::asprintf(ALPS_XT_HOSTNAME_FMT, nid);

			} else {
				return cstr::gethostname();
			}
		};

		// Cache the hostname result.
		static auto hostname = tryParseHostnameFile(ALPS_XT_NID);
		return hostname;
	}

	// Launch a SLURM app under MPIR control and hold at barrier.
	static mpir_id_t
	launchApp(const char * const launcher_argv[], int stdout_fd, int stderr_fd,
		const char *inputFile, const char *chdirPath, const char * const env_list[])
	{
		// Open input file (or /dev/null to avoid stdin contention).
		auto openFileOrDevNull = [&](char const* inputFile) {
			int input_fd = -1;
			if (inputFile == nullptr) {
				inputFile = "/dev/null";
			}
			errno = 0;
			input_fd = open(inputFile, O_RDONLY);
			if (input_fd < 0) {
				throw std::runtime_error("Failed to open input file " + std::string(inputFile) +": " + std::string(strerror(errno)));
			}

			return input_fd;
		};

		// Get the launcher path from CTI environment variable / default.
		if (auto const launcher_path = cstr::handle{_cti_pathFind(getLauncherName().c_str(), nullptr)}) {

			// Launch program under MPIR control.
			auto const mpir_id = _cti_mpir_newLaunchInstance(launcher_path.get(),
				launcher_argv, env_list, openFileOrDevNull(inputFile), stdout_fd, stderr_fd);

			if (mpir_id >= 0) {
				return mpir_id;

			} else { // failed to launch program under mpir control
				std::string argvString;
				for (const char* const* arg = launcher_argv; *arg != nullptr; arg++) {
					argvString.push_back(' ');
					argvString += *arg;
				}
				throw std::runtime_error("Failed to launch: " + argvString);
			}

		} else {
			throw std::runtime_error("Failed to find launcher in path: " + slurm_conventions::getLauncherName());
		}
	}
}

/* constructors / destructors */

CraySLURMApp::CraySLURMApp(uint32_t jobid, uint32_t stepid, mpir_id_t mpir_id)
	: srunInfo    { jobid, stepid }
	, stepLayout  { jobid, stepid }
	, barrier     { mpir_id }
	, dlaunchSent { false }

	, toolPath    { CRAY_SLURM_TOOL_DIR }
	, attribsPath { cstr::asprintf(CRAY_SLURM_CRAY_DIR, CRAY_SLURM_APID(jobid, stepid)) }
	, stagePath   { cstr::mkdtemp(std::string{_cti_getCfgDir() + "/" + SLURM_STAGE_DIR}) }
	, extraFiles  { slurm_conventions::createNodeLayoutFile(stepLayout, stagePath) }
{
	// Ensure there are running nodes in the job.
	if (stepLayout.nodes.empty()) {
		throw std::runtime_error("Application " + getJobId() + " does not have any nodes.");
	}

	// If an active MPIR session was provided, extract the MPIR ProcTable and write the PID List File.
	if (barrier) {
		// FIXME: When/if pmi_attribs get fixed for the slurm startup barrier, this
		// call can be removed. Right now the pmi_attribs file is created in the pmi
		// ctor, which is called after the slurm startup barrier, meaning it will not
		// yet be created when launching. So we need to send over a file containing
		// the information to the compute nodes.
		if (auto const procTable = UniquePtrDestr<cti_mpir_procTable_t>
			{ _cti_mpir_newProcTable(barrier.get())
			, _cti_mpir_deleteProcTable
		}) {
			extraFiles.push_back(slurm_conventions::createPIDListFile(*procTable, stagePath));
		} else {
			throw std::runtime_error("failed to get MPIR proctable from mpir id " + std::to_string(barrier.get()));
		}
	}
}

CraySLURMApp::CraySLURMApp(CraySLURMApp&& moved)
	: srunInfo    { moved.srunInfo }
	, stepLayout  { moved.stepLayout }
	, barrier     { std::move(moved.barrier) }
	, dlaunchSent { moved.dlaunchSent }

	, toolPath    { moved.toolPath }
	, attribsPath { moved.attribsPath }
	, stagePath   { moved.stagePath }
	, extraFiles  { moved.extraFiles }
{
	// We have taken ownership of the staging path, so don't let moved delete the directory.
	moved.stagePath.erase();
}

CraySLURMApp::~CraySLURMApp()
{
	// Delete the staging directory if it exists.
	if (!stagePath.empty()) {
		_cti_removeDirectory(stagePath.c_str());
	}
}

/* app instance creation */

CraySLURMApp::CraySLURMApp(uint32_t jobid, uint32_t stepid)
	: CraySLURMApp{jobid, stepid, mpir_id_t{-1}}
{}

CraySLURMApp::CraySLURMApp(mpir_id_t mpir_id)
	: CraySLURMApp
		{ slurm_conventions::fetchJobId(mpir_id)
		, slurm_conventions::fetchStepId(mpir_id)
		, mpir_id
	}
{}

CraySLURMApp::CraySLURMApp(const char * const launcher_argv[], int stdout_fd, int stderr_fd,
	const char *inputFile, const char *chdirPath, const char * const env_list[])
	: CraySLURMApp{slurm_conventions::launchApp(launcher_argv, stdout_fd, stderr_fd, inputFile,
		chdirPath, env_list)}
{}

/* running app info accessors */

// Note that we should provide this as a jobid.stepid format. It will make turning
// it into a Cray apid easier on the backend since we don't lose any information
// with this format.
std::string
CraySLURMApp::getJobId() const
{
	return std::string{std::to_string(srunInfo.jobid) + "." + std::to_string(srunInfo.stepid)};
}

std::string
CraySLURMApp::getLauncherHostname() const
{
	throw std::runtime_error("not supported for Cray-SLURM: getLauncherHostname");
}

#include <algorithm>

std::vector<std::string>
CraySLURMApp::getHostnameList() const
{
	std::vector<std::string> result;
	// extract hostnames from each NodeLayout
	std::transform(stepLayout.nodes.begin(), stepLayout.nodes.end(), std::back_inserter(result),
		[](slurm_util::NodeLayout const& node) { return node.hostname; });
	return result;
}

std::vector<CTIHost>
CraySLURMApp::getHostsPlacement() const
{
	std::vector<CTIHost> result;
	// construct a CTIHost from each NodeLayout
	std::transform(stepLayout.nodes.begin(), stepLayout.nodes.end(), std::back_inserter(result),
		[](slurm_util::NodeLayout const& node) {
			return CTIHost{node.hostname, node.numPEs};
		});
	return result;
}

/* running app interaction interface */

void CraySLURMApp::releaseBarrier() {
	if (!barrier) {
		throw std::runtime_error("app not under MPIR control");
	}
	barrier.reset();
}

void CraySLURMApp::kill(int signum)
{
	// create the args for scancel
	auto scancelArgv = ManagedArgv
		{ SCANCEL // first argument should be "scancel"
		, "-Q"    // second argument is quiet
		, "-s", std::to_string(signum)    // third argument is signal number
		, getJobId() // fourth argument is the jobid.stepid
	};

	// fork off a process to launch scancel
	if (auto const scancelPid = fork()) {
		if (scancelPid < 0) {
			throw std::runtime_error("fork failed");
		}

		// parent case: wait until the scancel finishes
		waitpid(scancelPid, nullptr, 0);

	} else {
		// child case: exec scancel
		execvp(SCANCEL, scancelArgv.get());

		// exec shouldn't return
		fprintf(stderr, "CTI error: Return from exec.\n");
		perror("execvp");
		_exit(1);
	}
}

void CraySLURMApp::shipPackage(std::string const& tarPath) const {
	// create the args for sbcast
	auto launcherArgv = ManagedArgv
		{ SBCAST
		, "-C"
		, "-j", std::to_string(srunInfo.jobid)
		, tarPath
		, "--force"
	};

	if (auto packageName = cstr::handle{_cti_pathToName(tarPath.c_str())}) {
		launcherArgv.add(std::string(CRAY_SLURM_TOOL_DIR) + "/" + packageName.get());
	} else {
		throw std::runtime_error("_cti_pathToName failed");
	}

	// now ship the tarball to the compute nodes. fork off a process to launch sbcast
	if (auto const forkedPid = fork()) {
		if (forkedPid < 0) {
			throw std::runtime_error("fork failed");
		}

		// parent case: wait until the sbcast finishes

		// FIXME: There is no way to error check right now because the sbcast command
		// can only send to an entire job, not individual job steps. The /var/spool/alps/<apid>
		// directory will only exist on nodes associated with this particular job step, and the
		// sbcast command will exit with error if the directory doesn't exist even if the transfer
		// worked on the nodes associated with the step. I opened schedmd BUG 1151 for this issue.
		waitpid(forkedPid, nullptr, 0);

	} else {
		// child case: redirect stdin, stdout, stderr to /dev/null
		int devNullFd = open("/dev/null", O_RDONLY);
		dup2(devNullFd, STDIN_FILENO);
		dup2(devNullFd, STDOUT_FILENO);
		dup2(devNullFd, STDERR_FILENO);

		// exec sbcast
		execvp(SBCAST, launcherArgv.get());

		// exec shouldn't return
		fprintf(stderr, "CTI error: Return from exec.\n");
		perror("execvp");
		_exit(1);
	}
}

void CraySLURMApp::startDaemon(const char* const args[]) {
	// sanity check
	if (args == nullptr) {
		throw std::runtime_error("args array is null!");
	}

	// get max number of file descriptors - used later
	auto max_fd = size_t{};
	{ struct rlimit rl;
		if (getrlimit(RLIMIT_NOFILE, &rl) < 0) {
			throw std::runtime_error("getrlimit failed.");
		} else {
			max_fd = (rl.rlim_max == RLIM_INFINITY) ? 1024 : rl.rlim_max;
		}
	}

	// If we have not yet transfered the dlaunch binary, we need to do that in advance with
	// native slurm
	if (!dlaunchSent) {
		// Get the location of the daemon launcher
		if (_cti_getDlaunchPath().empty()) {
			throw std::runtime_error("Required environment variable not set:" + std::string(BASE_DIR_ENV_VAR));
		}

		shipPackage(_cti_getDlaunchPath());

		// set transfer to true
		dlaunchSent = true;
	}

	// use existing launcher binary on compute node
	std::string const launcherPath(toolPath + "/" + CTI_LAUNCHER);

	// Start adding the args to the launcher argv array
	//
	// This corresponds to:
	//
	// srun --jobid=<job_id> --gres=none --mem-per-cpu=0 --mem_bind=no
	// --cpu_bind=no --share --ntasks-per-node=1 --nodes=<numNodes>
	// --nodelist=<host1,host2,...> --disable-status --quiet --mpi=none 
	// --input=none --output=none --error=none <tool daemon> <args>
	//
	auto launcherArgv = ManagedArgv
		{ slurm_conventions::getLauncherName()
		, "--jobid=" + std::to_string(srunInfo.jobid)
		, "--gres=none"
		, "--mem-per-cpu=0"
		, "--mem_bind=no"
		, "--cpu_bind=no"
		, "--share"
		, "--ntasks-per-node=1"
		, "--nodes=" + std::to_string(stepLayout.nodes.size())
		, "--disable-status"
		, "--quiet"
		, "--mpi=none"
		, "--output=none"
		, "--error=none"
	};

	// create the hostlist by contencating all hostnames
	{ auto hostlist = std::string{};
		bool firstHost = true;
		for (auto const& node : stepLayout.nodes) {
			hostlist += (firstHost ? "" : ",") + node.hostname;
			firstHost = false;
		}
		launcherArgv.add("--nodelist=" + hostlist);
	}

	launcherArgv.add(launcherPath);

	// merge in the args array if there is one
	if (args != nullptr) {
		for (const char* const* arg = args; *arg != nullptr; arg++) {
			launcherArgv.add(*arg);
		}
	}

	// fork off a process to launch srun
	if (auto const forkedPid = fork()) {
		if (forkedPid < 0) {
			throw std::runtime_error("fork failed");
		}

		// parent case: place the child in its own group.
		setpgid(forkedPid, forkedPid);

	} else {
		// child case: Place this process in its own group to prevent signals being passed
		// to it. This is necessary in case the child code execs before the 
		// parent can put us into our own group.
		setpgid(0, 0);

		// dup2 all stdin/out/err to /dev/null
		auto const devNullFd = open("/dev/null", O_RDONLY);
		dup2(devNullFd, STDIN_FILENO);
		dup2(devNullFd, STDOUT_FILENO);
		dup2(devNullFd, STDERR_FILENO);

		// close all open file descriptors above STDERR
		for (size_t i = 3; i < max_fd; ++i) {
			close(i);
		}

		// clear out the blacklisted slurm env vars to ensure we don't get weird behavior
		for (auto const& envVar : slurm_conventions::blacklist_env_vars) {
			unsetenv(envVar);
		}

		// exec srun
		execvp(slurm_conventions::getLauncherName().c_str(), launcherArgv.get());

		// exec shouldn't return
		fprintf(stderr, "CTI error: Return from exec.\n");
		perror("execvp");
		_exit(1);
	}
}

/* cray slurm frontend implementation */

Frontend::AppId
CraySLURMFrontend::launchBarrier(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
	                             CStr inputFile, CStr chdirPath, CArgArray env_list)
{
	return registerAppPtr(shim::make_unique<CraySLURMApp>(launcher_argv, stdout_fd, stderr_fd, inputFile,
		chdirPath, env_list));
}

std::string
CraySLURMFrontend::getHostname() const
{
	return slurm_conventions::getHostname();
}

Frontend::AppId
CraySLURMFrontend::registerJobStep(uint32_t jobid, uint32_t stepid) {
	return registerAppPtr(shim::make_unique<CraySLURMApp>(jobid, stepid));
}

SrunInfo
CraySLURMFrontend::getSrunInfo(pid_t srunPid) {
	return slurm_conventions::attachAndFetchJobInfo(srunPid);
}