#
# Makefile.am for the backend components of the common tools interface
#
# Copyright 2014-2019 Cray Inc. All Rights Reserved.
#

SUBDIRS = daemon

SRC				= @COMMONTOOL_DIR@/src
INCLUDE			= @COMMONTOOL_DIR@/include

AM_CFLAGS		= -Wall -I$(SRC) -I$(INCLUDE)
AM_LIBTOOLFLAGS = --quiet

lib_LTLIBRARIES = libcommontools_be.la

libcommontools_be_la_SOURCES	=	cti_be.c slurm_be.c pmi_attribs_parser.c generic_ssh_be.c \
									pmi_attribs_parser.h
libcommontools_be_la_CFLAGS		= $(AM_CFLAGS) -fPIC
libcommontools_be_la_LIBADD		= -ldl
libcommontools_be_la_LDFLAGS	= -Wl,--no-undefined -Wl,--as-needed -version-info $(COMMONTOOL_BE_VERSION)
