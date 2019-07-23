/******************************************************************************\
 * cti_linking_test.c - An example program that tests linking in both FE and BE
 *                      libraries at the same time
 *
 * Copyright 2014-2019 Cray Inc. All Rights Reserved.
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

#include <stdio.h>

#include "cray_tools_fe.h"
#include "cray_tools_be.h"

int
main(int argc, char **argv)
{
    cti_wlm_type_t  mywlm;
    cti_wlm_type_t  mybewlm;

    /*
     * cti_current_wlm - Obtain the current workload manager (WLM) in use on the
     *                   system.
     */
    mywlm = cti_current_wlm();

    printf("Current fe workload manager: %s\n", cti_wlm_type_toString(mywlm));

    /*
     * cti_be_current_wlm - Obtain the current workload manager (WLM) in use on
     *                      the system.
     */
    mybewlm = cti_be_current_wlm();

    printf("Current be workload manager: %s\n", cti_be_wlm_type_toString(mybewlm));

    return 0;
}

