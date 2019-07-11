/******************************************************************************\
 * cray_tools_shared.h - Shared type definitions shared between the FE and BE API
 *
 * Copyright 2011-2019 Cray Inc.  All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
 *
 ******************************************************************************/

#ifndef _CRAY_TOOLS_SHARED_H
#define _CRAY_TOOLS_SHARED_H

#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    CTI_WLM_NONE,       // error/unitialized state
    CTI_WLM_CRAY_SLURM, // SLURM implementation
    CTI_WLM_SSH,        // Direct SSH implementation
    CTI_WLM_MOCK        // Used for unit testing
} cti_wlm_type_t;

#ifdef __cplusplus
}
#endif

#endif /* _CRAY_TOOLS_SHARED_H */
