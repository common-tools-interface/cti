#
# Makefile.am for the backend components of the craytool interface.
#
# Copyright 2014-2019 Cray Inc. All Rights Reserved.
#

SUBDIRS = daemon

SRC				= @CRAYTOOL_DIR@/src
INCLUDE			= @CRAYTOOL_DIR@/include

AM_CFLAGS		= -Wall -I$(SRC) -I$(INCLUDE)
AM_LIBTOOLFLAGS = --quiet

lib_LTLIBRARIES = libcraytools_be.la

libcraytools_be_la_SOURCES  =	cti_be.c cray_slurm_be.c \
								pmi_attribs_parser.c slurm_be.c
libcraytools_be_la_CFLAGS   =   $(AM_CFLAGS) -fPIC
libcraytools_be_la_LIBADD	=	-ldl
libcraytools_be_la_LDFLAGS  =   -Wl,--no-undefined -Wl,--as-needed -version-info $(CRAYTOOL_BE_VERSION)
