#
# Makefile.am for the frontend components of the craytool interface.
#
# Copyright 2014-2019 Cray Inc.  All Rights Reserved.
#

SRC             = @CRAYTOOL_DIR@/src
INCLUDE			= @CRAYTOOL_DIR@/include
SUPPORT         = @CRAYTOOL_DIR@/tests/test_support

GTEST           = $(SUPPORT)/googletest/googletest
GMOCK           = $(SUPPORT)/googletest/googlemock

AM_LIBTOOLFLAGS = --quiet
AUTOMAKE_OPTIONS = subdir-objects

check_LTLIBRARIES = libmock.la

# mock frontend library
libmock_la_SOURCES  =  MockFrontend/Frontend.cpp
libmock_la_CXXFLAGS =  $(AM_CXXFLAGS) -fPIC $(ARCHIVE_CFLAGS)
libmock_la_CPPFLAGS = -I$(SRC) -I$(INCLUDE) -I$(GMOCK)/include -I$(GMOCK) \
	-I$(GTEST)/include -I$(GTEST) \
	$(ARCHIVE_CPPFLAGS)
libmock_la_LDFLAGS  =  -static -Wl,--no-undefined -larchive
