#
# Makefile.am for the test utilities
#
# Copyright 2011-2019 Cray Inc. All Rights Reserved.
#

AM_CFLAGS		= -Wall
AM_LIBTOOLFLAGS	= --quiet

check_PROGRAMS	=	cti_barrier cti_launch cti_info cti_link cti_transfer cti_callback \
					cti_callback_daemon

cti_barrier_SOURCES			= cti_barrier_test.c cti_fe_common.c
cti_barrier_CFLAGS			= -I$(includedir) $(AM_CFLAGS)
cti_barrier_LDFLAGS			= -L$(libdir) -Wl,-rpath,$(libdir) $(CDST_SUP_LIBS) $(AM_LDFLAGS)
cti_barrier_LDADD			= -lcommontools_fe

cti_launch_SOURCES			= cti_launch_test.c cti_fe_common.c
cti_launch_CFLAGS			= -I$(includedir) $(AM_CFLAGS)
cti_launch_LDFLAGS			= -L$(libdir) -Wl,-rpath,$(libdir) $(CDST_SUP_LIBS) $(AM_LDFLAGS)
cti_launch_LDADD			= -lcommontools_fe

cti_info_SOURCES			= cti_info_test.c cti_fe_common.c
cti_info_CFLAGS				= -I$(includedir) $(AM_CFLAGS)
cti_info_LDFLAGS			= -L$(libdir) -Wl,-rpath,$(libdir) $(CDST_SUP_LIBS) $(AM_LDFLAGS)
cti_info_LDADD				= -lcommontools_fe

cti_link_SOURCES			= cti_linking_test.c
cti_link_CFLAGS				= -I$(includedir) $(AM_CFLAGS)
cti_link_LDFLAGS			= -L$(libdir) -Wl,-rpath,$(libdir) $(CDST_SUP_LIBS) $(AM_LDFLAGS)
cti_link_LDADD				= -lcommontools_fe -lcommontools_be

cti_transfer_SOURCES		= cti_transfer_test.c
cti_transfer_CFLAGS			= -I$(includedir) $(AM_CFLAGS)
cti_transfer_LDFLAGS		= -L$(libdir) -Wl,-rpath,$(libdir) $(CDST_SUP_LIBS) $(AM_LDFLAGS)
cti_transfer_LDADD			= -lcommontools_fe

cti_callback_SOURCES		= cti_callback_test.c
cti_callback_CFLAGS			= -I$(includedir) $(AM_CFLAGS)
cti_callback_LDFLAGS		= -L$(libdir) -Wl,-rpath,$(libdir) -pthread $(CDST_SUP_LIBS) $(AM_LDFLAGS)
cti_callback_LDADD			= -lcommontools_fe

cti_callback_daemon_SOURCES = cti_callback_daemon.c
cti_callback_daemon_CFLAGS	= -I$(includedir) $(AM_CFLAGS)
cti_callback_daemon_LDFLAGS = -L$(libdir) -Wl,-rpath,$(libdir) $(CDST_SUP_LIBS) $(AM_LDFLAGS)
cti_callback_daemon_LDADD	= -lcommontools_be

noinst_HEADERS				= cti_callback_test.h cti_fe_common.h
