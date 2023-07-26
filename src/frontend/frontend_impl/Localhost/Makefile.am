#
# Makefile.am for the generic SSH frontend implementation.
#
# Copyright 2023 Hewlett Packard Enterprise Development LP.
# SPDX-License-Identifier: Linux-OpenIB
#

SRC                 = @COMMONTOOL_DIR@/src
INCLUDE				= @COMMONTOOL_DIR@/include

AM_LIBTOOLFLAGS     = --quiet
AM_CXXFLAGS         = -Wall

noinst_LTLIBRARIES = liblocalhost.la

liblocalhost_la_SOURCES    = Frontend.cpp
liblocalhost_la_CXXFLAGS   = -I$(SRC) -I$(SRC)/frontend -I$(SRC)/frontend/frontend_impl -I$(INCLUDE) -fPIC \
							$(CDST_SUP_CFLAGS) $(CODE_COVERAGE_CXXFLAGS) $(AM_CXXFLAGS)
liblocalhost_la_CPPFLAGS	= $(CODE_COVERAGE_CPPFLAGS) $(AM_CPPFLAGS)
liblocalhost_la_LDFLAGS	= -Wl,--no-undefined \
							$(CDST_SUP_LIBS) $(AM_LDFLAGS)
liblocalhost_la_LIBADD		= $(SSH2_LIBS) $(CODE_COVERAGE_LIBS)
noinst_HEADERS				= Frontend.hpp

if CODE_COVERAGE_ENABLED
clean-local: code-coverage-clean
distclean-local: code-coverage-dist-clean
endif
