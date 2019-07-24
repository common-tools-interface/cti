#
# Makefile.am for the backend components of the craytool interface.
#
# Copyright 2014-2019 Cray Inc.  All Rights Reserved.
#

SUBDIRS = daemon

SRC				= @CRAYTOOL_DIR@/src
INCLUDE			= @CRAYTOOL_DIR@/include

AM_CFLAGS		= -Wall -I$(SRC) -I$(INCLUDE)
AM_LIBTOOLFLAGS = --quiet

@CODE_COVERAGE_RULES@

lib_LTLIBRARIES = libcraytools_be.la

libcraytools_be_la_SOURCES  =	cti_be.c cray_slurm_be.c \
								pmi_attribs_parser.c slurm_be.c
libcraytools_be_la_CFLAGS   =   $(AM_CFLAGS) -fPIC $(CODE_COVERAGE_CFLAGS)
libcraytools_be_la_LIBADD   =	-ldl $(CODE_COVERAGE_LIBS)
libcraytools_be_la_LDFLAGS  =   -Wl,--no-undefined -Wl,--as-needed -version-info $(CRAYTOOL_BE_VERSION) $(CODE_COVERAGE_LDFLAGS)
libcraytools_be_la_CPPFLAGS =   $(CODE_COVERAGE_CPPFLAGS)
libcraytoosl_be_la_CXXFLAGS =   $(CODE_COVERAGE_CXXFLAGS)

clean-local: code-coverage-clean
distclean-local: code-coverage-dist-clean
