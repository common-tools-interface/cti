
/*
 * (c) 2009 Cray Inc.  All Rights Reserved.  Unpublished Proprietary
 * Information.  This unpublished work is protected to trade secret,
 * copyright and other laws.  Except as permitted by contract or
 * express written permission of Cray Inc., no part of this work or
 * its content may be used, reproduced or disclosed in any form.
 */

#ifndef __LIBALPS_H
#define __LIBALPS_H

#ident "$Id: libalps.h 6081 2009-08-10 23:12:42Z ben $"

#include "alps.h"
#include "apInfo.h"

/*
 * libalps - an external library interface to ALPS for login/service node use
 */

/*
 * alps_get_apid - obtain an apid associated with an aprun
 *
 * Arguments
 *    aprun_nid - is the nid where the aprun is executed
 *    aprun_pid - is the pid of the aprun
 *
 * Returns
 *    The apid if found, 0 if not
 */
extern uint64_t    alps_get_apid(int aprun_nid, pid_t aprun_pid);

/*
 * alps_get_appinfo - open the ALPS application file and read the static
 *                    information associated with an apid
 * 
 * Detail
 *    There is a cmdDetail structure for each element of an MPMD application.
 *    The number of elements is of length appinfo.numCmds.  Memory for
 *    cmdDetail and places is obtained with malloc and must be freed.
 *
 * Arguments
 *    apid - the apid of the aprun
 *    appinfo - returns the appInfo_t struct containing general application
 *              information such as uid, gid, resId and fanout width as
 *              defined in apInfo.h
 *    cmdDetail - returns the cmdDetail_t struct containing width, depth,  
 *                memory and command name as defined in apInfo.h of
 *                length appinfo.numCmds
 *    places - returns the placeList_t struct containing information about each
 *             PE such as what nid and which processors reserved for the PE and
 *             any PE created threads (aprun -d) as defined in apInfo.h of
 *             length appinfo.numPlaces
 *
 * Returns
 *    1 if successful, else -1 for any error
 *
 */
extern int         alps_get_appinfo(uint64_t apid, appInfo_t *appinfo,
                       cmdDetail_t **cmdDetail, placeList_t **places);

/*
 * alps_get_appinfo_err - open the ALPS application file and read the static
 *                    information associated with an apid and return an
 *                    an error string and errno setting, as applicable
 * 
 * Detail
 *    There is a cmdDetail structure for each element of an MPMD application.
 *    The number of elements is of length appinfo.numCmds.  Memory for
 *    cmdDetail and places is obtained with malloc and must be freed.
 *    For any error, an error message string and errno, as applicable,
 *    are returned.  There is no memory to be freed with errMsg and err.
 *
 * Arguments
 *    apid - the apid of the aprun
 *    appinfo - returns the appInfo_t struct containing general application
 *              information such as uid, gid, resId and fanout width as
 *              defined in apInfo.h
 *    cmdDetail - returns the cmdDetail_t struct containing width, depth,  
 *                memory and command name as defined in apInfo.h of
 *                length appinfo.numCmds
 *    places - returns the placeList_t struct containing information about each
 *             PE such as what nid and which processors reserved for the PE and
 *             any PE created threads (aprun -d) as defined in apInfo.h of
 *             length appinfo.numPlaces
 *    errMsg - returns an error message for any error if errMsg is not NULL
 *    err - returns errno, as applicable, if err is not NULL
 *
 * Returns
 *    1 if successful, else -1 for any error
 *
 */
extern int         alps_get_appinfo_err(uint64_t apid, appInfo_t *appinfo,
                       cmdDetail_t **cmdDetail, placeList_t **places,
                       char **errMsg, int *err);

/*
 * alps_launch_tool_helper - assists in launching a tool helper program for
 *                           a specific application onto the same compute nodes
 *                           as the application
 * 
 * Detail
 *    Files needed by the tool helper program can be staged to the compute
 *    nodes by invoking this procedure multiple times, as needed, with transfer
 *    set to 1 and execute set to 0 for the staged files.  The application is
 *    already executing on the compute nodes, so this interface can't be used
 *    to stage application files before application execution.  If the command 
 *    count is more than one for MPMD on mixed mode architecture systems, the
 *    command array must be ordered to match the MPMD ordering of
 *    the application.
 *
 * Arguments
 *    apid - the apid of the aprun
 *    PE0_nid - nid of the first node in the placement list
 *    transfer - flag specifying whether helper program should be transferred
 *    execute - flag specifying whether the helper program should be executed
 *    cmd_count - number of tool helper programs
 *    cmd_string - array of string(s) containing the full path of the
 *                 tool helper program command name and arguments
 *
 * Returns
 *    If there is an error, a message will be returned.  Otherwise, NULL.
 * 
 */
extern const char *alps_launch_tool_helper(uint64_t apid, int PE0_nid,
                       int transfer, int execute, int cmd_count,
		       char **cmd_string);

typedef struct {
    int nid;
    alps_nodeState_t nodeState;
}  la_nodeState_t;

/*
 * alps_get_alps_nodeState - open the ALPS reservations file and read the
 *                           node state as known by apsched for the provided
 *                           node array
 *
 * Detail
 *    This procedure provides a way to query the compute node state as
 *    currently known within ALPS for one or more compute nodes.  For
 *    application placement within ALPS, this state is either unknown,
 *    down (i.e. unavailable for placement such as admindown, suspect, down),
 *    or available (i.e. up).
 *
 *    The caller malloc's an array of la_nodeState_t structures and fills
 *    in the compute node ids (nid) fields.  This procedure will fill in
 *    the node state fields.  The caller is responsible to free this malloc'd
 *    array of la_nodeState_t structures.  This procedure does not malloc any
 *    memory that the caller is responsible to free.
 * 
 * Arguments
 *    nid_state - caller malloc'd array of la_nodeState_t structures with
 *                the nid fields filled in by the caller
 *    numEntries - number of array entries
 *    err - returns an applicable errno; this argument can be null if the
 *          caller doesn't want errno information
 *
 * Returns
 *    For success, NULL is returned.  The nodeState field for each requested
 *    nid is initialized with state_unknown and then updated to the state
 *    value known by ALPS.  If there is an error, an error message will be
 *    returned, err will be set to an errno, as applicable, and nodeState
 *    fields may remain set to state_unknown.  Any returned error message
 *    is in static memory and does not need to be freed by the caller.
 */
extern const char *alps_get_alps_nodeState(la_nodeState_t *nid_state,
                       int numEntries, int *err);

#endif  /* __LIBALPS_H */
