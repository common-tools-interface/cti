/******************************************************************************\
 * alps_fe.c - alps specific frontend library functions.
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

#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include <linux/limits.h>

#include "alps/apInfo.h"

#include "cti_defs.h"
#include "cti_fe.h"

#include "frontend/Frontend.hpp"
#include "alps_fe.hpp"

#include "useful/cti_useful.h"
#include "useful/Dlopen.hpp"
#include "useful/make_unique.hpp"
#include "useful/strong_argv.hpp"

/* Types used here */

template <typename... Args>
static std::string
string_asprintf(const char* const formatCStr, Args... args) {
	char *rawResult = nullptr;
	if (asprintf(&rawResult, formatCStr, args...) < 0) {
		throw std::runtime_error("asprintf failed.");
	}
	UniquePtrDestr<char> result(rawResult, ::free);
	return std::string(result.get());
}

struct BarrierControl : private NonCopyable<BarrierControl> {
	int readPipe[2], writePipe[2];
	int sync_int;
	bool initialized;

	void closeIfValid(int& fd) {
		if (fd < 0) {
			return;
		} else {
			::close(fd);
			fd = -1;
		}
	}

	// return barrier read, write FD to pass to aprun
	std::pair<int, int> setupChild() {
		if (!initialized) {
			throw std::runtime_error("Control pipe not initialized.");
		}
		closeIfValid(readPipe[1]);
		closeIfValid(writePipe[0]);
		return {writePipe[1], readPipe[0]};
	}

	void setupParent() {
		if (!initialized) {
			throw std::runtime_error("Control pipe not initialized.");
		}
		closeIfValid(readPipe[0]);
		closeIfValid(writePipe[1]);
	}

	void wait() {
		if (!initialized) {
			throw std::runtime_error("Control pipe not initialized.");
		}
		if (read(writePipe[0], &sync_int, sizeof(sync_int)) <= 0) {
			throw std::runtime_error("Control pipe read failed.");
		}
	}

	void release() {
		if (!initialized) {
			throw std::runtime_error("Control pipe not initialized.");
		}
		// Conduct a pipe write for alps to release app from the startup barrier.
		// Just write back what we read earlier.
		if (write(readPipe[1], &sync_int, sizeof(sync_int)) <= 0) {
			throw std::runtime_error("Aprun barrier release operation failed.");
		}
	}

	void invalidate() {
		readPipe[0] = -1;
		readPipe[1] = -1;
		writePipe[0] = -1;
		writePipe[1] = -1;
		initialized = false;
	}

	void closeAll() {
		closeIfValid(readPipe[0]);
		closeIfValid(readPipe[1]);
		closeIfValid(writePipe[0]);
		closeIfValid(writePipe[1]);
	}

	BarrierControl() : initialized{true} {
		if ((pipe(readPipe) < 0) || (pipe(writePipe) < 0)) {
			throw std::runtime_error("Pipe creation failure.");
		}
	}

	BarrierControl& operator=(BarrierControl&& moved) {
		readPipe[0]  = moved.readPipe[0];
		readPipe[1]  = moved.readPipe[1];
		writePipe[0] = moved.writePipe[0];
		writePipe[1] = moved.writePipe[1];
		initialized  = moved.initialized;
		moved.invalidate();
		return *this;
	}

	~BarrierControl() {
		closeAll();
	}
};

// setup overwatch to ensure aprun gets killed off on error
struct OverwatchHandle {
	UniquePtrDestr<cti_overwatch_t> owatchPtr;

	OverwatchHandle() {}

	OverwatchHandle(std::string const& overwatchPath) {
		if (!overwatchPath.empty()) {

			// overwatch handler to enforce cleanup
			owatchPtr = UniquePtrDestr<cti_overwatch_t>(
				_cti_create_overwatch(overwatchPath.c_str()),
				_cti_exit_overwatch);

			// check results
			if (!owatchPtr) {
				throw std::runtime_error("_cti_create_overwatch failed.");
			}
		} else {
			throw std::runtime_error("_cti_getOverwatchPath empty.");
		}
	}

	void assign(pid_t appPid) {
		if (_cti_assign_overwatch(owatchPtr.get(), appPid)) {
			throw std::runtime_error("_cti_assign_overwatch failed.");
		}
	}
};

struct CTISignalGuard {
	sigset_t *mask;

	CTISignalGuard() : mask{_cti_block_signals()} {
		if (!mask) {
			throw std::runtime_error("_cti_block_signals failed.");
		}
	}

	~CTISignalGuard() {
		if (mask) {
			_cti_restore_signals(mask);
		}
	}

	void restoreChildSignals() {
		if (mask) {
			if (_cti_child_setpgid_restore(mask)) {
				// don't fail, but print out an error
				fprintf(stderr, "CTI error: _cti_child_setpgid_restore failed!\n");
			}
			mask = nullptr;
		}
	}

	void restoreParentSignals(pid_t childPid) {
		if (mask) {
			if (_cti_setpgid_restore(childPid, mask)) {
				// attempt to kill aprun since the caller will not recieve the aprun pid
				// just in case the aprun process is still hanging around.
				kill(childPid, DEFAULT_SIG);
				throw std::runtime_error("_cti_setpgid_restore failed.");
			}
			mask = nullptr;
		}
	}
};

/* dynamically loaded functions from libalps */

class LibALPS {
private: // types
	struct FnTypes {
		using alps_get_apid = uint64_t(int, pid_t);
		using alps_get_appinfo_ver2_err = int(uint64_t, appInfo_t *, cmdDetail_t **, placeNodeList_t **, char **, int *);
		using alps_launch_tool_helper = char*(uint64_t, int, int, int, int, char **);
		using alps_get_overlap_ordinal = int(uint64_t, char **, int *);
	};

private: // variables
	Dlopen::Handle libAlpsHandle;

public: // variables
	std::function<FnTypes::alps_get_apid>             alps_get_apid;
	std::function<FnTypes::alps_get_appinfo_ver2_err> alps_get_appinfo_ver2_err;
	std::function<FnTypes::alps_launch_tool_helper>   alps_launch_tool_helper;
	std::function<FnTypes::alps_get_overlap_ordinal>  alps_get_overlap_ordinal;

public: // interface
	LibALPS()
		: libAlpsHandle(ALPS_FE_LIB_NAME)
		, alps_get_apid(libAlpsHandle.load<FnTypes::alps_get_apid>("alps_get_apid"))
		, alps_get_appinfo_ver2_err(libAlpsHandle.load<FnTypes::alps_get_appinfo_ver2_err>("alps_get_appinfo_ver2_err"))
		, alps_launch_tool_helper(libAlpsHandle.load<FnTypes::alps_launch_tool_helper>("alps_launch_tool_helper"))
		, alps_get_overlap_ordinal(libAlpsHandle.load<FnTypes::alps_get_overlap_ordinal>("alps_get_overlap_ordinal")) {}
};
static const LibALPS libAlps;

/*
*       _cti_alps_getSvcNodeInfo - read nid from alps defined system locations
*
*       args: None.
*
*       return value: serviceNode_t pointer containing the service nodes nid,
*       or else NULL on error.
*
*/
struct ALPSSvcNodeInfo {
	int nid;

	ALPSSvcNodeInfo(const char* const nidPath) {
		if (FILE *nidFile = fopen(nidPath, "r")) {
			char file_buf[BUFSIZ];  // file read buffer

			// we expect this file to have a numeric value giving our current nid
			if (fgets(file_buf, BUFSIZ, nidFile)) {
				nid = atoi(file_buf); // convert this to an integer value
				fclose(nidFile); // close the file stream
			} else {
				fclose(nidFile); // close the file stream
				throw std::runtime_error("fgets failed:" + std::string(nidPath));
			}
		} else {
			throw std::runtime_error("fopen failed:" + std::string(nidPath));
		}
	}

	int getNid() const { return nid; }
};
static ALPSSvcNodeInfo svcNodeInfo(ALPS_XT_NID);

static std::string const
_cti_alps_getLauncherName() {
	if (char* launcher_name_env = getenv(CTI_LAUNCHER_NAME)) {
		return std::string(launcher_name_env);
	} else {
		return std::string(APRUN);
	}
}

// object containing all needed information for a running alps app. obtained on construction from libAlps
struct AlpsInfo {
	cti_app_id_t		appId;			// CTI appid associated with this AlpsInfo obj
	uint64_t			apid;			// ALPS apid
	int					pe0Node;		// ALPS PE0 node id
	appInfo_t			alpsAppInfo;	// ALPS application information
	UniquePtrDestr<cmdDetail_t> cmdDetail;		// ALPS application command information (width, depth, memory, command name) of length appinfo.numCmds
	UniquePtrDestr<placeNodeList_t> places;		// ALPS application placement information (nid, processors, PE threads) of length appinfo.numPlaces

	pid_t				aprunPid;		// Optional objects used for launched applications.
	BarrierControl		startupBarrier;
	OverwatchHandle		overwatchHandle;

	std::string			toolPath;				// Backend staging directory
	std::string			attribsPath;			// Backend directory where pmi_attribs is located
	bool				dlaunch_sent = false;	// True if we have already transfered the dlaunch utility

	AlpsInfo(uint64_t apid_, cti_app_id_t appId_) : appId(appId_), apid(apid_) {
		// ensure proper wlm in use
		if (cti_current_wlm() != CTI_WLM_ALPS) {
			throw std::runtime_error("Invalid call. ALPS WLM not in use.");
		}

		// ensure valid apid
		if (apid == 0) {
			throw std::runtime_error("Invalid apid " + std::to_string(apid));
		}

		// retrieve detailed information about our app
		{ char *appinfo_err = NULL;
			cmdDetail_t *rawCmdDetail;
			placeNodeList_t *rawPlaces;
			if (libAlps.alps_get_appinfo_ver2_err(apid, &alpsAppInfo, &rawCmdDetail, &rawPlaces, &appinfo_err, nullptr) != 1) {
				if ((appinfo_err != nullptr) || (rawCmdDetail == nullptr) || (places == nullptr)) {
					throw std::runtime_error("alps_get_appinfo_ver2_err() failed: " + std::string(appinfo_err));
				} else {
					throw std::runtime_error("alps_get_appinfo_ver2_err() failed.");
				}
			}
			cmdDetail = UniquePtrDestr<cmdDetail_t>(rawCmdDetail, ::free);
			places    = UniquePtrDestr<placeNodeList_t>(rawPlaces, ::free);
		}

		// Note that cmdDetail is a two dimensional array with appinfo.numCmds elements.
		// Note that places is a two dimensional array with appinfo.numPlaces elements.
		
		// save pe0 NID
		pe0Node = places.get()[0].nid;

		// Check to see if this system is using the new OBS system for the alps
		// dependencies. This will affect the way we set the toolPath for the backend
		{ struct stat statbuf;
			if (stat(ALPS_OBS_LOC, &statbuf) < 0) {
				// Could not stat ALPS_OBS_LOC, assume it's using the old format.
				toolPath = string_asprintf(OLD_TOOLHELPER_DIR, apid, apid);
				attribsPath = string_asprintf(OLD_ATTRIBS_DIR, apid);
			} else {
				// Assume it's using the OBS format
				toolPath = string_asprintf(OBS_TOOLHELPER_DIR, apid, apid);
				attribsPath = string_asprintf(OBS_ATTRIBS_DIR, apid);
			}
		}
	}
};

static uint64_t
_cti_alps_getApid(pid_t aprunPid) {
	// sanity check
	if (aprunPid <= 0) {
		throw std::runtime_error("Invalid pid " + std::to_string(aprunPid));
	}

	return libAlps.alps_get_apid(svcNodeInfo.getNid(), aprunPid);
}

static ALPSFrontend::AprunInfo
_cti_alps_getAprunInfo(AlpsInfo& my_app) {
	ALPSFrontend::AprunInfo aprunInfo;
	aprunInfo.apid = my_app.apid;
	aprunInfo.aprunPid = my_app.alpsAppInfo.aprunPid;
	return aprunInfo;
}

static std::string
_cti_alps_getHostName(void) {
	return string_asprintf(ALPS_XT_HOSTNAME_FMT, svcNodeInfo.getNid());
}

static std::string
_cti_alps_getLauncherHostName(AlpsInfo& my_app) {
	return string_asprintf(ALPS_XT_HOSTNAME_FMT, my_app.alpsAppInfo.aprunNid);
}

static int
_cti_alps_getNumAppPEs(AlpsInfo& my_app) {
	// loop through the placement list
	int numPEs = 0;
	for (int i = 0; i < my_app.alpsAppInfo.numPlaces; ++i) {
		numPEs += my_app.places.get()[i].numPEs;
	}

	return numPEs;
}

static int
_cti_alps_getNumAppNodes(AlpsInfo& my_app)
{
	return my_app.alpsAppInfo.numPlaces;
}

static std::vector<std::string>
_cti_alps_getAppHostsList(AlpsInfo& my_app) {
	std::vector<std::string> hosts;

	// ensure my_app.alpsAppInfo.numPlaces is non-zero
	if ( my_app.alpsAppInfo.numPlaces <= 0 ) {
		// no nodes in the application
		throw std::runtime_error("Application " + std::to_string(my_app.apid) + "does not have any nodes.");
	}

	// set the number of hosts for the application
	size_t numHosts = my_app.alpsAppInfo.numPlaces;
	hosts.reserve(numHosts);

	// loop through the placement list
	for (size_t i = 0; i < numHosts; ++i) {
		hosts.emplace_back(std::move(string_asprintf(ALPS_XT_HOSTNAME_FMT, my_app.places.get()[i].nid)));
	}

	return hosts;
}

static std::vector<Frontend::CTIHost>
_cti_alps_getAppHostsPlacement(AlpsInfo& my_app) {
	std::vector<Frontend::CTIHost> placement_list;

	// ensure my_app.alpsAppInfo.numPlaces is non-zero
	if ( my_app.alpsAppInfo.numPlaces <= 0 ) {
		// no nodes in the application
		throw std::runtime_error("Application " + std::to_string(my_app.apid) + "does not have any nodes.");
	}

	// set the number of hosts for the application
	size_t numHosts = my_app.alpsAppInfo.numPlaces;
	placement_list.reserve(numHosts);

	// loop through the placement list
	for (size_t i = 0; i < numHosts; i++) {
		// create the hostname string and set the number of PEs
		placement_list.emplace_back(
			std::move(string_asprintf(ALPS_XT_HOSTNAME_FMT, my_app.places.get()[i].nid)),
			my_app.places.get()[i].numPEs);
	}

	return placement_list;
}

static bool
_cti_alps_pathIsWrappedAprun(const char *aprun_path) {
	auto realPath = [&](const char* path) {
		return UniquePtrDestr<char>(realpath(path, nullptr), ::free);
	};

	auto matchesAprunPath = [&](const char *otherPath) {
		return !strncmp(aprun_path, otherPath, strlen(otherPath));
	};

	// The following is used when a user sets the CRAY_APRUN_PATH environment
	// variable to the absolute location of aprun. It overrides the default
	// behavior.
	if (auto usr_aprun_path = getenv(USER_DEF_APRUN_LOC_ENV_VAR)) {
		// There is a path to aprun set, lets try to stat it to make sure it exists
		struct stat st;
		if (stat(usr_aprun_path, &st)) {
			// We were unable to stat the file pointed to by usr_aprun_path, lets
			// print a warning and fall back to using the default method.
			fprintf(stderr, "%s is set but cannot stat its value.", USER_DEF_APRUN_LOC_ENV_VAR);
			return false;
		} else {
			// We were able to stat it! If doesn't match aprun_path, then is wrapper
			return !matchesAprunPath(usr_aprun_path);
		}
	}
	
	// check to see if the path points at the old aprun location
	if (!matchesAprunPath(OLD_APRUN_LOCATION)) {
		// it doesn't point to the old aprun location, so check the new OBS
		// location. Note that we need to resolve this location with a call to 
		// realpath.
		if (auto default_obs_realpath = realPath(OBS_APRUN_LOCATION)) {
			// Check the string. If doesn't match aprun_path, then is wrapper
			return !matchesAprunPath(default_obs_realpath.get());
		} else {
			// Fix for BUG 810204 - Ensure that the OLD_APRUN_LOCATION exists before giving up.
			if (auto default_old_realpath = realPath(OLD_APRUN_LOCATION)) {
				return false; // FIXME: Assume this is the real aprun...
			} else {
				// This is a wrapper. Return 1.
				return true;
			}
		}
	} else {
		return false; // matches path, no wrapper
	}
}

static int
_cti_alps_filename_is_pid(const struct dirent *a) {
	unsigned long int pid;

	// We only want to get files that are of the format /proc/<pid>/
	// if the assignment succeeds then the file matches this type.
	return sscanf(a->d_name, "%lu", &pid);
}

/*
 * _cti_alps_set_dsl_env_var: Ensure DSL is enabled for the alps tool helper unless explicitly overridden
 *
 * Detail:
 * 		Sets the environment variable defined in LIBALPS_ENABLE_DSL_ENV_VAR which enables the DSL service
 *		in the alps tool helper. This can be overriden with the environment variable defined by
 *		CTI_LIBALPS_ENABLE_DSL_ENV_VAR. If this environment variable is set to 0, DSL will be disabled.
 *
 * Arguments:
 * 		None
 *
 */
static void
_cti_alps_set_dsl_env_var() {
	setenv(LIBALPS_ENABLE_DSL_ENV_VAR, "1", 1);
	if(const char* cti_libalps_enable_dsl = getenv(CTI_LIBALPS_ENABLE_DSL_ENV_VAR)) {
		if(strcmp(cti_libalps_enable_dsl, "0") == 0 ) {
			unsetenv(LIBALPS_ENABLE_DSL_ENV_VAR);
		}
	}
}

// The following code was added to detect if a site is using a wrapper script
// around aprun. Some sites use these as prologue/epilogue. I know this
// functionality has been added to alps, but sites are still using the
// wrapper. If this is no longer true in the future, rip this stuff out.
static pid_t
getWrappedAprunPid(pid_t forkedPid) {
	// FIXME: This doesn't handle multiple layers of depth.

	// first read the link of the exe in /proc for the aprun pid.
	auto readLink = [](std::string const& path) {
		char buf[PATH_MAX];
		ssize_t len = ::readlink(path.c_str(), buf, sizeof(buf)-1);
		if (len >= 0) {
			buf[len] = '\0';
			return std::string(buf);
		} else {
			return std::string();
		}
	};

	// create the path to the /proc/<pid>/exe location
	std::string const forkedExeLink("/proc/" + std::to_string(forkedPid) + "/exe");
	
	// alloc size for the path buffer, base this on PATH_MAX. Note that /proc
	// is not posix compliant so trying to do the right thing by calling lstat
	// won't work.
	std::string forkedExePath = readLink(forkedExeLink);
	if (forkedExePath.empty()) {
		fprintf(stderr, "readlink failed on %s", forkedExeLink.c_str());
		return forkedPid;
	}

	// check the link path to see if its the real aprun binary
	if (_cti_alps_pathIsWrappedAprun(forkedExePath.c_str())) {
		// aprun is wrapped, we need to start harvesting stuff out from /proc.

		// start by getting all the /proc/<pid>/ files
		std::vector<UniquePtrDestr<struct dirent>> direntList;
		{ struct dirent **rawDirentList;
			size_t numDirents = 0;
			int direntListLen = scandir("/proc", &rawDirentList, _cti_alps_filename_is_pid, nullptr);
			if (direntListLen < 0) {
				kill(forkedPid, DEFAULT_SIG);
				throw std::runtime_error("Could not enumerate /proc for real aprun process.");
			} else {
				for (size_t i = 0; i < numDirents; i++) {
					direntList.emplace_back(rawDirentList[i], ::free);
				}
			}
		}

		// loop over each entry reading in its ppid from its stat file
		for (auto const& dirent : direntList) {

			// create the path to the /proc/<pid>/stat for this entry
			std::string const childStatPath("/proc/" + std::string(dirent->d_name) + "/stat");

			pid_t proc_ppid;

			// open the stat file for reading the ppid
			if (auto statFile = fopen(childStatPath.c_str(), "r")) {

				// parse the stat file for the ppid
				int parsedFields = fscanf(statFile, "%*d %*s %*c %d", &proc_ppid);

				// close the stat file
				fclose(statFile);

				// verify fscanf result
				if (parsedFields != 1) {
					// could not get the ppid?? continue to the next entry
					continue;
				}
			} else {
				// ignore this entry and go onto the next
				continue;
			}

			// check to see if the ppid matches the pid of our child
			if (proc_ppid == forkedPid) {
				// it matches, check to see if this is the real aprun

				// create the path to the /proc/<pid>/exe for this entry
				std::string const childExeLink("/proc/" + std::string(dirent->d_name) + "/exe");
				
				// read the exe link to get what its pointing at
				std::string const childExePath(readLink(childExeLink));
				if (childExePath.empty()) {
					// if the readlink failed, ignore the error and continue to
					// the next entry. Its possible that this could fail under
					// certain scenarios like the process is running as root.
					continue;
				}
				
				// check if this is the real aprun
				if (!_cti_alps_pathIsWrappedAprun(childExePath.c_str())) {
					// success! This is the real aprun. stored in the appinfo later
					return (pid_t)strtoul(dirent->d_name, nullptr, 10);
				}
			}
		}

		// we did not find the child aprun process. We should error out at
		// this point since we will error out later in an alps call anyways.
		throw std::runtime_error("Could not find child aprun process of wrapped aprun command.");
	} else {
		return forkedPid;
	}
}

// This is the actual function that can do either a launch with barrier or one
// without.
static std::unique_ptr<AlpsInfo>
_cti_alps_launch_common(	const char * const launcher_argv[], int stdout_fd, int stderr_fd,
							const char *inputFile, const char *chdirPath,
							const char * const env_list[], int doBarrier, cti_app_id_t newAppId) {

	_cti_alps_set_dsl_env_var(); // Ensure DSL is enabled for the alps tool helper unless explicitly overridden
	BarrierControl startupBarrier; // only let child continue when the parent is ready to control
	CTISignalGuard signalGuard; // disable signals when launching
	OverwatchHandle overwatchHandle(_cti_getOverwatchPath()); // ensure aprun gets killed off on error
	
	// fork off a process to launch aprun
	pid_t forkedPid = fork();
	
	// error case
	if (forkedPid < 0) {
		throw std::runtime_error("Fatal fork error.");
	}
	
	// child case
	// Note that this should not use the _cti_set_error() interface since it is a child process.
	if (forkedPid == 0) {

		// create the argv array for the actual aprun exec and add the initial aprun argv
		ManagedArgv aprunArgv;
		aprunArgv.add(_cti_alps_getLauncherName());

		if (doBarrier) {
			int barrierReadFd, barrierWriteFd;
			std::tie(barrierReadFd, barrierWriteFd) = startupBarrier.setupChild();

			// Add the pipe r/w fd args
			aprunArgv.add("-P");
			aprunArgv.add(std::to_string(barrierReadFd) + "," + std::to_string(barrierWriteFd));
		}

		// set the rest of the argv for aprun from the passed in args
		if (launcher_argv != nullptr) {
			for (const char* const* arg = launcher_argv; *arg != nullptr; arg++) {
				aprunArgv.add(*arg);
			}
		}

		try {
			// redirect stdout/stderr if directed - do this early so that we can
			// print out errors to the proper descriptor.
			if ((stdout_fd >= 0) && (dup2(stdout_fd, STDOUT_FILENO) < 0)) {
				throw std::runtime_error("Unable to redirect aprun stdout.");
			}

			if ((stderr_fd >= 0) && (dup2(stderr_fd, STDERR_FILENO) < 0)) {
				throw std::runtime_error("Unable to redirect aprun stderr.");
			}

			// open the provided input file if non-null and redirect it to stdin

			// we don't want this aprun to suck up stdin of the tool program, so use /dev/null if no inputFile is provided
			const char* stdin_path = inputFile ? inputFile : "/dev/null";
			int new_stdin = open(stdin_path, O_RDONLY);
			if (new_stdin < 0) {
				throw std::runtime_error("Unable to open path for reading:" + std::string(stdin_path));
			} else {
				// redirect new_stdin to STDIN_FILENO
				if (dup2(new_stdin, STDIN_FILENO) < 0) {
					throw std::runtime_error("Unable to redirect aprun stdin.");
				}
				close(new_stdin);
			}
			
			// chdir if directed
			if ((chdirPath != nullptr) && chdir(chdirPath)) {
				throw std::runtime_error("Unable to chdir to provided path.");
			}
			
			// if env_list is not null, call putenv for each entry in the list
			if (env_list != nullptr) {
				for (const char* const* env_var = env_list; *env_var != nullptr; env_var++) {
					// putenv returns non-zero on error
					if (putenv(strdup(*env_var))) {
						throw std::runtime_error("Unable to putenv provided env_list.");
					}
				}
			}
			
			// assign the overwatch process to our pid
			overwatchHandle.assign(getpid());

		} catch (std::exception const& ex) {
			// XXX: How to properly print this error? The parent won't be
			// expecting the error message on this stream
			fprintf(stderr, "CTI error: %s\n", ex.what());
			_exit(1);
		}
		
		// restore signals
		signalGuard.restoreChildSignals();
		
		// exec aprun
		execvp(_cti_alps_getLauncherName().c_str(), aprunArgv.get());
		
		// exec shouldn't return
		fprintf(stderr, "CTI error: Return from exec.\n");
		perror("execvp");
		_exit(1);
	}

	// parent case

	// restore signals
	signalGuard.restoreParentSignals(forkedPid);
	
	// only do the following if we are using the barrier variant
	if (doBarrier) {
		startupBarrier.setupParent();

		// Wait on pipe read for app to start and get to barrier - once this happens
		// we know the real aprun is up and running
		try {
			startupBarrier.wait();
		} catch (...) {
			// attempt to kill aprun since the caller will not recieve the aprun pid
			// just in case the aprun process is still hanging around.
			kill(forkedPid, DEFAULT_SIG);
			throw std::runtime_error("Control pipe read failed.");
		}
	} else {
		// sleep long enough for the forked process to exec itself so that the
		// check for wrapped aprun process doesn't fail.
		sleep(1);
	}

	pid_t aprunPid = getWrappedAprunPid(forkedPid);
	if (aprunPid == 0) {
		throw std::runtime_error("could not determine the aprun pid during launch");
	}

	// set the apid associated with the pid of aprun
	uint64_t apid = cti_alps_getApid(aprunPid);
	if (apid == 0) {
		// attempt to kill aprun since the caller will not recieve the aprun pid
		// just in case the aprun process is still hanging around.
		kill(aprunPid, DEFAULT_SIG);
		throw std::runtime_error("Could not obtain apid associated with pid of aprun.");
	}

	// register this app with the application interface
	try {
		auto alpsInfo = shim::make_unique<AlpsInfo>(apid, newAppId);

		// complete, move the launch coordination objects into alpsInfo obj
		alpsInfo->aprunPid = aprunPid;
		alpsInfo->startupBarrier  = std::move(startupBarrier);
		alpsInfo->overwatchHandle = std::move(overwatchHandle);

		return alpsInfo;
	} catch (std::exception const& ex) {
		kill(aprunPid, DEFAULT_SIG);
		throw ex;
	}
}

static void
_cti_alps_killApp(AlpsInfo& my_app, int signum) {
	// create a new args obj
	ManagedArgv apkillArgv;
	apkillArgv.add(APKILL); // first argument should be "apkill"
	apkillArgv.add("-" + std::to_string(signum)); // second argument is -signum
	apkillArgv.add(std::to_string(my_app.apid)); // third argument is apid

	// fork off a process to launch apkill
	pid_t forkedPid = fork();

	// error case
	if (forkedPid < 0) {
		throw std::runtime_error("Fatal fork error.");
	}

	// child case
	if (forkedPid == 0) {
		// exec apkill
		execvp(APKILL, apkillArgv.get());

		// exec shouldn't return
		fprintf(stderr, "CTI error: Return from exec.\n");
		perror("execvp");
		_exit(1);
	}

	// parent case: wait until the apkill finishes
	waitpid(forkedPid, nullptr, 0);
}

static const size_t LAUNCH_TOOL_RETRY = 5;

static void
_cti_alps_ship_package(AlpsInfo& my_app, std::string const& tarPath) {

 	// suppress stderr for "gzip: broken pipe"
	int saved_stderr = dup(STDERR_FILENO);
	fflush(stderr);
	{ int new_stderr = open("/dev/null", O_WRONLY);
		dup2(new_stderr, STDERR_FILENO);
		close(new_stderr);
	}

	// ship the tarball to the compute node

	// discard const qualifier because alps isn't const correct
	auto rawTarPath = UniquePtrDestr<char>(strdup(tarPath.c_str()), ::free);
	char *nonconstTarPath = (char*)rawTarPath.get();

	const char* errmsg = nullptr;
	// checks: problem on crystal where alps_launch_tool_helper will report bad apid
	for (size_t checks = 0; checks < LAUNCH_TOOL_RETRY; checks++) {
		// if errmsg is nonnull, we failed to ship the file to the compute nodes for some reason - catastrophic failure
		errmsg = libAlps.alps_launch_tool_helper(my_app.apid, my_app.pe0Node, 1, 0, 1, &nonconstTarPath);
		if (!errmsg) {
			break;
		}

		usleep(500000);
	}

	// unsuppress stderr
	fflush(stderr);
	dup2(saved_stderr, STDERR_FILENO);
	close(saved_stderr);

	// if there was an error rethrow it
	if (errmsg != nullptr) {
		throw std::runtime_error("alps_launch_tool_helper error: " + std::string(errmsg));
	}
}

static void
_cti_alps_start_daemon(AlpsInfo& my_app, const char * const argv[]) {
	// sanity check
	if (argv == nullptr) {
		throw std::runtime_error("argv array is null!");
	}

	// Create the launcher path based on the value of dlaunch_sent in AlpsInfo. If this is
	// false, we have not yet transfered the dlaunch utility to the compute nodes, so we need
	// to find the location of it on our end and have alps transfer it.
	std::string launcherPath;
	if (my_app.dlaunch_sent) {
		// use existing launcher binary on compute node
		launcherPath = my_app.toolPath + "/" + CTI_LAUNCHER;
	} else {
		// Need to transfer launcher binary
		if (_cti_getDlaunchPath().empty()) {
			throw std::runtime_error("Required environment variable not set: " + std::string(BASE_DIR_ENV_VAR));
		}
		launcherPath = _cti_getDlaunchPath();
	}

	// get the flattened args string since alps needs to use that
	std::string argvString{launcherPath};
	for (const char* const* arg = argv; *arg != nullptr; arg++) {
		argvString.push_back(' ');
		argvString += std::string(*arg);
	}

	// discard const qualifier because alps isn't const correct
	auto rawArgvString = UniquePtrDestr<char>(strdup(argvString.c_str()), ::free);
	char *nonconstArgvString = (char*)rawArgvString.get();

	// launch the tool daemon onto the compute nodes
	if (const char* errmsg = libAlps.alps_launch_tool_helper(my_app.apid, my_app.pe0Node,
		!my_app.dlaunch_sent, 1, 1, &nonconstArgvString)) {

		// we failed to launch the launcher on the compute nodes for some reason - catastrophic failure
		throw std::runtime_error("alps_launch_tool_helper error: " + std::string(errmsg));
	}

	// set transfer value in my_app to true if applicable
	if (!my_app.dlaunch_sent) {
		my_app.dlaunch_sent = true;
	}
}

static int
_cti_alps_getAlpsOverlapOrdinal(AlpsInfo& my_app) {
	char *errmsg = nullptr;
	int rtn = libAlps.alps_get_overlap_ordinal(my_app.apid, &errmsg, nullptr);
	if (rtn < 0) {
		throw std::runtime_error(errmsg ? errmsg :
			"cti_alps_getAlpsOverlapOrdinal: Unknown alps_get_overlap_ordinal failure");
	} else {
		return rtn;
	}
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

static std::unordered_map<AppId, std::unique_ptr<AlpsInfo>> appList;
static const AppId APP_ERROR = 0;
static AppId newAppId() noexcept {
	static AppId nextId = 1;
	return nextId++;
}

static AlpsInfo&
getAppInfo(AppId appId) {
	auto infoPtr = appList.find(appId);
	if (infoPtr != appList.end()) {
		return *(infoPtr->second);
	}

	throw std::runtime_error("invalid appId: " + std::to_string(appId));
}

bool
ALPSFrontend::appIsValid(AppId appId) const {
	return appList.find(appId) != appList.end();
}

void
ALPSFrontend::deregisterApp(AppId appId) const {
	appList.erase(appId);
}

cti_wlm_type
ALPSFrontend::getWLMType() const {
	return CTI_WLM_ALPS;
}

std::string const
ALPSFrontend::getJobId(AppId appId) const {
	return std::to_string(getAppInfo(appId).apid);
}

AppId
ALPSFrontend::launch(CArgArray launcher_argv, int stdout_fd, int stderr,
                     CStr inputFile, CStr chdirPath, CArgArray env_list) {
	auto const appId = newAppId();
	appList.insert(std::make_pair(appId, std::move(_cti_alps_launch_common(launcher_argv, stdout_fd, stderr, inputFile, chdirPath, env_list, 0, appId))));
	return appId;
}

AppId
ALPSFrontend::launchBarrier(CArgArray launcher_argv, int stdout_fd, int stderr,
                            CStr inputFile, CStr chdirPath, CArgArray env_list) {
	auto const appId = newAppId();
	appList.insert(std::make_pair(appId, std::move(_cti_alps_launch_common(launcher_argv, stdout_fd, stderr, inputFile, chdirPath, env_list, 1, appId))));
	return appId;
}

void
ALPSFrontend::releaseBarrier(AppId appId) {
	getAppInfo(appId).startupBarrier.release();
}

void
ALPSFrontend::killApp(AppId appId, int signal) {
	_cti_alps_killApp(getAppInfo(appId), signal);
}


std::vector<std::string> const
ALPSFrontend::getExtraLibraries(AppId unused) const {
	std::vector<std::string> result {
		ALPS_BE_LIB_NAME
	};
	return result;
}

void
ALPSFrontend::shipPackage(AppId appId, std::string const& tarPath) const {
	_cti_alps_ship_package(getAppInfo(appId), tarPath);
}

void
ALPSFrontend::startDaemon(AppId appId, CArgArray argv) const {
	_cti_alps_start_daemon(getAppInfo(appId), argv);
}


size_t
ALPSFrontend::getNumAppPEs(AppId appId) const {
	return _cti_alps_getNumAppPEs(getAppInfo(appId));
}

size_t
ALPSFrontend::getNumAppNodes(AppId appId) const {
	return _cti_alps_getNumAppNodes(getAppInfo(appId));
}

std::vector<std::string> const
ALPSFrontend::getAppHostsList(AppId appId) const {
	return _cti_alps_getAppHostsList(getAppInfo(appId));
}

std::vector<CTIHost> const
ALPSFrontend::getAppHostsPlacement(AppId appId) const {
	return _cti_alps_getAppHostsPlacement(getAppInfo(appId));
}

std::string const
ALPSFrontend::getHostName(void) const {
	return _cti_alps_getHostName();
}

std::string const
ALPSFrontend::getLauncherHostName(AppId appId) const {
	return _cti_alps_getLauncherHostName(getAppInfo(appId));
}

std::string const
ALPSFrontend::getToolPath(AppId appId) const {
	return getAppInfo(appId).toolPath;
}

std::string const
ALPSFrontend::getAttribsPath(AppId appId) const {
	return getAppInfo(appId).attribsPath;
}

/* extended frontend implementation */

AppId
ALPSFrontend::registerApid(uint64_t apid) {
	// iterate through the _cti_alps_info list to try to find an existing entry for this apid
	for (auto const& appIdInfoPair : appList) {
		if (appIdInfoPair.second->apid == apid) {
			return appIdInfoPair.first;
		}
	}

	// aprun pid not found in the global _cti_alps_info list, so lets create a new AlpsInfo object for it
	auto appId = newAppId();
	appList.insert(std::make_pair(appId, shim::make_unique<AlpsInfo>(apid, appId)));
	return appId;
}

uint64_t
ALPSFrontend::getApid(pid_t appPid) {
	return _cti_alps_getApid(appPid);
}

ALPSFrontend::AprunInfo
ALPSFrontend::getAprunInfo(AppId appId) {
	return _cti_alps_getAprunInfo(getAppInfo(appId));
}

int
ALPSFrontend::getAlpsOverlapOrdinal(AppId appId) {
	return _cti_alps_getAlpsOverlapOrdinal(getAppInfo(appId));
}
