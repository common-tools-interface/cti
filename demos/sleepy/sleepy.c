#include "mpi.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

main(int argc, char **argv)
{
	int	i, rank, seconds;

	MPI_Init(&argc, &argv);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	if (argc == 2)
		seconds = atoi(argv[1]);
	else
		seconds = 9000;

	printf("Sleeping %d seconds...\n", seconds);
	for (i = seconds; i > 0; --i)
	  sleep(1);
	MPI_Finalize();
	printf("...done sleeping\n");
}
