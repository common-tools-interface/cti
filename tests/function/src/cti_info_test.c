/******************************************************************************\
 * cti_info_test.c - An example program which takes advantage of the common
 *          tools interface which will gather information from the WLM about a
 *          previously launched job.
 *
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
    fprintf(stdout, "Gather information about a previously launched application\n");
    fprintf(stdout, "using the common tools interface.\n\n");

    fprintf(stdout, "\t-j, --jobid     Job ID - Slurm & Flux WLM only. For Slurm, use with -s.\n");
    fprintf(stdout, "\t-s, --stepid    Step ID - Slurm WLM only. Use with -j.\n");
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
    uint64_t            slurm_job_id = 0;
    uint64_t            slurm_step_id = 0;
    uint64_t            alps_apid = 0;
    char *              pals_apid = NULL;
    char *              flux_job_id = NULL;
    pid_t               launcher_pid = 0;
    // values returned by the tool_frontend library.
    cti_wlm_type_t      mywlm;
    cti_app_id_t        myapp = 0;

    if (argc < 2) {
        usage(argv[0]);
        assert(argc > 2);
        return 1;
    }

    for (int i = 0; i < argc; i++) {
        printf("\"%s\" ", argv[i]);
    }
    printf("\n");

    /*
     * cti_current_wlm - Obtain the current workload manager (WLM) in use on the
     *                   system.
     */
    mywlm = cti_current_wlm();

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

                if (mywlm == CTI_WLM_SLURM) {

                    // This is the job id
                    errno = 0;
                    slurm_job_id = (uint64_t)strtol(optarg, &eptr, 10);

                    // check for error
                    if ((errno == ERANGE && slurm_job_id == ULONG_MAX)
                            || (errno != 0 && slurm_job_id == 0)) {
                        perror("strtol");
                        assert(0);
                        return 1;
                    }

                    // check for invalid input
                    if (eptr == optarg || *eptr != '\0') {
                        fprintf(stderr, "Invalid --jobid argument for Slurm WLM (expecting numeric).\n");
                        assert(0);
                        return 1;
                    }

                } else if (mywlm == CTI_WLM_FLUX) {

                    // Job ID is character string
                    flux_job_id = strdup(optarg);

                } else {
                    fprintf(stderr, "Invalid parameter --jobid for WLM %s\n", cti_wlm_type_toString(mywlm));
                    assert(0);
                    return 1;
                }

                break;

            case 's':
                if (optarg == NULL) {
                    usage(argv[0]);
                    assert(0);
                    return 1;
                }

                if (mywlm == CTI_WLM_SLURM) {

                    // This is the step id
                    errno = 0;
                    slurm_step_id = (uint64_t)strtol(optarg, &eptr, 10);

                    // check for error
                    if ((errno == ERANGE && slurm_step_id == ULONG_MAX)
                            || (errno != 0 && slurm_step_id == 0)) {
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

                } else {
                    fprintf(stderr, "Invalid parameter --stepid for WLM %s\n", cti_wlm_type_toString(mywlm));
                    assert(0);
                    return 1;
                }


                break;

            case 'a':
                if (optarg == NULL) {
                    usage(argv[0]);
                    assert(0);
                    return 1;
                }

                if (mywlm == CTI_WLM_ALPS) {

                    // This is the apid
                    errno = 0;
                    alps_apid = (uint64_t)strtoull(optarg, &eptr, 10);

                    // check for error
                    if ((errno == ERANGE && alps_apid == ULONG_MAX)
                            || (errno != 0 && alps_apid == 0)) {
                        perror("strtol");
                        assert(0);
                        return 1;
                    }

                    // check for invalid input
                    if (eptr == optarg || *eptr != '\0') {
                        fprintf(stderr, "Invalid --apid argument.\n");
                        assert(0);
                        return 1;
                    }

                } else if (mywlm == CTI_WLM_PALS) {

                    pals_apid = strdup(optarg);

                } else {
                    fprintf(stderr, "Invalid parameter --jobid for WLM %s\n", cti_wlm_type_toString(mywlm));
                    assert(0);
                    return 1;
                }

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
                if ((errno == ERANGE && launcher_pid == ULONG_MAX)
                        || (errno != 0 && launcher_pid == 0)) {
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

    // Register application with CTI
    switch (mywlm) {
        case CTI_WLM_SLURM:
        {
            if (slurm_job_id == 0) {
                fprintf(stderr, "Error: Missing --jobid and --stepid argument. This is required for the SLURM WLM.\n");
            }
            cti_slurm_ops_t * slurm_ops = NULL;
            cti_wlm_type_t ret = cti_open_ops((void **)&slurm_ops);
            assert(ret == mywlm);
            assert(slurm_ops != NULL);
            myapp = slurm_ops->registerJobStep(slurm_job_id, slurm_step_id);
            if (myapp == 0) {
                fprintf(stderr, "Error: registerJobStep failed!\n");
                fprintf(stderr, "CTI error: %s\n", cti_error_str());
            }
            assert(myapp != 0);
        }
            break;

        case CTI_WLM_ALPS:
        {
            if (alps_apid == 0) {
                fprintf(stderr, "Error: Missing --apid argument. This is required for the ALPS WLM.\n");
            }
            cti_alps_ops_t * alps_ops = NULL;
            cti_wlm_type_t ret = cti_open_ops((void **)&alps_ops);
            assert(ret == mywlm);
            assert(alps_ops != NULL);
            myapp = alps_ops->registerApid(alps_apid);
            if (myapp == 0) {
                fprintf(stderr, "Error: registerApid failed!\n");
                fprintf(stderr, "CTI error: %s\n", cti_error_str());
            }
            assert(myapp != 0);
        }
            break;

        case CTI_WLM_SSH:
        {
            if (launcher_pid == 0) {
                fprintf(stderr, "Error: Missing --pid argument. This is required for the generic WLM.\n");
            }
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
            if (pals_apid == NULL) {
                fprintf(stderr, "Error: Missing --apid argument. This is required for the PALS WLM.\n");
            }
            cti_pals_ops_t * pals_ops = NULL;
            cti_wlm_type_t ret = cti_open_ops((void **)&pals_ops);
            assert(ret == mywlm);
            assert(pals_ops != NULL);
            myapp = pals_ops->registerApid(pals_apid);
            if (myapp == 0) {
                fprintf(stderr, "Error: registerApid failed!\n");
                fprintf(stderr, "CTI error: %s\n", cti_error_str());
            }
            assert(myapp != 0);
        }
            break;

        case CTI_WLM_FLUX:
        {
            if (flux_job_id == NULL) {
                fprintf(stderr, "Error: Missing --jobid argument. This is required for the Flux WLM.\n");
            }
            cti_flux_ops_t * flux_ops = NULL;
            cti_wlm_type_t ret = cti_open_ops((void **)&flux_ops);
            assert(ret == mywlm);
            assert(flux_ops != NULL);
            myapp = flux_ops->registerJob(flux_job_id);
            if (myapp == 0) {
                fprintf(stderr, "Error: registerJob failed!\n");
                fprintf(stderr, "CTI error: %s\n", cti_error_str());
            }
            assert(myapp != 0);
        }
            break;

        case CTI_WLM_MOCK:
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
