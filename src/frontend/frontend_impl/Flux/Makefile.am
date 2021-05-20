#
# Makefile.am for the Flux frontend implementation.
#
# Copyright 2020 Hewlett Packard Enterprise Development LP.
#

SRC             	= @COMMONTOOL_DIR@/src
INCLUDE				= @COMMONTOOL_DIR@/include

AM_LIBTOOLFLAGS 	= --quiet
AM_CXXFLAGS			= -Wall

noinst_LTLIBRARIES = libflux.la

libflux_la_SOURCES		= Frontend.cpp
libflux_la_CXXFLAGS	= -I$(SRC) -I$(SRC)/frontend -I$(SRC)/frontend/frontend_impl -I$(INCLUDE) -fPIC \
						$(CDST_SUP_CFLAGS) $(CODE_COVERAGE_CXXFLAGS) $(BOOST_CXXFLAGS) $(FLUX_CORE_CFLAGS) $(AM_CXXFLAGS)
libflux_la_CPPFLAGS	= $(CODE_COVERAGE_CPPFLAGS) $(BOOST_CPPFLAGS) $(AM_CPPFLAGS)
libflux_la_LDFLAGS		= -Wl,--no-undefined \
						$(CDST_SUP_LIBS) $(AM_LDFLAGS) -pthread
libflux_la_LIBADD		= $(CODE_COVERAGE_LIBS) $(BOOST_LIBS) -lpthread
noinst_HEADERS			= Frontend.hpp

if CODE_COVERAGE_ENABLED
clean-local: code-coverage-clean
distclean-local: code-coverage-dist-clean
endif
