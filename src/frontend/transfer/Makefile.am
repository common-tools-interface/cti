#
# Makefile.am for the transfer components of the common tools interface
#
# Copyright 2014-2020 Hewlett Packard Enterprise Development LP.
#

SRC             = @COMMONTOOL_DIR@/src
INCLUDE			= @COMMONTOOL_DIR@/include

AM_CXXFLAGS		= -Wall
AM_LIBTOOLFLAGS = --quiet

noinst_LTLIBRARIES		= libtransfer.la

libtransfer_la_SOURCES	= Archive.cpp Manifest.cpp Session.cpp
libtransfer_la_CXXFLAGS	= -I$(SRC) -I$(SRC)/frontend -I$(INCLUDE) -fPIC \
						$(CDST_SUP_CFLAGS) $(CODE_COVERAGE_CXXFLAGS) $(AM_CXXFLAGS)
libtransfer_la_LDFLAGS	= -Wl,--no-undefined $(CDST_SUP_LIBS) \
						$(AM_LDFLAGS)
libtransfer_la_CPPFLAGS	= $(CODE_COVERAGE_CPPFLAGS) $(AM_CPPFLAGS)
libtransfer_la_LIBADD	= $(ARCHIVE_LIBS) $(CODE_COVERAGE_LIBS)
noinst_HEADERS			= Archive.hpp Manifest.hpp Session.hpp

if CODE_COVERAGE_ENABLED
clean-local: code-coverage-clean
distclean-local: code-coverage-dist-clean
endif
