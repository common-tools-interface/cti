/******************************************************************************\
 *
 * Copyright 2022 Hewlett Packard Enterprise Development LP.
 * SPDX-License-Identifier: Linux-OpenIB
 ******************************************************************************/

#include <stdlib.h>
#include <signal.h>

#include "common_tools_be.h"

int main(int argc, char* argv[]) {
    if (argc != 2) {
        return -1;
    }

    // parse signal
    int signal = strtol(argv[1], NULL, 10);
    if (signal <= 0 || signal > 64) {
        return -1;
    }

    // get node pids
    cti_pidList_t* app_pids = cti_be_findAppPids();

    if (!app_pids) {
        return -1;
    }

    // send the specified signal to each pid
    int failed = 0;

    for (int i = 0; i < app_pids->numPids; i++) {
        if (kill(app_pids->pids[i].pid, signal)) {
            failed = 1;
        }
    }

    // clean up
    cti_be_destroyPidList(app_pids);

    return failed;
}
