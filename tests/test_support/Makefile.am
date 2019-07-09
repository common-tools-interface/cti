#
# Makefile.am for the CTI unit tests
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
SUPPORT         = @CRAYTOOL_DIR@/tests/test_support

GTEST           = $(SUPPORT)/googletest/googletest
GMOCK           = $(SUPPORT)/googletest/googlemock

AM_LIBTOOLFLAGS = --quiet
AUTOMAKE_OPTIONS = subdir-objects

# google test framework library
check_LTLIBRARIES   = libgtest.la
libgtest_la_SOURCES  = googletest/googletest/src/gtest-all.cc
libgtest_la_CPPFLAGS = -I$(GTEST)/include -I$(GTEST)
libgtest_la_LDFLAGS  = -pthread

# google mock framework library
check_LTLIBRARIES  += libgmock.la
libgmock_la_SOURCES  = googletest/googlemock/src/gmock-all.cc
libgmock_la_CPPFLAGS = -I$(GTEST)/include -I$(GTEST) \
	 -I$(GMOCK)/include -I$(GMOCK)
libgmock_la_LDFLAGS  = -pthread

# support binaries for function and unit tests

# stage test helpers (one_socket, two_socket, libprint.so)
check_PROGRAMS = one_socket two_socket

message_one/libprint.so: message_one/message.c
	cc -fPIC -shared message_one/message.c -o message_one/libprint.so

one_socket_DEPENDENCIES = message_one/libprint.so
one_socket_SOURCES = one_socket.c
one_socket_LDADD   = -L./message_one -lprint
one_socket_LDFLAGS = -Wl,-rpath,\$$ORIGIN/message_one

clean-local:
	rm -f message_one/libprint.so
	rm -f message_two/libprint.so

message_two/libprint.so: message_two/message.c
	cc -fPIC -shared message_two/message.c -o message_two/libprint.so

two_socket_DEPENDENCIES = message_two/libprint.so
two_socket_SOURCES = two_socket.c
two_socket_LDADD   = -L./message_two -lprint
two_socket_LDFLAGS = -Wl,-rpath,\$$ORIGIN/message_two
