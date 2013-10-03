/******************************************************************************\
 * tool_backend.h - The public API definitions for the backend portion of the
 *                  tool_interface.
 *
 * © 2011-2013 Cray Inc.  All Rights Reserved.
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

#ifndef _TOOL_BACKEND_H
#define _TOOL_BACKEND_H

#include <stdint.h>
#include <sys/types.h>

/*
 * The tool interface needs to read environment specific locations
 * dynamically at run time. The environment variables that are read
 * are defined here.
 *
 * APID_ENV_VAR		Used to keep track of the apid associated with the
 *					tool daemon. This is automatically set when the dlaunch
 *					utility launches the tool daemon on the compute node.
 * SCRATCH_ENV_VAR	The environment variable that is used to denote temporary
 *					storage space. This is automatically set when the dlaunch
 *					utility launches the tool daemon on the compute node. This
 *					is the canonical Unix environment variable that denotes
 *					scratch space.
 * BIN_DIR_VAR      This can be used to get at any binaries that were shipped
 *                  to the compute node with the manifest.
 * LIB_DIR_VAR      This can be used to get at any libraries that were shipped
 *                  to the compute node with the manifest.
 */
#define APID_ENV_VAR     "CRAYTOOL_APID"
#define SCRATCH_ENV_VAR  "TMPDIR"
#define ROOT_DIR_VAR		"CRAYTOOL_ROOT_DIR"
#define BIN_DIR_VAR      "CRAYTOOL_BIN_DIR"
#define LIB_DIR_VAR      "CRAYTOOL_LIB_DIR"

/* struct typedefs */
typedef struct
{
        int             rank;   // This entries rank
        pid_t           pid;    // This entries pid
} nodeRankPidPair_t;

typedef struct
{
        int                     numPairs;
        nodeRankPidPair_t *     rankPidPairs;
} nodeAppPidList_t;

/*
 * alps_backend commands
 */


/*
 * findAppPids - Returns a nodeAppPidList_t struct containing nodeRankPidPair_t
 *               entries that contain the PE rank and PE PID parings for all
 *               application PEs that reside on the compute node.
 *
 * Detail
 *      This function creates a nodeAppPidList_t struct that contains the number
 *      of PE rank/PE PID pairs that reside on the compute node that the tool
 *      daemon interfacing with this library is running on as well the
 *      nodeRankPidPair_t struct entries that contain the actual rank number
 *      along with the associated pid of the PE. Note that on older systems that
 *      do not create the pmi_attribs file, an older mechanism will be used
 *      which "guesses" the association based on the order in which the pid's
 *      were placed into the job container.
 *
 * Arguments
 *      None.
 *
 * Returns
 *      An nodeAppPidList_t struct that contains the number of PE rank/PE pid
 *      pairings on the node and an array of nodeRankPidPair_t structs that
 *      contain the actual PE rank/PE pid pairings.
 *
 */
extern nodeAppPidList_t *	findAppPids(void);

/*
 * destroy_nodeAppPidList - Used to destroy the memory allocated for a 
 *                          nodeAppPidList_t struct.
 * 
 * Detail
 *      This function free's a nodeAppPidList_t struct. This is used to
 *      safely destroy the data structure returned by a call to the 
 *      findAppPids function when the caller is done with the data that was 
 *      allocated during its creation.
 *
 * Arguments
 *      pid_list - A pointer to the nodeAppPidList_t to free.
 *
 * Returns
 *      Void. This function behaves similarly to free().
 *
 */
extern void	destroy_nodeAppPidList(nodeAppPidList_t *pid_list);

/*
 * getNodeCName - Returns the cabinet hostname of the compute node.
 * 
 * Detail
 *      This function determines the cname of the current compute node where
 *      the tool daemon interfacing with this library resides. Note that it is
 *      up to the user to free the returned string.
 *
 * Arguments
 *      None.
 *
 * Returns
 *      A string containing the cname host, or else a null string on error.
 * 
 */
extern char *	getNodeCName(void);

/*
 * getNodeNidName - Returns the nid hostname of the compute node.
 * 
 * Detail
 *      This function determines the nid (node id) hostname of the current
 *      compute node where the tool daemon interfacing with this library 
 *      resides. Note that it is up to the user to free the returned string.
 *
 * Arguments
 *      None.
 *
 * Returns
 *      A string containing the nid hostname, or else a null string on error.
 * 
 */
extern char *	getNodeNidName(void);

/*
 * getNodeNid - Returns the node id of the compute node.
 * 
 * Detail
 *      This function determines the nid (node id) of the current compute node
 *      where the tool daemon interfacing with this library resides.
 *
 * Arguments
 *      None.
 *
 * Returns
 *      The integer value of the nid, or else -1 on error.
 * 
 */
extern int	getNodeNid(void);

/*
 * getFirstPE - Returns the first PE number that resides on the compute node.
 * 
 * Detail
 *      This function determines the first PE (as in lowest numbered) PE that
 *      resides on the current compute where the tool daemon interfacing with
 *      this library resides.
 *
 * Arguments
 *      None.
 *
 * Returns
 *      The integer value of the first PE on the node, or else -1 on error.
 * 
 */
extern int	getFirstPE(void);

/*
 * getPesHere - Returns the number of PEs that reside on the compute node.
 * 
 * Detail
 *      This function determines the number of PEs that reside on the current 
 *      compute where the tool daemon interfacing with this library resides.
 *
 * Arguments
 *      None.
 *
 * Returns
 *      The integer value of the number of PEs on the node, or else -1 on error.
 * 
 */
extern int	getPesHere(void);

#endif /* _TOOL_BACKEND_H */
