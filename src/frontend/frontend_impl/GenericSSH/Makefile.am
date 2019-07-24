#
# Makefile.am for the generic SSH frontend implementation.
#
# Copyright 2019 Cray Inc. All Rights Reserved.
#

SRC                 = @COMMONTOOL_DIR@/src
INCLUDE				= @COMMONTOOL_DIR@/include

@CODE_COVERAGE_RULES@

AM_LIBTOOLFLAGS     = --quiet
AM_CXXFLAGS         = -Wall -I$(SRC) -I$(SRC)/frontend -I$(SRC)/frontend/frontend_impl -I$(INCLUDE)

noinst_LTLIBRARIES = libgenericssh.la

libgenericssh_la_SOURCES    =   Frontend.cpp
libgenericssh_la_CXXFLAGS   =   $(AM_CXXFLAGS) -fPIC $(DYNINST_CXXFLAGS) $(BOOST_CXXFLAGS) $(SSH2_CFLAGS) $(CODE_COVERAGE_CXXFLAGS)
libgenericssh_la_CPPFLAGS	=	$(DYNINST_CPPFLAGS) $(BOOST_CPPFLAGS) $(CODE_COVERAGE_CPPFLAGS)
libgenericssh_la_LDFLAGS	=   -static -Wl,--no-undefined $(CODE_COVERAGE_LDFLAGS)
libgenericssh_la_LIBADD		=	$(CODE_COVERAGE_LIBS)
libgenericssh_la_CFLAGS		=	$(CODE_COVERAGE_CFLAGS)
noinst_HEADERS				=   Frontend.hpp
