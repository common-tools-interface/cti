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
#include "cti_error.h"

#include "frontend/Frontend.hpp"
#include "cray_slurm_fe.hpp"

#include "useful/cti_useful.h"
#include "useful/make_unique.hpp"

#include "mpir_iface/mpir_iface.h"

#include "slurm_util/slurm_util.h"

/* Types used here */

typedef struct
{
	cti_app_id_t		appId;			// CTI appid associated with this alpsInfo_t obj
	uint32_t			jobid;			// SLURM job id
	uint32_t			stepid;			// SLURM step id
	uint64_t			apid;			// Cray variant of step+job id
	slurmStepLayout_t *	layout;			// Layout of job step
	mpir_id_t			mpir_id;		// MPIR instance handle
	cti_mpir_procTable_t *app_pids;		// Optional object used to hold the rank->pid association
	char *				toolPath;		// Backend staging directory
	char *				attribsPath;	// Backend Cray specific directory
	int					dlaunch_sent;	// True if we have already transfered the dlaunch utility
	char *				stagePath;		// directory to stage this instance files in for transfer to BE
	char **				extraFiles;		// extra files to transfer to BE associated with this app
} craySlurmInfo_t;

const char * slurm_blacklist_env_vars[] = {
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
		"SLURM_WORKING_DIR",
		NULL};

static char* 		_cti_cray_slurm_launcher_name = NULL;

static craySlurmInfo_t *
_cti_cray_slurm_newSlurmInfo(void)
{
	craySlurmInfo_t *	my_app;

	if ((my_app = (decltype(my_app))malloc(sizeof(craySlurmInfo_t))) == NULL)
	{
		// Malloc failed
		_cti_set_error("malloc failed.");
		
		return NULL;
	}
	
	// init the members
	my_app->appId			= 0;
	my_app->jobid			= 0;
	my_app->stepid		= 0;
	my_app->apid			= 0;
	my_app->layout		= NULL;
	my_app->mpir_id		= -1;
	my_app->app_pids		= NULL;
	my_app->toolPath		= NULL;
	my_app->attribsPath	= NULL;
	my_app->dlaunch_sent	= 0;
	my_app->stagePath		= NULL;
	my_app->extraFiles	= NULL;
	
	return my_app;
}

static void 
_cti_cray_slurm_consumeSlurmInfo(craySlurmInfo_t *sinfo)
{

	// sanity
	if (sinfo == NULL)
		return;

	_cti_cray_slurm_freeLayout(sinfo->layout);
	if (sinfo->mpir_id >= 0) {
		_cti_mpir_releaseInstance(sinfo->mpir_id);
	}
	_cti_mpir_deleteProcTable(sinfo->app_pids);
	
	if (sinfo->toolPath != NULL)
		free(sinfo->toolPath);
		
	if (sinfo->attribsPath != NULL)
		free(sinfo->attribsPath);
		
	// cleanup staging directory if it exists
	if (sinfo->stagePath != NULL)
	{
		_cti_removeDirectory(sinfo->stagePath);
		free(sinfo->stagePath);
	}
	
	// cleanup the extra files array
	if (sinfo->extraFiles != NULL)
	{
		char **	ptr = sinfo->extraFiles;
		
		while (*ptr != NULL)
		{
			free(*ptr++);
		}
		
		free(sinfo->extraFiles);
	}
	
	free(sinfo);
}

// Note that we should provide this as a jobid.stepid format. It will make turning
// it into a Cray apid easier on the backend since we don't lose any information
// with this format.
static char *
_cti_cray_slurm_getJobId(craySlurmInfo_t *my_app)
{
	char *				rtn = NULL;
	
	// sanity check
	if (my_app == NULL)
	{
		_cti_set_error("Null wlm obj.");
		return NULL;
	}
	
	if (asprintf(&rtn, "%lu.%lu", (long unsigned int)my_app->jobid, (long unsigned int)my_app->stepid) <= 0)
	{
		_cti_set_error("asprintf failed.");
		return NULL;
	}
	
	return rtn;
}

static char *
_cti_cray_slurm_getLauncherName()
{
	if(_cti_cray_slurm_launcher_name == NULL){
		char* launcher_name_env;
		if ((launcher_name_env = getenv(CTI_LAUNCHER_NAME)) != NULL)
		{
			_cti_cray_slurm_launcher_name = strdup(launcher_name_env);
		}
		else{
			_cti_cray_slurm_launcher_name = SRUN;
		}
	}

	return _cti_cray_slurm_launcher_name;
}

// this function creates a new craySlurmInfo_t object for the app
static craySlurmInfo_t*
_cti_cray_slurm_registerJobStep(uint32_t jobid, uint32_t stepid, cti_app_id_t newAppId)
{
	uint64_t			apid;	// Cray version of the job+step
	craySlurmInfo_t	*	sinfo;
	char *				toolPath;
	char *				attribsPath;
	
	// sanity check
	if (cti_current_wlm() != CTI_WLM_CRAY_SLURM)
	{
		_cti_set_error("Invalid call. Cray SLURM WLM not in use.");
		return 0;
	}
	
	// sanity check - Note that 0 is a valid step id.
	if (jobid == 0)
	{
		_cti_set_error("Invalid jobid %d.", (int)jobid);
		return 0;
	}
	
	// create the cray variation of the jobid+stepid
	apid = CRAY_SLURM_APID(jobid, stepid);

	// create the new craySlurmInfo_t object
	if ((sinfo = _cti_cray_slurm_newSlurmInfo()) == NULL)
	{
		// error already set
		return 0;
	}
	
	// set the jobid
	sinfo->jobid = jobid;
	// set the stepid
	sinfo->stepid = stepid;
	// set the apid
	sinfo->apid = apid;
	
	// retrieve detailed information about our app
	if ((sinfo->layout = _cti_cray_slurm_getLayout(jobid, stepid)) == NULL)
	{
		// error already set
		_cti_cray_slurm_consumeSlurmInfo(sinfo);
		return 0;
	}

	// create the toolPath
	if (asprintf(&toolPath, CRAY_SLURM_TOOL_DIR) <= 0)
	{
		_cti_set_error("asprintf failed");
		_cti_cray_slurm_consumeSlurmInfo(sinfo);
		return 0;
	}
	sinfo->toolPath = toolPath;
	
	// create the attribsPath
	if (asprintf(&attribsPath, CRAY_SLURM_CRAY_DIR, (long long unsigned int)sinfo->apid) <= 0)
	{
		_cti_set_error("asprintf failed");
		_cti_cray_slurm_consumeSlurmInfo(sinfo);
		return 0;
	}
	sinfo->attribsPath = attribsPath;

	// set the appid in the sinfo obj
	sinfo->appId = newAppId;

	return sinfo;
}

static CraySLURMFrontend::SrunInfo *
_cti_cray_slurm_getSrunInfo(craySlurmInfo_t* sinfo)
{
	CraySLURMFrontend::SrunInfo *	srunInfo;

	
	// sanity check
	if (sinfo == NULL)
	{
		_cti_set_error("cti_cray_slurm_getSrunInfo: _wlmObj is NULL!");
		return NULL;
	}
	
	// allocate space for the CraySLURMFrontend::SrunInfo struct
	if ((srunInfo = (decltype(srunInfo))malloc(sizeof(CraySLURMFrontend::SrunInfo))) == NULL)
	{
		// malloc failed
		_cti_set_error("malloc failed.");
		return NULL;
	}
	
	srunInfo->jobid = sinfo->jobid;
	srunInfo->stepid = sinfo->stepid;
	
	return srunInfo;
}

static CraySLURMFrontend::SrunInfo *
_cti_cray_slurm_getJobInfo(pid_t srunPid)
{
	mpir_id_t			mpir_id;
	const char *		launcher_path;
	char *				sym_str;
	char *				end_p;
	uint32_t			jobid;
	uint32_t			stepid;
	CraySLURMFrontend::SrunInfo *	srunInfo; // return object
	
	// sanity check
	if (srunPid <= 0)
	{
		_cti_set_error("Invalid srunPid %d.", (int)srunPid);
		return NULL;
	}
	
	// get the launcher path
	launcher_path = _cti_pathFind(SRUN, NULL);
	if (launcher_path == NULL)
	{
		_cti_set_error("Required environment variable %s not set.", BASE_DIR_ENV_VAR);
		return NULL;
	}
	
	// Create a new MPIR instance. We want to interact with it.
	if ((mpir_id = _cti_mpir_newAttachInstance(launcher_path, srunPid)) < 0)
	{
		// error already set
		return NULL;
	}

	// get the jobid string for slurm
	if ((sym_str = _cti_mpir_getStringAt(mpir_id, "totalview_jobid")) == NULL)
	{
		_cti_set_error("failed to get jobid string via MPIR.\n");
		_cti_mpir_releaseInstance(mpir_id);
		return NULL;
	}
	
	// convert the string into the actual jobid
	errno = 0;
	jobid = (uint32_t)strtoul(sym_str, &end_p, 10);
	
	// check for errors
	if ((errno == ERANGE && jobid == ULONG_MAX) || (errno != 0 && jobid == 0))
	{
		_cti_set_error("strtoul failed.\n");
		_cti_mpir_releaseInstance(mpir_id);
		free(sym_str);
		
		return NULL;
	}
	if (end_p == NULL || *end_p != '\0')
	{
		_cti_set_error("strtoul failed.\n");
		_cti_mpir_releaseInstance(mpir_id);
		free(sym_str);
		
		return NULL;
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
	if ((errno == ERANGE && stepid == ULONG_MAX) || (errno != 0 && stepid == 0))
	{
		_cti_set_error("strtoul failed.\n");
		_cti_mpir_releaseInstance(mpir_id);
		free(sym_str);
		
		return NULL;
	}
	if (end_p == NULL || *end_p != '\0')
	{
		_cti_set_error("strtoul failed.\n");
		_cti_mpir_releaseInstance(mpir_id);
		free(sym_str);
		
		return NULL;
	}

	free(sym_str);
	
	// Cleanup this mpir instance, we are done with it
	_cti_mpir_releaseInstance(mpir_id);
	
	// allocate space for the CraySLURMFrontend::SrunInfo struct
	if ((srunInfo = (decltype(srunInfo))malloc(sizeof(CraySLURMFrontend::SrunInfo))) == NULL)
	{
		// malloc failed
		_cti_set_error("malloc failed.");
		return NULL;
	}
	
	// set the members
	srunInfo->jobid = jobid;
	srunInfo->stepid = stepid;
	
	return srunInfo;
}

static int _cti_cray_slurm_release(craySlurmInfo_t *sinfo);

static craySlurmInfo_t*
_cti_cray_slurm_launch_common(	const char * const launcher_argv[], int stdout_fd, int stderr_fd,
								const char *inputFile, const char *chdirPath,
								const char * const env_list[], int doBarrier, cti_app_id_t newAppId)
{
	mpir_id_t			mpir_id;
	craySlurmInfo_t	*	sinfo = nullptr;
	char *				sym_str;
	char *				end_p;
	uint32_t			jobid;
	uint32_t			stepid;
	cti_mpir_procTable_t *	pids;
	const char *		launcher_path;

	if(!_cti_is_valid_environment()){
		// error already set
		return 0;
	}

	// get the launcher path
	launcher_path = _cti_pathFind(SRUN, NULL);
	if (launcher_path == NULL)
	{
		_cti_set_error("Required environment variable %s not set.", BASE_DIR_ENV_VAR);
		return 0;
	}

	// open input file (or /dev/null to avoid stdin contention)
	int input_fd = -1;
	if (inputFile == NULL) {
		inputFile = "/dev/null";
	}
	errno = 0;
	input_fd = open(inputFile, O_RDONLY);
	if (input_fd < 0) {
		_cti_set_error("Failed to open input file %s: ", inputFile, strerror(errno));
		return 0;
	}
	
	// Create a new MPIR instance. We want to interact with it.
	if ((mpir_id = _cti_mpir_newLaunchInstance(launcher_path, launcher_argv, env_list, input_fd, stdout_fd, stderr_fd)) < 0)
	{
			_cti_set_error("Failed to launch %s", launcher_argv[0]);

		return 0;
	}
	
	// get the jobid string for slurm
	if ((sym_str = _cti_mpir_getStringAt(mpir_id, "totalview_jobid")) == NULL)
	{
		// error already set
		_cti_mpir_releaseInstance(mpir_id);
		
		return 0;
	}
	
	// convert the string into the actual jobid
	errno = 0;
	jobid = (uint32_t)strtoul(sym_str, &end_p, 10);
	
	// check for errors
	if ((errno == ERANGE && jobid == ULONG_MAX) || (errno != 0 && jobid == 0))
	{
		_cti_set_error("strtoul failed (parse).\n");
		_cti_mpir_releaseInstance(mpir_id);
		free(sym_str);
		
		return 0;
	}
	if (end_p == NULL || *end_p != '\0')
	{
		_cti_set_error("strtoul failed (partial parse).\n");
		_cti_mpir_releaseInstance(mpir_id);
		free(sym_str);
		
		return 0;
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
	if ((errno == ERANGE && stepid == ULONG_MAX) || (errno != 0 && stepid == 0))
	{
		_cti_set_error("strtoul failed (parse).\n");
		_cti_mpir_releaseInstance(mpir_id);
		free(sym_str);
		
		return 0;
	}
	if (end_p == NULL || *end_p != '\0')
	{
		_cti_set_error("strtoul failed (partial parse).\n");
		_cti_mpir_releaseInstance(mpir_id);
		free(sym_str);
		
		return 0;
	}
	free(sym_str);
	
	// get the pid information from slurm
	// FIXME: When/if pmi_attribs get fixed for the slurm startup barrier, this
	// call can be removed. Right now the pmi_attribs file is created in the pmi
	// ctor, which is called after the slurm startup barrier, meaning it will not
	// yet be created when launching. So we need to send over a file containing
	// the information to the compute nodes.
	if ((pids = _cti_mpir_newProcTable(mpir_id)) == NULL)
	{
		_cti_set_error("failed to get proctable.\n");
		
		return 0;
	}

	// register this app with the application interface
	if ((sinfo = _cti_cray_slurm_registerJobStep(jobid, stepid, newAppId)) == 0)
	{
		// failed to register the jobid/stepid, error is already set.
		_cti_mpir_deleteProcTable(pids);
		_cti_mpir_releaseInstance(mpir_id);
		
		return 0;
	}

	// set the inv
	sinfo->mpir_id = mpir_id;
	
	// set the pids
	sinfo->app_pids = pids;
	
	// If we should not wait at the barrier, call the barrier release function.
	if (!doBarrier)
	{
		if (_cti_cray_slurm_release(sinfo))
		{
			// error already set - appEntry already holds all info to be cleaned up
			return nullptr;
		}
	}

	return sinfo;
}

static craySlurmInfo_t*
_cti_cray_slurm_launch(	const char * const a1[], int a2, int a3,
						const char *a4, const char *a5,
						const char * const a6[], cti_app_id_t newAppId)
{
	// call the common launch function
	return _cti_cray_slurm_launch_common(a1, a2, a3, a4, a5, a6, 0, newAppId);
}

static craySlurmInfo_t*
_cti_cray_slurm_launchBarrier(	const char * const a1[], int a2, int a3,
								const char *a4, const char *a5,
								const char * const a6[], cti_app_id_t newAppId)
{
	// call the common launch function
	return _cti_cray_slurm_launch_common(a1, a2, a3, a4, a5, a6, 1, newAppId);
}

static int
_cti_cray_slurm_release(craySlurmInfo_t *my_app)
{
	// sanity check
	if (my_app == NULL)
	{
		_cti_set_error("srun barrier release operation failed.");
		return 1;
	}
	
	// call the release function
	if (_cti_mpir_releaseInstance(my_app->mpir_id))
	{
		_cti_set_error("srun barrier release operation failed.");
		return 1;
	}
	my_app->mpir_id = -1;

	// done
	return 0;
}

static int
_cti_cray_slurm_killApp(craySlurmInfo_t *my_app, int signum)
{
	cti_args_t *		my_args;
	int					mypid;
	
	// sanity check
	if (my_app == NULL)
	{
		_cti_set_error("srun kill operation failed.");
		return 1;
	}
	
	// create a new args obj
	if ((my_args = _cti_newArgs()) == NULL)
	{
		_cti_set_error("_cti_newArgs failed.");
		return 1;
	}
	
	// create the args for scancel
	
	// first argument should be "scancel"
	if (_cti_addArg(my_args, "%s", SCANCEL))
	{
		_cti_set_error("_cti_addArg failed.");
		_cti_freeArgs(my_args);
		return 1;
	}
	
	// second argument is quiet
	if (_cti_addArg(my_args, "-Q"))
	{
		_cti_set_error("_cti_addArg failed.");
		_cti_freeArgs(my_args);
		return 1;
	}
	
	// third argument is signal number
	if (_cti_addArg(my_args, "-s"))
	{
		_cti_set_error("_cti_addArg failed.");
		_cti_freeArgs(my_args);
		return 1;
	}
	if (_cti_addArg(my_args, "%d", signum))
	{
		_cti_set_error("_cti_addArg failed.");
		_cti_freeArgs(my_args);
		return 1;
	}
	
	// fourth argument is the jobid.stepid
	if (_cti_addArg(my_args, "%u.%u", my_app->jobid, my_app->stepid))
	{
		_cti_set_error("_cti_addArg failed.");
		_cti_freeArgs(my_args);
		return 1;
	}
	
	// fork off a process to launch scancel
	mypid = fork();
	
	// error case
	if (mypid < 0)
	{
		_cti_set_error("Fatal fork error.");
		// cleanup
		_cti_freeArgs(my_args);
		
		return 1;
	}
	
	// child case
	if (mypid == 0)
	{
		// exec scancel
		execvp(SCANCEL, my_args->argv);
		
		// exec shouldn't return
		fprintf(stderr, "CTI error: Return from exec.\n");
		perror("execvp");
		_exit(1);
	}
	
	// parent case
	// cleanup
	_cti_freeArgs(my_args);
	
	// wait until the scancel finishes
	waitpid(mypid, NULL, 0);
	
	return 0;
}

static const char * const *
_cti_cray_slurm_extraFiles(craySlurmInfo_t *my_app)
{
	const char *			cfg_dir;
	FILE *					myFile;
	char *					layoutPath;
	slurmLayoutFileHeader_t	layout_hdr;
	slurmLayoutFile_t		layout_entry;
	char *					pidPath = NULL;
	slurmPidFileHeader_t	pid_hdr;
	slurmPidFile_t			pid_entry;
	int						i;
	
	// sanity check
	if (my_app == NULL)
		return NULL;
		
	// sanity check
	if (my_app->layout == NULL)
		return NULL;
	
	// If we already have created the extraFiles array, return that
	if (my_app->extraFiles != NULL)
	{
		return (const char * const *)my_app->extraFiles;
	}
	
	// init data structures
	memset(&layout_hdr, 0, sizeof(layout_hdr));
	memset(&layout_entry, 0, sizeof(layout_entry));
	memset(&pid_hdr, 0, sizeof(pid_hdr));
	memset(&pid_entry, 0, sizeof(pid_entry));
	
	// Check to see if we should create the staging directory
	if (my_app->stagePath == NULL)
	{
		// Get the configuration directory
		if ((cfg_dir = _cti_getCfgDir().c_str()) == NULL)
		{
			// cannot continue, so return NULL. BE API might fail.
			// TODO: How to handle this error?
			return NULL;
		}
		
		// create the directory to stage the needed files
		if (asprintf(&my_app->stagePath, "%s/%s", cfg_dir, SLURM_STAGE_DIR) <= 0)
		{
			// cannot continue, so return NULL. BE API might fail.
			// TODO: How to handle this error?
			my_app->stagePath = NULL;
			return NULL;
		}
		
		// create the temporary directory for the manifest package
		if (mkdtemp(my_app->stagePath) == NULL)
		{
			// cannot continue, so return NULL. BE API might fail.
			// TODO: How to handle this error?
			free(my_app->stagePath);
			my_app->stagePath = NULL;
			return NULL;
		}
	}
	
	// create path string to layout file
	if (asprintf(&layoutPath, "%s/%s", my_app->stagePath, SLURM_LAYOUT_FILE) <= 0)
	{
		// cannot continue, so return NULL. BE API might fail.
		// TODO: How to handle this error?
		return NULL;
	}
	
	// Open the layout file
	if ((myFile = fopen(layoutPath, "wb")) == NULL)
	{
		// cannot continue, so return NULL. BE API might fail.
		// TODO: How to handle this error?
		free(layoutPath);
		return NULL;
	}
	
	// init the layout header
	layout_hdr.numNodes = my_app->layout->numNodes;
	
	// write the header
	if (fwrite(&layout_hdr, sizeof(slurmLayoutFileHeader_t), 1, myFile) != 1)
	{
		// cannot continue, so return NULL. BE API might fail.
		// TODO: How to handle this error?
		free(layoutPath);
		fclose(myFile);
		return NULL;
	}
	
	// write each of the entries
	for (i=0; i < my_app->layout->numNodes; ++i)
	{
		// ensure we have good hostname information
		if (strlen(my_app->layout->hosts[i].host) > (sizeof(layout_entry.host) - 1))
		{
			// No way to continue, the hostname will not fit in our fixed size buffer
			// TODO: How to handle this error?
			free(layoutPath);
			fclose(myFile);
			return NULL;
		}
		
		// set this entry
		memcpy(&layout_entry.host[0], my_app->layout->hosts[i].host, sizeof(layout_entry.host));
		layout_entry.PEsHere = my_app->layout->hosts[i].PEsHere;
		layout_entry.firstPE = my_app->layout->hosts[i].firstPE;
		
		if (fwrite(&layout_entry, sizeof(slurmLayoutFile_t), 1, myFile) != 1)
		{
			// cannot continue, so return NULL. BE API might fail.
			// TODO: How to handle this error?
			free(layoutPath);
			fclose(myFile);
			return NULL;
		}
	}
	
	// done with the layout file
	fclose(myFile);
	
	// check to see if there is an app_pids member, if so we need to create the 
	// pid file
	if (my_app->app_pids != NULL)
	{
		// create path string to pid file
		if (asprintf(&pidPath, "%s/%s", my_app->stagePath, SLURM_PID_FILE) <= 0)
		{
			// cannot continue, so return NULL. BE API might fail.
			// TODO: How to handle this error?
			free(layoutPath);
			return NULL;
		}
	
		// Open the pid file
		if ((myFile = fopen(pidPath, "wb")) == NULL)
		{
			// cannot continue, so return NULL. BE API might fail.
			// TODO: How to handle this error?
			free(layoutPath);
			free(pidPath);
			return NULL;
		}
	
		// init the pid header
		pid_hdr.numPids = my_app->app_pids->num_pids;
		
		// write the header
		if (fwrite(&pid_hdr, sizeof(slurmPidFileHeader_t), 1, myFile) != 1)
		{
			// cannot continue, so return NULL. BE API might fail.
			// TODO: How to handle this error?
			free(layoutPath);
			free(pidPath);
			fclose(myFile);
			return NULL;
		}
	
		// write each of the entries
		for (i=0; i < my_app->app_pids->num_pids; ++i)
		{
			// set this entry
			pid_entry.pid = my_app->app_pids->pids[i];
			
			// write this entry
			if (fwrite(&pid_entry, sizeof(slurmPidFile_t), 1, myFile) != 1)
			{
				// cannot continue, so return NULL. BE API might fail.
				// TODO: How to handle this error?
				free(layoutPath);
				free(pidPath);
				fclose(myFile);
				return NULL;
			}
		}
	
		// done with the pid file
		fclose(myFile);
	}
	
	// create the extraFiles array - This should be the length of the above files
	if ((my_app->extraFiles = (decltype(my_app->extraFiles))calloc(3, sizeof(char *))) == NULL)
	{
		// calloc failed
		// cannot continue, so return NULL. BE API might fail.
		// TODO: How to handle this error?
		free(layoutPath);
		return NULL;
	}
	
	// set the layout file
	my_app->extraFiles[0] = layoutPath;
	// TODO: set the pid file
	my_app->extraFiles[1] = pidPath;
	// set the null terminator
	my_app->extraFiles[2] = NULL;
	
	return (const char * const *)my_app->extraFiles;
}

static int
_cti_cray_slurm_ship_package(craySlurmInfo_t *my_app, const char *package)
{
	cti_args_t *		my_args;
	char *				str1;
	char *				str2;
	int					mypid;
	
	// sanity check
	if (my_app == NULL)
	{
		_cti_set_error("WLM obj is null!");
		return 1;
	}
	
	// sanity check
	if (my_app->layout == NULL)
	{
		_cti_set_error("craySlurmInfo_t layout is null!");
		return 1;
	}
	
	// sanity check
	if (package == NULL)
	{
		_cti_set_error("package string is null!");
		return 1;
	}
	
	// ensure numNodes is non-zero
	if (my_app->layout->numNodes <= 0)
	{
		_cti_set_error("Application %d.%d does not have any nodes.", my_app->jobid, my_app->stepid);
		// no nodes in the application
		return 1;
	}
	
	// create a new args obj
	if ((my_args = _cti_newArgs()) == NULL)
	{
		_cti_set_error("_cti_newArgs failed.");
		return 1;
	}
	
	// create the args for sbcast
	
	if (_cti_addArg(my_args, "%s", SBCAST))
	{
		_cti_set_error("_cti_addArg failed.");
		_cti_freeArgs(my_args);
		return 1;
	}
	
	if (_cti_addArg(my_args, "-C"))
	{
		_cti_set_error("_cti_addArg failed.");
		_cti_freeArgs(my_args);
		return 1;
	}
	
	if (_cti_addArg(my_args, "-j"))
	{
		_cti_set_error("_cti_addArg failed.");
		_cti_freeArgs(my_args);
		return 1;
	}
	
	if (_cti_addArg(my_args, "%d", my_app->jobid))
	{
		_cti_set_error("_cti_addArg failed.");
		_cti_freeArgs(my_args);
		return 1;
	}
	
	if (_cti_addArg(my_args, "%s", package))
	{
		_cti_set_error("_cti_addArg failed.");
		_cti_freeArgs(my_args);
		return 1;
	}
	
	if (_cti_addArg(my_args, "--force"))
	{
		_cti_set_error("_cti_addArg failed.");
		_cti_freeArgs(my_args);
		return 1;
	}
	
	if (asprintf(&str1, CRAY_SLURM_TOOL_DIR) <= 0)
	{
		_cti_set_error("asprintf failed");
		_cti_freeArgs(my_args);
		return 1;
	}
	if ((str2 = _cti_pathToName(package)) == NULL)
	{
		_cti_set_error("_cti_pathToName failed");
		_cti_freeArgs(my_args);
		free(str1);
		return 1;
	}
	if (_cti_addArg(my_args, "%s/%s", str1, str2))
	{
		_cti_set_error("_cti_addArg failed.");
		_cti_freeArgs(my_args);
		free(str1);
		free(str2);
		return 1;
	}
	free(str1);
	free(str2);
	
	// now ship the tarball to the compute nodes
	// fork off a process to launch sbcast
	mypid = fork();
	
	// error case
	if (mypid < 0)
	{
		_cti_set_error("Fatal fork error.");
		_cti_freeArgs(my_args);
		
		return 1;
	}
	
	// child case
	if (mypid == 0)
	{
		int fd;
		
		// we want to redirect stdin/stdout/stderr to /dev/null
		fd = open("/dev/null", O_RDONLY);
		
		// dup2 stdin
		dup2(fd, STDIN_FILENO);
		
		// dup2 stdout
		dup2(fd, STDOUT_FILENO);
		
		// dup2 stderr
		dup2(fd, STDERR_FILENO);
		
		// exec sbcast
		execvp(SBCAST, my_args->argv);
		
		// exec shouldn't return
		fprintf(stderr, "CTI error: Return from exec.\n");
		perror("execvp");
		_exit(1);
	}
	
	// parent case
	// cleanup
	_cti_freeArgs(my_args);
	
	// wait until the sbcast finishes
	// FIXME: There is no way to error check right now because the sbcast command
	// can only send to an entire job, not individual job steps. The /var/spool/alps/<apid>
	// directory will only exist on nodes associated with this particular job step, and the
	// sbcast command will exit with error if the directory doesn't exist even if the transfer
	// worked on the nodes associated with the step. I opened schedmd BUG 1151 for this issue.
	waitpid(mypid, NULL, 0);
	
	return 0;
}

static int
_cti_cray_slurm_start_daemon(craySlurmInfo_t *my_app, cti_args_t * args)
{
	char *				launcher;
	cti_args_t *		my_args;
	char *				hostlist;
	char *				tmp;
	int					fd, i, mypid;
	struct rlimit		rl;
	
	// sanity check
	if (my_app == NULL)
	{
		_cti_set_error("WLM obj is null!");
		return 1;
	}
	
	// sanity check
	if (my_app->layout == NULL)
	{
		_cti_set_error("craySlurmInfo_t layout is null!");
		return 1;
	}
	
	// sanity check
	if (args == NULL)
	{
		_cti_set_error("args string is null!");
		return 1;
	}
	
	// ensure numNodes is non-zero
	if (my_app->layout->numNodes <= 0)
	{
		_cti_set_error("Application %d.%d does not have any nodes.", my_app->jobid, my_app->stepid);
		// no nodes in the application
		return 1;
	}
	
	// get max number of file descriptors - used later
	if (getrlimit(RLIMIT_NOFILE, &rl) < 0)
	{
		_cti_set_error("getrlimit failed.");
		return 1;
	}
	
	// we want to redirect stdin/stdout/stderr to /dev/null since it is not required
	if ((fd = open("/dev/null", O_RDONLY)) < 0)
	{
		_cti_set_error("Unable to open /dev/null for reading.");
		return 1;
	}

	// If we have not yet transfered the dlaunch binary, we need to do that in advance with
	// native slurm
	if (!my_app->dlaunch_sent)
	{
		// Need to transfer launcher binary
		const char *	launcher_path;
		
		// Get the location of the daemon launcher
		if ((launcher_path = _cti_getDlaunchPath().c_str()) == NULL)
		{
			_cti_set_error("Required environment variable %s not set.", BASE_DIR_ENV_VAR);
			close(fd);
			return 1;
		}
		
		if (_cti_cray_slurm_ship_package(my_app, launcher_path))
		{
			// error already set
			close(fd);
			return 1;
		}
		
		// set transfer to true
		my_app->dlaunch_sent = 1;
	}
	
	// use existing launcher binary on compute node
	if (asprintf(&launcher, "%s/%s", my_app->toolPath, CTI_LAUNCHER) <= 0)
	{
		_cti_set_error("asprintf failed.");
		close(fd);
		return 1;
	}
	
	// create a new args obj
	if ((my_args = _cti_newArgs()) == NULL)
	{
		_cti_set_error("_cti_newArgs failed.");
		close(fd);
		free(launcher);
		return 1;
	}
	
	// Start adding the args to the my_args array
	//
	// This corresponds to:
	//
	// srun --jobid=<job_id> --gres=none --mem-per-cpu=0 --mem_bind=no
	// --cpu_bind=no --share --ntasks-per-node=1 --nodes=<numNodes>
	// --nodelist=<host1,host2,...> --disable-status --quiet --mpi=none 
	// --input=none --output=none --error=none <tool daemon> <args>
	//
	
	if (_cti_addArg(my_args, "%s", _cti_cray_slurm_getLauncherName()))
	{
		_cti_set_error("_cti_addArg failed.");
		close(fd);
		free(launcher);
		_cti_freeArgs(my_args);
		return 1;
	}
	
	if (_cti_addArg(my_args, "--jobid=%d", my_app->jobid))
	{
		_cti_set_error("_cti_addArg failed.");
		close(fd);
		free(launcher);
		_cti_freeArgs(my_args);
		return 1;
	}
	
	if (_cti_addArg(my_args, "--gres=none"))
	{
		_cti_set_error("_cti_addArg failed.");
		close(fd);
		free(launcher);
		_cti_freeArgs(my_args);
		return 1;
	}
	
	if (_cti_addArg(my_args, "--mem-per-cpu=0"))
	{
		_cti_set_error("_cti_addArg failed.");
		close(fd);
		free(launcher);
		_cti_freeArgs(my_args);
		return 1;
	}
	
	if (_cti_addArg(my_args, "--mem_bind=no"))
	{
		_cti_set_error("_cti_addArg failed.");
		close(fd);
		free(launcher);
		_cti_freeArgs(my_args);
		return 1;
	}
	
	if (_cti_addArg(my_args, "--cpu_bind=no"))
	{
		_cti_set_error("_cti_addArg failed.");
		close(fd);
		free(launcher);
		_cti_freeArgs(my_args);
		return 1;
	}
	
	if (_cti_addArg(my_args, "--share"))
	{
		_cti_set_error("_cti_addArg failed.");
		close(fd);
		free(launcher);
		_cti_freeArgs(my_args);
		return 1;
	}
	
	if (_cti_addArg(my_args, "--ntasks-per-node=1"))
	{
		_cti_set_error("_cti_addArg failed.");
		close(fd);
		free(launcher);
		_cti_freeArgs(my_args);
		return 1;
	}
	
	if (_cti_addArg(my_args, "--nodes=%d", my_app->layout->numNodes))
	{
		_cti_set_error("_cti_addArg failed.");
		close(fd);
		free(launcher);
		_cti_freeArgs(my_args);
		return 1;
	}
	
	// create the hostlist. If there is only one entry, then we don't need to
	// iterate over the list.
	if (my_app->layout->numNodes == 1)
	{
		hostlist = strdup(my_app->layout->hosts[0].host);
	} else
	{
		// get the first host entry
		tmp = strdup(my_app->layout->hosts[0].host);
		for (i=1; i < my_app->layout->numNodes; ++i)
		{
			if (asprintf(&hostlist, "%s,%s", tmp, my_app->layout->hosts[i].host) <= 0)
			{
				_cti_set_error("asprintf failed.");
				close(fd);
				free(launcher);
				_cti_freeArgs(my_args);
				free(tmp);
				return 1;
			}
			free(tmp);
			tmp = hostlist;
		}
	}
	if (_cti_addArg(my_args, "--nodelist=%s", hostlist))
	{
		_cti_set_error("_cti_addArg failed.");
		close(fd);
		free(launcher);
		_cti_freeArgs(my_args);
		free(hostlist);
		return 1;
	}
	free(hostlist);
	
	if (_cti_addArg(my_args, "--disable-status"))
	{
		_cti_set_error("_cti_addArg failed.");
		close(fd);
		free(launcher);
		_cti_freeArgs(my_args);
		return 1;
	}
	
	if (_cti_addArg(my_args, "--quiet"))
	{
		_cti_set_error("_cti_addArg failed.");
		close(fd);
		free(launcher);
		_cti_freeArgs(my_args);
		return 1;
	}
	
	if (_cti_addArg(my_args, "--mpi=none"))
	{
		_cti_set_error("_cti_addArg failed.");
		close(fd);
		free(launcher);
		_cti_freeArgs(my_args);
		return 1;
	}
	
	if (_cti_addArg(my_args, "--output=none"))
	{
		_cti_set_error("_cti_addArg failed.");
		close(fd);
		free(launcher);
		_cti_freeArgs(my_args);
		return 1;
	}
	
	if (_cti_addArg(my_args, "--error=none"))
	{
		_cti_set_error("_cti_addArg failed.");
		close(fd);
		free(launcher);
		_cti_freeArgs(my_args);
		return 1;
	}
	
	if (_cti_addArg(my_args, "%s", launcher))
	{
		_cti_set_error("_cti_addArg failed.");
		close(fd);
		free(launcher);
		_cti_freeArgs(my_args);
		return 1;
	}
	free(launcher);
	
	// merge in the args array if there is one
	if (args != NULL)
	{
		if (_cti_mergeArgs(my_args, args))
		{
			_cti_set_error("_cti_mergeArgs failed.");
			close(fd);
			_cti_freeArgs(my_args);
			return 1;
		}
	}
	
	// fork off a process to launch srun
	mypid = fork();
	
	// error case
	if (mypid < 0)
	{
		_cti_set_error("Fatal fork error.");
		close(fd);
		_cti_freeArgs(my_args);
		
		return 1;
	}
	
	// child case
	if (mypid == 0)
	{
		const char **	env_ptr;
	
		// Place this process in its own group to prevent signals being passed
		// to it. This is necessary in case the child code execs before the 
		// parent can put us into our own group.
		setpgid(0, 0);
	
		// dup2 stdin
		if (dup2(fd, STDIN_FILENO) < 0)
		{
			// XXX: How to handle error?
			_exit(1);
		}
		
		// dup2 stdout
		if (dup2(fd, STDOUT_FILENO) < 0)
		{
			// XXX: How to handle error?
			_exit(1);
		}
		
		// dup2 stderr
		if (dup2(fd, STDERR_FILENO) < 0)
		{
			// XXX: How to handle error?
			_exit(1);
		}
		
		// close all open file descriptors above STDERR
		if (rl.rlim_max == RLIM_INFINITY)
		{
			rl.rlim_max = 1024;
		}
		for (i=3; i < rl.rlim_max; ++i)
		{
			close(i);
		}
		
		// clear out the blacklisted slurm env vars to ensure we don't get weird
		// behavior
		env_ptr = slurm_blacklist_env_vars;
		while (*env_ptr != NULL)
		{
			unsetenv(*env_ptr++);
		}
		
		// exec srun
		execvp(_cti_cray_slurm_getLauncherName(), my_args->argv);
		
		// exec shouldn't return
		fprintf(stderr, "CTI error: Return from exec.\n");
		perror("execvp");
		_exit(1);
	}
	
	// Place the child in its own group.
	setpgid(mypid, mypid);
	
	// cleanup
	close(fd);
	_cti_freeArgs(my_args);
	
	// done
	return 0;
}

static int
_cti_cray_slurm_getNumAppPEs(craySlurmInfo_t *my_app)
{
	// sanity check
	if (my_app == NULL)
	{
		_cti_set_error("getNumAppPEs operation failed.");
		return 0;
	}
	
	// sanity check
	if (my_app->layout == NULL)
	{
		_cti_set_error("getNumAppPEs operation failed.");
		return 0;
	}
	
	return my_app->layout->numPEs;
}

static int
_cti_cray_slurm_getNumAppNodes(craySlurmInfo_t *my_app)
{
	// sanity check
	if (my_app == NULL)
	{
		_cti_set_error("getNumAppPEs operation failed.");
		return 0;
	}
	
	// sanity check
	if (my_app->layout == NULL)
	{
		_cti_set_error("getNumAppPEs operation failed.");
		return 0;
	}
	
	return my_app->layout->numNodes;
}

static char **
_cti_cray_slurm_getAppHostsList(craySlurmInfo_t *my_app)
{
	char **				hosts;
	int					i;
	
	// sanity check
	if (my_app == NULL)
	{
		_cti_set_error("getNumAppPEs operation failed.");
		return NULL;
	}
	
	// sanity check
	if (my_app->layout == NULL)
	{
		_cti_set_error("getNumAppPEs operation failed.");
		return NULL;
	}
	
	// ensure numNodes is non-zero
	if (my_app->layout->numNodes <= 0)
	{
		_cti_set_error("Application %d.%d does not have any nodes.", my_app->jobid, my_app->stepid);
		// no nodes in the application
		return NULL;
	}
	
	// allocate space for the hosts list, add an extra entry for the null terminator
	if ((hosts = (decltype(hosts))calloc(my_app->layout->numNodes + 1, sizeof(char *))) == NULL)
	{
		// calloc failed
		_cti_set_error("calloc failed.");
		return NULL;
	}
	
	// iterate through the hosts list
	for (i=0; i < my_app->layout->numNodes; ++i)
	{
		hosts[i] = strdup(my_app->layout->hosts[i].host);
	}
	
	// set null term
	hosts[i] = NULL;
	
	// done
	return hosts;
}

static cti_hostsList_t *
_cti_cray_slurm_getAppHostsPlacement(craySlurmInfo_t *my_app)
{
	cti_hostsList_t *	placement_list;
	int					i;
	
	// sanity check
	if (my_app == NULL)
	{
		_cti_set_error("getNumAppPEs operation failed.");
		return NULL;
	}
	
	// sanity check
	if (my_app->layout == NULL)
	{
		_cti_set_error("getNumAppPEs operation failed.");
		return NULL;
	}
	
	// ensure numNodes is non-zero
	if (my_app->layout->numNodes <= 0)
	{
		_cti_set_error("Application %d.%d does not have any nodes.", my_app->jobid, my_app->stepid);
		// no nodes in the application
		return NULL;
	}
	
	// allocate space for the cti_hostsList_t struct
	if ((placement_list = (decltype(placement_list))malloc(sizeof(cti_hostsList_t))) == NULL)
	{
		// malloc failed
		_cti_set_error("malloc failed.");
		return NULL;
	}
	
	// set the number of hosts for the application
	placement_list->numHosts = my_app->layout->numNodes;
	
	// allocate space for the cti_host_t structs inside the placement_list
	if ((placement_list->hosts = (decltype(placement_list->hosts))malloc(placement_list->numHosts * sizeof(cti_host_t))) == NULL)
	{
		// malloc failed
		_cti_set_error("malloc failed.");
		free(placement_list);
		return NULL;
	}
	// clear the nodeHostPlacment_t memory
	memset(placement_list->hosts, 0, placement_list->numHosts * sizeof(cti_host_t));
	
	// iterate through the hosts list
	for (i=0; i < my_app->layout->numNodes; ++i)
	{
		placement_list->hosts[i].hostname = strdup(my_app->layout->hosts[i].host);
		placement_list->hosts[i].numPEs = my_app->layout->hosts[i].PEsHere;
	}
	
	// done
	return placement_list;
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
_cti_cray_slurm_getHostName(void)
{
    static char *hostname = NULL; // Cache the result

    // Determined the answer previously?
    if (hostname)
        return strdup(hostname);    // return cached value

	// Try the Cray /proc extension short cut
    FILE *nid_fp;             // NID file stream
	if ((nid_fp = fopen(ALPS_XT_NID, "r")) != NULL)
	{
	    // we expect this file to have a numeric value giving our current nid
        char file_buf[BUFSIZ];   // file read buffer
	    if (fgets(file_buf, BUFSIZ, nid_fp) == NULL)
	    {
		    fprintf(stderr, "_cti_cray_slurm_getHostName fgets failed.\n");
		    fclose(nid_fp);
		    return NULL;
	    }

	    // close the file stream
	    fclose(nid_fp);

	    // convert this to an integer value
        errno = 0;
        char *  eptr;
	    int nid = (int)strtol(file_buf, &eptr, 10);

        // check for error
        if ((errno == ERANGE && nid == INT_MAX)
                || (errno != 0 && nid == 0))
        {
            fprintf(stderr, "_cti_cray_slurm_getHostName: strtol failed.\n");
            return NULL;
        }

        // check for invalid input
        if (eptr == file_buf)
        {
            fprintf(stderr, "_cti_cray_slurm_getHostName: Bad data in %s\n", ALPS_XT_NID);
            return NULL;
        }

	    // create the nid hostname string
	    if (asprintf(&hostname, ALPS_XT_HOSTNAME_FMT, nid) <= 0)
	    {
		    fprintf(stderr, "_cti_cray_slurm_getHostName asprintf failed.\n");
            free(hostname);
            hostname = NULL;
		    return NULL;
	    }
	}

    else // Fallback to standard hostname
    {
	    // allocate memory for the hostname
	    if ((hostname = (decltype(hostname))malloc(HOST_NAME_MAX)) == NULL)
	    {
		    fprintf(stderr, "_cti_cray_slurm_getHostName: malloc failed.\n");
		    return NULL;
	    }

        if (gethostname(hostname, HOST_NAME_MAX) < 0)
        {
            fprintf(stderr, "%s", "_cti_cray_slurm_getHostName: gethostname() failed!\n");
            hostname = NULL;
		    return NULL;
        }
    }

    return strdup(hostname); // One way or the other
}

static const char *
_cti_cray_slurm_getToolPath(craySlurmInfo_t *my_app)
{
	// sanity check
	if (my_app == NULL)
	{
		_cti_set_error("getToolPath operation failed.");
		return NULL;
	}
	
	// sanity check
	if (my_app->toolPath == NULL)
	{
		_cti_set_error("toolPath info missing from sinfo obj!");
		return NULL;
	}

	return (const char *)my_app->toolPath;
}

static const char *
_cti_cray_slurm_getAttribsPath(craySlurmInfo_t *my_app)
{
	// sanity check
	if (my_app == NULL)
	{
		_cti_set_error("getAttribsPath operation failed.");
		return NULL;
	}
	
	// sanity check
	if (my_app->attribsPath == NULL)
	{
		_cti_set_error("attribsPath info missing from sinfo obj!");
		return NULL;
	}
	
	return (const char *)my_app->attribsPath;
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

static std::unordered_map<AppId, UniquePtrDestr<craySlurmInfo_t>> appList;
static const AppId APP_ERROR = 0;
static AppId newAppId() noexcept {
	static AppId nextId = 1;
	return nextId++;
}

static craySlurmInfo_t*
getInfoPtr(AppId appId) {
	auto infoPtr = appList.find(appId);
	if (infoPtr != appList.end()) {
		return infoPtr->second.get();
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
	return _cti_cray_slurm_getJobId(getInfoPtr(appId));
}

AppId
CraySLURMFrontend::launch(CArgArray launcher_argv, int stdout_fd, int stderr,
                     CStr inputFile, CStr chdirPath, CArgArray env_list) {
	auto const appId = newAppId();
	if (auto craySlurmInfoPtr = _cti_cray_slurm_launch(launcher_argv, stdout_fd, stderr, inputFile, chdirPath, env_list, appId)) {
		appList[appId] = UniquePtrDestr<craySlurmInfo_t>(craySlurmInfoPtr, _cti_cray_slurm_consumeSlurmInfo);
		return appId;
	} else {
		throw std::runtime_error(std::string("launch: ") + cti_error_str());
	}
}

AppId
CraySLURMFrontend::launchBarrier(CArgArray launcher_argv, int stdout_fd, int stderr,
                            CStr inputFile, CStr chdirPath, CArgArray env_list) {
	auto const appId = newAppId();
	if (auto craySlurmInfoPtr = _cti_cray_slurm_launchBarrier(launcher_argv, stdout_fd, stderr, inputFile, chdirPath, env_list, appId)) {
		appList[appId] = UniquePtrDestr<craySlurmInfo_t>(craySlurmInfoPtr, _cti_cray_slurm_consumeSlurmInfo);
		return appId;
	} else {
		throw std::runtime_error(std::string("launchBarrier: ") + cti_error_str());
	}
}

void
CraySLURMFrontend::releaseBarrier(AppId appId) {
	_cti_mpir_releaseInstance(getInfoPtr(appId)->mpir_id);
}

void
CraySLURMFrontend::killApp(AppId appId, int signal) {
	if (_cti_cray_slurm_killApp(getInfoPtr(appId), signal)) {
		throw std::runtime_error(std::string("killApp: ") + cti_error_str());
	}
}

std::vector<std::string> const
CraySLURMFrontend::getExtraFiles(AppId appId) const {
	std::vector<std::string> result;
	auto extraFileList = _cti_cray_slurm_extraFiles(getInfoPtr(appId));
	for (const char* const* filePath = extraFileList; *filePath != nullptr; filePath++) {
		result.emplace_back(*filePath);
	}
	return result;
}


void
CraySLURMFrontend::shipPackage(AppId appId, std::string const& tarPath) const {
	if (_cti_cray_slurm_ship_package(getInfoPtr(appId), tarPath.c_str())) {
		throw std::runtime_error(std::string("shipPackage: ") + cti_error_str());
	}
}

void
CraySLURMFrontend::startDaemon(AppId appId, CArgArray argv) const {
	auto cti_argv = _cti_newArgs();
	for (const char* const* arg = argv; *arg != nullptr; arg++) {
		_cti_addArg(cti_argv, *arg);
	}
	if (_cti_cray_slurm_start_daemon(getInfoPtr(appId), cti_argv)) {
		_cti_freeArgs(cti_argv);
		throw std::runtime_error(std::string("startDaemon: ") + cti_error_str());
	}
	_cti_freeArgs(cti_argv);
}


size_t
CraySLURMFrontend::getNumAppPEs(AppId appId) const {
	if (auto numAppPEs = _cti_cray_slurm_getNumAppPEs(getInfoPtr(appId))) {
		return numAppPEs;
	} else {
		throw std::runtime_error(std::string("getNumAppPEs: ") + cti_error_str());
	}
}

size_t
CraySLURMFrontend::getNumAppNodes(AppId appId) const {
	if (auto numAppNodes = _cti_cray_slurm_getNumAppNodes(getInfoPtr(appId))) {
		return numAppNodes;
	} else {
		throw std::runtime_error(std::string("getNumAppPEs: ") + cti_error_str());
	}
}

std::vector<std::string> const
CraySLURMFrontend::getAppHostsList(AppId appId) const {
	if (char** appHostsList = _cti_cray_slurm_getAppHostsList(getInfoPtr(appId))) {
		std::vector<std::string> result;
		for (char** host = appHostsList; *host != nullptr; host++) {
			result.emplace_back(*host);
			free(*host);
		}
		free(appHostsList);
		return result;
	} else {
		throw std::runtime_error(std::string("getNumAppPEs: ") + cti_error_str());
	}
}

std::vector<CTIHost> const
CraySLURMFrontend::getAppHostsPlacement(AppId appId) const {
	if (auto appHostsPlacement = _cti_cray_slurm_getAppHostsPlacement(getInfoPtr(appId))) {
		std::vector<CTIHost> result;
		for (int i = 0; i < appHostsPlacement->numHosts; i++) {
			result.emplace_back(appHostsPlacement->hosts[i].hostname, appHostsPlacement->hosts[i].numPEs);
		}
		cti_destroyHostsList(appHostsPlacement);
		return result;
	} else {
		throw std::runtime_error(std::string("getAppHostsPlacement: ") + cti_error_str());
	}
}

std::string const
CraySLURMFrontend::getHostName(void) const {
	if (auto hostname = _cti_cray_slurm_getHostName()) {
		return hostname;
	} else {
		throw std::runtime_error(std::string("getHostName: ") + cti_error_str());
	}
}

std::string const
CraySLURMFrontend::getLauncherHostName(AppId appId) const {
	throw std::runtime_error("getLauncherHostName not supported for Cray SLURM (app ID " + std::to_string(appId));
}

std::string const
CraySLURMFrontend::getToolPath(AppId appId) const {
	if (auto toolPath = _cti_cray_slurm_getToolPath(getInfoPtr(appId))) {
		return toolPath;
	} else {
		throw std::runtime_error(std::string("getToolPath: ") + cti_error_str());
	}
}

std::string const
CraySLURMFrontend::getAttribsPath(AppId appId) const {
	if (auto attribsPath = _cti_cray_slurm_getAttribsPath(getInfoPtr(appId))) {
		return attribsPath;
	} else {
		throw std::runtime_error(std::string("getAttribsPath: ") + cti_error_str());
	}
}

/* extended frontend implementation */

CraySLURMFrontend::~CraySLURMFrontend() {
	// force cleanup to happen on any pending srun launches
	_cti_mpir_releaseAllInstances();
}

AppId
CraySLURMFrontend::registerJobStep(uint32_t jobid, uint32_t stepid) {
	// create the cray variation of the jobid+stepid
	auto apid = CRAY_SLURM_APID(jobid, stepid);

	// iterate through the app list to try to find an existing entry for this apid
	for (auto const& appIdInfoPair : appList) {
		if (appIdInfoPair.second->apid == apid) {
			return appIdInfoPair.first;
		}
	}

	// aprun pid not found in the global _cti_alps_info list
	// so lets create a new appEntry_t object for it
	auto appId = newAppId();
	if (auto slurmInfoPtr = _cti_cray_slurm_registerJobStep(jobid, stepid, appId)) {
		appList[appId] = std::unique_ptr<craySlurmInfo_t>(slurmInfoPtr);
		return appId;
	} else {
		throw std::runtime_error(std::string("registerJobStep: ") + cti_error_str());
	}
}

CraySLURMFrontend::SrunInfo*
CraySLURMFrontend::getSrunInfo(AppId appId) {
	if (auto srunInfo = _cti_cray_slurm_getSrunInfo(getInfoPtr(appId))) {
		return srunInfo;
	} else {
		throw std::runtime_error(std::string("getSrunInfo: ") + cti_error_str());
	}
}