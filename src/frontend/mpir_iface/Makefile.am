#
# Makefile.am for the MPIR interface components of the common tools interface
#
# Copyright 2018-2020 Hewlett Packard Enterprise Development LP.
#

SRC             = @COMMONTOOL_DIR@/src
INCLUDE			= @COMMONTOOL_DIR@/include

AM_CXXFLAGS		= -Wall
AM_LIBTOOLFLAGS = --quiet

noinst_LTLIBRARIES      	= libmpir_iface.la

libmpir_iface_la_SOURCES	= MPIRInstance.cpp Inferior.cpp
libmpir_iface_la_CXXFLAGS	= -I$(SRC) -I$(INCLUDE) -fPIC \
							$(MPIR_CFLAGS) $(CODE_COVERAGE_CXXFLAGS) $(AM_CXXFLAGS)
libmpir_iface_la_CPPFLAGS	= $(CODE_COVERAGE_CPPFLAGS) $(AM_CPPFLAGS)
libmpir_iface_la_LDFLAGS	= -Wl,--no-undefined $(AM_LDFLAGS)
libmpir_iface_la_LIBADD		= $(MPIR_LIBS) $(CODE_COVERAGE_LIBS)
noinst_HEADERS				= Inferior.hpp MPIRInstance.hpp MPIRProctable.hpp

if CODE_COVERAGE_ENABLED
clean-local: code-coverage-clean
distclean-local: code-coverage-dist-clean
endif
