#
# Makefile.am for the useful components of the common tools interface
#
# Copyright 2014-2019 Cray Inc. All Rights Reserved.
#

SUBDIRS			= ld_val

SRC             = @COMMONTOOL_DIR@/src
INCLUDE			= @COMMONTOOL_DIR@/include

AM_LIBTOOLFLAGS = --quiet
AM_CFLAGS		= -Wall

noinst_LTLIBRARIES      = libuseful.la

libuseful_la_SOURCES    = 	cti_log.c cti_path.c cti_stack.c
libuseful_la_CFLAGS     = 	-I$(SRC) -I. -I$(INCLUDE) -fPIC \
							$(CODE_COVERAGE_CFLAGS) $(AM_CFLAGS)
libuseful_la_CPPFLAGS	=	$(CODE_COVERAGE_CPPFLAGS) $(AM_CPPFLAGS)
libuseful_la_LIBADD		=	ld_val/libld_val.la $(CODE_COVERAGE_LIBS)
libuseful_la_LDFLAGS    = 	-Wl,--no-undefined $(AM_LDFLAGS)

noinst_HEADERS			= 	cti_argv.hpp cti_dlopen.hpp cti_execvp.hpp \
							cti_log.h cti_path.h cti_split.hpp \
							cti_stack.h cti_useful.h cti_wrappers.hpp
