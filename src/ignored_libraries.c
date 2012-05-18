/*********************************************************************************\
 * ignored_libraries.c - This file contains the ignored_libs array which is used
 *                       to indicate libraries that already exist on the compute
 *                       node.
 *
 * © 2012 Cray Inc.  All Rights Reserved.
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

#include <stdio.h>

/* 
** This list may need to be updated with each new release of CNL.
*/
const char * __ignored_libs[] = {
	"libdl.so.2",
	"libc.so.6",
	"libvolume_id.so.1",
	"libcidn.so.1",
	"libnsl.so.1",
	"librt.so.1",
	"libutil.so.1",
	"libpthread.so.0",
	"libudev.so.0",
	"libcrypt.so.1",
	"libz.so.1",
	"libm.so.6",
	"libnss_files.so.2",
	NULL
};

