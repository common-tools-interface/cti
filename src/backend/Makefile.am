#
# Makefile.am for the backend components of the common tools interface
#
# Copyright 2014-2023 Hewlett Packard Enterprise Development LP.
# SPDX-License-Identifier: Linux-OpenIB
#

SUBDIRS = daemon

SRC				= @COMMONTOOL_DIR@/src
INCLUDE			= @COMMONTOOL_DIR@/include

AM_CFLAGS		= -Wall
AM_LIBTOOLFLAGS = --quiet

lib_LTLIBRARIES = libcommontools_be.la

libcommontools_be_la_SOURCES	= cti_be.c slurm_be.c pmi_attribs_parser.c generic_ssh_be.c \
					localhost_be.c pmi_attribs_parser.h
if ENABLE_ALPS
libcommontools_be_la_SOURCES += alps_be.c
endif
if ENABLE_PALS
libcommontools_be_la_SOURCES += pals_be.c
endif
if ENABLE_FLUX
libcommontools_be_la_SOURCES += flux_be.c
endif

libcommontools_be_la_CFLAGS		= -I$(SRC) -I$(INCLUDE) -fPIC $(CODE_COVERAGE_CFLAGS) $(AM_CFLAGS) -I$(LIBALPS_HEADERS)

libcommontools_be_la_LIBADD		= -ldl $(CODE_COVERAGE_LIBS)
libcommontools_be_la_LDFLAGS	= -shared -Wl,--no-undefined -Wl,--as-needed \
								-version-info $(COMMONTOOL_BE_VERSION) \
								$(AM_LDFLAGS)
libcommontools_be_la_CPPFLAGS	= $(CODE_COVERAGE_CPPFLAGS) $(AM_CPPFLAGS)
noinst_HEADERS					= cti_be.h

if CODE_COVERAGE_ENABLED
clean-local: code-coverage-clean
distclean-local: code-coverage-dist-clean
endif
