#
# Makefile.am for the generic SSH frontend implementation.
#
# Copyright 2019 Cray Inc. All Rights Reserved.
#

SRC                 = @COMMONTOOL_DIR@/src
INCLUDE				= @COMMONTOOL_DIR@/include

@CODE_COVERAGE_RULES@

AM_LIBTOOLFLAGS     = --quiet
AM_CXXFLAGS         = -Wall

noinst_LTLIBRARIES = libgenericssh.la

libgenericssh_la_SOURCES    = Frontend.cpp
libgenericssh_la_CXXFLAGS   = -I$(SRC) -I$(SRC)/frontend -I$(SRC)/frontend/frontend_impl -I$(INCLUDE) -fPIC \
							$(CDST_SUP_CFLAGS) $(CODE_COVERAGE_CXXFLAGS) $(AM_CXXFLAGS)
libgenericssh_la_CPPFLAGS	= $(CODE_COVERAGE_CPPFLAGS) $(AM_CPPFLAGS)
libgenericssh_la_LDFLAGS	= -Wl,--no-undefined $(CODE_COVERAGE_LDFLAGS) \
							$(CDST_SUP_LIBS) $(AM_LDFLAGS)
libgenericssh_la_LIBADD		= $(SSH2_LIBS) $(CODE_COVERAGE_LIBS)
noinst_HEADERS				= Frontend.hpp
