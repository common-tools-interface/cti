# Copyright 2020 Hewlett Packard Enterprise Development LP.

EXECS = hello_mpi hello_mpi_wait mpi_wrapper

all: $(EXECS)

clean:
	rm -f $(EXECS)

CFLAGS := $(CFLAGS) -Wall -g -O0

hello_mpi : hello_mpi.c
	cc $(CFLAGS) $^ -o $@

hello_mpi_wait : hello_mpi_wait.c
	cc $(CFLAGS) $^ -o $@

mpi_wrapper : mpi_wrapper.c
	cc $(CFLAGS) $^ -o $@
