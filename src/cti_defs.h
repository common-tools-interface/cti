/******************************************************************************\
 * cti_defs.h - A header file for common defines.
 *
 * NOTE: These defines are used throughout the internal code base and are all
 *       placed inside this file to make modifications due to WLM changes
 *       easier. The environment variables here should match those found in the
 *       public cray_tools_be.h and cray_tools_fe.h headers.
 *
 * © 2013-2014 Cray Inc.	All Rights Reserved.
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

#ifndef _CTI_DEFS_H
#define _CTI_DEFS_H

// WLM identifier. This is system specific. Right now only one WLM at a time
// is supported.
enum cti_wlm_type
{
	CTI_WLM_NONE,	// error/unitialized state
	CTI_WLM_ALPS,
	CTI_WLM_CRAY_SLURM,
	CTI_WLM_SLURM
};
typedef enum cti_wlm_type	cti_wlm_type;

enum cti_be_wlm_type
{
	CTI_BE_WLM_NONE,	// error/unitialized state
	CTI_BE_WLM_ALPS,
	CTI_BE_WLM_CRAY_SLURM,
	CTI_BE_WLM_SLURM
};
typedef enum cti_be_wlm_type	cti_be_wlm_type;

/*******************************************************************************
** Generic defines
*******************************************************************************/
#define CTI_BUF_SIZE				4096

/*******************************************************************************
** Frontend defines relating to the login node 
*******************************************************************************/
#define CTI_OVERWATCH_BINARY		"cti_overwatch"									// name of the overwatch binary
#define DEFAULT_SIG				9													// default signal value to use
#define WLM_DETECT_LIB_NAME		"libwlm_detect.so"								// wlm_detect library

/*******************************************************************************
** MPIR_iface specific information
*******************************************************************************/
#define CTI_GDB_BINARY			"cti_approved_gdb"								// name of gdb binary
#define GDB_MPIR_STARTER			"cti_starter"										// name of starter binary

/*******************************************************************************
** Backend defines relating to the compute node
*******************************************************************************/
#define CTI_LAUNCHER							"cti_dlaunch"							// name of the tool daemon launcher binary
// The following needs the 'X' for random char replacement.
#define DEFAULT_STAGE_DIR					"cti_daemonXXXXXX"					// default directory name for the fake root of the tool daemon
#define SHELL_ENV_VAR							"SHELL"									// The environment variable to set shell info
#define SHELL_PATH							"/bin/sh"								// The location of the shell to set SHELL to
#define PMI_ATTRIBS_FILE_NAME				"pmi_attribs"							// Name of the pmi_attribs file to find pid info
#define PMI_ATTRIBS_DEFAULT_FOPEN_TIMEOUT	60										// default timeout in seconds for trying to open pmi_attribs file

/*******************************************************************************
** Alps specific information
*******************************************************************************/
#define APRUN						"aprun"												// name of the ALPS job launcher binary
#define OLD_APRUN_LOCATION		"/usr/bin/aprun"									// default legacy location of the aprun binary
#define OBS_APRUN_LOCATION		"/opt/cray/alps/default/bin/aprun"				// default new location of the aprun binary
#define APKILL						"apkill"											// name of the ALPS job kill binary
#define ALPS_XT_NID				"/proc/cray_xt/nid"								// file where nid info is located
#define ALPS_XT_HOSTNAME_FMT		"nid%05d"											// hostname format string
#define ALPS_OBS_LOC				"/opt/cray/alps"									// used to test if the system is using the OBS format
#define ALPS_FE_LIB_NAME			"libalps.so"										// name of the alps library used on the frontend
#define ALPS_BE_LIB_NAME			"libalpsutil.so"									// name of the alps library used on the backend
#define OLD_TOOLHELPER_DIR		"/var/spool/alps/%llu/toolhelper%llu"			// old alps toolhelper path format on compute node
#define OBS_TOOLHELPER_DIR		"/var/opt/cray/alps/spool/%llu/toolhelper%llu"// new alps toolhelper path format on compute node
#define OLD_ATTRIBS_DIR			"/var/spool/alps/%llu"							// old pmi_attribs path format on compute node
#define OBS_ATTRIBS_DIR			"/var/opt/cray/alps/spool/%llu"					// new pmi_attribs path format on compute node

/*******************************************************************************
** Cray SLURM specific information
*******************************************************************************/

// Used when reading/writing layout file - used on FE and BE
// File will begin with the following header
typedef struct
{
	int	numNodes;
}	slurmLayoutFileHeader_t;
// Followed by numNodes of the following:
typedef struct
{
	char	host[9];	// hostname of this node in nidXXXXX\0 format - 9 chars total
	int		PEsHere;	// Number of PEs placed on this node
	int		firstPE;	// first PE on this node
}	slurmLayoutFile_t;

// Used when reading/writing pid file - used on FE and BE
// File will begin with the following header
typedef struct
{
	int	numPids;
}	slurmPidFileHeader_t;
// Followed by numPids of the following:
typedef struct
{
	pid_t	pid;		// pid_t of this PE
}	slurmPidFile_t;

#define SRUN						"srun"											// name of slurm job launcher binary
#define SATTACH					"sattach"										// name of slurm io redirect binary
#define SCANCEL					"scancel"										// name of slurm job signal binary
#define SBCAST						"sbcast"										// name of slurm transfer binary
#define SLURM_STEP_UTIL			"cti_slurm_util"								// name of cti slurm job step info utility
#define CRAY_SLURM_APID(jobid, stepid)	((stepid * 10000000000) + jobid)	// formula for creating Cray apid from SLURM jobid.stepid
#define CRAY_SLURM_TOOL_DIR		"/tmp"											// Cray SLURM staging path on compute node
#define	 CRAY_SLURM_CRAY_DIR		"/var/opt/cray/alps/spool/%llu"				// Location of cray specific directory on compute node - pmi_attribs is here
#define SLURM_STAGE_DIR			"slurmXXXXXX"									// directory name for staging slurm specific files to transfer
#define SLURM_LAYOUT_FILE		"slurm_layout"									// name of file containing layout information
#define SLURM_PID_FILE			"slurm_pid"									// name of file containing pid information

/*******************************************************************************
** Environment variables that are set/maintained by this library
** Note that (read) is used to denote environment variables that the user will
** define and the library reads in, and (set) are environment variables that
** are set in the users environment by the library.
*******************************************************************************/
#define USER_DEF_APRUN_LOC_ENV_VAR		"CRAY_APRUN_PATH"					// Frontend: Used to override the default location of the aprun binary (read)
#define GDB_LOC_ENV_VAR					"CRAY_CTI_GDB_PATH"				// Frontend: Used to override the default location of gdb for the MPIR_iface (read)
#define CFG_DIR_VAR						"CRAY_CTI_CFG_DIR"				// Frontend: Used to define a writable location to create the manifest tarball (read)
#define DAEMON_STAGE_VAR    				"CRAY_CTI_STAGE_DIR"				// Frontend: Used to define a directory name for the fake root of the tool daemon (read)
#define DBG_LOG_ENV_VAR 					"CRAY_DBG_LOG_DIR"				// Frontend: Used to define a directory to write debug logs to (read)

#define APID_ENV_VAR						"CRAYTOOL_APID"					// Backend: Used to hold the string representation of the apid (set)
#define WLM_ENV_VAR						"CRAYTOOL_WLM"						// Backend: Used to hold the enum representation of the wlm (set)
#define SCRATCH_ENV_VAR					"TMPDIR"							// Backend: Used to denote temporary storage space (set)
#define OLD_SCRATCH_ENV_VAR				"CRAYTOOL_OLD_TMPDIR"				// Backend: Used to denote the old setting of TMPDIR (set)
#define OLD_CWD_ENV_VAR					"CRAYTOOL_OLD_CWD"				// Backend: Used to denote the old setting of CWD (set)
#define TOOL_DIR_VAR						"CRAYTOOL_TOP_LEVEL"				// Backend: KEEP HIDDEN! Used to point at the top level toolhelper dir (set)
#define ROOT_DIR_VAR						"CRAYTOOL_ROOT_DIR"				// Backend: Used to denote the fake root of the tool daemon (set)
#define BIN_DIR_VAR						"CRAYTOOL_BIN_DIR"				// Backend: Used to denote where binaries are located (set)
#define LIB_DIR_VAR						"CRAYTOOL_LIB_DIR"				// Backend: Used to denote where libraries are located (set)
#define FILE_DIR_VAR						"CRAYTOOL_FILE_DIR"				// Backend: Used to denote where files are located (set)
#define PMI_ATTRIBS_DIR_VAR				"CRAYTOOL_PMI_ATTRIBS_DIR"		// Backend: Used to denote where the pmi_attribs file is located (set)
#define PMI_ATTRIBS_TIMEOUT_ENV_VAR		"CRAY_CTI_PMI_FOPEN_TIMEOUT"		// Backend: Used to define a sleep timeout period for creation of pmi_attribs file (read)
#define PMI_EXTRA_SLEEP_ENV_VAR			"CRAY_CTI_PMI_EXTRA_SLEEP"		// Backend: Used to sleep a fixed period of time after the pmi_attribs file has been opened (read)

#endif /* _CTI_DEFS_H */
