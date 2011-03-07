
/*
 * (c) 2009 Cray Inc.  All Rights Reserved.  Unpublished Proprietary
 * Information.  This unpublished work is protected to trade secret,
 * copyright and other laws.  Except as permitted by contract or
 * express written permission of Cray Inc., no part of this work or
 * its content may be used, reproduced or disclosed in any form.
 */

#ifndef __ALPS_TOOLASSIST_H
#define __ALPS_TOOLASSIST_H

#ident "$Id: alps_toolAssist.h 6022 2009-05-19 15:12:29Z ben $"

#include <inttypes.h>
#include <sys/types.h>
#include <netinet/in.h>

/*
 * A file is created in the local filesystem that contains placement
 * information for the application.
 * The name, structure and content of that file are described here.
 * The file is in the directory defined by ALPS_CNODE_PATH (See
 * alps.h)
 * The file name is places<apid>
 */
#define ALPS_CNODE_PLACEMENT_FMT	"places%llu"

/*
 * A directory is created in the local filesystem that holds copies
 * of the files transported for the tool helpers working with the app.
 * This directory descends from the primary apid directory.
 *
 * The full name of this directory is ALPS_CNODE_PATH/<apid>/toolhelper<apid>
 */
#define ALPS_CNODE_TOOL_FMT		"toolhelper%llu"

/*
 *
 * All references to the variable length information is by byte
 * offsets relative to the start of the alpsAppLayout_t structure.
 * The information is written to a file so the offsets can be used
 * as the seek address of the beginning of each portion of the information.
 */

typedef struct {
    size_t	totLength;	/* total byte count in this file */
    uint64_t	apid;		/* application ID */
    int		controlNid;	/* NID of this node's controller */
    struct in_addr conIpAddr;	/* IP address of this node's controller */
    int		firstPe;	/* first PE number on this node */
    int		numPesHere;	/* number of PEs on this node */
    int		cpuMask;	/* CPUs alocated on this node */
    int		peDepth;	/* CPUs per PE on this node */
    int		cmdNumber;	/* command number in MPMD set */

    /*
     * The following integer arrays have numPes entries
     * The arrays are indexed by PE number.
     */
    int		numPes;		/* total PEs in the entire MPMD set */

    /*
     * This is the full placement list.
     */
    size_t	offNidList;	/* NID of each PE */

    /*
     * The following information is specific to the particular node on
     * which it is found. The targets are the nodes controlled by this
     * node. (The target nodes are branches of the control network
     * rooted at this node.) Each node applies this information in a
     * consistent way to create the control network.
     */
    int		numTargets;	/* number of nodes controlled by this node */
    /*
     * The following integer arrays have numTargets entries
     * PE numbers can be used as an index into offNid* arrays
     */
    size_t	offTargetNid;	/* controlled NIDs */
    size_t	offTargetPe;	/* first PE for each target */
    size_t	offTargetLen;	/* offNidList entries per target */

    /* This is an array of numTargets struct in_addr items */
    size_t	offTargetIp;	/* IP address of the target */
    
    /*
     * The following three integer arrays have numCmds entries
     */
    int		numCmds;	/* number of commands in MPMD set */
    size_t	offStartPe;	/* starting PE number of the command */
    size_t	offNumPes;	/* total number of PEs in the command */
    size_t	offPerNode;	/* number of PEs per node in the command */
    size_t	offDepth;	/* number of CPUs per PE in the command */
    
} alpsAppLayout_t;

#endif /* __ALPS_TOOLASSIST_H */
