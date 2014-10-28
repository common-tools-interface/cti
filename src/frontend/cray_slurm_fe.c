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

#include "cray_slurm_fe.h"
#include "cti_fe.h"
#include "cti_defs.h"
#include "cti_error.h"
#include "cti_useful.h"

#include "gdb_MPIR_iface.h"

/* Types used here */

typedef struct
{
	cti_gdb_id_t	gdb_id;
	pid_t			gdb_pid;			// pid of the gdb process for the mpir starter
	pid_t			sattach_pid;		// Optional pid of the sattach process if we are redirecting io
} srunInv_t;

typedef struct
{
	char *			host;				// hostname of this node
	int				PEsHere;			// Number of PEs running on this node
	int				firstPE;			// First PE number on this node
} slurmNodeLayout_t;

typedef struct
{
	int					numPEs;			// Number of PEs associated with the job step
	int					numNodes;		// Number of nodes associated with the job step
	slurmNodeLayout_t *	hosts;			// Array of hosts of length numNodes
} slurmStepLayout_t;

typedef struct
{
	uint32_t			jobid;			// SLURM job id
	uint32_t			stepid;			// SLURM step id
	uint64_t			apid;			// Cray variant of step+job id
	slurmStepLayout_t *	layout;			// Layout of job step
	srunInv_t *			inv;			// Optional object used for launched applications.
	cti_mpir_pid_t *	app_pids;		// Optional object used to hold the rank->pid association
	char *				toolPath;		// Backend staging directory
	int					dlaunch_sent;	// True if we have already transfered the dlaunch utility
	char *				stagePath;		// directory to stage this instance files in for transfer to BE
	char **				extraFiles;		// extra files to transfer to BE associated with this app
} craySlurmInfo_t;

/* Static prototypes */
static int					_cti_cray_slurm_init(void);
static void					_cti_cray_slurm_fini(void);
static craySlurmInfo_t *	_cti_cray_slurm_newSlurmInfo(void);
static void 				_cti_cray_slurm_consumeSlurmInfo(cti_wlm_obj);
static srunInv_t *			_cti_cray_slurm_newSrunInv(void);
static void					_cti_cray_slurm_consumeSrunInv(srunInv_t *);
static slurmStepLayout_t *	_cti_cray_slurm_newSlurmLayout(int, int);
static void					_cti_cray_slurm_consumeSlurmLayout(slurmStepLayout_t *);
static slurmStepLayout_t *	_cti_cray_slurm_getLayout(uint32_t, uint32_t);
static int					_cti_cray_slurm_cmpJobId(cti_wlm_obj, cti_wlm_apid);
static char *				_cti_cray_slurm_getJobId(cti_wlm_obj);
static cti_app_id_t			_cti_cray_slurm_launchBarrier(const char * const [], int, int, int, int, const char *, const char *, const char * const []);
static int					_cti_cray_slurm_releaseBarrier(cti_wlm_obj);
static int					_cti_cray_slurm_killApp(cti_wlm_obj, int);
static int					_cti_cray_slurm_verifyBinary(cti_wlm_obj, const char *);
static int					_cti_cray_slurm_verifyLibrary(cti_wlm_obj, const char *);
static int					_cti_cray_slurm_verifyLibDir(cti_wlm_obj, const char *);
static int					_cti_cray_slurm_verifyFile(cti_wlm_obj, const char *);
static const char * const *	_cti_cray_slurm_extraBinaries(cti_wlm_obj);
static const char * const *	_cti_cray_slurm_extraLibraries(cti_wlm_obj);
static const char * const *	_cti_cray_slurm_extraLibDirs(cti_wlm_obj);
static const char * const *	_cti_cray_slurm_extraFiles(cti_wlm_obj);
static int					_cti_cray_slurm_ship_package(cti_wlm_obj, const char *);
static int					_cti_cray_slurm_start_daemon(cti_wlm_obj, cti_args_t *);
static int					_cti_cray_slurm_getNumAppPEs(cti_wlm_obj);
static int					_cti_cray_slurm_getNumAppNodes(cti_wlm_obj);
static char **				_cti_cray_slurm_getAppHostsList(cti_wlm_obj);
static cti_hostsList_t *	_cti_cray_slurm_getAppHostsPlacement(cti_wlm_obj);
static char *				_cti_cray_slurm_getHostName(void);
static const char *			_cti_cray_slurm_getToolPath(cti_wlm_obj);

/* cray slurm wlm proto object */
const cti_wlm_proto_t		_cti_cray_slurm_wlmProto =
{
	CTI_WLM_CRAY_SLURM,						// wlm_type
	_cti_cray_slurm_init,					// wlm_init
	_cti_cray_slurm_fini,					// wlm_fini
	_cti_cray_slurm_consumeSlurmInfo,		// wlm_destroy
	_cti_cray_slurm_cmpJobId,				// wlm_cmpJobId
	_cti_cray_slurm_getJobId,				// wlm_getJobId
	_cti_cray_slurm_launchBarrier,			// wlm_launchBarrier
	_cti_cray_slurm_releaseBarrier,			// wlm_releaseBarrier
	_cti_cray_slurm_killApp,				// wlm_killApp
	_cti_cray_slurm_verifyBinary,			// wlm_verifyBinary
	_cti_cray_slurm_verifyLibrary,			// wlm_verifyLibrary
	_cti_cray_slurm_verifyLibDir,			// wlm_verifyLibDir
	_cti_cray_slurm_verifyFile,				// wlm_verifyFile
	_cti_cray_slurm_extraBinaries,			// wlm_extraBinaries
	_cti_cray_slurm_extraLibraries,			// wlm_extraLibraries
	_cti_cray_slurm_extraLibDirs,			// wlm_extraLibDirs
	_cti_cray_slurm_extraFiles,				// wlm_extraFiles
	_cti_cray_slurm_ship_package,			// wlm_shipPackage
	_cti_cray_slurm_start_daemon,			// wlm_startDaemon
	_cti_cray_slurm_getNumAppPEs,			// wlm_getNumAppPEs
	_cti_cray_slurm_getNumAppNodes,			// wlm_getNumAppNodes
	_cti_cray_slurm_getAppHostsList,		// wlm_getAppHostsList
	_cti_cray_slurm_getAppHostsPlacement,	// wlm_getAppHostsPlacement
	_cti_cray_slurm_getHostName,			// wlm_getHostName
	_cti_wlm_getLauncherHostName_none,		// wlm_getLauncherHostName - FIXME: Not supported by slurm
	_cti_cray_slurm_getToolPath				// wlm_getToolPath
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

static craySlurmInfo_t *
_cti_cray_slurm_newSlurmInfo(void)
{
	craySlurmInfo_t *	this;

	if ((this = malloc(sizeof(craySlurmInfo_t))) == NULL)
	{
		// Malloc failed
		_cti_set_error("malloc failed.");
		
		return NULL;
	}
	
	// init the members
	this->jobid			= 0;
	this->stepid		= 0;
	this->apid			= 0;
	this->layout		= NULL;
	this->inv			= NULL;
	this->app_pids		= NULL;
	this->toolPath		= NULL;
	this->dlaunch_sent	= 0;
	this->stagePath		= NULL;
	this->extraFiles	= NULL;
	
	return this;
}

static void 
_cti_cray_slurm_consumeSlurmInfo(cti_wlm_obj this)
{
	craySlurmInfo_t *	sinfo = (craySlurmInfo_t *)this;

	// sanity
	if (sinfo == NULL)
		return;

	_cti_cray_slurm_consumeSlurmLayout(sinfo->layout);
	_cti_cray_slurm_consumeSrunInv(sinfo->inv);
	_cti_gdb_freeMpirPid(sinfo->app_pids);
	
	if (sinfo->toolPath != NULL)
		free(sinfo->toolPath);
		
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

static srunInv_t *
_cti_cray_slurm_newSrunInv(void)
{
	srunInv_t *	this;

	if ((this = malloc(sizeof(srunInv_t))) == NULL)
	{
		// Malloc failed
		_cti_set_error("malloc failed.");
		
		return NULL;
	}
	
	// init the members
	this->gdb_id = -1;
	this->gdb_pid = -1;
	this->sattach_pid = -1;
	
	return this;
}

static void
_cti_cray_slurm_consumeSrunInv(srunInv_t *this)
{
	// sanity
	if (this == NULL)
		return;
	
	if (this->gdb_id >= 0)
	{
		_cti_gdb_cleanup(this->gdb_id);
	}
	
	if (this->gdb_pid >= 0)
	{
		// wait for the starter to exit
		waitpid(this->gdb_pid, NULL, 0);
	}
	
	if (this->sattach_pid >= 0)
	{
		// kill sattach
		kill(this->sattach_pid, DEFAULT_SIG);
		waitpid(this->sattach_pid, NULL, 0);
	}
	
	// free the object from memory
	free(this);
}

static slurmStepLayout_t *
_cti_cray_slurm_newSlurmLayout(int numPEs, int numNodes)
{
	slurmStepLayout_t *	this;

	if ((this = malloc(sizeof(slurmStepLayout_t))) == NULL)
	{
		// Malloc failed
		_cti_set_error("malloc failed.");
		
		return NULL;
	}
	
	// init the members
	this->numPEs = numPEs;
	this->numNodes = numNodes;
	if ((this->hosts = malloc(sizeof(slurmNodeLayout_t) * numNodes)) == NULL)
	{
		// Malloc failed
		_cti_set_error("malloc failed.");
		
		return NULL;
	}
	memset(this->hosts, 0, sizeof(slurmNodeLayout_t) * numNodes);
	
	return this;
}

static void
_cti_cray_slurm_consumeSlurmLayout(slurmStepLayout_t *this)
{
	int i;

	// sanity
	if (this == NULL)
		return;
		
	for (i=0; i < this->numNodes; ++i)
	{
		if (this->hosts[i].host != NULL)
		{
			free(this->hosts[i].host);
		}
	}
	
	free(this->hosts);
	free(this);
}

static slurmStepLayout_t *
_cti_cray_slurm_getLayout(uint32_t jobid, uint32_t stepid)
{
	cti_args_t *		my_args;
	char *				slurm_util_loc;
	int					pipe_r[2];		// pipes to read result of command
	int					pipe_e[2];		// error pipes if error occurs
	int					mypid;
	int					status;
	int					r;
	int					n = 0;
	int					size = 0;
	int					cont = 1;
	char *				read_buf;
	slurmStepLayout_t *	rtn = NULL;
	
	// create a new args obj
	if ((my_args = _cti_newArgs()) == NULL)
	{
		_cti_set_error("_cti_newArgs failed.");
		return NULL;
	}
	
	// create the args for slurm util
	
	if ((slurm_util_loc = _cti_pathFind(SLURM_STEP_UTIL, NULL)) == NULL)
	{
		_cti_set_error("Could not locate CTI slurm job step utility in PATH.");
		return NULL;
	}
	if (_cti_addArg(my_args, "%s", slurm_util_loc))
	{
		_cti_set_error("_cti_addArg failed.");
		_cti_freeArgs(my_args);
		free(slurm_util_loc);
		return NULL;
	}
	free(slurm_util_loc);
	
	if (_cti_addArg(my_args, "-j %d", jobid))
	{
		_cti_set_error("_cti_addArg failed.");
		_cti_freeArgs(my_args);
		return NULL;
	}
	
	if (_cti_addArg(my_args, "-s %d", stepid))
	{
		_cti_set_error("_cti_addArg failed.");
		_cti_freeArgs(my_args);
		return NULL;
	}
	
	// make the pipes for the command
	if (pipe(pipe_r) < 0)
	{
		_cti_set_error("Pipe creation failure.");
		_cti_freeArgs(my_args);
		
		return NULL;
	}
	if (pipe(pipe_e) < 0)
	{
		_cti_set_error("Pipe creation failure.");
		_cti_freeArgs(my_args);
		
		return NULL;
	}
	
	// fork off a process for the slurm utility
	mypid = fork();
	
	// error case
	if (mypid < 0)
	{
		_cti_set_error("Fatal fork error.");
		_cti_freeArgs(my_args);
		
		return NULL;
	}
	
	// child case
	if (mypid == 0)
	{
		int fd;
	
		// close unused ends of pipe
		close(pipe_r[0]);
		close(pipe_e[0]);
		
		// dup2 stdout
		if (dup2(pipe_r[1], STDOUT_FILENO) < 0)
		{
			// XXX: How to properly print this error? The parent won't be
			// expecting the error message on this stream since dup2 failed.
			fprintf(stderr, "CTI error: Unable to redirect stdout.\n");
			exit(1);
		}
		
		// dup2 stderr
		if (dup2(pipe_e[1], STDERR_FILENO) < 0)
		{
			// XXX: How to properly print this error? The parent won't be
			// expecting the error message on this stream since dup2 failed.
			fprintf(stderr, "CTI error: Unable to redirect stderr.\n");
			exit(1);
		}
		
		// we want to redirect stdin to /dev/null since it is not required
		if ((fd = open("/dev/null", O_RDONLY)) < 0)
		{
			// XXX: How to properly print this error?
			fprintf(stderr, "CTI error: Unable to open /dev/null for reading.\n");
			exit(1);
		}
		
		// dup2 the fd onto STDIN_FILENO
		if (dup2(fd, STDIN_FILENO) < 0)
		{
			// XXX: How to properly print this error?
			fprintf(stderr, "CTI error: Unable to redirect stdin.\n");
			exit(1);
		}
		close(fd);
		
		// exec slurm utility
		execvp(my_args->argv[0], my_args->argv);
		
		// exec shouldn't return
		fprintf(stderr, "CTI error: Return from exec.\n");
		exit(1);
	}
	
	// parent case
	
	// close unused ends of pipe
	close(pipe_r[1]);
	close(pipe_e[1]);
	
	// cleanup
	_cti_freeArgs(my_args);
	
	// allocate the read buffer
	if ((read_buf = malloc(CTI_BUF_SIZE)) == NULL)
	{
		_cti_set_error("malloc failed.");
		close(pipe_r[0]);
		close(pipe_e[0]);
		
		return NULL;
	}
	
	size = CTI_BUF_SIZE;
	n = 0;
	
	// read from the stdout pipe until it is closed
	while (cont)
	{
		errno = 0;
		r = read(pipe_r[0], read_buf+n, size-n);
		switch (r)
		{
			case -1:
				// error occured
				if (errno == EINTR)
				{
					// ignore the error and try again
					break;
				}
				_cti_set_error("read failed.");
				free(read_buf);
				close(pipe_r[0]);
				close(pipe_e[0]);
				
				return NULL;
		
			case 0:
				// done
				cont = 0;
				
				break;
		
			default:
				// ensure we have room in the buffer for another read
				n += r;
				if (n == size)
				{
					char *	tmp_buf;
					
					// need to realloc the buffer
					size += CTI_BUF_SIZE;
					if ((tmp_buf = realloc(read_buf, size)) == NULL)
					{
						// failure
						_cti_set_error("realloc failed.");
						free(read_buf);
						close(pipe_r[0]);
						close(pipe_e[0]);
				
						return NULL;
					}
					read_buf = tmp_buf;
				}
				
				break;
		}
	}
	
	// ensure that read_buf is null terminated
	// First make sure there is room for the null terminator
	if (n == size)
	{
		char *	tmp_buf;
		
		// need to add one more byte to the read buffer
		size += 1;
		if ((tmp_buf = realloc(read_buf, size)) == NULL)
		{
			// failure
			_cti_set_error("realloc failed.");
			free(read_buf);
			close(pipe_r[0]);
			close(pipe_e[0]);
	
			return NULL;
		}
		read_buf = tmp_buf;
	}
	
	// set the null terminator
	read_buf[n] = '\0';
	
	// wait until the command finishes
	if (waitpid(mypid, &status, 0) != mypid)
	{
		// waitpid failed
		_cti_set_error("waitpid failed.");
		free(read_buf);
		close(pipe_r[0]);
		close(pipe_e[0]);
		
		return NULL;
	}
	
	if (WIFEXITED(status))
	{
		switch(WEXITSTATUS(status))
		{
			case 0:
			{
				// exited normally, we should now parse the output from the read_buf
				// format is num_PEs num_nodes host:num_here:PE0 ...
				long int	numPEs_l, numNodes_l;	
				int			numPEs, numNodes;	
				char *		ptr;
				char *		e_ptr = NULL;
				int			i;
				
				ptr = read_buf;
				errno = 0;
				
				// get the numPEs arg
				numPEs_l = strtol(ptr, &e_ptr, 10);
				
				// check for error
				if ((errno == ERANGE && (numPEs_l == LONG_MAX || numPEs_l == LONG_MIN))
						|| (errno != 0 && numPEs_l == 0))
				{
					_cti_set_error("strtol failed.");
					free(read_buf);
					close(pipe_r[0]);
					close(pipe_e[0]);
					
					return NULL;
				}
				
				// check for invalid input
				if ((e_ptr == ptr) || (numPEs_l > INT_MAX) || (numPEs_l < INT_MIN))
				{
					_cti_set_error("Bad slurm job step utility output.");
					free(read_buf);
					close(pipe_r[0]);
					close(pipe_e[0]);
					
					return NULL;
				}
				
				numPEs = (int)numPEs_l;
				ptr = e_ptr;
				e_ptr = NULL;
				errno = 0;
				
				// get the numNodes arg
				numNodes_l = strtol(ptr, &e_ptr, 10);
				
				// check for error
				if ((errno == ERANGE && (numNodes_l == LONG_MAX || numNodes_l == LONG_MIN))
						|| (errno != 0 && numNodes_l == 0))
				{
					_cti_set_error("strtol failed.");
					free(read_buf);
					close(pipe_r[0]);
					close(pipe_e[0]);
					
					return NULL;
				}
				
				// check for invalid input
				if ((e_ptr == ptr) || (numNodes_l > INT_MAX) || (numNodes_l < INT_MIN))
				{
					_cti_set_error("Bad slurm job step utility output.");
					free(read_buf);
					close(pipe_r[0]);
					close(pipe_e[0]);
					
					return NULL;
				}
				
				numNodes = (int)numNodes_l;
				ptr = e_ptr;
				// advance past whitespace
				while (*ptr == ' ')
				{
					++ptr;
				}
				
				// create the return object
				if ((rtn = _cti_cray_slurm_newSlurmLayout(numPEs, numNodes)) == NULL)
				{
					// error already set
					free(read_buf);
					close(pipe_r[0]);
					close(pipe_e[0]);
					
					return NULL;
				}
				
				// now read in each of the host layout strings
				for (i=0; i < numNodes; ++i)
				{
					char *		tok;
					char		del[2];
					long int	val;
					char *		e;
					
					// setup delimiter
					del[0] = ':';
					del[1] = '\0';
					
					// error check
					if (*ptr == '\0')
					{
						_cti_set_error("Bad slurm job step utility output.");
						_cti_cray_slurm_consumeSlurmLayout(rtn);
						free(read_buf);
						close(pipe_r[0]);
						close(pipe_e[0]);
					
						return NULL;
					}
					
					// walk to the end of this substring
					e_ptr = ptr;
					for ( ; *e_ptr != ' ' && *e_ptr != '\0'; ++e_ptr);
					
					// only set/advance e_ptr if we are not pointing at the end
					// of the string
					if (*e_ptr == ' ')
					{
						// set the null term
						*e_ptr = '\0';
						// advance past the null term
						++e_ptr;
					}
					
					// get the first token in the substring
					// format is host:num_here:PE0 ...
					tok = strtok(ptr, del);
					if (tok == NULL)
					{
						// could not find token, error occurred
						_cti_set_error("Bad slurm job step utility output.");
						_cti_cray_slurm_consumeSlurmLayout(rtn);
						free(read_buf);
						close(pipe_r[0]);
						close(pipe_e[0]);
					
						return NULL;
					}
					// set the hostname
					rtn->hosts[i].host = strdup(tok);
					
					// get the second token in the substring
					tok = strtok(NULL, del);
					if (tok == NULL)
					{
						// could not find token, error occurred
						_cti_set_error("Bad slurm job step utility output.");
						_cti_cray_slurm_consumeSlurmLayout(rtn);
						free(read_buf);
						close(pipe_r[0]);
						close(pipe_e[0]);
					
						return NULL;
					}
					
					// process the num_here tok
					errno = 0;
					e = NULL;
					val = strtol(tok, &e, 10);
					
					// check for error
					if ((errno == ERANGE && (val == LONG_MAX || val == LONG_MIN))
							|| (errno != 0 && val == 0))
					{
						_cti_set_error("strtol failed.");
						_cti_cray_slurm_consumeSlurmLayout(rtn);
						free(read_buf);
						close(pipe_r[0]);
						close(pipe_e[0]);
					
						return NULL;
					}
				
					// check for invalid input
					if ((e == tok) || (val > INT_MAX) || (val < INT_MIN))
					{
						_cti_set_error("Bad slurm job step utility output.");
						_cti_cray_slurm_consumeSlurmLayout(rtn);
						free(read_buf);
						close(pipe_r[0]);
						close(pipe_e[0]);
					
						return NULL;
					}
					
					// set the num_here val
					rtn->hosts[i].PEsHere = (int)val;
					
					// get the third token in the substring
					tok = strtok(NULL, del);
					if (tok == NULL)
					{
						// could not find token, error occurred
						_cti_set_error("Bad slurm job step utility output.");
						_cti_cray_slurm_consumeSlurmLayout(rtn);
						free(read_buf);
						close(pipe_r[0]);
						close(pipe_e[0]);
					
						return NULL;
					}
					
					// process the firstPE tok
					errno = 0;
					e = NULL;
					val = strtol(tok, &e, 10);
					
					// check for error
					if ((errno == ERANGE && (val == LONG_MAX || val == LONG_MIN))
							|| (errno != 0 && val == 0))
					{
						_cti_set_error("strtol failed.");
						_cti_cray_slurm_consumeSlurmLayout(rtn);
						free(read_buf);
						close(pipe_r[0]);
						close(pipe_e[0]);
					
						return NULL;
					}
				
					// check for invalid input
					if ((e == tok) || (val > INT_MAX) || (val < INT_MIN))
					{
						_cti_set_error("Bad slurm job step utility output.");
						_cti_cray_slurm_consumeSlurmLayout(rtn);
						free(read_buf);
						close(pipe_r[0]);
						close(pipe_e[0]);
					
						return NULL;
					}
					
					// set the firstPE val
					rtn->hosts[i].firstPE = (int)val;
					
					// advance ptr
					ptr = e_ptr;
				}
			}
				// all done
				free(read_buf);
				close(pipe_r[0]);
				close(pipe_e[0]);
				
				return rtn;
				
			default:
				// error occured
				goto handle_error;
		}
	} else
	{
handle_error:
		// Check to see if we can read from the error fd
		if (ioctl(pipe_e[0], FIONREAD, &n) != 0)
		{
			_cti_set_error("ioctl failed.");
			close(pipe_r[0]);
			close(pipe_e[0]);
			
			return NULL;
		}
		if (n < 1)
		{
			// nothing to read on the pipe
			_cti_set_error("Undefined slurm job step utility failure.");
			close(pipe_r[0]);
			close(pipe_e[0]);
			
			return NULL;
		}
		// free existing read_buf
		free(read_buf);
		// allocate the read buffer
		// XXX: Note that there should not be more data than the size of the
		// pipe on stderr. These messages must not be large, otherwise we could
		// encounter a deadlock.
		if ((read_buf = malloc(n+1)) == NULL)
		{
			_cti_set_error("malloc failed.");
			close(pipe_r[0]);
			close(pipe_e[0]);
			
			return NULL;
		}
		memset(read_buf, '\0', n+1);
		r = read(pipe_e[0], read_buf, n);
		if (r > 0)
		{
			_cti_set_error("slurm job step utility: %s", read_buf);
			free(read_buf);
			close(pipe_r[0]);
			close(pipe_e[0]);
			
			return NULL;
		} else
		{
			// nothing was read - shouldn't happen
			_cti_set_error("Undefined slurm job step utility failure.");
			free(read_buf);
			close(pipe_r[0]);
			close(pipe_e[0]);
			
			return NULL;
		}
	}
	
	// Shouldn't get here...
	free(read_buf);
	close(pipe_r[0]);
	close(pipe_e[0]);
	
	return rtn;
}

static int
_cti_cray_slurm_cmpJobId(cti_wlm_obj this, cti_wlm_apid id)
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
_cti_cray_slurm_getJobId(cti_wlm_obj this)
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
cti_cray_slurm_registerJobStep(uint32_t jobid, uint32_t stepid)
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
	if (_cti_findAppEntryByJobId((cti_wlm_apid)&apid) == NULL)
	{
		// apid not found in the global _cti_my_apps list
		// so lets create a new appEntry_t object for it
	
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
		if (asprintf(&toolPath, CRAY_SLURM_TOOL_DIR, (long long unsigned int)apid) <= 0)
		{
			_cti_set_error("asprintf failed");
			_cti_cray_slurm_consumeSlurmInfo(sinfo);
			return 0;
		}
		sinfo->toolPath = toolPath;
		
		// create the new app entry
		if ((appId = _cti_newAppEntry(&_cti_cray_slurm_wlmProto, (cti_wlm_obj)sinfo)) == 0)
		{
			// we failed to create a new appEntry_t entry - catastrophic failure
			// error string already set
			_cti_cray_slurm_consumeSlurmInfo(sinfo);
			return 0;
		}
	} else
	{
		// apid was already registered. This is a failure.
		_cti_set_error("apid already registered");
		return 0;
	}

	return appId;
}

cti_srunProc_t *
cti_cray_slurm_getSrunInfo(cti_app_id_t appId)
{
	appEntry_t *		app_ptr;
	craySlurmInfo_t	*	sinfo;
	cti_srunProc_t *	srunInfo;
	
	// sanity check
	if (appId == 0)
	{
		_cti_set_error("Invalid appId %d.", (int)appId);
		return NULL;
	}
	
	// try to find an entry in the _cti_my_apps list for the apid
	if ((app_ptr = _cti_findAppEntry(appId)) == NULL)
	{
		// couldn't find the entry associated with the apid
		// error string already set
		return NULL;
	}
	
	// sanity check
	if (app_ptr->wlmProto->wlm_type != CTI_WLM_CRAY_SLURM)
	{
		_cti_set_error("cti_cray_slurm_getSrunInfo: WLM mismatch.");
		return NULL;
	}
	
	// sanity check
	sinfo = (craySlurmInfo_t *)app_ptr->_wlmObj;
	if (sinfo == NULL)
	{
		_cti_set_error("cti_cray_slurm_getSrunInfo: _wlmObj is NULL!");
		return NULL;
	}
	
	// allocate space for the cti_srunProc_t struct
	if ((srunInfo = malloc(sizeof(cti_srunProc_t))) == NULL)
	{
		// malloc failed
		_cti_set_error("malloc failed.");
		return NULL;
	}
	
	srunInfo->jobid = sinfo->jobid;
	srunInfo->stepid = sinfo->stepid;
	
	return srunInfo;
}

static cti_app_id_t
_cti_cray_slurm_launchBarrier(	const char * const launcher_argv[], int redirectOutput, int redirectInput, 
								int stdout_fd, int stderr_fd, const char *inputFile, const char *chdirPath,
								const char * const env_list[]	)
{
	srunInv_t *			myapp;
	appEntry_t *		appEntry;
	craySlurmInfo_t	*	sinfo;
	int					i;
	sigset_t			mask, omask;	// used to ignore SIGINT
	pid_t				mypid;
	char *				sym_str;
	char *				end_p;
	uint32_t			jobid;
	uint32_t			stepid;
	cti_mpir_pid_t *	pids;
	cti_app_id_t		rtn;			// return object
	

	// create a new srunInv_t object
	if ((myapp = _cti_cray_slurm_newSrunInv()) == NULL)
	{
		// error already set
		return 0;
	}
	
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
	// a child process.
	if (mypid == 0)
	{
		const char *	i_file = NULL;
		
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
		if (env_list != NULL)
		{
			for (i=0; env_list[i] != NULL; ++i)
			{
				// putenv returns non-zero on error
				if (putenv(strdup(env_list[i])))
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
	
	// save the pid for later so that we can waitpid() on it when finished
	myapp->gdb_pid = mypid;
	
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
	if ((sym_str = _cti_gdb_getSymbolVal(myapp->gdb_id, "totalview_jobid")) == NULL)
	{
		// error already set
		_cti_cray_slurm_consumeSrunInv(myapp);
		
		return 0;
	}
	
	// convert the string into the actual jobid
	errno = 0;
	jobid = (uint32_t)strtoul(sym_str, &end_p, 10);
	
	// check for errors
	if ((errno == ERANGE && jobid == ULONG_MAX) || (errno != 0 && jobid == 0))
	{
		_cti_set_error("_cti_cray_slurm_launchBarrier: strtoul failed.\n");
		_cti_cray_slurm_consumeSrunInv(myapp);
		free(sym_str);
		
		return 0;
	}
	if (end_p == NULL || *end_p != '\0')
	{
		_cti_set_error("_cti_cray_slurm_launchBarrier: strtoul failed.\n");
		_cti_cray_slurm_consumeSrunInv(myapp);
		free(sym_str);
		
		return 0;
	}
	
	free(sym_str);
	
	// get the stepid string for slurm
	if ((sym_str = _cti_gdb_getSymbolVal(myapp->gdb_id, "totalview_stepid")) == NULL)
	{
		/*
		// error already set
		_cti_cray_slurm_consumeSrunInv(myapp);
		
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
		_cti_set_error("_cti_cray_slurm_launchBarrier: strtoul failed.\n");
		_cti_cray_slurm_consumeSrunInv(myapp);
		free(sym_str);
		
		return 0;
	}
	if (end_p == NULL || *end_p != '\0')
	{
		_cti_set_error("_cti_cray_slurm_launchBarrier: strtoul failed.\n");
		_cti_cray_slurm_consumeSrunInv(myapp);
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
	if ((pids = _cti_gdb_getAppPids(myapp->gdb_id)) == NULL)
	{
		// error already set
		_cti_cray_slurm_consumeSrunInv(myapp);
		
		return 0;
	}
	
	// We now need to fork off an sattach process.
	// For SLURM, sattach makes the iostreams of srun available. This process
	// will exit when the srun process exits. 
	
	// fork off a process to start the sattach command
	mypid = fork();
		
		
	// error case
	if (mypid < 0)
	{
		_cti_set_error("Fatal fork error.");
		_cti_cray_slurm_consumeSrunInv(myapp);
		_cti_gdb_freeMpirPid(pids);
	
		return 0;
	}

	// child case
	// Note that this should not use the _cti_set_error() interface since it is
	// a child process.
	if (mypid == 0)
	{
		int 			fd;
		cti_args_t *	my_args;
	
		// if redirectOutput is true, we use the caller provided fds.
		if (redirectOutput)
		{
			// dup2 stdout
			if (dup2(stdout_fd, STDOUT_FILENO) < 0)
			{
				// XXX: How to properly print this error? The parent won't be
				// expecting the error message on this stream since dup2 failed.
				fprintf(stderr, "CTI error: Unable to redirect srun stdout.\n");
				exit(1);
			}
		
			// dup2 stderr
			if (dup2(stderr_fd, STDERR_FILENO) < 0)
			{
				// XXX: How to properly print this error? The parent won't be
				// expecting the error message on this stream since dup2 failed.
				fprintf(stderr, "CTI error: Unable to redirect srun stderr.\n");
				exit(1);
			}
		}
		
		// we set the input redirection in the mpir interface call. So we just
		// redirect stdin to /dev/null for sattach.
		if ((fd = open("/dev/null", O_RDONLY)) < 0)
		{
			fprintf(stderr, "CTI error: Unable to open /dev/null for reading.\n");
			exit(1);
		}
		
		// dup2 the fd onto STDIN_FILENO
		if (dup2(fd, STDIN_FILENO) < 0)
		{
			fprintf(stderr, "CTI error: Unable to redirect aprun stdin.\n");
			exit(1);
		}
		close(fd);
		
		// create a new args obj
		if ((my_args = _cti_newArgs()) == NULL)
		{
			fprintf(stderr, "CTI error: _cti_newArgs failed.");
			exit(1);
		}
		
		// create the args for sattach
		
		if (_cti_addArg(my_args, "%s", SATTACH))
		{
			fprintf(stderr, "CTI error: _cti_addArg failed.");
			_cti_freeArgs(my_args);
			exit(1);
		}
		
		if (_cti_addArg(my_args, "-Q"))
		{
			fprintf(stderr, "CTI error: _cti_addArg failed.");
			_cti_freeArgs(my_args);
			exit(1);
		}
		
		if (_cti_addArg(my_args, "%u.%u", jobid, stepid))
		{
			fprintf(stderr, "CTI error: _cti_addArg failed.");
			_cti_freeArgs(my_args);
			exit(1);
		}
		
		// exec sattach
		execvp(SATTACH, my_args->argv);
	
		// exec shouldn't return
		fprintf(stderr, "CTI error: Return from exec.\n");
		perror("execvp");
		exit(1);
	}
	
	// parent case
	
	// save the pid for later so that we can waitpid() on it when finished
	myapp->sattach_pid = mypid;
	
	// register this app with the application interface
	if ((rtn = cti_cray_slurm_registerJobStep(jobid, stepid)) == 0)
	{
		// failed to register the jobid/stepid, error is already set.
		_cti_cray_slurm_consumeSrunInv(myapp);
		_cti_gdb_freeMpirPid(pids);
		
		return 0;
	}
	
	// assign the run specific objects to the application obj
	if ((appEntry = _cti_findAppEntry(rtn)) == NULL)
	{
		// this should never happen
		_cti_set_error("_cti_cray_slurm_launchBarrier: impossible null appEntry error!\n");
		_cti_cray_slurm_consumeSrunInv(myapp);
		_cti_gdb_freeMpirPid(pids);
		
		return 0;
	}
	
	// sanity check
	sinfo = (craySlurmInfo_t *)appEntry->_wlmObj;
	if (sinfo == NULL)
	{
		// this should never happen
		_cti_set_error("_cti_cray_slurm_launchBarrier: impossible null sinfo error!\n");
		_cti_cray_slurm_consumeSrunInv(myapp);
		_cti_gdb_freeMpirPid(pids);
		
		return 0;
	}
	
	// set the inv
	sinfo->inv = myapp;
	
	// set the pids
	sinfo->app_pids = pids;
	
	// return the cti_app_id_t
	return rtn;
}

static int
_cti_cray_slurm_releaseBarrier(cti_wlm_obj this)
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
	
	// cleanup the gdb instance, we are done with it. This will release memory
	// and free up the hash table for more possible gdb instances. It is
	// important to do this step here and not later on.
	_cti_gdb_cleanup(my_app->inv->gdb_id);
	my_app->inv->gdb_id = -1;
	
	// wait for the starter to exit
	waitpid(my_app->inv->gdb_pid, NULL, 0);
	my_app->inv->gdb_pid = -1;
	
	// done
	return 0;
}

static int
_cti_cray_slurm_killApp(cti_wlm_obj this, int signum)
{
	craySlurmInfo_t *	my_app = (craySlurmInfo_t *)this;
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
	if (_cti_addArg(my_args, "-s %d", signum))
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
		exit(1);
	}
	
	// parent case
	// cleanup
	_cti_freeArgs(my_args);
	
	// wait until the scancel finishes
	waitpid(mypid, NULL, 0);
	
	return 0;
}

static int
_cti_cray_slurm_verifyBinary(cti_wlm_obj this, const char *fstr)
{
	// all binaries are valid
	return 0;
}

static int
_cti_cray_slurm_verifyLibrary(cti_wlm_obj this, const char *fstr)
{
	// all libraries are valid
	return 0;
}

static int
_cti_cray_slurm_verifyLibDir(cti_wlm_obj this, const char *fstr)
{
	// all library directories are valid
	return 0;
}

static int
_cti_cray_slurm_verifyFile(cti_wlm_obj this, const char *fstr)
{
	// all files are valid
	return 0;
}

static const char * const *
_cti_cray_slurm_extraBinaries(cti_wlm_obj this)
{
	// no extra binaries needed
	return NULL;
}

static const char * const *
_cti_cray_slurm_extraLibraries(cti_wlm_obj this)
{
	// no extra libraries needed
	return NULL;
}

static const char * const *
_cti_cray_slurm_extraLibDirs(cti_wlm_obj this)
{
	// no extra library directories needed
	return NULL;
}

static const char * const *
_cti_cray_slurm_extraFiles(cti_wlm_obj this)
{
	craySlurmInfo_t *		my_app = (craySlurmInfo_t *)this;
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
		if ((cfg_dir = _cti_getCfgDir()) == NULL)
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
		if (strlen(my_app->layout->hosts[i].host) != (sizeof(layout_entry.host) - 1))
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
			pid_entry.pid = my_app->app_pids->pid[i];
			
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
	if ((my_app->extraFiles = calloc(3, sizeof(char *))) == NULL)
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
_cti_cray_slurm_ship_package(cti_wlm_obj this, const char *package)
{
	craySlurmInfo_t *	my_app = (craySlurmInfo_t *)this;
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
	
	if (_cti_addArg(my_args, "-j %d", my_app->jobid))
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
	
	if (asprintf(&str1, CRAY_SLURM_TOOL_DIR, (long long unsigned int)my_app->apid) <= 0)
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
		// exec scancel
		execvp(SBCAST, my_args->argv);
		
		// exec shouldn't return
		fprintf(stderr, "CTI error: Return from exec.\n");
		exit(1);
	}
	
	// parent case
	// cleanup
	_cti_freeArgs(my_args);
	
	// wait until the scancel finishes
	// FIXME: There is no way to error check right now because the sbcast command
	// can only send to an entire job, not individual job steps. The /var/spool/alps/<apid>
	// directory will only exist on nodes associated with this particular job step, and the
	// sbcast command will exit with error if the directory doesn't exist even if the transfer
	// worked on the nodes associated with the step. I opened schedmd BUG 1151 for this issue.
	waitpid(mypid, NULL, 0);
	
	return 0;
}

static int
_cti_cray_slurm_start_daemon(cti_wlm_obj this, cti_args_t * args)
{
	craySlurmInfo_t *	my_app = (craySlurmInfo_t *)this;
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
		
		// Find the location of the daemon launcher program
		if ((launcher = _cti_pathFind(CTI_LAUNCHER, NULL)) == NULL)
		{
			_cti_set_error("Could not locate the launcher application in PATH.");
			close(fd);
			return 1;
		}
		
		if (_cti_cray_slurm_ship_package(this, launcher))
		{
			// error already set
			free(launcher);
			close(fd);
			return 1;
		}
		
		free(launcher);
		
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
	// This corresponds to:
	// srun --jobid=<job_id> --gres=none --mem-per-cpu=0 --nodelist=<host1,host2,...> --share --quiet <tool daemon> <args>
	
	if (_cti_addArg(my_args, "%s", SRUN))
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
	
	if (_cti_addArg(my_args, "--share"))
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
		// dup2 stdin
		if (dup2(fd, STDIN_FILENO) < 0)
		{
			// XXX: How to handle error?
			exit(1);
		}
		
		// dup2 stdout
		if (dup2(fd, STDOUT_FILENO) < 0)
		{
			// XXX: How to handle error?
			exit(1);
		}
		
		// dup2 stderr
		if (dup2(fd, STDERR_FILENO) < 0)
		{
			// XXX: How to handle error?
			exit(1);
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
		
		// Place this process in its own group to prevent signals being passed
		// to it. This is necessary in case the child code execs before the 
		// parent can put us into our own group.
		setpgid(0, 0);
		
		// exec srun
		execvp(SRUN, my_args->argv);
		
		// exec shouldn't return
		exit(1);
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
_cti_cray_slurm_getNumAppPEs(cti_wlm_obj this)
{
	craySlurmInfo_t *	my_app = (craySlurmInfo_t *)this;
	
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
_cti_cray_slurm_getNumAppNodes(cti_wlm_obj this)
{
	craySlurmInfo_t *	my_app = (craySlurmInfo_t *)this;
	
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
_cti_cray_slurm_getAppHostsList(cti_wlm_obj this)
{
	craySlurmInfo_t *	my_app = (craySlurmInfo_t *)this;
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
	if ((hosts = calloc(my_app->layout->numNodes + 1, sizeof(char *))) == NULL)
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
_cti_cray_slurm_getAppHostsPlacement(cti_wlm_obj this)
{
	craySlurmInfo_t *	my_app = (craySlurmInfo_t *)this;
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
	if ((placement_list = malloc(sizeof(cti_hostsList_t))) == NULL)
	{
		// malloc failed
		_cti_set_error("malloc failed.");
		return NULL;
	}
	
	// set the number of hosts for the application
	placement_list->numHosts = my_app->layout->numNodes;
	
	// allocate space for the cti_host_t structs inside the placement_list
	if ((placement_list->hosts = malloc(placement_list->numHosts * sizeof(cti_host_t))) == NULL)
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
		placement_list->hosts[i].numPes = my_app->layout->hosts[i].PEsHere;
	}
	
	// done
	return placement_list;
}

static char *
_cti_cray_slurm_getHostName(void)
{
	FILE *	nid_fd;				// Cray NID file stream
	char	file_buf[BUFSIZ];	// file read buffer
	int		nid = 0;			// nid of this node
	char *	eptr;
	char *	hostname;			// hostname to return
	
	// open up the file containing our node id (nid) - since we are using the
	// Cray variant of native slurm this is required to work.
	if ((nid_fd = fopen(ALPS_XT_NID, "r")) == NULL)
	{
		_cti_set_error("fopen on %s failed.", ALPS_XT_NID);
		return NULL;
	}
	
	// we expect this file to have a numeric value giving our current nid
	if (fgets(file_buf, BUFSIZ, nid_fd) == NULL)
	{
		_cti_set_error("fgets on %s failed.", ALPS_XT_NID);
		fclose(nid_fd);
		return NULL;
	}
	// convert this to an integer value
	nid = (int)strtol(file_buf, &eptr, 10);
	
	// close the file stream
	fclose(nid_fd);
	
	// check for error
	if ((errno == ERANGE && nid == INT_MAX)
			|| (errno != 0 && nid == 0))
	{
		_cti_set_error("strtol failed.");
		return NULL;
	}
	
	// check for invalid input
	if (eptr == file_buf)
	{
		_cti_set_error("Bad data in %s", ALPS_XT_NID);
		return NULL;
	}
	
	if (asprintf(&hostname, ALPS_XT_HOSTNAME_FMT, nid) < 0)
	{
		_cti_set_error("asprintf failed.");
		return NULL;
	}
	
	return hostname;
}

static const char *
_cti_cray_slurm_getToolPath(cti_wlm_obj this)
{
	craySlurmInfo_t *	my_app = (craySlurmInfo_t *)this;
	
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

