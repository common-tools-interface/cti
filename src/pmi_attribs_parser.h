/******************************************************************************\
 * pmi_attribs_parser.c - A header file for the pmi_attribs file parser.
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

#ifndef _PMI_ATTRIBS_PARSER_H
#define _PMI_ATTRIBS_PARSER_H

#include <stdint.h>
#include <sys/types.h>

#include "alps/alps.h"

/* We expect the pmi_attribs format to be /var/spool/alps/<apid>/pmi_attribs */
#define PMI_ATTRIBS_FILE_NAME        "pmi_attribs"
#define PMI_ATTRIBS_FILE_PATH_FMT    ALPS_CNODE_PATH_FMT "/" PMI_ATTRIBS_FILE_NAME

/* Timeout length in seconds for trying to open pmi_attribs file */
#define PMI_ATTRIBS_DEFAULT_FOPEN_TIMEOUT    60
// User defined timeout variables
#define PMI_ATTRIBS_TIMEOUT_ENV_VAR		"CRAY_CTI_PMI_FOPEN_TIMEOUT"
#define PMI_EXTRA_SLEEP_ENV_VAR			"CRAY_CTI_PMI_EXTRA_SLEEP"

/* struct typedefs */
typedef struct
{
        int             rank;   // This entries rank
        pid_t           pid;    // This entries pid
} nodeRankPidPair_t;

typedef struct
{
        uint64_t                apid;                   // apid for this pmi_attribs_t file
        int                     pmi_file_ver;           // pmi_attribs file layout version
        int                     cnode_nidNum;           // compute node nid number
        int                     mpmd_cmdNum;            // command number this node represents in the MPMD set
        int                     app_nodeNumRanks;       // Number of ranks present on this node
        nodeRankPidPair_t *     app_rankPidPairs;     // Rank/Pid pairs
} pmi_attribs_t;

/* function prototypes */
pmi_attribs_t * getPmiAttribsInfo(uint64_t);
void            freePmiAttribs(pmi_attribs_t *);

#endif /* _PMI_ATTRIBS_PARSER_H */

