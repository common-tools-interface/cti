#
# Makefile.am for the test utilities
#
# Copyright 2011-2020 Cray Inc. All Rights Reserved.
#

SRC             = @COMMONTOOL_DIR@/src
SUPPORT         = @COMMONTOOL_DIR@/tests/test_support
EXTERNAL        = @COMMONTOOL_DIR@/include

GTEST           = $(SUPPORT)/googletest/googletest

AM_CFLAGS		= -Wall
AM_LIBTOOLFLAGS	= --quiet
AUTOMAKE_OPTIONS = subdir-objects

noinst_PROGRAMS	=	cti_barrier cti_launch cti_info cti_link cti_callback \
					cti_callback_daemon cti_kill cti_wlm cti_mpmd function_tests hello_mpi \
					hello_mpi_wait mpi_wrapper

cti_kill_CFLAGS				= $(CRAY_CTI_FE_CFLAGS) $(AM_CFLAGS)
cti_kill_SOURCES			= cti_kill_test.c cti_fe_common.c
cti_kill_LDFLAGS			= -Wl,-rpath,$(libdir) -Wl,--as-needed $(AM_LDFLAGS) $(CRAY_CTI_FE_LIBS)

cti_wlm_CFLAGS				= $(CRAY_CTI_FE_CFLAGS) $(AM_CFLAGS)
cti_wlm_SOURCES				= cti_wlm_test.c cti_fe_common.c
cti_wlm_LDFLAGS				= -Wl,-rpath,$(libdir) -Wl,--as-needed $(AM_LDFLAGS) $(CRAY_CTI_FE_LIBS)

cti_barrier_SOURCES			= cti_barrier_test.c cti_fe_common.c
cti_barrier_CFLAGS			= $(CRAY_CTI_FE_CFLAGS) $(AM_CFLAGS)
cti_barrier_LDFLAGS			= -Wl,-rpath,$(libdir) -Wl,--as-needed $(AM_LDFLAGS) $(CRAY_CTI_FE_LIBS)

cti_launch_SOURCES			= cti_launch_test.c cti_fe_common.c
cti_launch_CFLAGS			= $(CRAY_CTI_FE_CFLAGS) $(AM_CFLAGS)
cti_launch_LDFLAGS			= -Wl,-rpath,$(libdir) -Wl,--as-needed $(AM_LDFLAGS) $(CRAY_CTI_FE_LIBS)

cti_info_SOURCES			= cti_info_test.c cti_fe_common.c
cti_info_CFLAGS				= $(CRAY_CTI_FE_CFLAGS) $(AM_CFLAGS)
cti_info_LDFLAGS			= -Wl,-rpath,$(libdir) -Wl,--as-needed $(AM_LDFLAGS) $(CRAY_CTI_FE_LIBS)

cti_mpmd_SOURCES			= cti_mpmd_test.c cti_fe_common.c
cti_mpmd_CFLAGS				= $(CRAY_CTI_FE_CFLAGS) $(AM_CFLAGS)
cti_mpmd_LDFLAGS			= -Wl,-rpath,$(libdir) -Wl,--as-needed $(AM_LDFLAGS) $(CRAY_CTI_FE_LIBS)

cti_link_SOURCES			= cti_linking_test.c
cti_link_CFLAGS				= $(CRAY_CTI_FE_CFLAGS) $(CRAY_CTI_BE_CFLAGS) $(AM_CFLAGS)
cti_link_LDFLAGS			= -Wl,-rpath,$(libdir) -Wl,--as-needed $(AM_LDFLAGS) $(CRAY_CTI_FE_LIBS) $(CRAY_CTI_BE_LIBS)

cti_callback_SOURCES		= cti_callback_test.c
cti_callback_CFLAGS			= $(CRAY_CTI_FE_CFLAGS) $(AM_CFLAGS)
cti_callback_LDFLAGS		= -Wl,-rpath,$(libdir) -Wl,--as-needed -pthread $(AM_LDFLAGS) $(CRAY_CTI_FE_LIBS)

cti_callback_daemon_SOURCES = cti_callback_daemon.c
cti_callback_daemon_CFLAGS	= $(CRAY_CTI_FE_CFLAGS) $(AM_CFLAGS)
cti_callback_daemon_LDFLAGS = -Wl,-rpath,$(libdir) -Wl,--as-needed $(AM_LDFLAGS) $(CRAY_CTI_BE_LIBS)

function_tests_SOURCES		=	function_tests.cpp cti_fe_function_test.cpp
function_tests_CFLAGS		=	$(AM_CFLAGS) -fPIC -I$(SRC) \
								-I$(GTEST)/include -I$(GTEST) -I$(EXTERNAL)
function_tests_CXXFLAGS		=	$(AM_CXXFLAGS) $(function_tests_CFLAGS)
function_tests_LDADD		=	$(CRAY_CTI_FE_LIBS) \
								$(SUPPORT)/libgtest.la \
								-ldl -lrt -lstdc++
function_tests_LDFLAGS		=	-pthread -Wl,--enable-new-dtags -Wl,--no-undefined \
								-Wl,--as-needed -Wl,-rpath,$(libdir)

hello_mpi_CFLAGS			= $(AM_CFLAGS)
hello_mpi_SOURCES			= hello_mpi.c

hello_mpi_wait_CFLAGS		= $(AM_CFLAGS)
hello_mpi_wait_SOURCES		= hello_mpi_wait.c

mpi_wrapper_CFLAGS			= $(AM_CFLAGS)
mpi_wrapper_SOURCES			= mpi_wrapper.c

noinst_HEADERS				= cti_callback_test.h cti_fe_common.h cti_fe_function_test.hpp
