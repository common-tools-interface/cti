#
# Makefile.am for the PALS frontend implementation.
#
# Copyright 2019 Cray Inc. All Rights Reserved.
#

SRC             	= @COMMONTOOL_DIR@/src
INCLUDE				= @COMMONTOOL_DIR@/include

AM_LIBTOOLFLAGS 	= --quiet
AM_CXXFLAGS			= -Wall

noinst_LTLIBRARIES = libpals.la

libpals_la_SOURCES		= Frontend.cpp
libpals_la_CXXFLAGS	= -I$(SRC) -I$(SRC)/frontend -I$(SRC)/frontend/frontend_impl -I$(INCLUDE) -fPIC \
						$(CDST_SUP_CFLAGS) $(CODE_COVERAGE_CXXFLAGS) $(BOOST_CXXFLAGS) $(AM_CXXFLAGS)
libpals_la_CPPFLAGS	= $(CODE_COVERAGE_CPPFLAGS) $(BOOST_CPPFLAGS) $(AM_CPPFLAGS)
libpals_la_LDFLAGS		= -Wl,--no-undefined \
						$(CDST_SUP_LIBS) $(AM_LDFLAGS) -pthread
libpals_la_LIBADD		= $(CODE_COVERAGE_LIBS) $(BOOST_LIBS) -lpthread
noinst_HEADERS			= Frontend.hpp

if CODE_COVERAGE_ENABLED
clean-local: code-coverage-clean
distclean-local: code-coverage-dist-clean
endif