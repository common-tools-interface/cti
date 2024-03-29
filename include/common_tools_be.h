/******************************************************************************\
 * common_tools_be.h - The public API definitions for the backend portion of
 *                     the common tools interface. Backend refers to the
 *                     location where applications are run.
 *
 * Copyright 2011-2020 Hewlett Packard Enterprise Development LP.
 * SPDX-License-Identifier: Linux-OpenIB
 ******************************************************************************/

#ifndef _COMMON_TOOLS_BE_H
#define _COMMON_TOOLS_BE_H

#include "common_tools_shared.h"

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

/*
 * The common tools interface backend calls are defined below.
 */

/*
 * cti_be_version - Returns the version string of the CTI backend library.
 *
 * Detail
 *      This function returns the version string of the backend library. This
 *      can be used to check for compatibility.
 *
 * Arguments
 *      None.
 *
 * Returns
 *      A string containing the current backend library version in the form
 *      major.minor.revision.   For a libtool current:revison:age format
 *      major = current - age and minor = age.
 */
const char * cti_be_version(void);

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
 *      A cti_wlm_type_t that contains the current WLM in use on the system.
 *
 */
cti_wlm_type_t cti_be_current_wlm(void);

/*
 * cti_be_wlm_type_toString - Obtain the stringified representation of the
 *                            cti_be_wlm_type.
 *
 * Detail
 *      This call can be used to turn the cti_wlm_type_t returned by
 *      cti_be_current_wlm into a human readable format.
 *
 * Arguments
 *      wlm_type - The cti_wlm_type_t to stringify
 *
 * Returns
 *      A string containing the human readable format.
 *
 */
const char * cti_be_wlm_type_toString(cti_wlm_type_t wlm_type);

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
char * cti_be_getAppId();

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
cti_pidList_t * cti_be_findAppPids(void);

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
void cti_be_destroyPidList(cti_pidList_t *pid_list);

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
char * cti_be_getNodeHostname();

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
int cti_be_getNodeFirstPE(void);

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
int cti_be_getNodePEs(void);

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
char * cti_be_getRootDir(void);

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
char * cti_be_getBinDir(void);

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
char * cti_be_getLibDir(void);

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
char * cti_be_getFileDir(void);

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
char * cti_be_getTmpDir(void);

#ifdef __cplusplus
}
#endif

#endif /* _COMMON_TOOLS_BE_H */
