#
# Makefile.am for the frontend components of the craytool interface.
#
# Copyright 2014-2019 Cray Inc.  All Rights Reserved.
#
# Unpublished Proprietary Information.
# This unpublished work is protected to trade secret, copyright and other laws.
# Except as permitted by contract or express written permission of Cray Inc.,
# no part of this work or its content may be used, reproduced or disclosed
# in any form.
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
