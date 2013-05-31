/*********************************************************************************\
 * ld_val.h - Header interface for the ld_val program. Used by ld_val.c
 *            This contains function prototypes as well as definitions
 *            for the dynamic linker run order.
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

#ifndef _LD_VAL_H
#define _LD_VAL_H

// User should pass in a fullpath string of an executable, this returns a null
// terminated array of strings containing location of dso dependencies.
// The caller is expected to free each of the returned strings as well as the
// string buffer.
char ** ld_val(char *);

#endif /* _LD_VAL_H */
