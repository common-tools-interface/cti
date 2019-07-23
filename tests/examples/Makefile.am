#
# Makefile.am for the test utilities
#
# Copyright 2011-2019 Cray Inc. All Rights Reserved.
#

AM_CFLAGS 	= -I$(includedir) -L$(libdir)
AM_LIBTOOLFLAGS = --quiet

check_PROGRAMS = cti_barrier cti_launch cti_info cti_link cti_transfer cti_callback \
	 cti_callback_daemon

cti_barrier_CFLAGS			= $(AM_CFLAGS)
cti_barrier_SOURCES			= cti_barrier_test.c cti_fe_common.c
cti_barrier_LDFLAGS			= $(LDFLAGS) -Wl,-rpath,$(libdir) -lcraytools_fe

cti_launch_CFLAGS			= $(AM_CFLAGS)
cti_launch_SOURCES			= cti_launch_test.c cti_fe_common.c
cti_launch_LDFLAGS			= $(LDFLAGS) -Wl,-rpath,$(libdir) -lcraytools_fe

cti_info_CFLAGS				= $(AM_CFLAGS)
cti_info_SOURCES			= cti_info_test.c cti_fe_common.c
cti_info_LDFLAGS			= $(LDFLAGS) -Wl,-rpath,$(libdir) -lcraytools_fe

cti_link_CFLAGS				= $(AM_CFLAGS)
cti_link_SOURCES			= cti_linking_test.c
cti_link_LDFLAGS			= $(LDFLAGS) -Wl,-rpath,$(libdir) -lcraytools_fe -lcraytools_be

cti_transfer_CFLAGS			= $(AM_CFLAGS)
cti_transfer_SOURCES		= cti_transfer_test.c
cti_transfer_LDFLAGS		= $(LDFLAGS) -Wl,-rpath,$(libdir) -lcraytools_fe

cti_callback_CFLAGS			= $(AM_CFLAGS)
cti_callback_SOURCES		= cti_callback_test.c
cti_callback_LDFLAGS		= $(LDFLAGS) -Wl,-rpath,$(libdir) -lcraytools_fe -pthread

cti_callback_daemon_CFLAGS	= $(AM_CFLAGS)
cti_callback_daemon_SOURCES = cti_callback_daemon.c
cti_callback_daemon_LDFLAGS = $(LDFLAGS) -Wl,-rpath,$(libdir) -lcraytools_be

