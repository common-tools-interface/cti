#
# Makefile.am for the useful components of the common tools interface
#
# Copyright 2014-2019 Cray Inc. All Rights Reserved.
#

SUBDIRS			= ld_val

SRC             = @COMMONTOOL_DIR@/src
INCLUDE			= @COMMONTOOL_DIR@/include

AM_LIBTOOLFLAGS = --quiet
AM_CFLAGS		= -Wall -I$(SRC) -I. -I$(INCLUDE)

@CODE_COVERAGE_RULES@

noinst_LTLIBRARIES      = libuseful.la

libuseful_la_SOURCES    = 	cti_log.c cti_path.c cti_stack.c
libuseful_la_CFLAGS     = 	$(AM_CFLAGS) -fPIC $(CODE_COVERAGE_CFLAGS)
libuseful_la_CXXFLAGS   = 	$(AM_CXXFLAGS) $(libuseful_la_CFLAGS) $(CODE_COVERAGE_CXXFLAGS)
libuseful_la_CPPFLAGS	=	$(CODE_COVERAGE_CPPFLAGS)
libuseful_la_LIBADD		=	ld_val/libld_val.la $(CODE_COVERAGE_LIBS)
libuseful_la_LDFLAGS    = 	-static -Wl,--no-undefined $(CODE_COVERAGE_LDFLAGS)

noinst_HEADERS			= 	cti_argv.hpp cti_log.h cti_path.h cti_split.hpp \
							cti_stack.h cti_wrappers.hpp cti_dlopen.hpp \
							cti_execvp.hpp cti_useful.h
