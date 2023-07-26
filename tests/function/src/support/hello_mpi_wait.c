/*
 * Very simple hello-world using C and MPI
 *
 * Copyright 2019-2023 Hewlett Packard Enterprise Development LP.
 * SPDX-License-Identifier: Linux-OpenIB
*/

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "mpi.h"

int main(int argc, char* argv[]) {
    int myRank, numProcs;
    int source, dest=0, tag=0;
    char message[100];
    MPI_Status status;

    MPI_Init(&argc,&argv);

    MPI_Comm_rank(MPI_COMM_WORLD, &myRank);
    MPI_Comm_size(MPI_COMM_WORLD, &numProcs);

    if (myRank != 0) {
        sprintf(message,"Hello World! from process %d", myRank);
        MPI_Send(message, strlen(message)+1, MPI_CHAR, dest, tag, MPI_COMM_WORLD);
    } else {
        printf("Hello World! from process %d\n", myRank);
        for(source=1; source<numProcs; source++) {
            MPI_Recv(message, 100, MPI_CHAR, source, tag, MPI_COMM_WORLD, &status);
            printf("%s\n", message);
        }
    }

    sleep(120);

    MPI_Finalize();

    return 0;
}
