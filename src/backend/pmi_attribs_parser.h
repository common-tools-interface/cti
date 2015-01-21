/******************************************************************************\
 * pmi_attribs_parser.h - A header file for the pmi_attribs file parser.
 *
 * Copyright 2011-2014 Cray Inc.  All Rights Reserved.
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

/* struct typedefs */
typedef struct
{
	int		rank;	// This entries rank
	pid_t	pid;	// This entries pid
} nodeRankPidPair_t;

typedef struct
{
	int						pmi_file_ver;		// pmi_attribs file layout version
	int						cnode_nidNum;		// compute node nid number
	int						mpmd_cmdNum;		// command number this node represents in the MPMD set
	int						app_nodeNumRanks;	// Number of ranks present on this node
	nodeRankPidPair_t *		app_rankPidPairs;	// Rank/Pid pairs
} pmi_attribs_t;

/* function prototypes */
pmi_attribs_t *	_cti_be_getPmiAttribsInfo(void);
void			_cti_be_freePmiAttribs(pmi_attribs_t *);

#endif /* _PMI_ATTRIBS_PARSER_H */

