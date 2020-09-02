/*
 * mpi_wrapper
 *
 * launch program passed on command line wrapped in mpi functionality
 *
 * useful for alps systems where launchAppBarrier only works on mpi apps
 *
 * Copyright 2019-2020 Hewlett Packard Enterprise Development LP.
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
 */

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <assert.h>
#include <stdio.h>
#include <errno.h>

#include "mpi.h"

// need to use environ instead of envp from main since MPI_Init corrupts envp
// on some systems
extern char ** environ;

void usage(char* argv[]) {
    fprintf(stderr, "./%s <program>\n", argv[0]);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        usage(argv);
        assert(0);
        return 1;
    }

    MPI_Init(&argc,&argv);

    pid_t pid = fork();

    if (pid == 0) {
        errno = 0;
        execve(argv[1], &argv[1], environ);
        perror("execve failed: ");
    } else {
        wait(0);
    }

    MPI_Finalize();

    return 0;
}
