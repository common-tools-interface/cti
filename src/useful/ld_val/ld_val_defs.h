/*********************************************************************************\
 * ld_val_defs.h - A header file for the ld_val project containing standard
 *                 definitions used throughout the project. This has been 
 *                 simplified to read from a pipe instead of more advanced IPC
 *                 techniques.
 *
 * Copyright 2011-2015 Cray Inc.  All Rights Reserved.
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

#define LD_AUDIT				"LD_AUDIT"

#define BLOCK_SIZE			16
// Do not make this larger than the pipe capacity.
#define READ_BUF_LEN			1024

#define MANIFEST_BLACKLIST "/lib:/lib64:/usr/lib:/usr/lib64"

#define MANIFEST_BLACKLIST_ENV_VAR "CRAY_CTI_BLACKLIST_DIRS"

#endif /* _LD_VAL_DEFS_H */
