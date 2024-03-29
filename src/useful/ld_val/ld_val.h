/*********************************************************************************\
 * ld_val.h - Header interface for the ld_val program. Used by ld_val.c
 *            This contains function prototypes as well as definitions
 *            for the dynamic linker run order.
 *
 * Copyright 2011-2020 Hewlett Packard Enterprise Development LP.
 * SPDX-License-Identifier: Linux-OpenIB
 ******************************************************************************/

#ifndef _LD_VAL_H
#define _LD_VAL_H

#ifdef __cplusplus
extern "C" {
#endif

// User should pass in a fullpath string of an executable and the path to the ld_audit
// library, this returns a null terminated array of strings containing location of
// dso dependencies.
// The caller is expected to free each of the returned strings as well as the
// string buffer.
char ** _cti_ld_val(const char *executable, const char *ld_audit_path);

#ifdef __cplusplus
}
#endif

#endif /* _LD_VAL_H */
