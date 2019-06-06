#
# Makefile.am for the CTI unit tests
#
# Copyright 2019 Cray Inc.  All Rights Reserved.
#
# Unpublished Proprietary Information.
# This unpublished work is protected to trade secret, copyright and other laws.
# Except as permitted by contract or express written permission of Cray Inc.,
# no part of this work or its content may be used, reproduced or disclosed
# in any form.
#

SRC             = @CRAYTOOL_DIR@/src
SUPPORT         = @CRAYTOOL_DIR@/tests/test_support
MOCK            = @CRAYTOOL_DIR@/tests/mock

GTEST           = $(SUPPORT)/googletest/googletest
GMOCK           = $(SUPPORT)/googletest/googlemock

AM_LIBTOOLFLAGS = --quiet
AUTOMAKE_OPTIONS = subdir-objects

# specify test binaries
TESTS = unit_tests
check_PROGRAMS = unit_tests

# unit test binary
unit_tests_SOURCES  = unit_tests.cpp cti_fe_unit_test.cpp cti_archive_unit_test.cpp
unit_tests_CFLAGS   = $(AM_CFLAGS) -fPIC -I$(SRC) \
	-I$(GTEST)/include -I$(GTEST) \
	-I$(GMOCK)/include -I$(GMOCK) \
	$(ARCHIVE_CFLAGS) \
	-I$(MOCK) \
	-DINSTALL_PATH=\"$(prefix)\"
unit_tests_CXXFLAGS = $(AM_CXXFLAGS) $(unit_tests_CFLAGS)
unit_tests_LDADD    = -L$(prefix)/lib -lcraytools_fe \
	$(SUPPORT)/libgtest.la $(SUPPORT)/libgmock.la $(MOCK)/libmock.la $(ARCHIVE_LIBS) \
	-ldl -lrt -lstdc++
unit_tests_LDFLAGS  = -pthread -Wl,-rpath,$(prefix)/lib \
	-Wl,--enable-new-dtags -Wl,--no-undefined -Wl,--as-needed $(ARCHIVE_LDFLAGS)
