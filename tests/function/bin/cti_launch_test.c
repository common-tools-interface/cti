/******************************************************************************\
 * cti_launch_test.c - An example program which takes advantage of the common
 *          tools interface which will launch an application from the given
 *          argv and display information about the job
 *
 * Copyright 2015-2020 Hewlett Packard Enterprise Development LP.
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

    if (argc < 2) {
        usage(argv[0]);
        assert(argc > 2);
        return 1;
    }

    /*
     * cti_launchApp - Start an application using the application launcher
     *                 with the provided argv array.
     */
    myapp = cti_launchApp((const char * const *)&argv[1],-1,-1,NULL,NULL,NULL);
    if (myapp == 0) {
        fprintf(stderr, "Error: cti_launchApp failed!\n");
        fprintf(stderr, "CTI error: %s\n", cti_error_str());
    }
    assert(myapp != 0);

    assert(cti_appIsValid(myapp) == 1);

    // call the common FE tests
    cti_test_fe(myapp);

    // give the app time to do its thing before it's killed at deregister
    sleep(5);

    /*
     * cti_deregisterApp - Assists in cleaning up internal allocated memory
     *                     associated with a previously registered application.
     */
    cti_deregisterApp(myapp);

    // ensure deregister worked.
    assert(cti_appIsValid(myapp) == 0);

    return 0;
}
