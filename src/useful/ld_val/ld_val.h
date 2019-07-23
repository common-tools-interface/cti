/*********************************************************************************\
 * ld_val.h - Header interface for the ld_val program. Used by ld_val.c
 *            This contains function prototypes as well as definitions
 *            for the dynamic linker run order.
 *
 * Copyright 2011-2015 Cray Inc.  All Rights Reserved.
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
