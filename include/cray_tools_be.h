/******************************************************************************\
 * cray_tools_be.h - The public API definitions for the backend portion of
 *                   the Cray tools interface. This interface should be used
 *                   only on Cray compute nodes. It will not function on eslogin
 *                   or login nodes. Backend refers to the location where
 *                   applications are run.
 *
 * © 2011-2014 Cray Inc.  All Rights Reserved.
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

#ifndef _CRAY_TOOLS_BE_H
#define _CRAY_TOOLS_BE_H

#include <stdint.h>
#include <sys/types.h>

/*
 * The Cray tools interface automatically creates/sets several environment 
 * variables when the dlaunch utility launches the tool daemon on the compute
 * node. They are defined here. Note that the value of these environment
 * variables are subject to change. Use the defines to guarantee portability.
 * These should all be read-only.
 *
 * CTI_SCRATCH_ENV_VAR
 *
 *         The environment variable that is used to denote temporary
 *         storage space. This is set to a location that is guaranteed
 *         to be writable and unique to the instance of the tool daemon.
 *         If temporary storage space is required to be shared between a tool
 *         daemon and the application, the caller should set the value of 
 *         CTI_SCRATCH_ENV_VAR in the environment of aprun and then set
 *         CTI_SCRATCH_ENV_VAR in their tool daemon to the value of 
 *         OLD_SCRATCH_ENV_VAR. If multiple instances of tool daemons need to
 *         share temporary storage space, CTI_SCRATCH_ENV_VAR can be set to the
 *         value of CTI_ROOT_DIR_VAR.
 *
 * CTI_OLD_SCRATCH_ENV_VAR
 *
 *         The environment variable that is used to contain the value of
 *         CTI_SCRATCH_ENV_VAR that was set inside the environment of the aprun
 *         associated with this tool daemon. This is useful if you need to query
 *         the value that was used in conjunction with the environment of the 
 *         application. If CTI_SCRATCH_ENV_VAR was not set in the environment of
 *         aprun, CTI_OLD_SCRATCH_ENV_VAR will not exist in the environment of
 *         the tool daemon.
 *
 * CTI_OLD_CWD_ENV_VAR
 *
 *         The environment variable that is used to contain the value of
 *         current working directory of the aprun associated with this tool
 *         daemon. This is useful if the tool daemon needs to know about the
 *         directory the application is running in to read or write a file.
 */
#define CTI_SCRATCH_ENV_VAR     "TMPDIR"
#define CTI_OLD_SCRATCH_ENV_VAR "CRAYTOOL_OLD_TMPDIR"
#define CTI_OLD_CWD_ENV_VAR     "CRAYTOOL_OLD_CWD"

#ifdef __cplusplus
extern "C" {
#endif

/* 
 * The following are types used as return values for some API calls.
 */
typedef struct
{
        pid_t           pid;    // This entries pid
        int             rank;   // This entries rank
} cti_rankPidPair_t;

typedef struct
{
        int                 numPids;
        cti_rankPidPair_t * pids;
} cti_pidList_t;

enum cti_be_wlm_type
{
    CTI_BE_WLM_NONE,    // error/unitialized state
    CTI_BE_WLM_ALPS,
    CTI_BE_WLM_CRAY_SLURM,
    CTI_BE_WLM_SLURM
};
typedef enum cti_be_wlm_type cti_be_wlm_type;

/*
 * The Cray tools interface backend calls are defined below.
 */

/*
 * cti_be_current_wlm - Obtain the current workload manager (WLM) in use on the 
 *                      system.
 * 
 * Detail
 *      This call can be used to obtain the current WLM in use on the system.
 *      The result can be used by the caller to validate arguments to functions
 *      and learn which WLM specific calls can be made.
 *
 * Arguments
 *      None.
 *
 * Returns
 *      A cti_be_wlm_type that contains the current WLM in use on the system.
 *
 */
extern cti_be_wlm_type cti_be_current_wlm(void);

/*
 * cti_be_wlm_type_toString - Obtain the stringified representation of the 
 *                            cti_be_wlm_type.
 * 
 * Detail
 *      This call can be used to turn the cti_be_wlm_type returned by 
 *      cti_current_wlm into a human readable format.
 *
 * Arguments
 *      wlm_type - The cti_be_wlm_type to stringify
 *
 * Returns
 *      A string containing the human readable format.
 *
 */
extern const char * cti_be_wlm_type_toString(cti_be_wlm_type wlm_type);

/*
 * cti_be_getAppId - Returns the Application id in string format of the
 *                   application associated with this tool daemon.
 * 
 * Detail
 *      This function returns the application id in string format. This string
 *      is formated in a WLM specific way. It is up to the caller to free the 
 *      returned string.
 *
 * Arguments
 *      None.
 *
 * Returns
 *      A string containing the application id, or else NULL on error.
 * 
 */
extern char * cti_be_getAppId();

/*
 * cti_be_findAppPids - Returns a cti_pidList_t containing entries that hold
 *                      the PE rank and PE PID parings for all application PEs
 *                      that reside on this compute node.
 *
 * Detail
 *      This function creates and returns a cti_pidList_t that contains the
 *      number of PE rank/PE PID pairs and cti_nodeRankPidPair entries that 
 *      contain the actual rank number along with the associated pid of the PE. 
 *
 * Arguments
 *      None.
 *
 * Returns
 *      A cti_pidList_t that contains the number of PE rank/PE pid pairings
 *      on the node and an array of cti_nodeRankPidPair that contain the actual 
 *      PE rank/PE pid pairings. Returns NULL on error.
 *
 */
extern cti_pidList_t * cti_be_findAppPids(void);

/*
 * cti_be_destroyPidList - Used to destroy the memory allocated for a 
 *                         cti_pidList_t.
 * 
 * Detail
 *      This function free's a cti_pidList_t. It is used to safely destroy
 *      the data structure returned by a call to the cti_findAppPids function 
 *      when the caller is done with the data that was allocated during its 
 *      creation.
 *
 * Arguments
 *      pid_list - A pointer to the cti_pidList_t to free.
 *
 * Returns
 *      Void. This function behaves similarly to free().
 *
 */
extern void cti_be_destroyPidList(cti_pidList_t *pid_list);

/*
 * cti_be_getNodeHostname - Returns the hostname of this compute node.
 * 
 * Detail
 *      This function determines the hostname of the current compute node. It is
 *      up to the caller to free the returned string.
 *
 * Arguments
 *      None.
 *
 * Returns
 *      A string containing the hostname, or else a null string on error.
 * 
 */
extern char * cti_be_getNodeHostname();

/*
 * cti_be_getNodeFirstPE - Returns the first PE number that resides on this
 *                         compute node.
 * 
 * Detail
 *      This function determines the first PE (as in lowest numbered) that 
 *      resides on the compute node. The PE acronym stands for Processing
 *      Elements and for an entire application are doled out starting at zero
 *      and incrementing progressively through all of the nodes. Any given node
 *      has a consecutive set of PE numbers starting at cti_getNodeFirstPE() up 
 *      through cti_getNodeFirstPE() + cti_getNodePEs() - 1.
 *
 * Arguments
 *      None.
 *
 * Returns
 *      The integer value of the first PE on the node, or else -1 on error.
 * 
 */
extern int cti_be_getNodeFirstPE(void);

/*
 * cti_be_getNodePEs - Returns the number of PEs that reside on this compute 
 *                     node.
 * 
 * Detail
 *      This function determines the number of PEs that reside on the compute
 *
 * Arguments
 *      None.
 *
 * Returns
 *      The integer value of the number of PEs on the node, or else -1 on error.
 * 
 */
extern int cti_be_getNodePEs(void);

/*
 * cti_be_getRootDir - Get root directory for this tool daemon.
 * 
 * Detail
 *      This function is used to return the path of the root location for this
 *      tool daemon. Any files that were transfered over will be found inside 
 *      this directory. The binary, libraries, and temp directories are all 
 *      subdirectories of this root value. The cwd of the tool daemon is 
 *      automatically set to this location. It is the callers responsibility to 
 *      free the allocated storage when it is no longer needed.
 *
 * Arguments
 *      None.
 *
 * Returns
 *      The path string on success, or NULL on error.
 * 
 */
extern char * cti_be_getRootDir(void);

/*
 * cti_be_getBinDir - Get bin directory for this tool daemon.
 * 
 * Detail
 *      This function is used to return the path of the binary location for this
 *      tool daemon. This directory is used to hold the location of any binaries 
 *      that were shipped to the compute node with the manifest. This value is 
 *      automatically added to PATH of the tool daemon. It is the callers 
 *      responsibility to free the allocated storage when it is no longer 
 *      needed.
 *
 * Arguments
 *      None.
 *
 * Returns
 *      The path string on success, or NULL on error.
 * 
 */
extern char * cti_be_getBinDir(void);

/*
 * cti_be_getLibDir - Get lib directory for this tool daemon.
 * 
 * Detail
 *      This function is used to return the path of the library location for
 *      this tool daemon. This directory is used to hold the location of any
 *      libraries that were shipped to the compute node with the manifest. This
 *      value is automatically added to LD_LIBRARY_PATH of the tool daemon. It
 *      is the callers responsibility to free the allocated storage when it is 
 *      no longer needed. 
 *
 * Arguments
 *      None.
 *
 * Returns
 *      The path string on success, or NULL on error.
 * 
 */
extern char * cti_be_getLibDir(void);

/*
 * cti_be_getFileDir - Get file directory for this tool daemon.
 * 
 * Detail
 *      This function is used to return the path of the file location for this
 *      tool daemon. This directory is used to hold the location of any files
 *      that were shipped to the compute node with the manifest. It is the 
 *      callers responsibility to free the allocated storage when it is no 
 *      longer needed. 
 *
 * Arguments
 *      None.
 *
 * Returns
 *      The path string on success, or NULL on error.
 * 
 */
extern char * cti_be_getFileDir(void);

/*
 * cti_be_getTmpDir - Get tmp directory for this tool daemon.
 * 
 * Detail
 *      This function is used to return the path of the tmp location for this
 *      tool daemon. This directory is guaranteed to be writable and is suitable
 *      for temporary file storage. It will be cleaned up on tool daemon exit.
 *      It is the callers responsibility to free the allocated storage when it
 *      is no longer needed.
 *
 * Arguments
 *      None.
 *
 * Returns
 *      The path string on success, or NULL on error.
 * 
 */
extern char * cti_be_getTmpDir(void);

#ifdef __cplusplus
}
#endif

#endif /* _CRAY_TOOLS_BE_H */
