/*********************************************************************************\
 * audit.c - A custom rtld audit interface to deliver locations of loaded dso's
 *           over stdout.
 *
 * Copyright 2011-2019 Cray Inc.  All Rights Reserved.
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <limits.h>
#include <link.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ld_val_defs.h"

// This is always the first thing called
unsigned int
la_version(unsigned int version)
{
    // Set stderr to be fully buffered. This should not be larger than the
    // capicity of the pipe, otherwise we will have problems.
    // XXX: How to handle failure? I don't think this will matter too much if
    // things fail.
    setvbuf(stderr, NULL, _IOFBF, READ_BUF_LEN);

    return version;
}

// This is called every time a shared library is loaded
unsigned int
la_objopen(struct link_map *map, Lmid_t lmid, uintptr_t *cookie)
{
    // Ensure the library name has a length, otherwise return
    if (strlen(map->l_name) != 0)
    {
        // write the lib string followed by a null terminator
        fprintf(stderr, "%s%c", map->l_name, '\0');
        // flush the output
        fflush(stderr);
    }

    // return normally
    return LA_FLG_BINDTO | LA_FLG_BINDFROM;
}
