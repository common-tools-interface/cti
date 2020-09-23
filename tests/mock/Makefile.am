#
# Makefile.am for the frontend components of the common tools interface.
#
# Copyright 2014-2020 Hewlett Packard Enterprise Development LP.
#

SRC             = @COMMONTOOL_DIR@/src
INCLUDE			= @COMMONTOOL_DIR@/include
SUPPORT         = @COMMONTOOL_DIR@/tests/test_support

GTEST           = $(SUPPORT)/googletest/googletest
GMOCK           = $(SUPPORT)/googletest/googlemock

AM_LIBTOOLFLAGS = --quiet
AUTOMAKE_OPTIONS = subdir-objects

check_LTLIBRARIES = libmock.la

# mock frontend library
libmock_la_SOURCES	=	MockFrontend/Frontend.cpp
libmock_la_CXXFLAGS	=	$(AM_CXXFLAGS) -fPIC -I$(SRC) -I$(INCLUDE) \
						-I$(GMOCK)/include -I$(GMOCK) \
						-I$(GTEST)/include -I$(GTEST) \
						$(CDST_SUP_CFLAGS)
libmock_la_LDFLAGS	=	-static -Wl,--as-needed -Wl,--no-undefined

noinst_HEADERS		=	MockFrontend/Frontend.hpp
