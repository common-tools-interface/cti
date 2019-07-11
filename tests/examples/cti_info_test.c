/******************************************************************************\
 * cti_info_test.c - An example program which takes advantage of the Cray
 *          tools interface which will gather information from the WLM about a
 *          previously launched job.
 *
 * Copyright 2012-2019 Cray Inc.    All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
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

#include "cray_tools_fe.h"
#include "cti_fe_common.h"

const struct option long_opts[] = {
    {"jobid",       required_argument,  0, 'j'},
    {"stepid",      required_argument,  0, 's'},
    {"pid",         required_argument,  0, 'p'},
    {"help",        no_argument,        0, 'h'},
    {0, 0, 0, 0}
    };

void
usage(char *name)
{
    fprintf(stdout, "USAGE: %s [OPTIONS]...\n", name);
    fprintf(stdout, "Gather information about a previously launched application\n");
    fprintf(stdout, "using the Cray tools interface.\n\n");

    fprintf(stdout, "\t-j, --jobid     slurm job id - SLURM WLM only. Use with -s.\n");
    fprintf(stdout, "\t-s, --stepid    slurm step id - SLURM WLM only. Use with -j.\n");
    fprintf(stdout, "\t-p, --pid       pid of launcher process - SSH WLM only.");
    fprintf(stdout, "\t-h, --help      Display this text and exit\n\n");

    return;
}

int
main(int argc, char **argv)
{
    int                 opt_ind = 0;
    int                 c;
    char *              eptr;
    int                 j_arg = 0;
    int                 s_arg = 0;
    int                 p_arg = 0;
    uint32_t            job_id = 0;
    uint32_t            step_id = 0;
    pid_t               launcher_pid = 0;
    // values returned by the tool_frontend library.
    cti_wlm_type_t      mywlm;
    cti_app_id_t        myapp;

    if (argc < 2) {
        usage(argv[0]);
        assert(argc > 2);
        return 1;
    }

    // process longopts
    while ((c = getopt_long(argc, argv, "j:s:p:h", long_opts, &opt_ind)) != -1) {
        switch (c) {
            case 0:
                // if this is a flag, do nothing
                break;

            case 'j':
                if (optarg == NULL) {
                    usage(argv[0]);
                    assert(0);
                    return 1;
                }

                // This is the job id
                errno = 0;
                job_id = (uint32_t)strtol(optarg, &eptr, 10);

                // check for error
                if ((errno == ERANGE && job_id == ULONG_MAX)
                        || (errno != 0 && job_id == 0)) {
                    perror("strtol");
                    assert(0);
                    return 1;
                }

                // check for invalid input
                if (eptr == optarg || *eptr != '\0') {
                    fprintf(stderr, "Invalid --jobid argument.\n");
                    assert(0);
                    return 1;
                }

                j_arg = 1;

                break;

            case 's':
                if (optarg == NULL) {
                    usage(argv[0]);
                    assert(0);
                    return 1;
                }

                // This is the step id
                errno = 0;
                step_id = (uint32_t)strtol(optarg, &eptr, 10);

                // check for error
                if ((errno == ERANGE && step_id == ULONG_MAX)
                        || (errno != 0 && step_id == 0)) {
                    perror("strtol");
                    assert(0);
                    return 1;
                }

                // check for invalid input
                if (eptr == optarg || *eptr != '\0') {
                    fprintf(stderr, "Invalid --stepid argument.\n");
                    assert(0);
                    return 1;
                }

                s_arg = 1;

                break;

            case 'p':
                if (optarg == NULL) {
                    usage(argv[0]);
                    assert(0);
                    return 1;
                }

                // This is the pid of the launcher process
                errno = 0;
                launcher_pid = (pid_t)strtol(optarg, &eptr, 10);

                // check for error
                if ((errno == ERANGE && step_id == ULONG_MAX)
                        || (errno != 0 && step_id == 0)) {
                    perror("strtol");
                    assert(0);
                    return 1;
                }

                // check for invalid input
                if (eptr == optarg || *eptr != '\0') {
                    fprintf(stderr, "Invalid --pid argument.\n");
                    assert(0);
                    return 1;
                }

                p_arg = 1;

                break;

            case 'h':
                usage(argv[0]);
                assert(0);
                return 0;

            default:
                usage(argv[0]);
                assert(0);
                return 1;
        }
    }

    /*
     * cti_current_wlm - Obtain the current workload manager (WLM) in use on the
     *                   system.
     */
    mywlm = cti_current_wlm();

    // Check the args to make sure they are valid given the wlm in use
    switch (mywlm) {
        case CTI_WLM_CRAY_SLURM:
        {
            if (j_arg == 0 || s_arg == 0) {
                fprintf(stderr, "Error: Missing --jobid and --stepid argument. This is required for the SLURM WLM.\n");
            }
            assert(j_arg != 0 && s_arg != 0);
            cti_cray_slurm_ops_t * slurm_ops;
            cti_wlm_type ret = cti_open_ops(&slurm_ops);
            assert(ret == mywlm);
            assert(slurm_ops != NULL);
            myapp = slurm_ops->registerJobStep(job_id, step_id);
            if (myapp == 0) {
                fprintf(stderr, "Error: registerJobStep failed!\n");
                fprintf(stderr, "CTI error: %s\n", cti_error_str());
            }
            assert(myapp != 0);
        }
            break;

        case CTI_WLM_SSH:
        {
            if (p_arg == 0) {
                fprintf(stderr, "Error: Missing --pid argument. This is required for the generic WLM.\n");
            }
            assert(p_arg != 0);
            cti_ssh_ops_t * ssh_ops;
            cti_wlm_type ret = cti_open_ops(&ssh_ops);
            assert(ret == mywlm);
            assert(ssh_ops != NULL);
            myapp = ssh_ops->registerJob(launcher_pid);
            if (myapp == 0) {
                fprintf(stderr, "Error: registerJob failed!\n");
                fprintf(stderr, "CTI error: %s\n", cti_error_str());
            }
            assert(myapp != 0);
        }
            break;

        case CTI_WLM_NONE:
            fprintf(stderr, "Error: Unsupported WLM in use!\n");
            assert(0);
            return 1;
    }

    // call the common FE tests
    cti_test_fe(myapp);

    // cleanup
    cti_deregisterApp(myapp);

    // ensure deregister worked.
    assert(cti_appIsValid(myapp) == 0);

    return 0;
}
