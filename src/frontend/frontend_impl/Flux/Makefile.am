#
# Makefile.am for the Flux frontend implementation.
#
# Copyright 2021 Hewlett Packard Enterprise Development LP.
# SPDX-License-Identifier: Linux-OpenIB
#

BUILT_SOURCES = LibFlux.hpp LibFluxCompat.hpp
SRC             	= @COMMONTOOL_DIR@/src
INCLUDE				= @COMMONTOOL_DIR@/include

AM_LIBTOOLFLAGS 	= --quiet
AM_CXXFLAGS			= -Wall

noinst_LTLIBRARIES = libflux.la

libflux_la_SOURCES		= Frontend.cpp LibFlux.hpp ssh.hpp
libflux_la_CXXFLAGS	= -I$(SRC) -I$(SRC)/frontend -I$(SRC)/frontend/frontend_impl -I$(INCLUDE) -fPIC \
	$(CODE_COVERAGE_CXXFLAGS) $(FLUX_CORE_CFLAGS) $(AM_CXXFLAGS) $(BOOST_CFLAGS) \
	$(LIBARCHIVE_CFLAGS)
libflux_la_CPPFLAGS	= $(CODE_COVERAGE_CPPFLAGS) $(BOOST_CPPFLAGS) $(AM_CPPFLAGS) $(LIBARCHIVE_CFLAGS)
libflux_la_LDFLAGS		= -Wl,--no-undefined \
	$(AM_LDFLAGS) -pthread
libflux_la_LIBADD		= $(CODE_COVERAGE_LIBS) $(BOOST_LIBS) $(LIBARCHIVE_LIBS) -lpthread
noinst_HEADERS			= Frontend.hpp

LibFlux.hpp: generate_libflux.py LibFlux.hpp.in
	python3 generate_libflux.py LibFlux.hpp.in > LibFlux.hpp

LibFluxCompat.hpp: generate_libflux.py LibFlux.hpp.in
	python3 generate_libflux.py --compiler-check LibFlux.hpp.in > LibFluxCompat.hpp

if CODE_COVERAGE_ENABLED
clean-local: code-coverage-clean
distclean-local: code-coverage-dist-clean
endif
