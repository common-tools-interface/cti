/*********************************************************************************\
 * slurm_be.c - Native slurm specific backend library functions.
 *
 * Copyright 2016-2017 Cray Inc.  All Rights Reserved.
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
 *********************************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>

#include <limits.h>

#include "cti_be.h"
#include "pmi_attribs_parser.h"

// types used here

typedef struct
{
	int		PEsHere;	// Number of PEs placed on this node
	int		firstPE;	// first PE on this node
} slurmLayout_t;

/* static prototypes */
static int 					_cti_be_slurm_init(void);
static void					_cti_be_slurm_fini(void);
static int					_cti_be_slurm_getSlurmLayout(void);
static int					_cti_be_slurm_getSlurmPids(void);
static cti_pidList_t *		_cti_be_slurm_findAppPids(void);
static char *				_cti_be_slurm_getNodeHostname(void);
static int					_cti_be_slurm_getNodeFirstPE(void);
static int					_cti_be_slurm_getNodePEs(void);

/* cray slurm wlm proto object */
cti_be_wlm_proto_t			_cti_be_slurm_wlmProto =
{
	CTI_BE_WLM_SLURM,				// wlm_type
	_cti_be_slurm_init,			// wlm_init
	_cti_be_slurm_fini,			// wlm_fini
	_cti_be_slurm_findAppPids,		// wlm_findAppPids
	_cti_be_slurm_getNodeHostname,	// wlm_getNodeHostname
	_cti_be_slurm_getNodeFirstPE,	// wlm_getNodeFirstPE
	_cti_be_slurm_getNodePEs		// wlm_getNodePEs
};

// Global vars
static pmi_attribs_t *		_cti_attrs 			= NULL;	// node pmi_attribs information
static slurmLayout_t *		_cti_layout			= NULL;	// compute node layout for slurm app
static pid_t *				_cti_slurm_pids		= NULL;	// array of pids here if pmi_attribs is not available

/* Constructor/Destructor functions */

static int
_cti_be_slurm_init(void)
{
	return 0;
}

static void
_cti_be_slurm_fini(void)
{
	// cleanup
	if (_cti_attrs != NULL)
	{
		_cti_be_freePmiAttribs(_cti_attrs);
		_cti_attrs = NULL;
	}
	
	if (_cti_layout != NULL)
	{
		free(_cti_layout);
		_cti_layout = NULL;
	}
	
	if (_cti_slurm_pids != NULL)
	{
		free(_cti_slurm_pids);
		_cti_slurm_pids = NULL;
	}
	
	return;
}

/* Static functions */

static int
_cti_be_slurm_getSlurmLayout(void)
{
	slurmLayout_t *			my_layout;
	char *					file_dir;
	char *					layoutPath;
	FILE *					my_file;
	slurmLayoutFileHeader_t	layout_hdr;
	slurmLayoutFile_t *		layout;
	int						i, offset;
	
	// sanity
	if (_cti_layout != NULL)
		return 0;
		
	char* hostname = _cti_be_slurm_getNodeHostname();
	
	// allocate the slurmLayout_t object
	if ((my_layout = malloc(sizeof(slurmLayout_t))) == NULL)
	{
		fprintf(stderr, "malloc failed.\n");
		return 1;
	}
	
	// get the file directory were we can find the layout file
	if ((file_dir = cti_be_getFileDir()) == NULL)
	{
		fprintf(stderr, "_cti_be_slurm_getSlurmLayout failed.\n");
		free(my_layout);
		return 1;
	}
	
	// create the path to the layout file
	if (asprintf(&layoutPath, "%s/%s", file_dir, SLURM_LAYOUT_FILE) <= 0)
	{
		fprintf(stderr, "asprintf failed.\n");
		free(my_layout);
		free(file_dir);
		return 1;
	}
	// cleanup
	free(file_dir);
	
	// open the layout file for reading
	if ((my_file = fopen(layoutPath, "rb")) == NULL)
	{
		fprintf(stderr, "Could not open %s for reading\n", layoutPath);
		free(my_layout);
		free(layoutPath);
		return 1;
	}
	
	// read the header from the file
	if (fread(&layout_hdr, sizeof(slurmLayoutFileHeader_t), 1, my_file) != 1)
	{
		fprintf(stderr, "Could not read %s\n", layoutPath);
		free(my_layout);
		free(layoutPath);
		fclose(my_file);
		return 1;
	}
	
	// allocate the layout array based on the header
	if ((layout = calloc(layout_hdr.numNodes, sizeof(slurmLayoutFile_t))) == NULL)
	{
		fprintf(stderr, "calloc failed.\n");
		free(my_layout);
		free(layoutPath);
		fclose(my_file);
		return 1;
	}
	
	// read the layout info
	if (fread(layout, sizeof(slurmLayoutFile_t), layout_hdr.numNodes, my_file) != layout_hdr.numNodes)
	{
		fprintf(stderr, "Bad data in %s\n", layoutPath);
		free(my_layout);
		free(layoutPath);
		fclose(my_file);
		free(layout);
		return 1;
	}
	
	// done reading the file
	free(layoutPath);
	fclose(my_file);
	
	// find the entry for this nid, we need to offset into the host name based on
	// this nid
	offset = strlen(layout[0].host) - strlen(hostname);
	
	for (i=0; i < layout_hdr.numNodes; ++i)
	{
		// check if this entry corresponds to our nid
		if (strncmp(layout[i].host + offset, hostname, strlen(hostname)) == 0)
		{
			// found it
			my_layout->PEsHere = layout[i].PEsHere;
			my_layout->firstPE = layout[i].firstPE;
			
			// cleanup
			free(layout);
			
			// set global value
			_cti_layout = my_layout;
			
			// done
			return 0;
		}
	}
	
	// if we get here, we didn't find the host in the layout list!
	fprintf(stderr, "Could not find layout entry for hostname %s\n", hostname);
	free(my_layout);
	free(layout);
	return 1;
}

static int
_cti_be_slurm_getSlurmPids(void)
{
	pid_t *					my_pids;
	char *					file_dir;
	char *					pidPath;
	FILE *					my_file;
	slurmPidFileHeader_t	pid_hdr;
	slurmPidFile_t *		pids;
	int						i;
	
	// sanity
	if (_cti_slurm_pids != NULL)
		return 0;
		
	// make sure we have the layout
	if (_cti_layout == NULL)
	{
		// get the layout
		if (_cti_be_slurm_getSlurmLayout())
		{
			return 1;
		}
	}
	
	// get the file directory were we can find the pid file
	if ((file_dir = cti_be_getFileDir()) == NULL)
	{
		fprintf(stderr, "_cti_be_slurm_getSlurmPids failed.\n");
		return 1;
	}
	
	// create the path to the pid file
	if (asprintf(&pidPath, "%s/%s", file_dir, SLURM_PID_FILE) <= 0)
	{
		fprintf(stderr, "asprintf failed.\n");
		free(file_dir);
		return 1;
	}
	// cleanup
	free(file_dir);
	
	// open the pid file for reading
	if ((my_file = fopen(pidPath, "rb")) == NULL)
	{
		fprintf(stderr, "Could not open %s for reading\n", pidPath);
		free(pidPath);
		return 1;
	}
	
	// read the header from the file
	if (fread(&pid_hdr, sizeof(slurmPidFileHeader_t), 1, my_file) != 1)
	{
		fprintf(stderr, "Could not read %s\n", pidPath);
		free(pidPath);
		fclose(my_file);
		return 1;
	}
	
	// ensure the file data is in bounds
	if ((_cti_layout->firstPE + _cti_layout->PEsHere) > pid_hdr.numPids)
	{
		// data out of bounds
		fprintf(stderr, "Data out of bounds in %s\n", pidPath);
		free(pidPath);
		fclose(my_file);
		return 1;
	}
	
	// allocate the pids array based on the number of PEsHere
	if ((pids = calloc(_cti_layout->PEsHere, sizeof(slurmPidFile_t))) == NULL)
	{
		fprintf(stderr, "calloc failed.\n");
		free(pidPath);
		fclose(my_file);
		return 1;
	}
	
	// fseek to the start of the pid info for this compute node
	if (fseek(my_file, _cti_layout->firstPE * sizeof(slurmPidFile_t), SEEK_CUR))
	{
		fprintf(stderr, "fseek failed.\n");
		free(pidPath);
		fclose(my_file);
		free(pids);
		return 1;
	}
	
	// read the pid info
	if (fread(pids, sizeof(slurmPidFile_t), _cti_layout->PEsHere, my_file) != _cti_layout->PEsHere)
	{
		fprintf(stderr, "Bad data in %s\n", pidPath);
		free(pidPath);
		fclose(my_file);
		free(pids);
		return 1;
	}
	
	// done reading the file
	free(pidPath);
	fclose(my_file);
	
	// allocate an array of pids
	if ((my_pids = calloc(_cti_layout->PEsHere, sizeof(pid_t))) == NULL)
	{
		fprintf(stderr, "calloc failed.\n");
		free(pids);
		return 1;
	}
	
	// set the pids
	for (i=0; i < _cti_layout->PEsHere; ++i)
	{
		my_pids[i] = pids[i].pid;
	}
	
	// set global value
	_cti_slurm_pids = my_pids;
	
	// cleanup
	free(pids);
	
	return 0;
}

/* API related calls start here */

static cti_pidList_t *
_cti_be_slurm_findAppPids(void)
{
	char *			file_dir;
	char *			file_path;
	struct stat 	statbuf;
	cti_pidList_t * rtn;
	int				i;
	
	// get the file directory were we can find the pid file
	if ((file_dir = cti_be_getFileDir()) == NULL)
	{
		fprintf(stderr, "cti_be_getFileDir failed.\n");
		return NULL;
	}

	// create the path to the pid file
	if (asprintf(&file_path, "%s/%s", file_dir, SLURM_PID_FILE) <= 0)
	{
		fprintf(stderr, "asprintf failed.\n");
		free(file_dir);
		return NULL;
	}

	if (stat(file_path, &statbuf) == 0)
	{
		// pid file exists
		free(file_dir);
		free(file_path);
		
		if (_cti_slurm_pids == NULL)
		{
			// get the pids
			if (_cti_be_slurm_getSlurmPids())
			{
				return NULL;
			}
		}
		
		// allocate the return object
		if ((rtn = malloc(sizeof(cti_pidList_t))) == (void *)0)
		{
			fprintf(stderr, "malloc failed.\n");
			return NULL;
		}
		
		rtn->numPids = _cti_layout->PEsHere;
		
		// allocate the cti_rankPidPair_t array
		if ((rtn->pids = malloc(rtn->numPids * sizeof(cti_rankPidPair_t))) == (void *)0)
		{
			fprintf(stderr, "malloc failed.\n");
			free(rtn);
			return NULL;
		}
		
		// set the rtn rank/pid array
		for (i=0; i < rtn->numPids; ++i)
		{
			rtn->pids[i].pid = _cti_slurm_pids[i];
			rtn->pids[i].rank = i + _cti_layout->firstPE;
		}
		
	} else
	{
	
use_pmi_attribs:

		// slurm_pid not found, so use the pmi_attribs file
		
		// Call _cti_be_getPmiAttribsInfo - We require the pmi_attribs file to exist
		// in order to function properly.
		if (_cti_attrs == NULL)
		{
			if ((_cti_attrs = _cti_be_getPmiAttribsInfo()) == NULL)
			{
				// Something messed up, so fail.
				fprintf(stderr, "_cti_be_slurm_findAppPids failed (_cti_be_getPmiAttribsInfo NULL).\n");
				return NULL;
			}
		}
	
		// ensure the _cti_attrs object has a app_rankPidPairs array
		if (_cti_attrs->app_rankPidPairs == NULL)
		{
			// Something messed up, so fail.
			fprintf(stderr, "_cti_be_slurm_findAppPids failed (app_rankPidPairs NULL).\n");
			return NULL;
		}
		
		// allocate the return object
		if ((rtn = malloc(sizeof(cti_pidList_t))) == (void *)0)
		{
			fprintf(stderr, "malloc failed.\n");
			return NULL;
		}
		
		rtn->numPids = _cti_attrs->app_nodeNumRanks;
		
		// allocate the cti_rankPidPair_t array
		if ((rtn->pids = malloc(rtn->numPids * sizeof(cti_rankPidPair_t))) == (void *)0)
		{
			fprintf(stderr, "malloc failed.\n");
			free(rtn);
			return NULL;
		}
		
		// set the _cti_attrs rank/pid array to the rtn rank/pid array
		for (i=0; i < rtn->numPids; ++i)
		{
			rtn->pids[i].pid = _cti_attrs->app_rankPidPairs[i].pid;
			rtn->pids[i].rank = _cti_attrs->app_rankPidPairs[i].rank;
		}
	}
	
	return rtn;
}

static char *
_cti_be_slurm_getNodeHostname()
{
	char * hostname = malloc(HOST_NAME_MAX);
	
	//Malloc failure
	if(hostname == NULL){
		fprintf(stderr, "_cti_be_slurm_getNodeHostname: Could not allocate %d bytes for hostname\n", HOST_NAME_MAX);
		return NULL;
	}
	
	if(gethostname(hostname, HOST_NAME_MAX) < 0){
		fprintf(stderr, "%s", "_cti_be_slurm_getNodeHostname: gethostname() failed!");
		return NULL;
	}
	
	return hostname;
}


static int
_cti_be_slurm_getNodeFirstPE()
{
	// make sure we have the layout
	if (_cti_layout == NULL)
	{
		// get the layout
		if (_cti_be_slurm_getSlurmLayout())
		{
			return -1;
		}
	}
	
	return _cti_layout->firstPE;
}

static int
_cti_be_slurm_getNodePEs()
{
	// make sure we have the layout
	if (_cti_layout == NULL)
	{
		// get the layout
		if (_cti_be_slurm_getSlurmLayout())
		{
			return -1;
		}
	}
	
	return _cti_layout->PEsHere;
}

