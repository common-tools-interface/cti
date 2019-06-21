/******************************************************************************\
 * cti_linking_test.c - An example program that tests linking in both FE and BE
 *                      libraries at the same time
 *
 * Copyright 2014-2019 Cray Inc.    All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
 *
 ******************************************************************************/

#include <stdio.h>

#include "cray_tools_fe.h"
#include "cray_tools_be.h"

int
main(int argc, char **argv)
{
    cti_wlm_type        mywlm;
    cti_be_wlm_type     mybewlm;

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

