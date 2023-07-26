/******************************************************************************\
 * cti_linking_test.c - An example program that tests linking in both FE and BE
 *                      libraries at the same time
 *
 * Copyright 2014-2020 Hewlett Packard Enterprise Development LP.
 * SPDX-License-Identifier: Linux-OpenIB
 ******************************************************************************/

#include <stdio.h>

#include "common_tools_fe.h"
#include "common_tools_be.h"

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

    // emit "Launch complete" for test harness timeout detection
    fprintf(stderr, "Safe from launch timeout.\n");

    return 0;
}

