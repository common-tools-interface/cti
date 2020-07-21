#
# Makefile.am for the CTI unit tests
#
# Copyright 2014-2020 Cray Inc. All Rights Reserved.
#

SRC             = @COMMONTOOL_DIR@/src
SUPPORT         = @COMMONTOOL_DIR@/@TESTS_DIR_NAME@/test_support

GTEST           = $(SUPPORT)/googletest/googletest
GMOCK           = $(SUPPORT)/googletest/googlemock

AM_LIBTOOLFLAGS = --quiet
AUTOMAKE_OPTIONS = subdir-objects

# google test framework library
noinst_LTLIBRARIES		= libgtest.la
libgtest_la_SOURCES		= googletest/googletest/src/gtest-all.cc
libgtest_la_CPPFLAGS	= -I$(GTEST)/include -I$(GTEST)
libgtest_la_LDFLAGS		= -lpthread

# google mock framework library
noinst_LTLIBRARIES		+=	libgmock.la
libgmock_la_SOURCES		=	googletest/googlemock/src/gmock-all.cc
libgmock_la_CPPFLAGS	=	-I$(GTEST)/include -I$(GTEST) \
							-I$(GMOCK)/include -I$(GMOCK)
libgmock_la_LDFLAGS		=	-lpthread

# support binaries for function and unit tests

# stage test helpers
noinst_PROGRAMS = one_print one_socket two_socket remote_filecheck

message_one/libmessage.so: message_one/message.c message_one/message.h
	gcc -fPIC -shared message_one/message.c -o message_one/libmessage.so

message_two/libmessage.so: message_two/message.c message_two/message.h
	gcc -fPIC -shared message_two/message.c -o message_two/libmessage.so

one_socket$(EXEEXT): message_one/libmessage.so
	gcc one_socket.c -L./message_one -lmessage -Wl,-rpath,\$$ORIGIN/message_one -o one_socket

two_socket$(EXEEXT): message_two/libmessage.so
	gcc two_socket.c -L./message_two -lmessage -Wl,-rpath,\$$ORIGIN/message_two -o two_socket

remote_filecheck$(EXEEXT):
	gcc remote_filecheck.c -o remote_filecheck

clean-local:
	rm -f message_one/libmessage.so
	rm -f message_two/libmessage.so
	rm -f one_socket
	rm -f two_socket
	rm -f remote_filecheck

one_print_DEPENDENCIES  = message_one/libmessage.so
one_print_SOURCES 		= one_print.c
one_print_LDADD 		= -L./message_one -lmessage
one_print_LDFLAGS 		= -Wl,-rpath,\$$ORIGIN/message_one
