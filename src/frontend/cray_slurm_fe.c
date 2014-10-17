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

#include "cti_fe.h"
#include "cti_defs.h"
#include "cti_error.h"
#include "useful.h"

#include "gdb_MPIR_iface.h"

/* Types used here */

typedef struct
{
	cti_gdb_id_t	gdb_id;
	pid_t			gdb_pid;		// pid of the gdb process for the mpir starter
	pid_t			sattach_pid;	// Optional pid of the sattach process if we are redirecting io
} srunInv_t;

typedef struct
{
	char *			host;			// hostname of this node
	int				PEsHere;		// Number of PEs running on this node
	int				firstPE;		// First PE number on this node
} slurmNodeLayout_t;

typedef struct
{
	int					numPEs;		// Number of PEs associated with the job step
	int					numNodes;	// Number of nodes associated with the job step
	slurmNodeLayout_t *	hosts;	// Array of hosts of length numNodes
} slurmStepLayout_t;

typedef struct
{
	uint32_t			jobid;		// SLURM job id
	uint32_t			stepid;		// SLURM step id
	uint64_t			apid;		// Cray variant of step+job id
	slurmStepLayout_t *	layout;		// Layout of job step
	srunInv_t *			inv;		// Optional object used for launched applications.
} craySlurmInfo_t;

/* Static prototypes */
static int					_cti_cray_slurm_init(void);
static void					_cti_cray_slurm_fini(void);
static craySlurmInfo_t *	_cti_cray_slurm_newSlurmInfo(void);
static void 				_cti_cray_slurm_consumeSlurmInfo(void *);
static srunInv_t *			_cti_cray_slurm_newSrunInv(void);
static void					_cti_cray_slurm_consumeSrunInv(srunInv_t *);
static slurmStepLayout_t *	_cti_cray_slurm_newSlurmLayout(int, int);
static void					_cti_cray_slurm_consumeSlurmLayout(slurmStepLayout_t *);
static slurmStepLayout_t *	_cti_cray_slurm_getLayout(uint32_t, uint32_t);
static int					_cti_cray_slurm_cmpJobId(void *, void *);
static char *				_cti_cray_slurm_getJobId(void *);
static cti_app_id_t			_cti_cray_slurm_launchBarrier(char * const [], int, int, int, int, const char *, const char *, char * const []);
static int					_cti_cray_slurm_releaseBarrier(void *);
static int					_cti_cray_slurm_killApp(void *, int);
static int					_cti_cray_slurm_verifyBinary(const char *);
static int					_cti_cray_slurm_verifyLibrary(const char *);
static int					_cti_cray_slurm_verifyLibDir(const char *);
static int					_cti_cray_slurm_verifyFile(const char *);
static const char * const *	_cti_cray_slurm_extraBinaries(void);
static const char * const *	_cti_cray_slurm_extraLibraries(void);
static const char * const *	_cti_cray_slurm_extraLibDirs(void);
static const char * const *	_cti_cray_slurm_extraFiles(void);
static int					_cti_cray_slurm_ship_package(void *, const char *);
static int					_cti_cray_slurm_start_daemon(void *, int, const char *, const char *);
static int					_cti_cray_slurm_getNumAppPEs(void *);
static int					_cti_cray_slurm_getNumAppNodes(void *);
static char **				_cti_cray_slurm_getAppHostsList(void *);
static cti_hostsList_t *	_cti_cray_slurm_getAppHostsPlacement(void *);

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
	NULL, //_cti_cray_slurm_getHostName,			// wlm_getHostName
	_cti_wlm_getLauncherHostName_none		// wlm_getLauncherHostName - Not supported by slurm
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
	this->jobid		= 0;
	this->stepid	= 0;
	this->apid		= 0;
	this->layout	= NULL;
	this->inv		= NULL;
	
	return this;
}

static void 
_cti_cray_slurm_consumeSlurmInfo(void *this)
{
	craySlurmInfo_t *	sinfo = (craySlurmInfo_t *)this;

	// sanity
	if (sinfo == NULL)
		return;

	_cti_cray_slurm_consumeSlurmLayout(sinfo->layout);
	_cti_cray_slurm_consumeSrunInv(sinfo->inv);
	
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
	char *				my_argv[4];
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
	
	// create the args for sbcast
	if ((my_argv[0] = _cti_pathFind(SLURM_STEP_UTIL, NULL)) == NULL)
	{
		_cti_set_error("Could not locate CTI slurm job step utility in PATH.");
		return NULL;
	}
	if (asprintf(&my_argv[1], "-j %d", jobid) <= 0)
	{
		_cti_set_error("asprintf failed.");
		return NULL;
	}
	if (asprintf(&my_argv[2], "-s %d", stepid) <= 0)
	{
		_cti_set_error("asprintf failed.");
		return NULL;
	}
	// set null terminator
	my_argv[3] = NULL;
	
	// make the pipes for the command
	if (pipe(pipe_r) < 0)
	{
		_cti_set_error("Pipe creation failure.");
		free(my_argv[0]);
		free(my_argv[1]);
		free(my_argv[2]);
		
		return NULL;
	}
	if (pipe(pipe_e) < 0)
	{
		_cti_set_error("Pipe creation failure.");
		free(my_argv[0]);
		free(my_argv[1]);
		free(my_argv[2]);
		
		return NULL;
	}
	
	// fork off a process for the slurm utility
	mypid = fork();
	
	// error case
	if (mypid < 0)
	{
		_cti_set_error("Fatal fork error.");
		// cleanup my_argv array
		free(my_argv[0]);
		free(my_argv[1]);
		free(my_argv[2]);
		
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
		execvp(my_argv[0], my_argv);
		
		// exec shouldn't return
		fprintf(stderr, "CTI error: Return from exec.\n");
		exit(1);
	}
	
	// parent case
	
	// close unused ends of pipe
	close(pipe_r[1]);
	close(pipe_e[1]);
	
	// cleanup my_argv array
	free(my_argv[0]);
	free(my_argv[1]);
	free(my_argv[2]);
	
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
		
		// create the toolPath
		if (asprintf(&toolPath, CRAY_SLURM_TOOL_DIR, (long long unsigned int)apid) <= 0)
		{
			_cti_set_error("asprintf failed");
			_cti_cray_slurm_consumeSlurmInfo(sinfo);
			return 0;
		}
		
		// retrieve detailed information about our app
		if ((sinfo->layout = _cti_cray_slurm_getLayout(jobid, stepid)) == NULL)
		{
			// error already set
			_cti_cray_slurm_consumeSlurmInfo(sinfo);
			return 0;
		}
		
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
		// apid was already registered. This is a failure.
		_cti_set_error("apid already registered");
		return 0;
	}

	return appId;
}

static cti_app_id_t
_cti_cray_slurm_launchBarrier(	char * const launcher_argv[], int redirectOutput, int redirectInput, 
							int stdout_fd, int stderr_fd, const char *inputFile, const char *chdirPath,
							char * const env_list[]	)
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
	
	// get the stepid string for slurm
	if ((sym_str = _cti_gdb_getSymbolVal(myapp->gdb_id, "totalview_stepid")) == NULL)
	{
		// error already set
		_cti_cray_slurm_consumeSrunInv(myapp);
		
		return 0;
	}
	
	// convert the string into the actual stepid
	errno = 0;
	stepid = (uint32_t)strtoul(sym_str, &end_p, 10);
	
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
	
	// if redirectOutput is true, then we need to fork off an sattach process.
	// For SLURM, sattach makes the iostreams of srun available. This process
	// will exit when the srun process exits. 
	
	if (redirectOutput)
	{
		// fork off a process to start the sattach command
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
			int 	fd;
			char *	args[4];
		
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
			
			// create the args
			args[0] = SATTACH;
			args[1] = "-Q";
			if (asprintf(&args[2], "%u.%u", jobid, stepid)  <= 0)
			{
				fprintf(stderr, "CTI error: asprintf failed.\n");
				exit(1);
			}
			args[3] = NULL;
			
			// exec sattach
			execvp(SATTACH, args);
		
			// exec shouldn't return
			fprintf(stderr, "CTI error: Return from exec.\n");
			perror("execvp");
			exit(1);
		}
		
		// parent case
		
		// save the pid for later so that we can waitpid() on it when finished
		myapp->sattach_pid = mypid;
	}
	
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
_cti_cray_slurm_killApp(void *this, int signum)
{
	craySlurmInfo_t *	my_app = (craySlurmInfo_t *)this;
	char * 				my_argv[5];
	int					mypid;
	
	// sanity check
	if (my_app == NULL)
	{
		_cti_set_error("srun kill operation failed.");
		return 1;
	}
	
	// create the string to pass to exec
	
	// first argument should be "scancel"
	my_argv[0] = SCANCEL;
	
	// second argument is quiet
	my_argv[1] = "-Q";
	
	// third argument is signal number
	if (asprintf(&my_argv[2], "-s %d", signum) <= 0)
	{
		_cti_set_error("asprintf failed.");
		return 1;
	}
	
	// fourth argument is the jobid.stepid
	if (asprintf(&my_argv[3], "%u.%u", my_app->jobid, my_app->stepid)  <= 0)
	{
		_cti_set_error("asprintf failed.");
		free(my_argv[2]);
		return 1;
	}

	// set the final null terminator
	my_argv[4] = NULL;
	
	// fork off a process to launch scancel
	mypid = fork();
	
	// error case
	if (mypid < 0)
	{
		_cti_set_error("Fatal fork error.");
		// cleanup my_argv array
		free(my_argv[2]);
		free(my_argv[3]);
		
		return 1;
	}
	
	// child case
	if (mypid == 0)
	{
		// exec scancel
		execvp(SCANCEL, my_argv);
		
		// exec shouldn't return
		fprintf(stderr, "CTI error: Return from exec.\n");
		exit(1);
	}
	
	// parent case
	// cleanup my_argv array
	free(my_argv[2]);
	free(my_argv[3]);
	
	// wait until the scancel finishes
	waitpid(mypid, NULL, 0);
	
	return 0;
}

static int
_cti_cray_slurm_verifyBinary(const char *fstr)
{
	// all binaries are valid
	return 0;
}

static int
_cti_cray_slurm_verifyLibrary(const char *fstr)
{
	// all libraries are valid
	return 0;
}

static int
_cti_cray_slurm_verifyLibDir(const char *fstr)
{
	// all library directories are valid
	return 0;
}

static int
_cti_cray_slurm_verifyFile(const char *fstr)
{
	// all files are valid
	return 0;
}

static const char * const *
_cti_cray_slurm_extraBinaries(void)
{
	// no extra binaries needed
	return NULL;
}

static const char * const *
_cti_cray_slurm_extraLibraries(void)
{
	// no extra libraries needed
	return NULL;
}

static const char * const *
_cti_cray_slurm_extraLibDirs(void)
{
	// no extra library directories needed
	return NULL;
}

static const char * const *
_cti_cray_slurm_extraFiles(void)
{
	// no extra files needed
	return NULL;
}

static int
_cti_cray_slurm_ship_package(void *this, const char *package)
{
	craySlurmInfo_t *	my_app = (craySlurmInfo_t *)this;
	char *				my_argv[6];
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
	if (package == NULL)
	{
		_cti_set_error("package string is null!");
		return 1;
	}
	
	// create the args for sbcast
	my_argv[0] = SBCAST;
	my_argv[1] = "-C";
	if (asprintf(&my_argv[2], "-j %d", my_app->jobid) <= 0)
	{
		_cti_set_error("asprintf failed.");
		return 1;
	}
	my_argv[3] = (char *)package;
	if (asprintf(&str1, CRAY_SLURM_TOOL_DIR, (long long unsigned int)my_app->apid) <= 0)
	{
		_cti_set_error("asprintf failed");
		free(my_argv[2]);
		return 1;
	}
	if ((str2 = _cti_pathToName(package)) == NULL)
	{
		_cti_set_error("_cti_pathToName failed");
		free(my_argv[2]);
		free(str1);
		return 1;
	}
	if (asprintf(&my_argv[4], "%s/%s", str1, str2) <= 0)
	{
		_cti_set_error("asprintf failed");
		free(my_argv[2]);
		free(str1);
		free(str2);
		return 1;
	}
	free(str1);
	free(str2);
	my_argv[5] = NULL;
	
	// now ship the tarball to the compute nodes
	// fork off a process to launch sbcast
	mypid = fork();
	
	// error case
	if (mypid < 0)
	{
		_cti_set_error("Fatal fork error.");
		// cleanup my_argv array
		free(my_argv[2]);
		free(my_argv[4]);
		
		return 1;
	}
	
	// child case
	if (mypid == 0)
	{
		// exec scancel
		execvp(SBCAST, my_argv);
		
		// exec shouldn't return
		fprintf(stderr, "CTI error: Return from exec.\n");
		exit(1);
	}
	
	// parent case
	// cleanup my_argv array
	free(my_argv[2]);
	free(my_argv[4]);
	
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
_cti_cray_slurm_start_daemon(void *this, int transfer, const char *tool_path, const char *args)
{
	craySlurmInfo_t *	my_app = (craySlurmInfo_t *)this;
	char *				launcher;
	char *				my_argv[10];
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
	if (args == NULL)
	{
		_cti_set_error("args string is null!");
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
	if (transfer)
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
	}
	
	// use existing launcher binary on compute node
	if (asprintf(&launcher, "%s/%s", tool_path, CTI_LAUNCHER) <= 0)
	{
		_cti_set_error("asprintf failed.");
		close(fd);
		return 1;
	}
	
	// TODO: Launch only on nodes associated with step id
	//srun --jobid=<job_id> --gres=none --mem-per-cpu=0 --nodelist=<host1,host2,...> --share --quiet <tool daemon> <args>
	my_argv[0] = SRUN;
	if (asprintf(&my_argv[1], "--jobid=%d", my_app->jobid) <= 0)
	{
		_cti_set_error("asprintf failed.");
		close(fd);
		free(launcher);
		return 1;
	}
	my_argv[2] = "--gres=none";
	my_argv[3] = "--mem-per-cpu=0";
	// get the first host entry
	tmp = strdup(my_app->layout->hosts[0].host);
	for (i=1; i < my_app->layout->numNodes; ++i)
	{
		if (asprintf(&hostlist, "%s,%s", tmp, my_app->layout->hosts[i].host) <= 0)
		{
			_cti_set_error("asprintf failed.");
			close(fd);
			free(launcher);
			free(my_argv[1]);
			free(tmp);
			return 1;
		}
		free(tmp);
		tmp = hostlist;
	}
	my_argv[4] = hostlist;
	my_argv[5] = "--share";
	my_argv[6] = "--quiet";
	my_argv[7] = launcher;
	my_argv[8] = (char *)args;	// yes I know this is bad, but look at what it is doing
	my_argv[9] = NULL;
	
	// fork off a process to launch srun
	mypid = fork();
	
	// error case
	if (mypid < 0)
	{
		_cti_set_error("Fatal fork error.");
		close(fd);
		free(my_argv[1]);
		free(my_argv[4]);
		free(my_argv[7]);
		
		return 1;
	}
	
	// child case
	if (mypid == 0)
	{
		// clear file creation mask
		umask(0);
		
		// change directory to root
		chdir("/");
		
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
		execvp(SRUN, my_argv);
		
		// exec shouldn't return
		exit(1);
	}
	
	// Place the child in its own group.
	setpgid(mypid, mypid);
	
	// cleanup
	close(fd);
	free(my_argv[1]);
	free(my_argv[4]);
	free(my_argv[7]);
	
	// done
	return 0;
}

static int
_cti_cray_slurm_getNumAppPEs(void *this)
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
_cti_cray_slurm_getNumAppNodes(void *this)
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
_cti_cray_slurm_getAppHostsList(void *this)
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
	if ((hosts = calloc(my_app->layout->numNodes + 1, sizeof(char *))) == (void *)0)
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
_cti_cray_slurm_getAppHostsPlacement(void *this)
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
	if ((placement_list = malloc(sizeof(cti_hostsList_t))) == (void *)0)
	{
		// malloc failed
		_cti_set_error("malloc failed.");
		return NULL;
	}
	
	// set the number of hosts for the application
	placement_list->numHosts = my_app->layout->numNodes;
	
	// allocate space for the cti_host_t structs inside the placement_list
	if ((placement_list->hosts = malloc(placement_list->numHosts * sizeof(cti_host_t))) == (void *)0)
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

