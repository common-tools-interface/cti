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
	using CStr = UniquePtrDestr<char>;

	// lifted asprintf
	template <typename... Args>
	static auto asprintf(char const* const formatCStr, Args&&... args) -> std::string {
		char *rawResult = nullptr;
		if (::asprintf(&rawResult, formatCStr, std::forward<Args>(args)...) < 0) {
			throw std::runtime_error("asprintf failed.");
		}
		auto const result = CStr{rawResult, ::free};
		return std::string(result.get());
	}

	// lifted mkdtemp
	static auto mkdtemp(std::string const& pathTemplate) -> std::string {
		auto rawPathTemplate = UniquePtrDestr<char>(strdup(pathTemplate.c_str()), ::free);

		if (::mkdtemp(rawPathTemplate.get())) {
			return std::string(rawPathTemplate.get());
		} else {
			throw std::runtime_error("mkdtemp failed on " + pathTemplate);
		}
	}
}


static auto
_cti_cray_slurm_extraFiles(slurmStepLayout_t const* layout, cti_mpir_procTable_t const* app_pids,
	std::string const& stagePath) -> std::vector<std::string> {

	// create SLURM layout file from steplayout data, return path
	auto createLayoutFile = [](slurmStepLayout_t const* layout, std::string const& stagePath) {
		// sanity check
		if (layout == nullptr) {
			throw std::runtime_error("app layout is null!");
		}

		// create path string to layout file
		auto const layoutPath = std::string{stagePath + "/" + SLURM_LAYOUT_FILE};

		// Open the layout file
		if (auto layoutFile = UniquePtrDestr<FILE>(fopen(layoutPath.c_str(), "wb"), ::fclose)) {

			// write the layout header
			auto const layout_hdr = slurmLayoutFileHeader_t{layout->numNodes};
			if (fwrite(&layout_hdr, sizeof(slurmLayoutFileHeader_t), 1, layoutFile.get()) != 1) {
				throw std::runtime_error("failed to write the layout header");
			}

			// write a new layout_entry with hostname/PE information from the slurm host layout info
			for (int i = 0; i < layout->numNodes; ++i) {
				auto const node = layout->hosts[i];

				auto layout_entry = slurmLayoutFile_t{};
				layout_entry.PEsHere = node.PEsHere;
				layout_entry.firstPE = node.firstPE;

				// ensure we have good hostname information
				auto const hostname_len = strlen(node.host) + 1;
				if (hostname_len > sizeof(layout_entry.host)) {
					throw std::runtime_error("hostname too large for layout buffer");
				}
				memcpy(layout_entry.host, node.host, hostname_len);

				// write the entry to file
				if (fwrite(&layout_entry, sizeof(slurmLayoutFile_t), 1, layoutFile.get()) != 1) {
					throw std::runtime_error("failed to write a host entry to layout file");
				}
			}

			return layoutPath;
		} else {
			throw std::runtime_error("failed to open layout path " + layoutPath);
		}
	};

	// create PID list file from mpir data, return path
	auto createPIDListFile = [](cti_mpir_procTable_t const* app_pids, std::string const& stagePath) {
		// sanity check
		if (app_pids == nullptr) {
			throw std::runtime_error("mpir data is null!");
		}

		// create path string to pid file
		auto const pidPath = std::string{stagePath + "/" + SLURM_PID_FILE};

		// Open the pid file
		if (auto pidFile = UniquePtrDestr<FILE>(fopen(pidPath.c_str(), "wb"), ::fclose)) {

			// write the pid header
			auto const pid_hdr = slurmPidFileHeader_t{app_pids->num_pids};
			if (fwrite(&pid_hdr, sizeof(slurmPidFileHeader_t), 1, pidFile.get()) != 1) {
				throw std::runtime_error("failed to write pidfile header");
			}

			// copy each mpir pid entry to file
			for (size_t i = 0; i < app_pids->num_pids; ++i) {

				// construct and write entry
				auto const pid_entry = slurmPidFile_t{app_pids->pids[i]};
				if (fwrite(&pid_entry, sizeof(slurmPidFile_t), 1, pidFile.get()) != 1) {
					throw std::runtime_error("failed to write pidfile entry");
				}
			}
		} else {
			throw std::runtime_error("failed to open pidfile path " + pidPath);
		}

		return pidPath;
	};

	std::vector<std::string> result;

	// add layout file as extra file to ship
	result.push_back(createLayoutFile(layout, stagePath));

	// check to see if there is an app_pids member, if so we need to create the 
	// pid file
	if (app_pids != nullptr) {
		result.push_back(createPIDListFile(app_pids, stagePath));
	}

	return result;
}

struct CraySlurmInfo {
	uint32_t			jobid;			// SLURM job id
	uint32_t			stepid;			// SLURM step id
	uint64_t			apid;			// Cray variant of step+job id
	UniquePtrDestr<slurmStepLayout_t>	layout;			// Layout of job step
	std::string			toolPath;		// Backend staging directory
	std::string			attribsPath;	// Backend Cray specific directory
	bool				dlaunch_sent;	// True if we have already transfered the dlaunch utility
	std::string			stagePath;		// directory to stage this instance files in for transfer to BE

	mpir_id_t			mpir_id;		// mpir id to release barrier
	std::vector<std::string> extraFiles;	// extra files to transfer to BE associated with this app

	CraySlurmInfo(mpir_id_t mpir_id_, uint32_t jobid_, uint32_t stepid_)
		: jobid{jobid_}
		, stepid{stepid_}
		, apid{CRAY_SLURM_APID(jobid, stepid)}
		, layout{_cti_cray_slurm_getLayout(jobid, stepid)}
		, toolPath{CRAY_SLURM_TOOL_DIR}
		, attribsPath{cstr::asprintf(CRAY_SLURM_CRAY_DIR, (long long unsigned int)apid)}
		, dlaunch_sent{false}
		, stagePath{cstr::mkdtemp(std::string{_cti_getCfgDir() + "/" + SLURM_STAGE_DIR})}
		, mpir_id{mpir_id_}
		, extraFiles(_cti_cray_slurm_extraFiles(layout.get(), nullptr, stagePath)) {

		// sanity check - Note that 0 is a valid step id.
		if (jobid == 0) {
			throw std::runtime_error("Invalid jobid " + std::to_string(jobid));
		}

		// if an mpir session id was provided, fetch more info from MPIR_PROCTABLE
		if (mpir_id > 0) {
			auto const procTable = UniquePtrDestr<cti_mpir_procTable_t>{
				_cti_mpir_newProcTable(mpir_id),
				_cti_mpir_deleteProcTable
			};
			extraFiles = _cti_cray_slurm_extraFiles(layout.get(), procTable.get(), stagePath);
		}
	}

	CraySlurmInfo(uint32_t jobid_, uint32_t stepid_)
		: CraySlurmInfo{mpir_id_t{-1}, jobid_, stepid_} {}

	~CraySlurmInfo() {
		// cleanup staging directory if it exists
		if (!stagePath.empty()) {
			_cti_removeDirectory(stagePath.c_str());
		}
	}

	void releaseBarrier() {
		if (_cti_mpir_releaseInstance(mpir_id)) {
			throw std::runtime_error("srun barrier release operation failed.");
		}
		mpir_id = -1;
	}
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

// Note that we should provide this as a jobid.stepid format. It will make turning
// it into a Cray apid easier on the backend since we don't lose any information
// with this format.
static std::string _cti_cray_slurm_getJobId(CraySlurmInfo const& my_app) {
	return std::string{std::to_string(my_app.jobid) + "." + std::to_string(my_app.stepid)};
}

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
	CraySLURMFrontend::SrunInfo result;

	// get the jobid string for slurm and convert to jobid
	if (auto const jobid_str = handle::cstr{_cti_mpir_getStringAt(mpir_id, "totalview_jobid")}) {
		result.jobid = std::stoul(jobid_str.get());
	} else {
		throw std::runtime_error("failed to get jobid string via MPIR.");
	}

	// get the jobid string for slurm and convert to jobid
	if (auto const stepid_str = handle::cstr{_cti_mpir_getStringAt(mpir_id, "totalview_stepid")}) {
		result.jobid = std::stoul(stepid_str.get());
	} else {
		fprintf(stderr, "cti_fe: Warning: stepid not found! Defaulting to 0.\n");
		result.jobid = 0;
	}

	return result;
}

// generate an SrunInfo from an existing app
static CraySLURMFrontend::SrunInfo
_cti_cray_slurm_getSrunInfo(CraySlurmInfo& my_app) {
	return CraySLURMFrontend::SrunInfo{my_app.jobid, my_app.stepid};
}

// fetch app info from its srun pid and generate an SrunInfo
static CraySLURMFrontend::SrunInfo
_cti_cray_slurm_attachAndFetchJobInfo(pid_t srunPid) {
	// sanity check
	if (srunPid <= 0) {
		throw std::runtime_error("Invalid srunPid " + std::to_string(srunPid));
	}

	// sanity check
	if (srunPid <= 0) {
		throw std::runtime_error("Invalid srunPid " + std::to_string(srunPid));
	}

	// get the launcher path
	if (auto const launcher_path = handle::cstr{_cti_pathFind(_cti_cray_slurm_getLauncherName().c_str(), nullptr)}) {

		// Create a new MPIR instance. We want to interact with it.
		if (auto const mpir_id = handle::MPIR{_cti_mpir_newAttachInstance(launcher_path.get(), srunPid)}) {
			return _cti_cray_slurm_fetchSrunInfo(mpir_id.get());
		} else {
			throw std::runtime_error("Failed to create MPIR attach instance.");
		}
	} else {
		throw std::runtime_error("Failed to find launcher in path: " + _cti_cray_slurm_getLauncherName());
	}
}

static std::unique_ptr<CraySlurmInfo>
_cti_cray_slurm_launch_common(	const char * const launcher_argv[], int stdout_fd, int stderr_fd,
								const char *inputFile, const char *chdirPath,
								const char * const env_list[], int doBarrier, cti_app_id_t newAppId)
{
	mpir_id_t			mpir_id;
	std::unique_ptr<CraySlurmInfo> sinfo;
	char *				sym_str;
	char *				end_p;
	uint32_t			jobid;
	uint32_t			stepid;
	cti_mpir_procTable_t *	pids;
	const char *		launcher_path;

	// get the launcher path
	launcher_path = _cti_pathFind(SRUN, NULL);
	if (launcher_path == NULL) {
		throw std::runtime_error("Required environment variable not set: " + std::string(BASE_DIR_ENV_VAR));
	}

	// open input file (or /dev/null to avoid stdin contention)
	int input_fd = -1;
	if (inputFile == NULL) {
		inputFile = "/dev/null";
	}
	errno = 0;
	input_fd = open(inputFile, O_RDONLY);
	if (input_fd < 0) {
		throw std::runtime_error("Failed to open input file " + std::string(inputFile) +": " + std::string(strerror(errno)));
	}
	
	// Create a new MPIR instance. We want to interact with it.
	if ((mpir_id = _cti_mpir_newLaunchInstance(launcher_path, launcher_argv, env_list, input_fd, stdout_fd, stderr_fd)) < 0) {
		// binary name is not always first argument (launcher_argv excludes the usual argv[0])
		std::string argvString;
		for (const char* const* arg = launcher_argv; *arg != nullptr; arg++) {
			argvString.push_back(' ');
			argvString += *arg;
		}
		throw std::runtime_error("Failed to launch: " + argvString);
	}
	
	// get the jobid string for slurm
	if ((sym_str = _cti_mpir_getStringAt(mpir_id, "totalview_jobid")) == NULL) {
		_cti_mpir_releaseInstance(mpir_id);
		throw std::runtime_error("failed to read totalview_jobid");
	}

	// convert the string into the actual jobid
	errno = 0;
	jobid = (uint32_t)strtoul(sym_str, &end_p, 10);

	// check for errors
	if ((errno == ERANGE && jobid == ULONG_MAX) || (errno != 0 && jobid == 0)) {
		_cti_mpir_releaseInstance(mpir_id);
		free(sym_str);
		throw std::runtime_error("strtoul failed (parse).");
	}
	if (end_p == NULL || *end_p != '\0') {
		_cti_mpir_releaseInstance(mpir_id);
		free(sym_str);
		throw std::runtime_error("strtoul failed (partial parse).");
	}
	free(sym_str);
	
	// get the stepid string for slurm
	if ((sym_str = _cti_mpir_getStringAt(mpir_id, "totalview_stepid")) == NULL)
	{
		/*
		// error already set
		_cti_mpir_releaseInstance(mpir_id);
		
		return 0;
		*/
		// FIXME: Once totalview_stepid starts showing up we can use it.
		fprintf(stderr, "cti_fe: Warning: stepid not found! Defaulting to 0.\n");
		sym_str = strdup("0");
	}
	
	// convert the string into the actual stepid
	errno = 0;
	stepid = (uint32_t)strtoul(sym_str, &end_p, 10);
	
	// check for errors
	if ((errno == ERANGE && stepid == ULONG_MAX) || (errno != 0 && stepid == 0)) {
		_cti_mpir_releaseInstance(mpir_id);
		free(sym_str);
		throw std::runtime_error("strtoul failed (parse).");
	}
	if (end_p == NULL || *end_p != '\0') {
		_cti_mpir_releaseInstance(mpir_id);
		free(sym_str);
		throw std::runtime_error("strtoul failed (partial parse).");
	}
	free(sym_str);
	
	// get the pid information from slurm
	// FIXME: When/if pmi_attribs get fixed for the slurm startup barrier, this
	// call can be removed. Right now the pmi_attribs file is created in the pmi
	// ctor, which is called after the slurm startup barrier, meaning it will not
	// yet be created when launching. So we need to send over a file containing
	// the information to the compute nodes.

	// register this app with the application interface
	try {
		sinfo = shim::make_unique<CraySlurmInfo>(mpir_id, jobid, stepid); // nonnull, throws
	} catch (std::exception const& ex) {
		// failed to register the jobid/stepid
		_cti_mpir_releaseInstance(mpir_id);
		throw ex;
	}

	// If we should not wait at the barrier, call the barrier release function.
	if (!doBarrier) {
		sinfo->releaseBarrier();
	}

	return sinfo;
}

static void
_cti_cray_slurm_killApp(CraySlurmInfo& my_app, int signum)
{
	ManagedArgv launcherArgv;
	int					mypid;

	// create the args for scancel

	// first argument should be "scancel"
	launcherArgv.add(SCANCEL);

	// second argument is quiet
	launcherArgv.add("-Q");

	// third argument is signal number
	launcherArgv.add("-s");
	launcherArgv.add(std::to_string(signum));

	// fourth argument is the jobid.stepid
	launcherArgv.add(std::to_string(my_app.jobid) + "." + std::to_string(my_app.stepid));

	// fork off a process to launch scancel
	mypid = fork();
	
	// error case
	if (mypid < 0) {
		throw std::runtime_error("Fatal fork error.");
	}
	
	// child case
	if (mypid == 0) {
		// exec scancel
		execvp(SCANCEL, launcherArgv.get());
		
		// exec shouldn't return
		fprintf(stderr, "CTI error: Return from exec.\n");
		perror("execvp");
		_exit(1);
	}

	// parent case

	// wait until the scancel finishes
	waitpid(mypid, NULL, 0);
}

static void
_cti_cray_slurm_ship_package(CraySlurmInfo& my_app, std::string const& package) {
	ManagedArgv launcherArgv;

	// ensure numNodes is non-zero
	if (my_app.layout->numNodes <= 0) {
		throw std::runtime_error("Application " + std::to_string(my_app.jobid) + "." + std::to_string(my_app.stepid) + " does not have any nodes.");
	}

	// create the args for sbcast
	
	launcherArgv.add(SBCAST);
	launcherArgv.add("-C");
	launcherArgv.add("-j");
	launcherArgv.add(std::to_string(my_app.jobid));
	launcherArgv.add(package);
	launcherArgv.add("--force");

	if (auto packageName = UniquePtrDestr<char>(_cti_pathToName(package.c_str()), ::free)) {
		launcherArgv.add(std::string(CRAY_SLURM_TOOL_DIR) + "/" + packageName.get());
	} else {
		throw std::runtime_error("_cti_pathToName failed");
	}

	// now ship the tarball to the compute nodes
	// fork off a process to launch sbcast
	pid_t forkedPid = fork();
	
	// error case
	if (forkedPid < 0) {
		throw std::runtime_error("Fatal fork error.");
	}

	// child case
	if (forkedPid == 0) {
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

static void
_cti_cray_slurm_start_daemon(CraySlurmInfo& my_app, const char* const args[]) {
	ManagedArgv launcherArgv;

	// sanity check
	if (args == nullptr) {
		throw std::runtime_error("args array is null!");
	}
	
	// ensure numNodes is non-zero
	if (my_app.layout->numNodes <= 0) {
		throw std::runtime_error("Application " + std::to_string(my_app.jobid) + "." + std::to_string(my_app.stepid) + " does not have any nodes.");
	}
	
	// get max number of file descriptors - used later
	struct rlimit rl;
	if (getrlimit(RLIMIT_NOFILE, &rl) < 0) {
		throw std::runtime_error("getrlimit failed.");
	}

	// we want to redirect stdin/stdout/stderr to /dev/null since it is not required
	handle::Fd devNullFd(open("/dev/null", O_RDONLY));
	if (devNullFd.get() < 0) {
		throw std::runtime_error("Unable to open /dev/null for reading.");
	}

	// If we have not yet transfered the dlaunch binary, we need to do that in advance with
	// native slurm
	if (!my_app.dlaunch_sent) {
		
		// Get the location of the daemon launcher
		if (_cti_getDlaunchPath().empty()) {
			throw std::runtime_error("Required environment variable not set:" + std::string(BASE_DIR_ENV_VAR));
		}

		_cti_cray_slurm_ship_package(my_app, _cti_getDlaunchPath());

		// set transfer to true
		my_app.dlaunch_sent = 1;
	}

	// use existing launcher binary on compute node
	std::string const launcherPath(my_app.toolPath + "/" + CTI_LAUNCHER);

	// Start adding the args to the launcher argv array
	//
	// This corresponds to:
	//
	// srun --jobid=<job_id> --gres=none --mem-per-cpu=0 --mem_bind=no
	// --cpu_bind=no --share --ntasks-per-node=1 --nodes=<numNodes>
	// --nodelist=<host1,host2,...> --disable-status --quiet --mpi=none 
	// --input=none --output=none --error=none <tool daemon> <args>
	//

	launcherArgv.add(_cti_cray_slurm_getLauncherName());

	launcherArgv.add("--jobid=" + std::to_string(my_app.jobid));
	launcherArgv.add("--gres=none");
	launcherArgv.add("--mem-per-cpu=0");
	launcherArgv.add("--mem_bind=no");
	launcherArgv.add("--cpu_bind=no");
	launcherArgv.add("--share");
	launcherArgv.add("--ntasks-per-node=1");
	launcherArgv.add("--nodes=" + std::to_string(my_app.layout->numNodes));

	// create the hostlist. If there is only one entry, then we don't need to
	// iterate over the list.
	std::string hostlist(my_app.layout->hosts[0].host);
	if (my_app.layout->numNodes > 1) {
		for (int i = 1; i < my_app.layout->numNodes; ++i) {
			hostlist.push_back(' ');
			hostlist += std::string(my_app.layout->hosts[i].host);
		}
	}
	launcherArgv.add("--nodelist=" + hostlist);

	launcherArgv.add("--disable-status");
	launcherArgv.add("--quiet");
	launcherArgv.add("--mpi=none");
	launcherArgv.add("--output=none");
	launcherArgv.add("--error=none");
	launcherArgv.add(launcherPath);
	
	// merge in the args array if there is one
	if (args != NULL) {
		for (const char* const* arg = args; *arg != nullptr; arg++) {
			launcherArgv.add(*arg);
		}
	}

	// fork off a process to launch srun
	pid_t forkedPid = fork();

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
	
		// dup2 stdin
		if (dup2(devNullFd.get(), STDIN_FILENO) < 0)
		{
			// XXX: How to handle error?
			_exit(1);
		}
		
		// dup2 stdout
		if (dup2(devNullFd.get(), STDOUT_FILENO) < 0)
		{
			// XXX: How to handle error?
			_exit(1);
		}
		
		// dup2 stderr
		if (dup2(devNullFd.get(), STDERR_FILENO) < 0)
		{
			// XXX: How to handle error?
			_exit(1);
		}
		
		// close all open file descriptors above STDERR
		if (rl.rlim_max == RLIM_INFINITY)
		{
			rl.rlim_max = 1024;
		}
		for (size_t i=3; i < rl.rlim_max; ++i)
		{
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

static int
_cti_cray_slurm_getNumAppPEs(CraySlurmInfo& my_app) {
	return my_app.layout->numPEs;
}

static int
_cti_cray_slurm_getNumAppNodes(CraySlurmInfo& my_app) {
	return my_app.layout->numNodes;
}

static std::vector<std::string> const
_cti_cray_slurm_getAppHostsList(CraySlurmInfo& my_app) {
	std::vector<std::string> result;

	// ensure numNodes is non-zero
	if (my_app.layout->numNodes <= 0) {
		throw std::runtime_error("Application " + std::to_string(my_app.jobid) + "." + std::to_string(my_app.stepid) + " does not have any nodes.");
	}
	
	// allocate space for the hosts list, add an extra entry for the null terminator
	result.reserve(my_app.layout->numNodes);

	// iterate through the hosts list
	for (int i = 0; i < my_app.layout->numNodes; ++i) {
		result.emplace_back(my_app.layout->hosts[i].host);
	}

	return result;
}

static std::vector<Frontend::CTIHost> const
_cti_cray_slurm_getAppHostsPlacement(CraySlurmInfo& my_app) {
	std::vector<Frontend::CTIHost> result;

	// ensure numNodes is non-zero
	if (my_app.layout->numNodes <= 0) {
		throw std::runtime_error("Application " + std::to_string(my_app.jobid) + "." + std::to_string(my_app.stepid) + " does not have any nodes.");
	}

	// allocate space for the cti_hostsList_t struct
	result.reserve(my_app.layout->numNodes);

	// iterate through the hosts list
	for (int i = 0; i < my_app.layout->numNodes; ++i) {
		auto const newPlacement = Frontend::CTIHost{
			my_app.layout->hosts[i].host,
			(size_t)my_app.layout->hosts[i].PEsHere
		};
		result.emplace_back(newPlacement);
	}

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
static char *
_cti_cray_slurm_getHostName(void) {
	static char *hostname = NULL; // Cache the result

	// Determined the answer previously?
	if (hostname)
		return strdup(hostname);    // return cached value

	// Try the Cray /proc extension short cut
	FILE *nid_fp; // NID file stream
	if ((nid_fp = fopen(ALPS_XT_NID, "r")) != NULL)
	{
		// we expect this file to have a numeric value giving our current nid
		char file_buf[BUFSIZ];   // file read buffer
		if (fgets(file_buf, BUFSIZ, nid_fp) == NULL) {
			fclose(nid_fp);
			throw std::runtime_error("_cti_cray_slurm_getHostName fgets failed.");
		}

		// close the file stream
		fclose(nid_fp);

		// convert this to an integer value
		errno = 0;
		char *  eptr;
		int nid = (int)strtol(file_buf, &eptr, 10);

		// check for error
		if ((errno == ERANGE && nid == INT_MAX) || (errno != 0 && nid == 0)) {
			throw std::runtime_error("_cti_cray_slurm_getHostName: strtol failed.");
		}

		// check for invalid input
		if (eptr == file_buf) {
			throw std::runtime_error("_cti_cray_slurm_getHostName: Bad data in " + std::string(ALPS_XT_NID));
		}

		// create the nid hostname string
		if (asprintf(&hostname, ALPS_XT_HOSTNAME_FMT, nid) <= 0) {
			free(hostname);
			throw std::runtime_error("_cti_cray_slurm_getHostName asprintf failed.");
		}
	}

	else // Fallback to standard hostname
	{
		// allocate memory for the hostname
		if ((hostname = (decltype(hostname))malloc(HOST_NAME_MAX)) == NULL) {
			throw std::runtime_error("_cti_cray_slurm_getHostName: malloc failed.");
		}

		if (gethostname(hostname, HOST_NAME_MAX) < 0) {
			throw std::runtime_error("_cti_cray_slurm_getHostName: gethostname() failed!");
		}
	}

	return strdup(hostname); // One way or the other
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

static std::unordered_map<AppId, UniquePtrDestr<CraySlurmInfo>> appList;
static const AppId APP_ERROR = 0;
static AppId newAppId() noexcept {
	static AppId nextId = 1;
	return nextId++;
}

static CraySlurmInfo&
getAppInfo(AppId appId) {
	auto infoPtr = appList.find(appId);
	if (infoPtr != appList.end()) {
		return *(infoPtr->second);
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
	return _cti_cray_slurm_getJobId(getAppInfo(appId));
}

AppId
CraySLURMFrontend::launch(CArgArray launcher_argv, int stdout_fd, int stderr,
					 CStr inputFile, CStr chdirPath, CArgArray env_list) {
	auto const appId = newAppId();
	appList[appId] = _cti_cray_slurm_launch_common(launcher_argv, stdout_fd, stderr, inputFile, chdirPath, env_list, 0, appId);
	return appId;
}

AppId
CraySLURMFrontend::launchBarrier(CArgArray launcher_argv, int stdout_fd, int stderr,
							CStr inputFile, CStr chdirPath, CArgArray env_list) {
	auto const appId = newAppId();
	appList[appId] = _cti_cray_slurm_launch_common(launcher_argv, stdout_fd, stderr, inputFile, chdirPath, env_list, 1, appId);
	return appId;
}

void
CraySLURMFrontend::releaseBarrier(AppId appId) {
	getAppInfo(appId).releaseBarrier();
}

void
CraySLURMFrontend::killApp(AppId appId, int signal) {
	_cti_cray_slurm_killApp(getAppInfo(appId), signal);
}

std::vector<std::string> const
CraySLURMFrontend::getExtraFiles(AppId appId) const {
	return getAppInfo(appId).extraFiles;
}


void
CraySLURMFrontend::shipPackage(AppId appId, std::string const& tarPath) const {
	_cti_cray_slurm_ship_package(getAppInfo(appId), tarPath);
}

void
CraySLURMFrontend::startDaemon(AppId appId, CArgArray argv) const {
	_cti_cray_slurm_start_daemon(getAppInfo(appId), argv);
}

size_t
CraySLURMFrontend::getNumAppPEs(AppId appId) const {
	return _cti_cray_slurm_getNumAppPEs(getAppInfo(appId));
}

size_t
CraySLURMFrontend::getNumAppNodes(AppId appId) const {
	return _cti_cray_slurm_getNumAppNodes(getAppInfo(appId));
}

std::vector<std::string> const
CraySLURMFrontend::getAppHostsList(AppId appId) const {
	return _cti_cray_slurm_getAppHostsList(getAppInfo(appId));
}

std::vector<CTIHost> const
CraySLURMFrontend::getAppHostsPlacement(AppId appId) const {
	return _cti_cray_slurm_getAppHostsPlacement(getAppInfo(appId));
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
	// create the cray variation of the jobid+stepid
	uint64_t apid = CRAY_SLURM_APID(jobid, stepid);

	// iterate through the app list to try to find an existing entry for this apid
	for (auto const& appIdInfoPair : appList) {
		if (appIdInfoPair.second->apid == apid) {
			return appIdInfoPair.first;
		}
	}

	// aprun pid not found in the global _cti_alps_info list
	// so lets create a new appEntry_t object for it
	auto const appId = newAppId();
	appList[appId] = shim::make_unique<CraySlurmInfo>(jobid, stepid);
	return appId;
}

CraySLURMFrontend::SrunInfo
CraySLURMFrontend::getSrunInfo(pid_t srunPid) {
	return _cti_cray_slurm_attachAndFetchJobInfo(srunPid);
}

CraySLURMFrontend::SrunInfo
CraySLURMFrontend::getSrunInfo(AppId appId) {
	return _cti_cray_slurm_getSrunInfo(getAppInfo(appId));
}