#
# Makefile.am for the generic SSH frontend implementation.
#
# Copyright 2019-2020 Hewlett Packard Enterprise Development LP.
# SPDX-License-Identifier: Linux-OpenIB
#

SRC                 = @COMMONTOOL_DIR@/src
INCLUDE				= @COMMONTOOL_DIR@/include

AM_LIBTOOLFLAGS     = --quiet
AM_CXXFLAGS         = -Wall

noinst_LTLIBRARIES = libgenericssh.la

libgenericssh_la_SOURCES    = Frontend.cpp
libgenericssh_la_CXXFLAGS   = -I$(SRC) -I$(SRC)/frontend -I$(SRC)/frontend/frontend_impl \
							-I$(INCLUDE) -fPIC 	$(MPIR_CFLAGS) $(LIBSSH2_CFLAGS) \
							$(CODE_COVERAGE_CXXFLAGS) $(AM_CXXFLAGS)
libgenericssh_la_CPPFLAGS	= $(CODE_COVERAGE_CPPFLAGS) $(AM_CPPFLAGS)
libgenericssh_la_LDFLAGS	= -Wl,--no-undefined \
							$(MPIR_LIBS) $(AM_LDFLAGS)
libgenericssh_la_LIBADD		= $(LIBSSH2_LIBS) $(CODE_COVERAGE_LIBS)
noinst_HEADERS				= Frontend.hpp

if CODE_COVERAGE_ENABLED
clean-local: code-coverage-clean
distclean-local: code-coverage-dist-clean
endif
