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
