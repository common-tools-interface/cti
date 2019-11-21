/******************************************************************************\
 * cti_kill_test.c - An example program which takes advantage of the common
 *          tools interface which will kill an application from the given
 *          argv and display information about the job
 *
 * Copyright 2015-2019 Cray Inc. All Rights Reserved.
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
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <limits.h>

#include "common_tools_fe.h"
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
    fprintf(stdout, "kill an application using the common tools interface.\n\n");
    fprintf(stdout, "\t-j, --jobid     slurm job id - SLURM WLM only. Use with -s.\n");
    fprintf(stdout, "\t-s, --stepid    slurm step id - SLURM WLM only. Use with -j.\n");
    fprintf(stdout, "\t-p, --pid       pid of launcher process - SSH WLM only.");
    fprintf(stdout, "\t-h, --help      Display this text and exit\n\n");

    return;
}

int
main(int argc, char **argv)
{
    // values returned by the tool_frontend library.
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
    cti_app_id_t        myapp = 0;

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
        case CTI_WLM_SLURM:
        {
            if (j_arg == 0 || s_arg == 0) {
                fprintf(stderr, "Error: Missing --jobid and --stepid argument. This is required for the SLURM WLM.\n");
            }
            assert(j_arg != 0 && s_arg != 0);
            cti_slurm_ops_t * slurm_ops;
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

        case CTI_WLM_SSH:
        {
            if (p_arg == 0) {
                fprintf(stderr, "Error: Missing --pid argument. This is required for the generic WLM.\n");
            } else {
                fprintf(stdout, "generic WLM: --pid argument %d.\n", p_arg);
            }
            assert(p_arg != 0);
            cti_ssh_ops_t * ssh_ops;
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

        case CTI_WLM_MOCK:
        case CTI_WLM_NONE:
            fprintf(stderr, "Error: Unsupported WLM in use!\n");
            assert(0);
            return 1;
    }

    /*
     * cti_killApp - Send signal = 0, using the appropriate launcher kill
     * mechanism to an application launcher.  According to the man page for
     * kill(2), "If sig is 0, then no signal is sent, but error checking is
     * still performed; this can be used to check for the existence of a
     * process ID or process group ID."
     */
    /*
     * cti_killApp - Kill an application using the application killer
     *                 with the provided argv array.
     */
    if (cti_killApp(myapp, 0)) { //if cti_killApp passes when passing signum=0, means the apps are still there.
        fprintf(stderr, "Error: cti_killApp(0) failed!\n");
        fprintf(stderr, "CTI error: %s\n", cti_error_str());
    } else {
        fprintf(stdout, "cti_killApp(0) passed!\n");
    }
    sleep(10);
    /*
    if (cti_killApp(myapp, SIGTERM)) {
        fprintf(stderr, "Error: cti_killApp(SIGTERM) failed!\n");
        fprintf(stderr, "CTI error: %s\n", cti_error_str());
    } else {
        fprintf(stdout, "cti_killApp(SIGTERM) passed!\n");
    }
    sleep(10);
    */
    if (cti_killApp(myapp, SIGKILL)) {
        fprintf(stderr, "Error: cti_killApp(SIGKILL) failed!\n");
        fprintf(stderr, "CTI error: %s\n", cti_error_str());
    } else {
        fprintf(stdout, "cti_killApp(SIGKILL) passed!\n");
    }
    sleep(10);
    if (cti_killApp(myapp, 0)) { //if cti_killApp failes when passing signum=0, means the apps have been killed.
        fprintf(stderr, "Error: cti_killApp(0) failed!\n");
        fprintf(stderr, "CTI error: %s\n", cti_error_str());
    } else {
        fprintf(stdout, "cti_killApp(0) passed!\n");
    }
    sleep(10);

    /*
     * cti_deregisterApp - Assists in cleaning up internal allocated memory
     *                     associated with a previously registered application.
     */
    cti_deregisterApp(myapp);

    // ensure deregister worked.
    assert(cti_appIsValid(myapp) == 0);

    return 0;
}

