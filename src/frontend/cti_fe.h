/*********************************************************************************\
 * cti_fe.h - A header file for the cti frontend interface.
 *
 * © 2014 Cray Inc.	All Rights Reserved.
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
 *********************************************************************************/

#ifndef _CTI_FE_H
#define _CTI_FE_H

enum cti_wlm_type
{
	CTI_WLM_NONE,
	CTI_WLM_ALPS,
	CTI_WLM_SLURM
};
typedef enum cti_wlm_type	cti_wlm_type;

/* function prototypes */

cti_wlm_type	cti_current_wlm(void);

#endif /* _CTI_FE_H */
