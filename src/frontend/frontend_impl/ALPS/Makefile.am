#
# Makefile.am for the ALPS frontend implementation.
#
# Copyright 2020 Hewlett Packard Enterprise Development LP.
# SPDX-License-Identifier: Linux-OpenIB
#

SRC             	= @COMMONTOOL_DIR@/src
INCLUDE				= @COMMONTOOL_DIR@/include

AM_LIBTOOLFLAGS 	= --quiet
AM_CXXFLAGS			= -Wall

noinst_LTLIBRARIES = libalps.la

libalps_la_SOURCES		= Frontend.cpp
libalps_la_CXXFLAGS	= -I$(SRC) -I$(SRC)/frontend -I$(SRC)/frontend/frontend_impl -I$(INCLUDE) -fPIC \
						$(CODE_COVERAGE_CXXFLAGS) $(AM_CXXFLAGS) -I$(LIBALPS_HEADERS) \
					$(BOOST_CFLAGS)
libalps_la_CPPFLAGS	= $(CODE_COVERAGE_CPPFLAGS) $(AM_CPPFLAGS) $(BOOST_CFLAGS)
libalps_la_LDFLAGS		= -Wl,--no-undefined \
						$(AM_LDFLAGS)
libalps_la_LIBADD		= $(CODE_COVERAGE_LIBS)
noinst_HEADERS			= Frontend.hpp

if CODE_COVERAGE_ENABLED
clean-local: code-coverage-clean
distclean-local: code-coverage-dist-clean
endif
