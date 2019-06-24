/******************************************************************************\
 * cti_fe_common.c - A test routine that exercises all of the FE API calls.
 *
 * Copyright 2015-2019 Cray Inc.    All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
 *
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>

#include "cray_tools_fe.h"

void
cti_test_fe(cti_app_id_t appId)
{
    cti_wlm_type        mywlm;
    const char *        mywlm_str;
    char *              myhostname;
    char *              mylauncherhostname;
    int                 mynumpes;
    int                 mynumnodes;
    char **             myhostlist;
    cti_hostsList_t *   myhostplacement;
    // internal variables
    char ** i;
    int     j;

    // Sanity of passed in arg
    assert(cti_appIsValid(appId) != 0);

    // test cti_error_str
    assert(cti_error_str() != NULL);

    printf("\nThe following is information about your application that the tool interface gathered:\n\n");

    /*
     * cti_current_wlm - Obtain the current workload manager (WLM) in use on the
     *                   system.
     */
    mywlm = cti_current_wlm();
    assert(mywlm != CTI_WLM_NONE);

    /*
     * cti_wlm_type_toString - Obtain stringified version of cti_wlm_type.
     */
    mywlm_str = cti_wlm_type_toString(mywlm);
    if (mywlm_str == NULL) {
        fprintf(stderr, "Error: cti_wlm_type_toString failed!\n");
        fprintf(stderr, "CTI error: %s\n", cti_error_str());
    }
    assert(mywlm_str != NULL);
    printf("Current workload manager: %s\n", mywlm_str);

    /*
     * cti_getHostname - Returns the hostname of the current login node.
     */
    myhostname = cti_getHostname();
    if (myhostname == NULL) {
        fprintf(stderr, "Error: cti_getHostname failed!\n");
        fprintf(stderr, "CTI error: %s\n", cti_error_str());
    }
    assert(myhostname != NULL);
    printf("Current hostname: %s\n", myhostname);
    free(myhostname);
    myhostname = NULL;

    // Conduct WLM specific calls
    switch (mywlm) {
        case CTI_WLM_CRAY_SLURM:
        {
            cti_srunProc_t *    mysruninfo;
            /*
             * cti_cray_slurm_getSrunInfo - Obtain information about the srun process
             */
            mysruninfo = cti_cray_slurm_getSrunInfo(appId);
            if (mysruninfo == NULL) {
                fprintf(stderr, "Error: cti_cray_slurm_getSrunInfo failed!\n");
                fprintf(stderr, "CTI error: %s\n", cti_error_str());
            }
            assert(mysruninfo != NULL);
            printf("jobid of application:  %lu\n", (long unsigned int)mysruninfo->jobid);
            printf("stepid of application: %lu\n", (long unsigned int)mysruninfo->stepid);
            free(mysruninfo);
            mysruninfo = NULL;
        }
            break;


        case CTI_WLM_SSH:
            break;

        default:
            // do nothing
            printf("Unsupported wlm!\n");
            assert(0);
            break;
    }
    /*
     * cti_getLauncherHostName - Returns the hostname of the login node where the
     *                           application launcher process resides.
     */
    mylauncherhostname = cti_getLauncherHostName(appId);
    if (mylauncherhostname != NULL) {
        assert(mylauncherhostname != NULL);
        printf("hostname where application launcher resides: %s\n", mylauncherhostname);
        free(mylauncherhostname);
        mylauncherhostname = NULL;
    }
    else {
        fprintf(stderr, "Warning: cti_getLauncherHostName unsupported.\n");
        fprintf(stderr, "CTI error: %s\n", cti_error_str());
    }

    /*
     * cti_getNumAppPEs -   Returns the number of processing elements in the application
     *                      associated with the apid.
     */
    mynumpes = cti_getNumAppPEs(appId);
    if (mynumpes == 0) {
        fprintf(stderr, "Error: cti_getNumAppPEs failed!\n");
        fprintf(stderr, "CTI error: %s\n", cti_error_str());
    }
    assert(mynumpes != 0);
    printf("Number of application PEs: %d\n", mynumpes);

    /*
     * cti_getNumAppNodes - Returns the number of compute nodes allocated for the
     *                      application associated with the launcher pid.
     */
    mynumnodes = cti_getNumAppNodes(appId);
    if (mynumnodes == 0) {
        fprintf(stderr, "Error: cti_getNumAppNodes failed!\n");
        fprintf(stderr, "CTI error: %s\n", cti_error_str());
    }
    assert(mynumnodes != 0);
    printf("Number of compute nodes used by application: %d\n", mynumnodes);

    /*
     * cti_getAppHostsList - Returns a null terminated array of strings containing
     *                      the hostnames of the compute nodes
     *                      for the application associated with the pid.
     */
    myhostlist = cti_getAppHostsList(appId);
    if (myhostlist == NULL) {
        fprintf(stderr, "Error: cti_getAppHostsList failed!\n");
        fprintf(stderr, "CTI error: %s\n", cti_error_str());
    }
    assert(myhostlist != NULL);
    printf("\nThe following is a list of compute node hostnames returned by cti_getAppHostsList():\n\n");
    i = myhostlist;
    while (*i != NULL) {
        printf("%s\n", *i);
        free(*i++);
    }
    free(myhostlist);
    myhostlist = NULL;

    /*
     * cti_getAppHostsPlacement -   Returns a cti_hostsList_t containing cti_host_t
     *                              entries that contain the hostname of the compute
     *                              nodes allocated and the number of PEs
     *                              assigned to that host for the application associated
     *                              with the launcher pid.
     */
    myhostplacement = cti_getAppHostsPlacement(appId);
    if (myhostplacement == NULL) {
        fprintf(stderr, "Error: cti_getAppHostsPlacement failed!\n");
        fprintf(stderr, "CTI error: %s\n", cti_error_str());
    }
    assert(myhostplacement != NULL);
    printf("\nThe following information was returned by cti_getAppHostsPlacement():\n\n");
    printf("There are %d host(s) in the cti_hostsList_t struct.\n", myhostplacement->numHosts);
    for (j=0; j < myhostplacement->numHosts; ++j) {
        printf("On host %s there are %d PEs.\n", myhostplacement->hosts[j].hostname, myhostplacement->hosts[j].numPes);
    }
    cti_destroyHostsList(myhostplacement);
    myhostplacement = NULL;

    /*
     * cti_killApp - Send a signal using the appropriate launcher kill mechanism to
     *               an application launcher.
     */
    j = cti_killApp(appId, 0);
    if (j) {
        fprintf(stderr, "Error: cti_killApp failed!\n");
        fprintf(stderr, "CTI error: %s\n", cti_error_str());
    }
    assert(j == 0);
}
