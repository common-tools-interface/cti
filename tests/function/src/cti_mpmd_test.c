/******************************************************************************\
 * Copyright 2012-2020 Hewlett Packard Enterprise Development LP.
 * SPDX-License-Identifier: Linux-OpenIB
 ******************************************************************************/

#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>

#include "common_tools_fe.h"
#include "cti_fe_common.h"

const struct option long_opts[] = {
    {"jobid",       required_argument,  0, 'j'},
    {"stepid",      required_argument,  0, 's'},
    {"apid",        required_argument,  0, 'a'},
    {"pid",         required_argument,  0, 'p'},
    {"help",        no_argument,        0, 'h'},
    {0, 0, 0, 0}
    };

void
usage(char *name)
{
    fprintf(stdout, "USAGE: %s [OPTIONS]...\n", name);

    fprintf(stdout, "\t-j, --jobid     Job id - SLURM WLM only (Flux does not support MPMD). Use with -s.\n");
    fprintf(stdout, "\t-s, --stepid    Step id - SLURM WLM only. Use with -j.\n");
    fprintf(stdout, "\t-a, --apid      Apid - ALPS and PALS WLM only.\n");
    fprintf(stdout, "\t-p, --pid       PID of launcher process - SSH WLM only.");
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
    int                 a_arg = 0;
    int                 p_arg = 0;
    uint64_t            job_id = 0;
    uint64_t            step_id = 0;
    uint64_t            apid = 0;
    char *              raw_apid = NULL;
    pid_t               launcher_pid = 0;
    // values returned by the tool_frontend library.
    cti_wlm_type_t      mywlm;
    cti_app_id_t        myapp = 0;

    if (argc < 2) {
        usage(argv[0]);
        assert(argc > 2);
        return 1;
    }

    // process longopts
    while ((c = getopt_long(argc, argv, "j:s:a:p:h", long_opts, &opt_ind)) != -1) {
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
                job_id = (uint64_t)strtol(optarg, &eptr, 10);

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
                step_id = (uint64_t)strtol(optarg, &eptr, 10);

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

            case 'a':
                if (optarg == NULL) {
                    usage(argv[0]);
                    assert(0);
                    return 1;
                }

                // This is the apid
                errno = 0;
                raw_apid = strdup(optarg);
                apid = (uint64_t)strtoull(raw_apid, &eptr, 10);

                // check for error
                if ((errno == ERANGE && apid == ULLONG_MAX)
                        || (errno != 0 && apid == 0)) {
                    perror("strtoull");
                    assert(0);
                    return 1;
                }

                // check for invalid input
                if (eptr == raw_apid || *eptr != '\0') {
                    fprintf(stderr, "Invalid --apid argument.\n");
                    assert(0);
                    return 1;
                }

                a_arg = 1;

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
        case CTI_WLM_SLURM:
        {
            if (j_arg == 0 || s_arg == 0) {
                fprintf(stderr, "Error: Missing --jobid and --stepid argument. This is required for the SLURM WLM.\n");
            }
            assert(j_arg != 0 && s_arg != 0);
            cti_slurm_ops_t * slurm_ops = NULL;
            cti_wlm_type_t ret = cti_open_ops((void **)&slurm_ops);
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

        case CTI_WLM_ALPS:
        {
            if (a_arg == 0 ) {
                fprintf(stderr, "Error: Missing --apid argument. This is required for the ALPS WLM.\n");
            }
            assert(a_arg != 0);

            cti_alps_ops_t * alps_ops = NULL;
            cti_wlm_type_t ret = cti_open_ops((void **)&alps_ops);
            assert(ret == mywlm);
            assert(alps_ops != NULL);
            myapp = alps_ops->registerApid(apid);
            if (myapp == 0) {
                fprintf(stderr, "Error: registerApid failed!\n");
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
            cti_ssh_ops_t * ssh_ops = NULL;
            cti_wlm_type_t ret = cti_open_ops((void **)&ssh_ops);
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

        case CTI_WLM_PALS:
        {
            if (a_arg == 0 ) {
                fprintf(stderr, "Error: Missing --apid argument. This is required for the PALS WLM.\n");
            }
            assert(a_arg != 0);
            cti_pals_ops_t * pals_ops = NULL;
            cti_wlm_type_t ret = cti_open_ops((void **)&pals_ops);
            assert(ret == mywlm);
            assert(pals_ops != NULL);
            myapp = pals_ops->registerApid(raw_apid);
            if (myapp == 0) {
                fprintf(stderr, "Error: PALS registerApid failed!\n");
                fprintf(stderr, "CTI error: %s\n", cti_error_str());
            }
            assert(myapp != 0);
        }
            break;

        case CTI_WLM_FLUX:
            fprintf(stderr, "Error: Flux WLM does not support MPMD\n");
            assert(0);
            return 1;

        case CTI_WLM_MOCK:
        case CTI_WLM_NONE:
            fprintf(stderr, "Error: Unsupported WLM in use!\n");
            assert(0);
            return 1;
    }

    // call the common FE tests
    cti_test_fe(myapp);

    // get mpmd info map
    cti_binaryList_t * binaryList = cti_getAppBinaryList(myapp);
    if (binaryList == NULL) {
        fprintf(stderr, "failed to get binary list: %s\n", cti_error_str());
        assert(0);
        return 1;
    }

    // print mpmd info map
    for (int i = 0; i < cti_getNumAppPEs(myapp); i++) {
        fprintf(stdout, "rank %3d: %s\n", i, binaryList->binaries[binaryList->rankMap[i]]);
    }

    // cleanup
    cti_destroyBinaryList(binaryList);
    cti_deregisterApp(myapp);
    if (raw_apid != NULL) {
        free(raw_apid);
        raw_apid = NULL;
    }

    // ensure deregister worked.
    assert(cti_appIsValid(myapp) == 0);

    return 0;
}
