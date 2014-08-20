/*********************************************************************************\
 * ld_val_defs.h - A header file for the ld_val project containing standard
 *                 definitions used throughout the project. This has been 
 *                 simplified to read from a pipe instead of more advanced IPC
 *                 techniques.
 *
 * © 2011-2014 Cray Inc.  All Rights Reserved.
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

#define LIBAUDIT_ENV_VAR		"CRAY_LD_VAL_LIBRARY"
#define LD_AUDIT				"LD_AUDIT"

#define BLOCK_SIZE			16

#define LIBAUDIT_SEP_CHAR	'%'

#endif /* _LD_VAL_DEFS_H */
