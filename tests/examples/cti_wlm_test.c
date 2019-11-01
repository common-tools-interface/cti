/******************************************************************************\
 * cti_wlm_test.c - An example program which takes advantage of the common
 *          tools interface which will gather information from the WLM about a
 *          previously launched job.
 *
 * Copyright 2012-2019 Cray Inc. All Rights Reserved.
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

#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>

#include "common_tools_fe.h"
#include "cti_fe_common.h"
#include "src/cti_defs.h"

void
usage(char *name)
{
    fprintf(stdout, "USAGE: %s\n", name);
    fprintf(stdout, "Print out the workload manager cti_wlm_type_t for this system\n");
    fprintf(stdout, "using the common tools interface.\n\n");
    return;
}

int
main(int argc, char **argv)
{
    // values returned by the tool_frontend library.
    cti_wlm_type_t      mywlm;
    /*
     * cti_current_wlm - Obtain the current workload manager (WLM) in use on the
     *                   system.
     */
    mywlm = cti_current_wlm();

    // Print out the wlm type using the defined text for each WLM type.
    switch (mywlm) {
        case CTI_WLM_SLURM:
        {
            fprintf(stdout, "%s WLM type.\n", SLURM_WLM_TYPE_IMPL);
        }
            break;
        case CTI_WLM_SSH:
        {
            fprintf(stdout, "%s WLM type.\n", SSH_WLM_TYPE_IMPL);
        }
            break;
        case CTI_WLM_MOCK:
        case CTI_WLM_NONE:
            fprintf(stderr, "Error: Unsupported WLM in use!\n");
            assert(0);
            return 1;
    }
    return 0;
}
