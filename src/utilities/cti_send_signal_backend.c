/******************************************************************************\
 *
 * Copyright 2022 Hewlett Packard Enterprise Development LP.
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
