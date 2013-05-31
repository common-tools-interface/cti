/*********************************************************************************\
 * ld_val_defs.h - A header file for the ld_val project containing standard
 *                 definitions used throughout the project. Note that KEY_A
 *                 and KEY_B coorespond to the key_t used to create the shm
 *                 segment. These definitions can be modified as needed.
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
 *********************************************************************************/

#ifndef _LD_VAL_DEFS_H
#define _LD_VAL_DEFS_H

#define LIBAUDIT_ENV_VAR				"CRAY_LD_VAL_LIBRARY"
#define LIBAUDIT_KEYFILE_ENV_VAR	"CRAY_LD_VAL_KEYFILE"
#define LD_AUDIT						"LD_AUDIT"

#define DEFAULT_KEYFILE				"/proc/cray_xt"
#define ID_A							'A'
#define ID_B							'B'
#define BLOCK_SIZE					20

#define LDVAL_SEM						0
#define AUDIT_SEM						1

#endif /* _LD_VAL_DEFS_H */
