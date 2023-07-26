/******************************************************************************\
 * cti_kill_test.c - An example program which takes advantage of the common
 *          tools interface which will launch an application, display info
 *          about the job, then send a sigterm to it.
 *
 * Copyright 2015-2023 Hewlett Packard Enterprise Development LP.
 * SPDX-License-Identifier: Linux-OpenIB
 ******************************************************************************/

#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

#include "common_tools_fe.h"
#include "cti_fe_common.h"
#include "cti_fe_function_test.hpp"

void
usage(char *name)
{
    fprintf(stdout, "USAGE: %s [LAUNCHER STRING] [SIGNAL]\n", name);
    fprintf(stdout, "Launch and then kill an application using the cti library\n");
    fprintf(stdout, "and print out information.\n");
    return;
}

void testSIGCONT(cti_app_id_t myapp) {
    // stop the app
    auto r = cti_killApp(myapp, SIGSTOP);
    if (r) {
        fprintf(stderr, "Error: cti_killApp(SIGSTOP) failed!\n");
        fprintf(stderr, "CTI error: %s\n", cti_error_str());
    }
    assert_true(r == 0, "cti_killApp(SIGSTOP) failed");

    // if the signal was delivered, the app will not respond to SIGINT. SIGINT will be
    // queued to be handled when the app wakes up.
    r = cti_killApp(myapp, SIGINT);
    if (r) {
        fprintf(stderr, "Error: cti_killApp(SIGINT) failed!\n");
        fprintf(stderr, "CTI error: %s\n", cti_error_str());
        // attempt to clean up as to not clog the test allocation
        cti_killApp(myapp, SIGKILL);
    }
    assert_true(r == 0, "cti_killApp(SIGINT) failed\n");

    // check (approximately) that the app successfully blocked the SIGINT
    for (int seconds_waited = 0; cti_appIsValid(myapp) && seconds_waited < 5; seconds_waited++) sleep(1);
    if (!cti_appIsValid(myapp)) {
        fprintf(stderr, "Error: cti_appIsValid reports false, app didn't block SIGINT?\n");
    }
    assert_true(cti_appIsValid(myapp), "cti_appIsValid returned false");

    // now send SIGCONT. the app should start again and immediately receive the SIGINT
    r = cti_killApp(myapp, SIGCONT);
    if (r) {
        fprintf(stderr, "Error: cti_killApp(SIGCONT) failed!\n");
        fprintf(stderr, "CTI error: %s\n", cti_error_str());
        // attempt to clean up as to not clog the test allocation
        cti_killApp(myapp, SIGKILL);
    }
    assert_true(r == 0, "cti_killApp(SIGCONT) failed\n");
}

// test that cti returns an error on all platforms for signal 0
void testSIGZERO(cti_app_id_t myapp) {
    auto r = cti_killApp(myapp, 0);
    if (!r) {
        fprintf(stderr, "Error: cti_killApp(0) did not report an error!\n");
    }
    // attempt to clean up as to not clog the test allocation
    cti_killApp(myapp, SIGKILL);
    assert_true(r != 0, "cti_killApp(0) /didn't/ fail");
}

// Not limited to SIGKILL, any signal that should terminate the job, like SIGINT and SIGTERM
void testKillSignal(cti_app_id_t myapp, int signal) {
    auto r = cti_killApp(myapp, signal);
    if (r) {
        fprintf(stderr, "Error: cti_killApp(%d) failed!\n", signal);
        fprintf(stderr, "CTI error: %s\n", cti_error_str());
    }
    assert_true(r == 0, std::string{"cti_killApp("} + std::to_string(signal) + ") failed");

    // check that the job actually died.
    // wait up to 20 seconds for the wlm to react
    for (int seconds_waited = 0; cti_appIsValid(myapp) && seconds_waited < 20; seconds_waited++) sleep(1);
    if (cti_appIsValid(myapp)) {
        fprintf(stderr, "Error: cti_appIsValid reports true after kill signal %d\n", signal);
    }
    assert_true(!cti_appIsValid(myapp), "cti_appIsValid is still true after waiting");
}

int main(int argc, char **argv) {
    // values returned by the tool_frontend library.
    cti_app_id_t        myapp;

    if (argc < 2) {
        usage(argv[0]);
        assert_true(argc > 2, "argc not > 2");
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
    assert_true(myapp != 0, "cti_launchApp failed");

    // call the common FE tests
    cti_test_fe(myapp);

    int signal = SIGTERM;
    signal = atoi(argv[argc-1]);

    try {
        struct Cleanup {
            Cleanup(cti_app_id_t appId) : m_id{appId} {}
            ~Cleanup() {cti_killApp(m_id, SIGKILL);}
            cti_app_id_t m_id;
        };
        auto cleanup = Cleanup(myapp);

        switch (signal) {
        case SIGCONT:
            reportTime("testSIGCONT", [&](){testSIGCONT(myapp);});
            break;
        case 0:
            reportTime("testSIGZERO", [&](){testSIGZERO(myapp);});
            break;
        default:
            // for all other signals, expect the app to die immediately
            reportTime(std::string{"test a job-ending signal "} + std::to_string(signal), [&]() {
                testKillSignal(myapp, signal);
            });
            break;
        }
    } catch (...) {
        // catch exceptions to ensure destructors run
        throw;
    }

    /*
     * cti_deregisterApp - Assists in cleaning up internal allocated memory
     *                     associated with a previously registered application.
     */
    cti_deregisterApp(myapp);

    // ensure deregister worked.
    assert_true(cti_appIsValid(myapp) == 0, "cti_appIsValid returned true after deregistering");

    return 0;
}
