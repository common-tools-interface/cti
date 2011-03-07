/*********************************************************************************\
 * ld_val.h - Header interface for the ld_val program. Used by ld_val.c
 *            This contains function prototypes as well as definitions
 *            for the dynamic linker run order.
 *
 * © 2011 Cray Inc.  All Rights Reserved.
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

char *  ld_verify(char *);
int     ld_load(char *, char *);
char *  ld_get_lib(int);
char ** ld_val(char *);

#endif /* _LD_VAL_H */
