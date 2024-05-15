/******************************************************************************\
 * pmi_attribs_parser.h - A header file for the pmi_attribs file parser.
 *
 * Copyright 2011-2020 Hewlett Packard Enterprise Development LP.
 * SPDX-License-Identifier: Linux-OpenIB
 ******************************************************************************/

#ifndef _PMI_ATTRIBS_PARSER_H
#define _PMI_ATTRIBS_PARSER_H

#include <stdint.h>
#include <sys/types.h>

/* struct typedefs */
typedef struct
{
    int     rank;   // This entries rank
    pid_t   pid;    // This entries pid
} nodeRankPidPair_t;

typedef struct
{
    int                     pmi_file_ver;       // pmi_attribs file layout version
    int                     cnode_nidNum;       // compute node nid number
    int                     mpmd_cmdNum;        // command number this node represents in the MPMD set
    int                     app_nodeNumRanks;   // Number of ranks present on this node
    nodeRankPidPair_t *     app_rankPidPairs;   // Rank/Pid pairs
} pmi_attribs_t;

/* function prototypes */
pmi_attribs_t * _cti_be_getPmiAttribsInfo(void);
pmi_attribs_t * _cti_be_getPmiAttribsInfoNoRetry(void);
void            _cti_be_freePmiAttribs(pmi_attribs_t *);

#endif /* _PMI_ATTRIBS_PARSER_H */

