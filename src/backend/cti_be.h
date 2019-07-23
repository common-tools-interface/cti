/******************************************************************************\
 * cti_be.h - A interface to interact with placement information on compute
 *        nodes. This provides the tool developer with an easy to use interface
 *        to obtain application information for backend tool daemons running on
 *        the compute nodes.
 *
 * Copyright 2011-2019 Cray Inc. All Rights Reserved.
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

#ifndef _CTI_BE_H
#define _CTI_BE_H

#include <sys/types.h>

#include "cti_defs.h"
#include "cray_tools_be.h"

// This is the wlm proto object that all wlm implementations should define.
// The noneness functions can be used if a function is not definable by your wlm,
// but that should only be used if an API call is truly incompatible with the wlm.
typedef struct
{
    cti_wlm_type_t      wlm_type;                       // wlm type
    int                 (*wlm_init)(void);              // wlm init function - return true on error
    void                (*wlm_fini)(void);              // wlm finish function
    cti_pidList_t *     (*wlm_findAppPids)(void);       // get pids of application ranks - return NULL on error
    char *              (*wlm_getNodeHostname)(void);   // get hostname of current compute node - return NULL on error
    int                 (*wlm_getNodeFirstPE)(void);    // get first numeric rank located on the current compute node - return -1 on error
    int                 (*wlm_getNodePEs)(void);        // get number of ranks located on the current compute node - return -1 on error
} cti_be_wlm_proto_t;

/* internal function prototypes */
char *              _cti_be_getToolDir(void);
char *              _cti_be_getAttribsDir(void);

/* Noneness functions for wlm proto - Use these if your wlm proto doesn't define the function */
int                 _cti_be_wlm_init_none(void);
void                _cti_be_wlm_fini_none(void);
cti_pidList_t *     _cti_be_wlm_findAppPids_none(void);
char *              _cti_be_wlm_getNodeHostname_none(void);
int                 _cti_be_wlm_getNodeFirstPE_none(void);
int                 _cti_be_wlm_getNodePEs_none(void);

#endif /* _CTI_BE_H */
