#
# Makefile.am for the CTI frontend unit tests
#
# Copyright 2019 Cray Inc. All Rights Reserved.
#
# Unpublished Proprietary Information.
# This unpublished work is protected to trade secret, copyright and other laws.
# Except as permitted by contract or express written permission of Cray Inc.,
# no part of this work or its content may be used, reproduced or disclosed
# in any form.
#

SRC             = @COMMONTOOL_DIR@/src
SUPPORT         = @COMMONTOOL_DIR@/tests/test_support
EXTERNAL        = @COMMONTOOL_DIR@/include

GTEST           = $(SUPPORT)/googletest/googletest

AM_LIBTOOLFLAGS = --quiet
AUTOMAKE_OPTIONS = subdir-objects

check_PROGRAMS = function_tests

function_tests_SOURCES	=	function_tests.cpp cti_fe_function_test.cpp
function_tests_CFLAGS	=	$(AM_CFLAGS) -fPIC -I$(SRC) \
							-I$(GTEST)/include -I$(GTEST) -I$(EXTERNAL)
function_tests_CXXFLAGS	=	$(AM_CXXFLAGS) $(function_tests_CFLAGS)
function_tests_LDADD	=	-L$(prefix)/lib -lcommontools_fe \
							$(SUPPORT)/libgtest.la -ldl -lrt -lstdc++
function_tests_LDFLAGS	=	-pthread -Wl,--enable-new-dtags -Wl,--no-undefined \
							-Wl,--as-needed

noinst_HEADERS			= cti_fe_function_test.hpp

