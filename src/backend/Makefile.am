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

@CODE_COVERAGE_RULES@

lib_LTLIBRARIES = libcommontools_be.la

libcommontools_be_la_SOURCES	=	cti_be.c slurm_be.c pmi_attribs_parser.c generic_ssh_be.c \
									pmi_attribs_parser.h
libcommontools_be_la_CFLAGS		= $(AM_CFLAGS) -fPIC $(CODE_COVERAGE_CFLAGS)
libcommontools_be_la_LIBADD		= -ldl $(CODE_COVERAGE_LIBS)
libcommontools_be_la_LDFLAGS	= -Wl,--no-undefined -Wl,--as-needed -version-info $(COMMONTOOL_BE_VERSION) $(CODE_COVERAGE_LDFLAGS)
libcommontools_be_la_CPPFLAGS =   $(CODE_COVERAGE_CPPFLAGS)
libcommontools_be_la_CXXFLAGS =   $(CODE_COVERAGE_CXXFLAGS)

clean-local: code-coverage-clean
distclean-local: code-coverage-dist-clean
