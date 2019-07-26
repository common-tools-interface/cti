/******************************************************************************\
 * cti_barrier_test.c - An example program which takes advantage of the Cray
 *          tools interface which will launch an application from the given
 *          argv, display information about the job, and hold it at the
 *          startup barrier.
 *
 * Copyright 2011-2019 Cray Inc. All Rights Reserved.
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

#include "common_tools_fe.h"
#include "cti_fe_common.h"

void
usage(char *name)
{
    fprintf(stdout, "USAGE: %s [LAUNCHER STRING]\n", name);
    fprintf(stdout, "Launch an application using the cti library\n");
    fprintf(stdout, "and print out information.\n");
    return;
}

int
main(int argc, char **argv)
{
    // values returned by the tool_frontend library.
    cti_app_id_t        myapp;
    int                 rtn;

    if (argc < 2) {
        usage(argv[0]);
        assert(argc > 2);
        return 1;
    }

    /*
     * cti_launchAppBarrier - Start an application using the application launcher
     *                        with the provided argv array and have the launcher
     *                        hold the application at its startup barrier for
     *                        MPI/SHMEM/UPC/CAF applications.
     */
    myapp = cti_launchAppBarrier((const char * const *)&argv[1],-1,-1,NULL,NULL,NULL);
    if (myapp == 0) {
        fprintf(stderr, "Error: cti_launchAppBarrier failed!\n");
        fprintf(stderr, "CTI error: %s\n", cti_error_str());
    }
    assert(myapp != 0);

    // call the common FE tests
    cti_test_fe(myapp);

    printf("\nHit return to release the application from the startup barrier...");

    // just read a single character from stdin then release the app/exit
    (void)getchar();

    /*
     * cti_releaseAppBarrier - Release the application launcher launched with the
     *                         cti_launchAppBarrier function from its startup
     *                         barrier.
     */
    rtn = cti_releaseAppBarrier(myapp);
    if (rtn) {
        fprintf(stderr, "Error: cti_releaseAppBarrier failed!\n");
        fprintf(stderr, "CTI error: %s\n", cti_error_str());
        cti_killApp(myapp, SIGKILL);
    }
    assert(rtn == 0);

    /*
     * cti_deregisterApp - Assists in cleaning up internal allocated memory
     *                     associated with a previously registered application.
     */
    cti_deregisterApp(myapp);

    // ensure deregister worked.
    assert(cti_appIsValid(myapp) == 0);

    return 0;
}
