/******************************************************************************\
 * cray_slurm_fe.c - Cray SLURM specific frontend library functions.
 *
 * Copyright 2014-2015 Cray Inc.  All Rights Reserved.
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

#include "useful/cti_useful.h"
#include "useful/make_unique.hpp"
#include "useful/strong_argv.hpp"
#include "useful/Dlopen.hpp"

#include "mpir_iface/mpir_iface.h"

#include "slurm_util/slurm_util.h"

/* Types used here */

using SrunInfo = CraySLURMFrontend::SrunInfo;

namespace handle {
	// managed file descriptor
	struct Fd {
		int data;

		operator bool() const { return (data >= 0); }
		void reset() { if (*this) { ::close(data); data = int{}; } }
		int get() const { return data; }

		Fd(int data_) : data{data_} {}
		Fd(Fd&& moved) : data{std::move(moved.data)} {}
		~Fd() { reset(); }
	};

	// managed MPIR session
	struct MPIR {
		mpir_id_t data;

		operator bool() const { return (data >= 0); }
		void reset() { if (*this) { _cti_mpir_releaseInstance(data); data = mpir_id_t{-1}; } }
		mpir_id_t get() const { return data; }

		MPIR() : data{-1} {}
		MPIR(mpir_id_t data_) : data{data_} {}
		MPIR(MPIR&& moved) : data{std::move(moved.data)} {}
		~MPIR() { reset(); }
	};

	// managed c-style string
	struct cstr : public UniquePtrDestr<char> {
		cstr(char* str) : UniquePtrDestr<char>{str, ::free} {}
	};
}

#include <functional>

namespace cstr {
	// lifted asprintf
	template <typename... Args>
	static auto asprintf(char const* const formatCStr, Args&&... args) -> std::string {
		char *rawResult = nullptr;
		if (::asprintf(&rawResult, formatCStr, std::forward<Args>(args)...) < 0) {
			throw std::runtime_error("asprintf failed.");
		}
		auto const result = handle::cstr{rawResult};
		return std::string(result.get());
	}

	// lifted mkdtemp
	static auto mkdtemp(std::string const& pathTemplate) -> std::string {
		auto rawPathTemplate = handle::cstr{strdup(pathTemplate.c_str())};

		if (::mkdtemp(rawPathTemplate.get())) {
			return std::string(rawPathTemplate.get());
		} else {
			throw std::runtime_error("mkdtemp failed on " + pathTemplate);
		}
	}

	// lifted gethostname
	static auto gethostname() -> std::string {
		char buf[HOST_NAME_MAX + 1];
		if (::gethostname(buf, HOST_NAME_MAX) < 0) {
			throw std::runtime_error("gethostname failed");
		}
		return std::string{buf};
	}
}

namespace file {
	// open a file path and return a unique FILE* or nullptr
	static auto try_open(std::string const& path, char const* mode) -> UniquePtrDestr<FILE> {
		if (auto ufp = UniquePtrDestr<FILE>(fopen(path.c_str(), mode), ::fclose)) {
			return ufp;
		} else {
			return nullptr;
		}
	};

	// open a file path and return a unique FILE* or throw
	static auto open(std::string const& path, char const* mode) -> UniquePtrDestr<FILE> {
		if (auto ufp = try_open(path, mode)) {
			return ufp;
		} else {
			throw std::runtime_error("failed to open path " + path);
		}
	};

	// write a POD to file
	template <typename T>
	static void writeT(FILE* fp, T const& data) {
		if (fwrite(&data, sizeof(T), 1, fp) != 1) {
			throw std::runtime_error("failed to write to file");
		}
	}
};

namespace slurm_util {
	struct NodeLayout {
		std::string hostname;
		size_t numPEs; // number of PEs running on node
		size_t firstPE; // first PE number on this node
	};

	struct StepLayout {
		size_t numPEs; // number of PEs associated with job step
		std::vector<NodeLayout> nodes; // array of hosts
	};

	// create a StepLayout from slurm_util layout object, free it
	StepLayout make_Layout(slurmStepLayout_t*&& raw_layout) {
		auto layout = StepLayout{ .numPEs = (size_t)raw_layout->numPEs };

		for (int i = 0; i < raw_layout->numNodes; ++i) {
			layout.nodes.push_back(NodeLayout
				{ std::string{raw_layout->hosts[i].host}
				, (size_t)raw_layout->hosts[i].PEsHere
				, (size_t)raw_layout->hosts[i].firstPE
			});
		}

		_cti_cray_slurm_freeLayout(raw_layout);

		return layout;
	}
}

static auto
_cti_cray_slurm_extraFiles(slurm_util::StepLayout const& layout, cti_mpir_procTable_t const* app_pids,
	std::string const& stagePath) -> std::vector<std::string> {

	// how to create a slurm layout file entry from a slurm_util node layout entry
	auto make_layoutFileEntry = [](slurm_util::NodeLayout const& node) {
		// ensure we have good hostname information
		auto const hostname_len = node.hostname.size() + 1;
		if (hostname_len > sizeof(slurmLayoutFile_t::host)) {
			throw std::runtime_error("hostname too large for layout buffer");
		}

		auto layout_entry    = slurmLayoutFile_t{};
		layout_entry.PEsHere = node.numPEs;
		layout_entry.firstPE = node.firstPE;
		memcpy(layout_entry.host, node.hostname.c_str(), hostname_len);

		return layout_entry;
	};

	// write SLURM layout file from steplayout data
	auto writeLayoutFile = [&make_layoutFileEntry](FILE* layoutFile, slurm_util::StepLayout const& layout) {
		// write the layout header
		file::writeT(layoutFile, slurmLayoutFileHeader_t{(int)layout.nodes.size()});

		// write a new layout_entry with hostname/PE information from the slurm host layout info
		for (auto const& node : layout.nodes) {
			file::writeT(layoutFile, make_layoutFileEntry(node));
		}
	};

	// create PID list file from mpir data, return path
	auto writePIDFile = [](FILE* pidFile, cti_mpir_procTable_t const* app_pids) {
		// write the pid header
		file::writeT(pidFile, slurmPidFileHeader_t{(int)app_pids->num_pids});

		// copy each mpir pid entry to file
		for (size_t i = 0; i < app_pids->num_pids; ++i) {
			file::writeT(pidFile, slurmPidFile_t{app_pids->pids[i]});
		}
	};

	std::vector<std::string> result;

	// add layout file as extra file to ship
	auto const layoutPath = std::string{stagePath + "/" + SLURM_LAYOUT_FILE};
	writeLayoutFile(file::open(layoutPath, "wb").get(), layout);
	result.push_back(layoutPath);

	// check to see if there is an app_pids member, if so we need to create the pid file
	if (app_pids != nullptr) {
		auto const pidPath = std::string{stagePath + "/" + SLURM_PID_FILE};
		writePIDFile(file::open(pidPath, "wb").get(), app_pids);
		result.push_back(pidPath);
	}

	return result;
}

struct CraySLURMApp {
	SrunInfo srunInfo;
	slurm_util::StepLayout	layout;			// Layout of job step
	std::string			toolPath;		// Backend staging directory
	std::string			attribsPath;	// Backend Cray specific directory
	bool				dlaunch_sent;	// True if we have already transfered the dlaunch utility
	std::string			stagePath;		// directory to stage this instance files in for transfer to BE

	handle::MPIR		barrier;		// mpir handle to release barrier
	std::vector<std::string> extraFiles;	// extra files to transfer to BE associated with this app

	CraySLURMApp(CraySLURMApp&&) = default;

	// registration constructor
	CraySLURMApp(uint32_t jobid, uint32_t stepid, mpir_id_t mpir_id = -1)
		: srunInfo{jobid, stepid}
		, layout{slurm_util::make_Layout(_cti_cray_slurm_getLayout(jobid, stepid))}
		, toolPath{CRAY_SLURM_TOOL_DIR}
		, attribsPath{cstr::asprintf(CRAY_SLURM_CRAY_DIR, CRAY_SLURM_APID(jobid, stepid))}
		, dlaunch_sent{false}
		, stagePath{cstr::mkdtemp(std::string{_cti_getCfgDir() + "/" + SLURM_STAGE_DIR})}
		, barrier{mpir_id}
		, extraFiles{_cti_cray_slurm_extraFiles(layout, nullptr, stagePath)} {

		// ensure there are running nodes in the layout
		if (layout.nodes.empty()) {
			throw std::runtime_error("Application " + getJobId() + " does not have any nodes.");
		}

		// if an mpir session id was provided, fetch more info from MPIR_PROCTABLE
		if (barrier) {
			// FIXME: When/if pmi_attribs get fixed for the slurm startup barrier, this
			// call can be removed. Right now the pmi_attribs file is created in the pmi
			// ctor, which is called after the slurm startup barrier, meaning it will not
			// yet be created when launching. So we need to send over a file containing
			// the information to the compute nodes.
			auto const procTable = UniquePtrDestr<cti_mpir_procTable_t>{
				_cti_mpir_newProcTable(barrier.get()),
				_cti_mpir_deleteProcTable
			};
			extraFiles = _cti_cray_slurm_extraFiles(layout, procTable.get(), stagePath);
		}
	}

	// attaching constructor
	static CraySLURMApp make_attach(mpir_id_t mpir_id);

	// launching constructor
	static CraySLURMApp make_launch(const char * const launcher_argv[], int stdout_fd,
		int stderr_fd, const char *inputFile, const char *chdirPath, const char * const env_list[]);

	~CraySLURMApp() {
		// cleanup staging directory if it exists
		if (!stagePath.empty()) {
			_cti_removeDirectory(stagePath.c_str());
		}
	}

	void releaseBarrier() {
		if (!barrier) {
			throw std::runtime_error("app not under MPIR control");
		}
		barrier.reset();
	}

	void killApp(int signum);

	void startDaemon(const char* const args[]);

	// Note that we should provide this as a jobid.stepid format. It will make turning
	// it into a Cray apid easier on the backend since we don't lose any information
	// with this format.
	std::string getJobId() const {
		return std::string{std::to_string(srunInfo.jobid) + "." + std::to_string(srunInfo.stepid)};
	}

	// generate apid from the job/stepid
	uint64_t getApid() const { return CRAY_SLURM_APID(srunInfo.jobid, srunInfo.stepid); }

	CraySLURMFrontend::SrunInfo getSrunInfo() const { return srunInfo; }
	int getNumAppPEs()   const { return layout.numPEs;   }
	int getNumAppNodes() const { return layout.nodes.size(); }

	std::vector<std::string>       getAppHostsList()      const;
	std::vector<Frontend::CTIHost> getAppHostsPlacement() const;
};

auto const static slurm_blacklist_env_vars = std::vector<char const*>{
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

auto static _cti_cray_slurm_launcher_name = std::string{};

// get launcher name from environment variable if set, otherwise default SLURM
static std::string _cti_cray_slurm_getLauncherName() {
	if (_cti_cray_slurm_launcher_name.empty()) {
		if (auto const launcher_name_env = getenv(CTI_LAUNCHER_NAME)) {
			_cti_cray_slurm_launcher_name = std::string(launcher_name_env);
		} else {
			_cti_cray_slurm_launcher_name = std::string{SRUN};
		}
	}

	return _cti_cray_slurm_launcher_name;
}

// read SrunInfo variables from existing mpir session
static CraySLURMFrontend::SrunInfo
_cti_cray_slurm_fetchSrunInfo(mpir_id_t mpir_id) {
	uint32_t jobid, stepid;

	// get the jobid string for slurm and convert to jobid
	if (auto const jobid_str = handle::cstr{_cti_mpir_getStringAt(mpir_id, "totalview_jobid")}) {
		jobid = std::stoul(jobid_str.get());
	} else {
		throw std::runtime_error("failed to get jobid string via MPIR.");
	}

	// get the jobid string for slurm and convert to jobid
	if (auto const stepid_str = handle::cstr{_cti_mpir_getStringAt(mpir_id, "totalview_stepid")}) {
		stepid = std::stoul(stepid_str.get());
	} else {
		fprintf(stderr, "cti_fe: Warning: stepid not found! Defaulting to 0.\n");
		stepid = 0;
	}

	return SrunInfo{jobid, stepid};
}

// fetch app info from its srun pid and generate an SrunInfo
static CraySLURMFrontend::SrunInfo
_cti_cray_slurm_attachAndFetchJobInfo(pid_t srunPid) {
	// sanity check
	if (srunPid <= 0) {
		throw std::runtime_error("Invalid srunPid " + std::to_string(srunPid));
	}

	// get the launcher path
	if (auto const launcher_path = handle::cstr{_cti_pathFind(_cti_cray_slurm_getLauncherName().c_str(), nullptr)}) {

		// Create a new MPIR instance. We want to interact with it.
		auto const mpir_id = _cti_mpir_newAttachInstance(launcher_path.get(), srunPid);
		if (mpir_id >= 0) {
			return _cti_cray_slurm_fetchSrunInfo(mpir_id);
		} else {
			throw std::runtime_error("Failed to create MPIR attach instance.");
		}
	} else {
		throw std::runtime_error("Failed to find launcher in path: " + _cti_cray_slurm_getLauncherName());
	}
}

CraySLURMApp CraySLURMApp::make_attach(mpir_id_t mpir_id)
{
	auto const srunInfo = _cti_cray_slurm_fetchSrunInfo(mpir_id);
	return CraySLURMApp{srunInfo.jobid, srunInfo.stepid, mpir_id};
}

CraySLURMApp CraySLURMApp::make_launch(const char * const launcher_argv[], int stdout_fd, int stderr_fd,
	const char *inputFile, const char *chdirPath, const char * const env_list[])
{
	// open input file (or /dev/null to avoid stdin contention)
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

	// get the launcher path from CTI environment variable / default
	if (auto const launcher_path = handle::cstr{_cti_pathFind(_cti_cray_slurm_getLauncherName().c_str(), nullptr)}) {

		// Launch program under MPIR control. On error, it will be automatically released from barrier
		auto const mpir_id = _cti_mpir_newLaunchInstance(launcher_path.get(),
			launcher_argv, env_list, openFileOrDevNull(inputFile), stdout_fd, stderr_fd);
		if (mpir_id >= 0) {
			return CraySLURMApp::make_attach(mpir_id);
		} else { // failed to launch program under mpir control

			// binary name is not always first argument (launcher_argv excludes the usual argv[0])
			std::string argvString;
			for (const char* const* arg = launcher_argv; *arg != nullptr; arg++) {
				argvString.push_back(' ');
				argvString += *arg;
			}
			throw std::runtime_error("Failed to launch: " + argvString);
		}
	} else {
		throw std::runtime_error("Failed to find launcher in path: " + _cti_cray_slurm_getLauncherName());
	}
}

void CraySLURMApp::killApp(int signum)
{
	// create the args for scancel
	auto scancelArgv = ManagedArgv
		{ SCANCEL // first argument should be "scancel"
		, "-Q"    // second argument is quiet
		, "-s", std::to_string(signum)    // third argument is signal number
		, getJobId() // fourth argument is the jobid.stepid
	};

	// fork off a process to launch scancel
	auto const scancelPid = fork();

	// error case
	if (scancelPid < 0) {
		throw std::runtime_error("Fatal fork error.");
	}

	// child case
	else if (scancelPid == 0) {
		// exec scancel
		execvp(SCANCEL, scancelArgv.get());

		// exec shouldn't return
		fprintf(stderr, "CTI error: Return from exec.\n");
		perror("execvp");
		_exit(1);
	}

	// parent case

	// wait until the scancel finishes
	waitpid(scancelPid, nullptr, 0);
}

static void
_cti_cray_slurm_ship_package(uint32_t job_id, std::string const& package) {
	// create the args for sbcast
	auto launcherArgv = ManagedArgv
		{ SBCAST
		, "-C"
		, "-j", std::to_string(job_id)
		, package
		, "--force"
	};

	if (auto packageName = handle::cstr{_cti_pathToName(package.c_str())}) {
		launcherArgv.add(std::string(CRAY_SLURM_TOOL_DIR) + "/" + packageName.get());
	} else {
		throw std::runtime_error("_cti_pathToName failed");
	}

	// now ship the tarball to the compute nodes
	// fork off a process to launch sbcast
	auto const forkedPid = fork();

	// error case
	if (forkedPid < 0) {
		throw std::runtime_error("Fatal fork error.");
	}

	// child case
	else if (forkedPid == 0) {
		// redirect stdin, stdout, stderr to /dev/null
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

	// parent case

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
	if (!dlaunch_sent) {
		// Get the location of the daemon launcher
		if (_cti_getDlaunchPath().empty()) {
			throw std::runtime_error("Required environment variable not set:" + std::string(BASE_DIR_ENV_VAR));
		}

		_cti_cray_slurm_ship_package(srunInfo.jobid, _cti_getDlaunchPath());

		// set transfer to true
		dlaunch_sent = true;
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
		{ _cti_cray_slurm_getLauncherName()
		, "--jobid=" + std::to_string(srunInfo.jobid)
		, "--gres=none"
		, "--mem-per-cpu=0"
		, "--mem_bind=no"
		, "--cpu_bind=no"
		, "--share"
		, "--ntasks-per-node=1"
		, "--nodes=" + std::to_string(layout.nodes.size())
		, "--disable-status"
		, "--quiet"
		, "--mpi=none"
		, "--output=none"
		, "--error=none"
	};

	// create the hostlist by contencating all hostnames
	{ auto hostlist = std::string{};
		for (auto const& node : layout.nodes) {
			hostlist += " " + node.hostname;
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
	auto const forkedPid = fork();

	// error case
	if (forkedPid < 0) {
		throw std::runtime_error("Fatal fork error.");
	}

	// child case
	if (forkedPid == 0) {
		// Place this process in its own group to prevent signals being passed
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
		for (auto const& envVar : slurm_blacklist_env_vars) {
			unsetenv(envVar);
		}

		// exec srun
		execvp(_cti_cray_slurm_getLauncherName().c_str(), launcherArgv.get());

		// exec shouldn't return
		fprintf(stderr, "CTI error: Return from exec.\n");
		perror("execvp");
		_exit(1);
	}

	// Place the child in its own group.
	setpgid(forkedPid, forkedPid);
}

#include <algorithm>

std::vector<std::string>
CraySLURMApp::getAppHostsList() const {
	std::vector<std::string> result;
	// extract hostnames from each NodeLayout
	std::transform(layout.nodes.begin(), layout.nodes.end(), std::back_inserter(result),
		[](slurm_util::NodeLayout const& node) { return node.hostname; });
	return result;
}

std::vector<Frontend::CTIHost>
CraySLURMApp::getAppHostsPlacement() const {
	std::vector<Frontend::CTIHost> result;
	// construct a CTIHost from each NodeLayout
	std::transform(layout.nodes.begin(), layout.nodes.end(), std::back_inserter(result),
		[](slurm_util::NodeLayout const& node) {
			return Frontend::CTIHost{node.hostname, node.numPEs};
		});
	return result;
}

/*
   I return a pointer to the hostname of the node I am running
   on. On Cray nodes this can be done with very little overhead
   by reading the nid number out of /proc. If that is not
   available I fall back to just doing a libc gethostname call
   to get the name. If the fall back is used, the name will
   not necessarily be in the form of "nidxxxxx".

   The caller is responsible for freeing the returned
   string.

   As an opaque implementation detail, I cache the results
   for successive calls.
 */
static std::string
_cti_cray_slurm_getHostName(void) {
	static auto hostname = std::string{}; // Cache the result

	// Determined the answer previously?
	if (!hostname.empty()) {
		return hostname;    // return cached value
	}

	// Try the Cray /proc extension short cut
	if (auto nidFile = file::try_open(ALPS_XT_NID, "r")) {
		int nid;
		{ // we expect this file to have a numeric value giving our current nid
			char buf[BUFSIZ];
			if (fgets(buf, BUFSIZ, nidFile.get()) == nullptr) {
				throw std::runtime_error("_cti_cray_slurm_getHostName fgets failed.");
			}
			nid = std::stoi(std::string{buf});
		}

		hostname = cstr::asprintf(ALPS_XT_HOSTNAME_FMT, nid);
	}

	// Fallback to standard hostname
	else {
		hostname = cstr::gethostname();
	}

	return hostname;
}

#include <vector>
#include <string>
#include <unordered_map>

#include <memory>

#include <stdexcept>

/* wlm interface implementation */

using AppId   = Frontend::AppId;
using CTIHost = Frontend::CTIHost;

/* active app management */

static std::unordered_map<AppId, CraySLURMApp> appList;
static const AppId APP_ERROR = 0;
static AppId newAppId() noexcept {
	static AppId nextId = 1;
	return nextId++;
}

static CraySLURMApp&
getAppInfo(AppId appId) {
	auto app = appList.find(appId);
	if (app != appList.end()) {
		return app->second;
	}

	throw std::runtime_error("invalid appId: " + std::to_string(appId));
}

bool
CraySLURMFrontend::appIsValid(AppId appId) const {
	return appList.find(appId) != appList.end();
}

void
CraySLURMFrontend::deregisterApp(AppId appId) const {
	appList.erase(appId);
}

cti_wlm_type
CraySLURMFrontend::getWLMType() const {
	return CTI_WLM_CRAY_SLURM;
}

std::string const
CraySLURMFrontend::getJobId(AppId appId) const {
	return getAppInfo(appId).getJobId();
}

AppId
CraySLURMFrontend::launch(CArgArray launcher_argv, int stdout_fd, int stderr,
					 CStr inputFile, CStr chdirPath, CArgArray env_list) {
	// delegate launching with barrier
	auto const appId = newAppId();
	appList.emplace(appId, CraySLURMApp::make_launch(launcher_argv, stdout_fd, stderr, inputFile, chdirPath, env_list));

	// we should not wait at the barrier, call the barrier release function.
	appList.at(appId).releaseBarrier();

	return appId;
}

AppId
CraySLURMFrontend::launchBarrier(CArgArray launcher_argv, int stdout_fd, int stderr,
							CStr inputFile, CStr chdirPath, CArgArray env_list) {
	auto const appId = newAppId();
	appList.emplace(appId, CraySLURMApp::make_launch(launcher_argv, stdout_fd, stderr, inputFile, chdirPath, env_list));
	return appId;
}

void
CraySLURMFrontend::releaseBarrier(AppId appId) {
	getAppInfo(appId).releaseBarrier();
}

void
CraySLURMFrontend::killApp(AppId appId, int signal) {
	getAppInfo(appId).killApp(signal);
}

std::vector<std::string> const
CraySLURMFrontend::getExtraFiles(AppId appId) const {
	return getAppInfo(appId).extraFiles;
}

void
CraySLURMFrontend::shipPackage(AppId appId, std::string const& tarPath) const {
	_cti_cray_slurm_ship_package(getAppInfo(appId).srunInfo.jobid, tarPath);
}

void
CraySLURMFrontend::startDaemon(AppId appId, CArgArray argv) const {
	getAppInfo(appId).startDaemon(argv);
}

size_t
CraySLURMFrontend::getNumAppPEs(AppId appId) const {
	return getAppInfo(appId).getNumAppPEs();
}

size_t
CraySLURMFrontend::getNumAppNodes(AppId appId) const {
	return getAppInfo(appId).getNumAppNodes();
}

std::vector<std::string> const
CraySLURMFrontend::getAppHostsList(AppId appId) const {
	return getAppInfo(appId).getAppHostsList();
}

std::vector<CTIHost> const
CraySLURMFrontend::getAppHostsPlacement(AppId appId) const {
	return getAppInfo(appId).getAppHostsPlacement();
}

std::string const
CraySLURMFrontend::getHostName(void) const {
	return _cti_cray_slurm_getHostName();
}

std::string const
CraySLURMFrontend::getLauncherHostName(AppId appId) const {
	throw std::runtime_error("getLauncherHostName not supported for Cray SLURM (app ID " + std::to_string(appId));
}

std::string const
CraySLURMFrontend::getToolPath(AppId appId) const {
	return getAppInfo(appId).toolPath;
}

std::string const
CraySLURMFrontend::getAttribsPath(AppId appId) const {
	return getAppInfo(appId).attribsPath;
}

/* extended frontend implementation */

CraySLURMFrontend::~CraySLURMFrontend() {
	// force cleanup to happen on any pending srun launches
	_cti_mpir_releaseAllInstances();
}

AppId
CraySLURMFrontend::registerJobStep(uint32_t jobid, uint32_t stepid) {
	// iterate through the app list to try to find an existing entry for this apid
	for (auto const& appIdInfoPair : appList) {
		auto const& appInfo = appIdInfoPair.second;
		if ((appInfo.srunInfo.jobid == jobid) && (appInfo.srunInfo.stepid == stepid)) {
			return appIdInfoPair.first;
		}
	}

	// aprun pid not found in the global _cti_alps_info list
	// so lets create a new appEntry_t object for it
	auto const appId = newAppId();
	appList.emplace(appId, CraySLURMApp{jobid, stepid});
	return appId;
}

CraySLURMFrontend::SrunInfo
CraySLURMFrontend::getSrunInfo(pid_t srunPid) {
	return _cti_cray_slurm_attachAndFetchJobInfo(srunPid);
}

CraySLURMFrontend::SrunInfo
CraySLURMFrontend::getSrunInfo(AppId appId) {
	return getAppInfo(appId).getSrunInfo();
}