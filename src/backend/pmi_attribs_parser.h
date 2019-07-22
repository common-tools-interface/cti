/******************************************************************************\
 * pmi_attribs_parser.h - A header file for the pmi_attribs file parser.
 *
 * Copyright 2011-2019 Cray Inc.  All Rights Reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
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
void            _cti_be_freePmiAttribs(pmi_attribs_t *);

#endif /* _PMI_ATTRIBS_PARSER_H */

