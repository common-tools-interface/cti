#
# Makefile.am for the ALPS frontend implementation.
#
# Copyright 2019 Cray Inc. All Rights Reserved.
#

SRC             	= @COMMONTOOL_DIR@/src
INCLUDE				= @COMMONTOOL_DIR@/include

AM_LIBTOOLFLAGS 	= --quiet
AM_CXXFLAGS			= -Wall

noinst_LTLIBRARIES = libalps.la

libalps_la_SOURCES		= Frontend.cpp
libalps_la_CXXFLAGS	= -I$(SRC) -I$(SRC)/frontend -I$(SRC)/frontend/frontend_impl -I$(INCLUDE) -fPIC \
						$(CDST_SUP_CFLAGS) $(CODE_COVERAGE_CXXFLAGS) $(AM_CXXFLAGS) $(ALPS_CFLAGS)
libalps_la_CPPFLAGS	= $(CODE_COVERAGE_CPPFLAGS) $(AM_CPPFLAGS)
libalps_la_LDFLAGS		= -Wl,--no-undefined \
						$(CDST_SUP_LIBS) $(AM_LDFLAGS)
libalps_la_LIBADD		= $(CODE_COVERAGE_LIBS)
noinst_HEADERS			= Frontend.hpp

if CODE_COVERAGE_ENABLED
clean-local: code-coverage-clean
distclean-local: code-coverage-dist-clean
endif
