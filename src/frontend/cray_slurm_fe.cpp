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
#include "ArgvDefs.hpp"
#include "cti_fe_iface.h"

#include "frontend/Frontend.hpp"
#include "cray_slurm_fe.hpp"

#include "useful/Dlopen.hpp"
#include "useful/ExecvpOutput.hpp"
#include "useful/cti_argv.hpp"
#include "useful/cti_split.hpp"
#include "useful/cti_wrappers.hpp"

#include "mpir_iface/MPIRInstance.hpp"

/* constructors / destructors */

CraySLURMApp::CraySLURMApp(uint32_t jobid, uint32_t stepid, std::unique_ptr<MPIRInstance>&& srunInstance)
	: m_srunInfo    { jobid, stepid }
	, m_stepLayout  { CraySLURMFrontend::fetchStepLayout(jobid, stepid) }
	, m_queuedOutFd { -1 }
	, m_queuedErrFd { -1 }
	, m_dlaunchSent { false }
	, m_sattachPids { }

	, m_srunInstance{ std::move(srunInstance) }

	, m_toolPath    { CRAY_SLURM_TOOL_DIR }
	, m_attribsPath { cstr::asprintf(CRAY_SLURM_CRAY_DIR, CRAY_SLURM_APID(jobid, stepid)) }
	, m_stagePath   { cstr::mkdtemp(std::string{_cti_getCfgDir() + "/" + SLURM_STAGE_DIR}) }
	, m_extraFiles  { CraySLURMFrontend::createNodeLayoutFile(m_stepLayout, m_stagePath) }

{
	// Ensure there are running nodes in the job.
	if (m_stepLayout.nodes.empty()) {
		throw std::runtime_error("Application " + getJobId() + " does not have any nodes.");
	}

	// If an active MPIR session was provided, extract the MPIR ProcTable and write the PID List File.
	if (m_srunInstance) {
		// FIXME: When/if pmi_attribs get fixed for the slurm startup barrier, this
		// call can be removed. Right now the pmi_attribs file is created in the pmi
		// ctor, which is called after the slurm startup barrier, meaning it will not
		// yet be created when launching. So we need to send over a file containing
		// the information to the compute nodes.
		m_extraFiles.push_back(CraySLURMFrontend::createPIDListFile(m_srunInstance->getProcTable(), m_stagePath));
	}
}

CraySLURMApp::CraySLURMApp(CraySLURMApp&& moved)
	: m_srunInfo    { moved.m_srunInfo }
	, m_stepLayout  { moved.m_stepLayout }
	, m_queuedOutFd { moved.m_queuedOutFd }
	, m_queuedErrFd { moved.m_queuedErrFd }
	, m_sattachPids { moved.m_sattachPids }
	, m_dlaunchSent { moved.m_dlaunchSent }

	, m_srunInstance{ std::move(moved.m_srunInstance) }

	, m_toolPath    { moved.m_toolPath }
	, m_attribsPath { moved.m_attribsPath }
	, m_stagePath   { moved.m_stagePath }
	, m_extraFiles  { moved.m_extraFiles }
{
	// we taken ownership of the running sattach redirections, if any
	moved.m_sattachPids.clear();

	// We have taken ownership of the staging path, so don't let moved delete the directory.
	moved.m_stagePath.erase();
}

CraySLURMApp::~CraySLURMApp()
{
	// kill and wait for sattach to exit
	for (auto&& sattachPid : m_sattachPids) {
		::kill(sattachPid, DEFAULT_SIG);
		waitpid(sattachPid, nullptr, 0);
	}

	// Delete the staging directory if it exists.
	if (!m_stagePath.empty()) {
		_cti_removeDirectory(m_stagePath.c_str());
	}
}

/* app instance creation */

CraySLURMApp::CraySLURMApp(uint32_t jobid, uint32_t stepid)
	: CraySLURMApp{jobid, stepid, nullptr}
{}

CraySLURMApp::CraySLURMApp(std::unique_ptr<MPIRInstance>&& srunInstance)
	: CraySLURMApp
		{ CraySLURMFrontend::fetchJobId(*srunInstance)
		, CraySLURMFrontend::fetchStepId(*srunInstance)
		, std::move(srunInstance)
	}
{}

CraySLURMApp::CraySLURMApp(const char * const launcher_argv[], int stdout_fd, int stderr_fd,
	const char *inputFile, const char *chdirPath, const char * const env_list[])
	: CraySLURMApp{ CraySLURMFrontend::launchApp(launcher_argv, inputFile, chdirPath, env_list) }
{
	// these FDs need to be remapped using sattach after barrier release
	m_queuedOutFd = stdout_fd;
	m_queuedErrFd = stderr_fd;
}

/* running app info accessors */

// Note that we should provide this as a jobid.stepid format. It will make turning
// it into a Cray apid easier on the backend since we don't lose any information
// with this format.
std::string
CraySLURMApp::getJobId() const
{
	return std::string{std::to_string(m_srunInfo.jobid) + "." + std::to_string(m_srunInfo.stepid)};
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
	std::transform(m_stepLayout.nodes.begin(), m_stepLayout.nodes.end(), std::back_inserter(result),
		[](CraySLURMFrontend::NodeLayout const& node) { return node.hostname; });
	return result;
}

std::vector<CTIHost>
CraySLURMApp::getHostsPlacement() const
{
	std::vector<CTIHost> result;
	// construct a CTIHost from each NodeLayout
	std::transform(m_stepLayout.nodes.begin(), m_stepLayout.nodes.end(), std::back_inserter(result),
		[](CraySLURMFrontend::NodeLayout const& node) {
			return CTIHost{node.hostname, node.numPEs};
		});
	return result;
}

/* running app interaction interface */

void CraySLURMApp::releaseBarrier() {
	// release MPIR barrier if applicable
	if (!m_srunInstance) {
		throw std::runtime_error("app not under MPIR control");
	}
	m_srunInstance.reset();

	// redirect output to proper FDs now that app is fully launched
	if ((m_queuedOutFd >= 0) && (m_queuedErrFd >= 0)) {
		redirectOutput(m_queuedOutFd, m_queuedErrFd);
		m_queuedOutFd = -1;
		m_queuedErrFd = -1;
	}
}

void
CraySLURMApp::redirectOutput(int stdoutFd, int stderrFd)
{
	// create the sattach process to redirect output
	if (auto const sattachPid = fork()) {
		if (sattachPid < 0) {
			throw std::runtime_error("fork failed");
		}

		// add to app list of active sattach
		m_sattachPids.push_back(sattachPid);
	} else {
		// create sattach argv
		ManagedArgv sattachArgv
			{ SATTACH // first argument should be "sattach"
			// , "-Q"    // second argument is quiet
			, getJobId() // third argument is the jobid.stepid
		};

		// redirect stdin / stderr / stdout
		int devNullFd = open("/dev/null", O_RDONLY);
		dup2(devNullFd, STDIN_FILENO);
		dup2(stdoutFd, STDOUT_FILENO);
		dup2(stderrFd, STDERR_FILENO);

		// child case: exec sattach
		execvp(SATTACH, sattachArgv.get());

		// exec shouldn't return
		fprintf(stderr, "CTI error: Return from exec.\n");
		perror("execvp");
		_exit(1);
	}
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
		, "-j", std::to_string(m_srunInfo.jobid)
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
	if (!m_dlaunchSent) {
		// Get the location of the daemon launcher
		if (_cti_getDlaunchPath().empty()) {
			throw std::runtime_error("Required environment variable not set:" + std::string(BASE_DIR_ENV_VAR));
		}

		shipPackage(_cti_getDlaunchPath());

		// set transfer to true
		m_dlaunchSent = true;
	}

	// use existing launcher binary on compute node
	std::string const launcherPath(m_toolPath + "/" + CTI_LAUNCHER);

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
		{ CraySLURMFrontend::getLauncherName()
		, "--jobid=" + std::to_string(m_srunInfo.jobid)
		, "--gres=none"
		, "--mem-per-cpu=0"
		, "--mem_bind=no"
		, "--cpu_bind=no"
		, "--share"
		, "--ntasks-per-node=1"
		, "--nodes=" + std::to_string(m_stepLayout.nodes.size())
		, "--disable-status"
		, "--quiet"
		, "--mpi=none"
		, "--output=none"
		, "--error=none"
	};

	// create the hostlist by contencating all hostnames
	{ auto hostlist = std::string{};
		bool firstHost = true;
		for (auto const& node : m_stepLayout.nodes) {
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
		auto const envVarBlacklist = std::vector<char const*>{
			"SLURM_CHECKPOINT",      "SLURM_CONN_TYPE",         "SLURM_CPUS_PER_TASK",
			"SLURM_DEPENDENCY",      "SLURM_DIST_PLANESIZE",    "SLURM_DISTRIBUTION",
			"SLURM_EPILOG",          "SLURM_GEOMETRY",          "SLURM_NETWORK",
			"SLURM_NPROCS",          "SLURM_NTASKS",            "SLURM_NTASKS_PER_CORE",
			"SLURM_NTASKS_PER_NODE", "SLURM_NTASKS_PER_SOCKET", "SLURM_PARTITION",
			"SLURM_PROLOG",          "SLURM_REMOTE_CWD",        "SLURM_REQ_SWITCH",
			"SLURM_RESV_PORTS",      "SLURM_TASK_EPILOG",       "SLURM_TASK_PROLOG",
			"SLURM_WORKING_DIR"
		};
		for (auto const& envVar : envVarBlacklist) {
			unsetenv(envVar);
		}

		// exec srun
		execvp(CraySLURMFrontend::getLauncherName().c_str(), launcherArgv.get());

		// exec shouldn't return
		fprintf(stderr, "CTI error: Return from exec.\n");
		perror("execvp");
		_exit(1);
	}
}

/* cray slurm frontend implementation */

std::unique_ptr<App>
CraySLURMFrontend::launchBarrier(CArgArray launcher_argv, int stdout_fd, int stderr_fd,
	CStr inputFile, CStr chdirPath, CArgArray env_list)
{
	return std::make_unique<CraySLURMApp>(launcher_argv, stdout_fd, stderr_fd, inputFile,
		chdirPath, env_list);
}

std::string
CraySLURMFrontend::getHostname() const
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
			return cstr::asprintf(CRAY_HOSTNAME_FMT, nid);

		} else {
			return cstr::gethostname();
		}
	};

	// Cache the hostname result.
	static auto hostname = tryParseHostnameFile(CRAY_NID_FILE);
	return hostname;
}

/* Cray-SLURM static implementations */

std::unique_ptr<App>
CraySLURMFrontend::registerJob(size_t numIds, ...) {
	if (numIds != 2) {
		throw std::logic_error("expecting job and step ID pair to register app");
	}

	va_list idArgs;
	va_start(idArgs, numIds);

	uint32_t jobId  = va_arg(idArgs, uint32_t);
	uint32_t stepId = va_arg(idArgs, uint32_t);

	va_end(idArgs);

	return std::make_unique<CraySLURMApp>(jobId, stepId);
}

std::string
CraySLURMFrontend::getLauncherName()
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

CraySLURMFrontend::StepLayout
CraySLURMFrontend::fetchStepLayout(uint32_t job_id, uint32_t step_id)
{
	// create sattach instance
	OutgoingArgv<SattachArgv> sattachArgv("sattach");
	sattachArgv.add(SattachArgv::DisplayLayout);
	sattachArgv.add(SattachArgv::Argument(std::to_string(job_id) + "." + std::to_string(step_id)));

	// create sattach output capture object
	ExecvpOutput sattachOutput("sattach", sattachArgv.get());
	auto& sattachStream = sattachOutput.stream();
	std::string sattachLine;

	// "Job step layout:"
	if (std::getline(sattachStream, sattachLine)) {
		if (sattachLine.compare("Job step layout:")) {
			throw std::runtime_error(std::string("sattach layout: wrong format: ") + sattachLine);
		}
	} else {
		throw std::runtime_error("sattach layout: wrong format: expected header");
	}

	StepLayout layout;
	size_t numNodes;

	// "  {numPEs} tasks, {numNodes} nodes ({hostname}...)"
	if (std::getline(sattachStream, sattachLine)) {
		// split the summary line
		std::string rawNumPEs, rawNumNodes;
		std::tie(rawNumPEs, std::ignore, rawNumNodes) =
			cti_split::string<3>(cti_split::removeLeadingWhitespace(sattachLine));

		// fill out sattach layout
		layout.numPEs = std::stoul(rawNumPEs);
		numNodes = std::stoul(rawNumNodes);
		layout.nodes.reserve(numNodes);
	} else {
		throw std::runtime_error("sattach layout: wrong format: expected summary");
	}

	// seperator line
	std::getline(sattachStream, sattachLine);

	// "  Node {nodeNum} ({hostname}), {numPEs} task(s): PE_0 {PE_i }..."
	for (int i = 0; std::getline(sattachStream, sattachLine); i++) {
		if (i >= numNodes) {
			throw std::runtime_error("malformed sattach output: too many nodes!");
		}

		// split the summary line
		std::string nodeNum, hostname, numPEs, pe_0;
		std::tie(std::ignore, nodeNum, hostname, numPEs, std::ignore, pe_0) =
			cti_split::string<6>(cti_split::removeLeadingWhitespace(sattachLine));

		// fill out node layout
		layout.nodes.push_back(NodeLayout
			{ hostname.substr(1, hostname.length() - 3) // remove parens and comma from hostname
			, std::stoul(numPEs)
			, std::stoul(pe_0)
		});
	}

	return layout;
}

std::string
CraySLURMFrontend::createNodeLayoutFile(StepLayout const& stepLayout, std::string const& stagePath)
{
	// How a SLURM Node Layout File entry is created from a Slurm Node Layout entry:
	auto make_layoutFileEntry = [](NodeLayout const& node) {
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

		// Write a Layout entry using node information from each Slurm Node Layout entry.
		for (auto const& node : stepLayout.nodes) {
			file::writeT(layoutFile.get(), make_layoutFileEntry(node));
		}

		return layoutPath;
	} else {
		throw std::runtime_error("failed to open layout file path " + layoutPath);
	}
}

std::string
CraySLURMFrontend::createPIDListFile(std::vector<MPIRInstance::MPIR_ProcTableElem> const& procTable, std::string const& stagePath)
{
	auto const pidPath = std::string{stagePath + "/" + SLURM_PID_FILE};
	if (auto const pidFile = file::open(pidPath, "wb")) {

		// Write the PID List header.
		file::writeT(pidFile.get(), slurmPidFileHeader_t
			{ .numPids = (int)procTable.size()
		});

		// Write a PID entry using information from each MPIR ProcTable entry.
		for (auto&& elem : procTable) {
			file::writeT(pidFile.get(), slurmPidFile_t
				{ .pid = elem.pid
			});
		}

		return pidPath;
	} else {
		throw std::runtime_error("failed to open PID file path " + pidPath);
	}
}

std::unique_ptr<MPIRInstance>
CraySLURMFrontend::launchApp(const char * const launcher_argv[], const char *inputFile, const char *chdirPath,
	const char * const env_list[])
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
	if (auto const launcher_path = cstr::handle{_cti_pathFind(CraySLURMFrontend::getLauncherName().c_str(), nullptr)}) {

		/* construct argv array & instance*/
		std::vector<std::string> launcherArgv{launcher_path.get()};
		for (const char* const* arg = launcher_argv; *arg != nullptr; arg++) {
			launcherArgv.emplace_back(*arg);
		}

		/* env_list null-terminated strings in format <var>=<val>*/
		std::vector<std::string> envVars;
		if (env_list != nullptr) {
			for (const char* const* arg = env_list; *arg != nullptr; arg++) {
				envVars.emplace_back(*arg);
			}
		}

		// redirect stdout / stderr to /dev/null; use sattach to redirect the output instead
		std::map<int, int> remapFds {
			{ openFileOrDevNull(inputFile), STDIN_FILENO  },
			{ open("/dev/null", O_RDWR),    STDOUT_FILENO },
			{ open("/dev/null", O_RDWR),    STDERR_FILENO }
		};

		// Launch program under MPIR control.
		return std::make_unique<MPIRInstance>(launcher_path.get(), launcherArgv, envVars, remapFds);
	} else {
		throw std::runtime_error("Failed to find launcher in path: " + CraySLURMFrontend::getLauncherName());
	}
}

uint32_t
CraySLURMFrontend::fetchJobId(MPIRInstance& srunInstance)
{
	return std::stoul(srunInstance.readStringAt("totalview_jobid"));
}

uint32_t
CraySLURMFrontend::fetchStepId(MPIRInstance& srunInstance)
{
	auto const stepid_str = srunInstance.readStringAt("totalview_stepid");
	if (!stepid_str.empty()) {
		return std::stoul(stepid_str);
	} else {
		fprintf(stderr, "cti_fe: Warning: stepid not found! Defaulting to 0.\n");
		return 0;
	}
}

SrunInfo
CraySLURMFrontend::getSrunInfo(pid_t srunPid) {
	// sanity check
	if (srunPid <= 0) {
		throw std::runtime_error("Invalid srunPid " + std::to_string(srunPid));
	}

	// Find the launcher path from the launcher name using helper _cti_pathFind.
	if (auto const launcherPath = cstr::handle{_cti_pathFind(getLauncherName().c_str(), nullptr)}) {

		// Start a new MPIR attach session on the provided PID using symbols from the launcher.
		auto const srunInstance = std::make_unique<MPIRInstance>(launcherPath.get(), srunPid);

		// extract Job and Step IDs.
		return SrunInfo
			{ .jobid  = fetchJobId(*srunInstance)
			, .stepid = fetchStepId(*srunInstance)
		};
	} else {
		throw std::runtime_error("Failed to find launcher in path: " + getLauncherName());
	}
}