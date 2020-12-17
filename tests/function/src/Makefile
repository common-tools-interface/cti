# Copyright 2020 Hewlett Packard Enterprise Development LP.

EXECS = hello_mpi hello_mpi_wait mpi_wrapper

all: $(EXECS)

clean:
	rm -f $(EXECS)

CFLAGS = -Wall -g -O0

hello_mpi : hello_mpi.c
	cc $(CLAGS) $^ -o $@

hello_mpi_wait : hello_mpi_wait.c
	cc $(CLAGS) $^ -o $@

mpi_wrapper : mpi_wrapper.c
	cc $(CLAGS) $^ -o $@
