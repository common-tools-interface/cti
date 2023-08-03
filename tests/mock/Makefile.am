#
# Makefile.am for the frontend components of the common tools interface.
#
# Copyright 2014-2021 Hewlett Packard Enterprise Development LP.
# SPDX-License-Identifier: Linux-OpenIB
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
						$(LIBARCHIVE_CFLAGS)
libmock_la_LDFLAGS	=	-static -Wl,--as-needed -Wl,--no-undefined

noinst_HEADERS		=	MockFrontend/Frontend.hpp
