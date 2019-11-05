#
# Makefile.am for the MPIR interface components of the common tools interface
#
# Copyright 2018-2019 Cray Inc. All Rights Reserved.
#

SRC             = @COMMONTOOL_DIR@/src
INCLUDE			= @COMMONTOOL_DIR@/include

AM_CXXFLAGS		= -Wall
AM_LIBTOOLFLAGS = --quiet

noinst_LTLIBRARIES      	= libmpir_iface.la

libmpir_iface_la_SOURCES	= MPIRInstance.cpp Inferior.cpp
libmpir_iface_la_CXXFLAGS	= -I$(SRC) -I$(INCLUDE) -fPIC \
							$(CDST_SUP_CFLAGS) $(CODE_COVERAGE_CXXFLAGS) $(AM_CXXFLAGS)
libmpir_iface_la_CPPFLAGS	= $(CODE_COVERAGE_CPPFLAGS) $(AM_CPPFLAGS)
libmpir_iface_la_LDFLAGS	= -Wl,--no-undefined \
							$(CDST_SUP_LIBS) $(AM_LDFLAGS)
libmpir_iface_la_LIBADD		= $(DYNINST_LIBS) $(CODE_COVERAGE_LIBS)
noinst_HEADERS				= Inferior.hpp MPIRInstance.hpp MPIRProctable.hpp
