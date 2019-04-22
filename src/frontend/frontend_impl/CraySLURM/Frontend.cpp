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
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "cti_defs.h"
#include "cti_fe_iface.h"

#include "CraySLURM/Frontend.hpp"

#include "useful/cti_argv.hpp"
#include "useful/cti_execvp.hpp"
#include "useful/cti_split.hpp"
#include "useful/cti_wrappers.hpp"

#include "mpir_iface/MPIRInstance.hpp"

/* constructors / destructors */

CraySLURMApp::CraySLURMApp(uint32_t jobid, uint32_t stepid, SrunInstance&& srunInstance)
	: m_launcherPid  { srunInstance.stoppedSrun ? srunInstance.stoppedSrun->getLauncherPid() : pid_t{0} }
	, m_srunInfo     { jobid, stepid }
	, m_stepLayout   { CraySLURMFrontend::fetchStepLayout(jobid, stepid) }
	, m_beDaemonSent  { false }

	, m_stoppedSrun { std::move(srunInstance.stoppedSrun) }
	, m_outputPath  { std::move(srunInstance.outputPath) }
	, m_errorPath   { std::move(srunInstance.errorPath) }

	, m_toolPath    { CRAY_SLURM_TOOL_DIR }
	, m_attribsPath { cti::cstr::asprintf(CRAY_SLURM_CRAY_DIR, CRAY_SLURM_APID(jobid, stepid)) }
	, m_stagePath   { cti::cstr::mkdtemp(std::string{_cti_getCfgDir() + "/" + SLURM_STAGE_DIR}) }
	, m_extraFiles  { CraySLURMFrontend::createNodeLayoutFile(m_stepLayout, m_stagePath) }

{
	// Ensure there are running nodes in the job.
	if (m_stepLayout.nodes.empty()) {
		throw std::runtime_error("Application " + getJobId() + " does not have any nodes.");
	}

	// If an active MPIR session was provided, extract the MPIR ProcTable and write the PID List File.
	if (m_stoppedSrun) {
		// FIXME: When/if pmi_attribs get fixed for the slurm startup barrier, this
		// call can be removed. Right now the pmi_attribs file is created in the pmi
		// ctor, which is called after the slurm startup barrier, meaning it will not
		// yet be created when launching. So we need to send over a file containing
		// the information to the compute nodes.
		m_extraFiles.push_back(CraySLURMFrontend::createPIDListFile(m_stoppedSrun->getProcTable(), m_stagePath));
	}
}

CraySLURMApp::CraySLURMApp(CraySLURMApp&& moved)
	: m_launcherPid { moved.m_launcherPid }
	, m_srunInfo    { moved.m_srunInfo }
	, m_stepLayout  { moved.m_stepLayout }
	, m_beDaemonSent { moved.m_beDaemonSent }

	, m_stoppedSrun { std::move(moved.m_stoppedSrun) }
	, m_outputPath  { std::move(moved.m_outputPath) }
	, m_errorPath   { std::move(moved.m_errorPath) }

	, m_toolPath    { moved.m_toolPath }
	, m_attribsPath { moved.m_attribsPath }
	, m_stagePath   { moved.m_stagePath }
	, m_extraFiles  { moved.m_extraFiles }
{
	// We have taken ownership of the staging path, so don't let moved delete the directory.
	moved.m_stagePath.erase();

	// we will deregister the overwatch app instance
	moved.m_launcherPid = pid_t{0};
}

CraySLURMApp::~CraySLURMApp()
{
	// Delete the staging directory if it exists.
	if (!m_stagePath.empty()) {
		_cti_removeDirectory(m_stagePath.c_str());
	}

	// clean up overwatch
	_cti_deregisterApp(m_launcherPid);
}

/* app instance creation */

CraySLURMApp::CraySLURMApp(uint32_t jobid, uint32_t stepid)
	: CraySLURMApp
		{ jobid
		, stepid
		, SrunInstance
			{ .stoppedSrun = nullptr
			, .outputPath  = cti::temp_file_handle{_cti_getCfgDir() + "/cti-output-fifo-XXXXXX"}
			, .errorPath   = cti::temp_file_handle{_cti_getCfgDir() + "/cti-error-fifo-XXXXXX"}
		}
	}
{}

CraySLURMApp::CraySLURMApp(SrunInstance&& srunInstance)
	: CraySLURMApp
		{ CraySLURMFrontend::fetchJobId(*srunInstance.stoppedSrun)
		, CraySLURMFrontend::fetchStepId(*srunInstance.stoppedSrun)
		, std::move(srunInstance)
	}
{}

CraySLURMApp::CraySLURMApp(const char * const launcher_argv[], int stdout_fd, int stderr_fd,
	const char *inputFile, const char *chdirPath, const char * const env_list[])
	: CraySLURMApp{ CraySLURMFrontend::launchApp(launcher_argv, inputFile, stdout_fd, stderr_fd, chdirPath, env_list) }
{}

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
	throw std::runtime_error("not supported for WLM: getLauncherHostname");
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
	// check MPIR barrier
	if (!m_stoppedSrun) {
		throw std::runtime_error("app not under MPIR control");
	}

	// release MPIR barrier
	m_stoppedSrun.reset();
}

void
CraySLURMApp::redirectOutput(int stdoutFd, int stderrFd)
{
	// create sattach argv
	cti::ManagedArgv sattachArgv
		{ SATTACH // first argument should be "sattach"
		, "-Q"    // second argument is quiet
		, getJobId() // third argument is the jobid.stepid
	};

	// redirect stdin / stderr / stdout
	std::map<int, int> remapFds {
		{ open("/dev/null", O_RDONLY), STDIN_FILENO }
	};
	if (stdoutFd >= 0) {
		remapFds[stdoutFd] = STDOUT_FILENO;
	}
	if (stderrFd >= 0) {
		remapFds[stderrFd] = STDERR_FILENO;
	}

	if (auto const sattachPath = cti::make_unique_destr(_cti_pathFind(SATTACH, nullptr), std::free)) {
		// make sure sattach is set up by running to MPIR_Breakpoint
		Inferior sattachInferior{sattachPath.get(), sattachArgv.get(), {}, remapFds};

		// add to app list of active sattach
		_cti_registerUtil(m_launcherPid, sattachInferior.getPid());

		// run to MPIR_Breakpoint
		sattachInferior.addSymbol("MPIR_Breakpoint");
		sattachInferior.addSymbol("MPIR_being_debugged");
		sattachInferior.setBreakpoint("MPIR_Breakpoint");
		sattachInferior.writeVariable("MPIR_being_debugged", 1);
		sattachInferior.continueRun();
	} else {
		throw std::runtime_error(std::string{"failed to find in path: "} + SATTACH);
	}
}

void CraySLURMApp::kill(int signum)
{
	// create the args for scancel
	auto scancelArgv = cti::ManagedArgv
		{ SCANCEL // first argument should be "scancel"
		, "-Q"    // second argument is quiet
		, "-s", std::to_string(signum)    // third argument is signal number
		, getJobId() // fourth argument is the jobid.stepid
	};

	// tell overwatch to launch scancel
	auto const scancelPid = _cti_forkExecvpUtil(m_launcherPid, SCANCEL, scancelArgv.get(), -1, -1, nullptr);

	// wait until the scancel finishes
	waitpid(scancelPid, nullptr, 0);
}

void CraySLURMApp::shipPackage(std::string const& tarPath) const {
	// create the args for sbcast
	auto sbcastArgv = cti::ManagedArgv
		{ SBCAST
		, "-C"
		, "-j", std::to_string(m_srunInfo.jobid)
		, tarPath
		, "--force"
	};

	if (auto packageName = cti::make_unique_destr(_cti_pathToName(tarPath.c_str()), std::free)) {
		sbcastArgv.add(std::string(CRAY_SLURM_TOOL_DIR) + "/" + packageName.get());
	} else {
		throw std::runtime_error("_cti_pathToName failed");
	}

	// now ship the tarball to the compute nodes. tell overwatch to launch sbcast
	auto const forkedPid = _cti_forkExecvpUtil(m_launcherPid, SBCAST, sbcastArgv.get(), -1, -1, nullptr);

	// wait until the sbcast finishes
	// FIXME: There is no way to error check right now because the sbcast command
	// can only send to an entire job, not individual job steps. The /var/spool/alps/<apid>
	// directory will only exist on nodes associated with this particular job step, and the
	// sbcast command will exit with error if the directory doesn't exist even if the transfer
	// worked on the nodes associated with the step. I opened schedmd BUG 1151 for this issue.
	waitpid(forkedPid, nullptr, 0);
}

void CraySLURMApp::startDaemon(const char* const args[]) {
	// sanity check
	if (args == nullptr) {
		throw std::runtime_error("args array is null!");
	}

	// If we have not yet transfered the backend daemon, need to do that in advance with
	// native slurm
	if (!m_beDaemonSent) {
		// Get the location of the daemon
		if (_cti_getBEDaemonPath().empty()) {
			throw std::runtime_error("Required environment variable not set:" + std::string(BASE_DIR_ENV_VAR));
		}

		shipPackage(_cti_getBEDaemonPath());

		// set transfer to true
		m_beDaemonSent = true;
	}

	// use existing daemon binary on compute node
	std::string const remoteBEDaemonPath(m_toolPath + "/" + CTI_BE_DAEMON_BINARY);

	// Start adding the args to the launchder argv array
	//
	// This corresponds to:
	//
	// srun --jobid=<job_id> --gres=none --mem-per-cpu=0 --mem_bind=no
	// --cpu_bind=no --share --ntasks-per-node=1 --nodes=<numNodes>
	// --nodelist=<host1,host2,...> --disable-status --quiet --mpi=none
	// --input=none --output=none --error=none <tool daemon> <args>
	//
	auto launcherArgv = cti::ManagedArgv
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

	launcherArgv.add(remoteBEDaemonPath);

	// merge in the args array if there is one
	if (args != nullptr) {
		for (const char* const* arg = args; *arg != nullptr; arg++) {
			launcherArgv.add(*arg);
		}
	}

	// build environment from blacklist
	auto const envVarBlacklist = std::vector<std::string>{
		"SLURM_CHECKPOINT",      "SLURM_CONN_TYPE",         "SLURM_CPUS_PER_TASK",
		"SLURM_DEPENDENCY",      "SLURM_DIST_PLANESIZE",    "SLURM_DISTRIBUTION",
		"SLURM_EPILOG",          "SLURM_GEOMETRY",          "SLURM_NETWORK",
		"SLURM_NPROCS",          "SLURM_NTASKS",            "SLURM_NTASKS_PER_CORE",
		"SLURM_NTASKS_PER_NODE", "SLURM_NTASKS_PER_SOCKET", "SLURM_PARTITION",
		"SLURM_PROLOG",          "SLURM_REMOTE_CWD",        "SLURM_REQ_SWITCH",
		"SLURM_RESV_PORTS",      "SLURM_TASK_EPILOG",       "SLURM_TASK_PROLOG",
		"SLURM_WORKING_DIR"
	};
	cti::ManagedArgv launcherEnv;
	for (auto&& envVar : envVarBlacklist) {
		launcherEnv.add(envVar + "=");
	}

	// tell overwatch to launch srun
	auto const forkedPid = _cti_forkExecvpUtil(m_launcherPid,
		CraySLURMFrontend::getLauncherName().c_str(), launcherArgv.get(), -1, -1, launcherEnv.get());
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
		if (auto nidFile = cti::file::try_open(filePath, "r")) {
			int nid;
			{ // We expect this file to have a numeric value giving our current Node ID.
				char buf[BUFSIZ];
				if (fgets(buf, BUFSIZ, nidFile.get()) == nullptr) {
					throw std::runtime_error("_cti_cray_slurm_getHostname fgets failed.");
				}
				nid = std::stoi(std::string{buf});
			}

			// Use the NID to create the standard hostname format.
			return cti::cstr::asprintf(CRAY_HOSTNAME_FMT, nid);

		} else {
			return cti::cstr::gethostname();
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
	cti::OutgoingArgv<SattachArgv> sattachArgv(SATTACH);
	sattachArgv.add(SattachArgv::DisplayLayout);
	sattachArgv.add(SattachArgv::Argument("-Q"));
	sattachArgv.add(SattachArgv::Argument(std::to_string(job_id) + "." + std::to_string(step_id)));

	// create sattach output capture object
	cti::Execvp sattachOutput(SATTACH, sattachArgv.get());
	auto& sattachStream = sattachOutput.stream();
	std::string sattachLine;

	// wait for sattach to complete
	auto const sattachCode = sattachOutput.getExitStatus();
	if (sattachCode > 0) {
		// sattach failed, restart it
		return fetchStepLayout(job_id, step_id);
	}

	// start parsing sattach output

	// "Job step layout:"
	if (std::getline(sattachStream, sattachLine)) {
		if (sattachLine.compare("Job step layout:")) {
			throw std::runtime_error(std::string("sattach layout: wrong format: ") + sattachLine);
		}
	} else {
		throw std::runtime_error("sattach layout: wrong format: expected header");
	}

	StepLayout layout;
	auto numNodes = int{0};

	// "  {numPEs} tasks, {numNodes} nodes ({hostname}...)"
	if (std::getline(sattachStream, sattachLine)) {
		// split the summary line
		std::string rawNumPEs, rawNumNodes;
		std::tie(rawNumPEs, std::ignore, rawNumNodes) =
			cti::split::string<3>(cti::split::removeLeadingWhitespace(sattachLine));

		// fill out sattach layout
		layout.numPEs = std::stoul(rawNumPEs);
		numNodes = std::stoi(rawNumNodes);
		layout.nodes.reserve(numNodes);
	} else {
		throw std::runtime_error("sattach layout: wrong format: expected summary");
	}

	// seperator line
	std::getline(sattachStream, sattachLine);

	// "  Node {nodeNum} ({hostname}), {numPEs} task(s): PE_0 {PE_i }..."
	for (auto i = int{0}; std::getline(sattachStream, sattachLine); i++) {
		if (i >= numNodes) {
			throw std::runtime_error("malformed sattach output: too many nodes!");
		}

		// split the summary line
		std::string nodeNum, hostname, numPEs, pe_0;
		std::tie(std::ignore, nodeNum, hostname, numPEs, std::ignore, pe_0) =
			cti::split::string<6>(cti::split::removeLeadingWhitespace(sattachLine));

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
	if (auto const layoutFile = cti::file::open(layoutPath, "wb")) {

		// Write the Layout header.
		cti::file::writeT(layoutFile.get(), slurmLayoutFileHeader_t
			{ .numNodes = (int)stepLayout.nodes.size()
		});

		// Write a Layout entry using node information from each Slurm Node Layout entry.
		for (auto const& node : stepLayout.nodes) {
			cti::file::writeT(layoutFile.get(), make_layoutFileEntry(node));
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
	if (auto const pidFile = cti::file::open(pidPath, "wb")) {

		// Write the PID List header.
		cti::file::writeT(pidFile.get(), slurmPidFileHeader_t
			{ .numPids = (int)procTable.size()
		});

		// Write a PID entry using information from each MPIR ProcTable entry.
		for (auto&& elem : procTable) {
			cti::file::writeT(pidFile.get(), slurmPidFile_t
				{ .pid = elem.pid
			});
		}

		return pidPath;
	} else {
		throw std::runtime_error("failed to open PID file path " + pidPath);
	}
}

CraySLURMFrontend::SrunInstance
CraySLURMFrontend::launchApp(const char * const launcher_argv[],
		const char *inputFile, int stdoutFd, int stderrFd, const char *chdirPath,
		const char * const env_list[])
{
	SrunInstance srunInstance
		{ .stoppedSrun = nullptr
		, .outputPath  = cti::temp_file_handle{_cti_getCfgDir() + "/cti-output-fifo-XXXXXX"}
		, .errorPath   = cti::temp_file_handle{_cti_getCfgDir() + "/cti-error-fifo-XXXXXX"}
	};

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

	// attach output / error fifo to user-provided FDs if applicable
	if (mkfifo(srunInstance.outputPath.get(), 0600) < 0) {
		throw std::runtime_error("mkfifo failed on " + std::string{srunInstance.outputPath.get()} +": " + std::string{strerror(errno)});
	}
	if (mkfifo(srunInstance.errorPath.get(), 0600) < 0) {
		throw std::runtime_error("mkfifo failed on " + std::string{srunInstance.errorPath.get()} +": " + std::string{strerror(errno)});
	}


	// Get the launcher path from CTI environment variable / default.
	if (auto const launcher_path = cti::make_unique_destr(_cti_pathFind(CraySLURMFrontend::getLauncherName().c_str(), nullptr), std::free)) {

		// set up arguments and FDs
		std::string const redirectPath = _cti_getBaseDir() + "/libexec/" + OUTPUT_REDIRECT_BINARY;
		char const* redirectArgv[] = {
			OUTPUT_REDIRECT_BINARY, srunInstance.outputPath.get(), srunInstance.errorPath.get(), nullptr
		};
		if (stdoutFd < 0) { stdoutFd = STDOUT_FILENO; }
		if (stderrFd < 0) { stderrFd = STDERR_FILENO; }

		// run output redirection binary as app (register as util later)
		auto const redirectPid = _cti_forkExecvpApp(redirectPath.c_str(), redirectArgv,
			stdoutFd, stderrFd, nullptr);

		// construct argv array & instance
		std::vector<std::string> launcherArgv
			{ launcher_path.get()
			, "--output=" + std::string{srunInstance.outputPath.get()}
			, "--error="  + std::string{srunInstance.errorPath.get()}
		};
		for (const char* const* arg = launcher_argv; *arg != nullptr; arg++) {
			launcherArgv.emplace_back(*arg);
		}

		// env_list null-terminated strings in format <var>=<val>
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
		srunInstance.stoppedSrun = std::make_unique<MPIRInstance>(launcher_path.get(), launcherArgv, envVars, remapFds);

		// overwatch app and redirect utility
		_cti_registerApp(srunInstance.stoppedSrun->getLauncherPid());
		_cti_registerUtil(srunInstance.stoppedSrun->getLauncherPid(), redirectPid);

		return srunInstance;
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
	if (auto const launcherPath = cti::make_unique_destr(_cti_pathFind(getLauncherName().c_str(), nullptr), std::free)) {

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
