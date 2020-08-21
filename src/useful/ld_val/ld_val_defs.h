/*********************************************************************************\
 * ld_val_defs.h - A header file for the ld_val project containing standard
 *                 definitions used throughout the project. This has been
 *                 simplified to read from a pipe instead of more advanced IPC
 *                 techniques.
 *
 * (C) Copyright 2011-2020 Hewlett Packard Enterprise Development LP.
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

#ifndef _LD_VAL_DEFS_H
#define _LD_VAL_DEFS_H

#define LD_AUDIT                "LD_AUDIT"

#define BLOCK_SIZE          16
// Do not make this larger than the pipe capacity.
#define READ_BUF_LEN            1024

#define MANIFEST_BLACKLIST "/lib:/lib64:/usr/lib:/usr/lib64"

#define MANIFEST_BLACKLIST_ENV_VAR "CTI_BLACKLIST_DIRS"

#endif /* _LD_VAL_DEFS_H */
