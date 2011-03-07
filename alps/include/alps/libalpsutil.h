
/*
 * (c) 2009 Cray Inc.  All Rights Reserved.  Unpublished Proprietary
 * Information.  This unpublished work is protected to trade secret,
 * copyright and other laws.  Except as permitted by contract or
 * express written permission of Cray Inc., no part of this work or
 * its content may be used, reproduced or disclosed in any form.
 */

#ifndef __LIBALPSUTIL_H
#define __LIBALPSUTIL_H

#ident "$Id: libalpsutil.h 6022 2009-05-19 15:12:29Z ben $"

/*
 * libalpsutil - an external library interface to ALPS for compute node use
 */

/*
 * alps_get_placement_info - get the application layout (alpsAppLayout_t struct)
 *                           and fanout tree information for a given apid by
 *                           opening the compute node placement file on the node
 *
 * Detail
 *    The control network root is aprun, which is the controller for PE0.  The
 *    target nodes are branches of the control network rooted at PE0.  Each
 *    node on a branch may also contain target nodes, unless the node is at
 *    the bottom of the controlled network.  The information is specific to a
 *    node and is only as wide as the ALPS fanout tree.
 *    Entries are read into malloc'd space which the caller is responsible to
 *    free.  Any NULL passed into the arguments, flags that the information for 
 *    that argument should not be collected and no space will be malloc'd 
 *    for that argument.
 *
 * Arguments
 *    apid - the apid of the aprun
 *    appLayout - returns the alpsAppLayout_t struct (alps_toolAssist.h) for
 *                the specific node
 *    placementList - returns the placement list containing the nid of each PE
 *                    (i.e. placementList[0] is the nid for PE0) in an integer
 *                    array format of length appLayout.numPes
 *    targetNids - returns the target nodes controlled by this node in an
 *                 integer array format of length appLayout.numTargets
 *    targetPes - returns the first PE on each controlled node in an integer
 *                array format of length appLayout.numTargets
 *    targetLen - returns the number of PEs per controlled node in an
 *                integer array of length appLayout.numTargets
 *    targetIps - returns the IP address of each controlled node in a type
 *                in_addr array of length appLayout.numTargets
 *    startPe - returns the starting PE of the command in the MPMD set in an
 *              integer array of length appLayout.numCmds
 *    totalPes - returns the total number of PEs in the MPMD set in an
 *               integer array of length appLayout.numCmds
 *    nodePes - returns the number of PEs per node in the MPMD set in an
 *              integer array of length appLayout.numCmds
 *    peCpus - returns the number of CPUs per PE in the MPMD set in an
 *             integer array of length appLayout.numCmds
 *
 * Returns
 *    1 if successful, else -1 for any error
 *
 */
extern int alps_get_placement_info(uint64_t apid, alpsAppLayout_t *appLayout,
		int **placementList, int **targetNids, int **targetPes,
		int **targetLen, struct in_addr **targetIps, int **startPe,
		int **totalPes, int **nodePes, int **peCpus);


#endif /* __LIBALPSUTIL_H */
