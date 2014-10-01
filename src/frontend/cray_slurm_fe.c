/******************************************************************************\
 * cray_slurm_fe.c - Cray SLURM specific frontend library functions.
 *
 * © 2014 Cray Inc.  All Rights Reserved.
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
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include "cti_fe.h"
#include "cti_defs.h"
#include "cti_error.h"

#include "gdb_MPIR_iface.h"

/* Types used here */

typedef struct
{
	cti_gdb_id_t	gdb_id;
} srunInv_t;

typedef struct
{
	uint32_t		jobid;			// SLURM job id
	uint32_t		stepid;			// SLURM step id
	uint64_t		apid;			// Cray variant of step+job id
	srunInv_t *		inv;			// Optional object used for launched applications.
} craySlurmInfo_t;

/* Static prototypes */
static int					_cti_cray_slurm_init(void);
static void					_cti_cray_slurm_fini(void);
static void 				_cti_cray_slurm_consumeSlurmInfo(void *);
static void					_cti_cray_slurm_consumeSrunInv(srunInv_t *);
static int					_cti_cray_slurm_cmpJobId(void *, void *);
static char *				_cti_cray_slurm_getJobId(void *);
static cti_app_id_t			_cti_cray_slurm_launchBarrier(char **, int, int, int, int, char *, char *, char **);
static int					_cti_cray_slurm_releaseBarrier(void *);

static void					_cti_cray_slurm_consumeSrunInv(srunInv_t *);

/* cray slurm wlm proto object */
cti_wlm_proto_t				_cti_cray_slurm_wlmProto =
{
	CTI_WLM_CRAY_SLURM,						// wlm_type
	_cti_cray_slurm_init,					// wlm_init
	_cti_cray_slurm_fini,					// wlm_fini
	_cti_cray_slurm_cmpJobId,				// wlm_cmpJobId
	_cti_cray_slurm_getJobId,				// wlm_getJobId
	_cti_cray_slurm_launchBarrier,			// wlm_launchBarrier
	_cti_cray_slurm_releaseBarrier,			// wlm_releaseBarrier
	NULL, //_cti_cray_slurm_killApp,				// wlm_killApp
	NULL, //_cti_cray_slurm_verifyBinary,			// wlm_verifyBinary
	NULL, //_cti_cray_slurm_verifyLibrary,			// wlm_verifyLibrary
	NULL, //_cti_cray_slurm_verifyLibDir,			// wlm_verifyLibDir
	NULL, //_cti_cray_slurm_verifyFile,				// wlm_verifyFile
	NULL, //_cti_cray_slurm_extraBinaries,			// wlm_extraBinaries
	NULL, //_cti_cray_slurm_extraLibraries,			// wlm_extraLibraries
	NULL, //_cti_cray_slurm_extraLibDirs,			// wlm_extraLibDirs
	NULL, //_cti_cray_slurm_extraFiles,				// wlm_extraFiles
	NULL, //_cti_cray_slurm_ship_package,			// wlm_shipPackage
	NULL, //_cti_cray_slurm_start_daemon,			// wlm_startDaemon
	NULL, //_cti_cray_slurm_getNumAppPEs,			// wlm_getNumAppPEs
	NULL, //_cti_cray_slurm_getNumAppNodes,			// wlm_getNumAppNodes
	NULL, //_cti_cray_slurm_getAppHostsList,		// wlm_getAppHostsList
	NULL, //_cti_cray_slurm_getAppHostsPlacement,	// wlm_getAppHostsPlacement
	NULL, //_cti_cray_slurm_getHostName,			// wlm_getHostName
	NULL, //_cti_cray_slurm_getLauncherHostName		// wlm_getLauncherHostName
};

/* Constructor/Destructor functions */

static int
_cti_cray_slurm_init(void)
{
	// done
	return 0;
}

static void
_cti_cray_slurm_fini(void)
{
	// force cleanup to happen on any pending srun launches - we do this to ensure
	// gdb instances don't get left hanging around.
	_cti_gdb_cleanupAll();

	// done
	return;
}

static void 
_cti_cray_slurm_consumeSlurmInfo(void *this)
{
	craySlurmInfo_t *	sinfo = (craySlurmInfo_t *)this;

	// sanity
	if (sinfo == NULL)
		return;

	_cti_cray_slurm_consumeSrunInv(sinfo->inv);
	
	free(sinfo);
}

static void
_cti_cray_slurm_consumeSrunInv(srunInv_t *runPtr)
{
	// sanity
	if (runPtr == NULL)
		return;
		
	_cti_gdb_cleanup(runPtr->gdb_id);
	
	// free the object from memory
	free(runPtr);
}

static int
_cti_cray_slurm_cmpJobId(void *this, void *id)
{
	craySlurmInfo_t *	my_app = (craySlurmInfo_t *)this;
	uint64_t			apid;
	
	// sanity check
	if (my_app == NULL)
	{
		_cti_set_error("Null wlm obj.");
		return -1;
	}
	
	// sanity check
	if (id == NULL)
	{
		_cti_set_error("Null job id.");
		return -1;
	}
	
	apid = *((uint64_t *)id);
	
	return my_app->apid == apid;
}

// Note that we should provide this as a jobid.stepid format. It will make turning
// it into a Cray apid easier on the backend since we don't lose any information
// with this format.
static char *
_cti_cray_slurm_getJobId(void *this)
{
	craySlurmInfo_t *	my_app = (craySlurmInfo_t *)this;
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

// this function creates a new appEntry_t object for the app
cti_app_id_t
cti_registerJobStep(uint32_t jobid, uint32_t stepid)
{
	uint64_t			apid;	// Cray version of the job+step
	craySlurmInfo_t	*	sinfo;
	char *				toolPath;
	cti_app_id_t		appId = 0;

	// sanity check - Note that 0 is a valid step id.
	if (jobid == 0)
	{
		_cti_set_error("Invalid jobid %d.", (int)jobid);
		return 0;
	}
	
	// sanity check
	if (cti_current_wlm() != CTI_WLM_CRAY_SLURM)
	{
		_cti_set_error("Invalid call. Cray SLURM WLM not in use.");
		return 0;
	}
	
	// create the cray variation of the jobid+stepid
	apid = CRAY_SLURM_APID(jobid, stepid);
	
	// try to find an entry in the _cti_my_apps list for the apid
	if (_cti_findAppEntryByJobId((void *)&apid) == NULL)
	{
		// apid not found in the global _cti_my_apps list
		// so lets create a new appEntry_t object for it
	
		// create the new craySlurmInfo_t object
		if ((sinfo = malloc(sizeof(craySlurmInfo_t))) == NULL)
		{
			_cti_set_error("malloc failed.");
			return 0;
		}
		memset(sinfo, 0, sizeof(craySlurmInfo_t));     // clear it to NULL
		
		// set the jobid
		sinfo->jobid = jobid;
		// set the stepid
		sinfo->stepid = stepid;
		// set the apid
		sinfo->apid = apid;
		
		// create the toolPath
		if (asprintf(&toolPath, CRAY_SLURM_TOOL_DIR, (long long unsigned int)apid) <= 0)
		{
			_cti_set_error("asprintf failed");
			_cti_cray_slurm_consumeSlurmInfo(sinfo);
			return 0;
		}
		
		// retrieve detailed information about our app
		// TODO: Figure out how to parse slurm utilities and what information is actually needed
		
		// create the new app entry
		if ((appId = _cti_newAppEntry(&_cti_cray_slurm_wlmProto, toolPath, (void *)sinfo, &_cti_cray_slurm_consumeSlurmInfo)) == 0)
		{
			// we failed to create a new appEntry_t entry - catastrophic failure
			// error string already set
			_cti_cray_slurm_consumeSlurmInfo(sinfo);
			free(toolPath);
			return 0;
		}
		
		free(toolPath);
	} else
	{
		// apid was already registerd. This is a failure.
		_cti_set_error("apid already registered");
		return 0;
	}

	return appId;
}

static cti_app_id_t
_cti_cray_slurm_launchBarrier(	char **launcher_argv, int redirectOutput, int redirectInput, 
							int stdout_fd, int stderr_fd, char *inputFile, char *chdirPath,
							char **env_list	)
{
	srunInv_t *			myapp;
	appEntry_t *		appEntry;
	craySlurmInfo_t	*	sinfo;
	int					i;
	sigset_t			mask, omask;	// used to ignore SIGINT
	pid_t				mypid;
	char *				jobid_str;
	char *				end_p;
	uint32_t			jobid;
	uint32_t			stepid;
	cti_app_id_t		rtn;			// return object
	

	// create a new srunInv_t object
	if ((myapp = malloc(sizeof(srunInv_t))) == (void *)0)
	{
		// Malloc failed
		_cti_set_error("malloc failed.");
		
		return 0;
	}
	memset(myapp, 0, sizeof(srunInv_t));     // clear it to NULL
	
	// Create a new gdb MPIR instance. We want to interact with it.
	if ((myapp->gdb_id = _cti_gdb_newInstance()) < 0)
	{
		// error already set
		_cti_cray_slurm_consumeSrunInv(myapp);
		
		return 0;
	}
	
	// We don't want slurm to pass along signals the caller recieves to the
	// application process. In order to stop this from happening we need to put
	// the child into a different process group.
	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);
	sigprocmask(SIG_BLOCK, &mask, &omask);
	
	// fork off a process to start the mpir starter
	mypid = fork();
	
	// error case
	if (mypid < 0)
	{
		_cti_set_error("Fatal fork error.");
		_cti_cray_slurm_consumeSrunInv(myapp);
		
		return 0;
	}
	
	// child case
	// Note that this should not use the _cti_set_error() interface since it is
	// a child process. Even though the libmi library will do its own fork, we
	// still do it here to setup various tasks
	if (mypid == 0)
	{
		char *	i_file = NULL;
		
		// *sigh* I don't know why I just didn't make this a null check...
		if (redirectInput)
		{
			i_file = inputFile;
		} else
		{
			// want to make sure srun doesn't suck up any input from us
			i_file = "/dev/null";
		}
		
		// chdir if directed
		if (chdirPath != NULL)
		{
			if (chdir(chdirPath))
			{
				fprintf(stderr, "CTI error: Unable to chdir to provided path.\n");
				exit(1);
			}
		}
		
		// if env_list is not null, call putenv for each entry in the list
		if (env_list != (char **)NULL)
		{
			i = 0;
			while(env_list[i] != NULL)
			{
				// putenv returns non-zero on error
				if (putenv(env_list[i++]))
				{
					fprintf(stderr, "CTI error: Unable to putenv provided env_list.\n");
					exit(1);
				}
			}
		}
		
		// Place this process in its own group to prevent signals being passed
		// to it. This is necessary in case the child code execs before the 
		// parent can put us into our own group. This is so that we won't get
		// the ctrl-c when aprun re-inits the signal handlers.
		setpgid(0, 0);
		
		// call the exec function - this should not return
		_cti_gdb_execStarter(myapp->gdb_id, SRUN, launcher_argv, i_file);
		
		// exec shouldn't return
		fprintf(stderr, "CTI error: Return from exec.\n");
		perror("execvp");
		exit(1);
	}
	
	// parent case
	
	// Place the child in its own group. We still need to block SIGINT in case
	// its delivered to us before we can do this. We need to do this again here
	// in case this code runs before the child code while we are still blocking 
	// ctrl-c
	setpgid(mypid, mypid);
	
	// unblock ctrl-c
	sigprocmask(SIG_SETMASK, &omask, NULL);
	
	// call the post fork setup - this will get us to the startup barrier
	if (_cti_gdb_postFork(myapp->gdb_id))
	{
		// error message already set
		_cti_cray_slurm_consumeSrunInv(myapp);
		
		return 0;
	}
	
	// get the jobid string for slurm
	if ((jobid_str = _cti_gdb_getJobId(myapp->gdb_id, "totalview_jobid")) == NULL)
	{
		// error already set
		_cti_cray_slurm_consumeSrunInv(myapp);
		
		return 0;
	}
	
	// convert the string into the actual jobid
	errno = 0;
	jobid = (uint32_t)strtoul(jobid_str, &end_p, 10);
	
	// check for errors
	if ((errno == ERANGE && (jobid == LONG_MAX || jobid == LONG_MIN)) || (errno != 0 && jobid == 0))
	{
		_cti_set_error("_cti_cray_slurm_launchBarrier: strtoul failed.\n");
		_cti_cray_slurm_consumeSrunInv(myapp);
		
		return 0;
	}
	if (end_p == NULL || *end_p != '\0')
	{
		_cti_set_error("_cti_cray_slurm_launchBarrier: strtoul failed.\n");
		_cti_cray_slurm_consumeSrunInv(myapp);
		
		return 0;
	}
	
	
	
	
	
	
	// TODO: get the step id and issue sattach
	
	
	
	
	
	
	
	
	// register this app with the application interface
	if ((rtn = cti_registerJobStep(jobid, stepid)) == 0)
	{
		// failed to register the jobid/stepid, error is already set.
		_cti_cray_slurm_consumeSrunInv(myapp);
		
		return 0;
	}
	
	// assign the run specific objects to the application obj
	if ((appEntry = _cti_findAppEntry(rtn)) == NULL)
	{
		// this should never happen
		_cti_set_error("_cti_cray_slurm_launchBarrier: impossible null appEntry error!\n");
		_cti_cray_slurm_consumeSrunInv(myapp);
		
		return 0;
	}
	
	// sanity check
	sinfo = (craySlurmInfo_t *)appEntry->_wlmObj;
	if (sinfo == NULL)
	{
		// this should never happen
		_cti_set_error("_cti_cray_slurm_launchBarrier: impossible null sinfo error!\n");
		_cti_cray_slurm_consumeSrunInv(myapp);
		
		return 0;
	}
	
	// set the inv
	sinfo->inv = myapp;
	
	// return the cti_app_id_t
	return rtn;
}

static int
_cti_cray_slurm_releaseBarrier(void *this)
{
	craySlurmInfo_t *	my_app = (craySlurmInfo_t *)this;

	// sanity check
	if (my_app == NULL)
	{
		_cti_set_error("srun barrier release operation failed.");
		return 1;
	}
	
	// sanity check
	if (my_app->inv == NULL)
	{
		_cti_set_error("srun barrier release operation failed.");
		return 1;
	}
	
	// call the release function
	if (_cti_gdb_releaseBarrier(my_app->inv->gdb_id))
	{
		_cti_set_error("srun barrier release operation failed.");
		return 1;
	}
	
	// cleanup the sruninv, we are done with it. This will release memory and
	// free up the hash table for more possible instances. It is important to do
	// this step here and not later on.
	_cti_cray_slurm_consumeSrunInv(my_app->inv);
	my_app->inv = NULL;
	
	// done
	return 0;
}
