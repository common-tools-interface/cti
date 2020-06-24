/*
 * mpi_wrapper
 *
 * launch program passed on command line wrapped in mpi functionality
 *
 * useful for alps systems where launchAppBarrier only works on mpi apps
 */

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <assert.h>
#include <stdio.h>

#include "mpi.h"

void usage() {
    printf("./mpi_wrapper <program>");
}

int main(int argc, char *argv[], char *envp[]) {
    if (argc < 2) {
        usage();
        assert(0);
        return 1;
    }

    MPI_Init(&argc,&argv);

    pid_t pid;

    pid = fork();

    if (pid == 0) {
        execve(argv[1], &argv[1], envp);
    } else {
        wait(0);
    }

    MPI_Finalize();

    return 0;
}
