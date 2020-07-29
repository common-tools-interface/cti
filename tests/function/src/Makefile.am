#
# Makefile.am for the test utilities
#
# Copyright 2011-2020 Cray Inc. All Rights Reserved.
#

AM_CFLAGS		= -Wall
AM_LIBTOOLFLAGS	= --quiet
AUTOMAKE_OPTIONS = subdir-objects

noinst_PROGRAMS	= hello_mpi hello_mpi_wait mpi_wrapper

hello_mpi_CFLAGS			= $(AM_CFLAGS)
hello_mpi_SOURCES			= hello_mpi.c

hello_mpi_wait_CFLAGS		= $(AM_CFLAGS)
hello_mpi_wait_SOURCES		= hello_mpi_wait.c

mpi_wrapper_CFLAGS			= $(AM_CFLAGS)
mpi_wrapper_SOURCES			= mpi_wrapper.c
