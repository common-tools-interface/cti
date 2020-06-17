#
# Makefile.am for the test utilities
#
# Copyright 2011-2020 Cray Inc. All Rights Reserved.
#

SRC             = @COMMONTOOL_DIR@/src

AM_CFLAGS		= -Wall
AM_LIBTOOLFLAGS	= --quiet

noinst_PROGRAMS	=	cti_barrier cti_launch cti_info cti_link cti_transfer cti_callback \
					cti_callback_daemon cti_kill cti_wlm cti_mpmd

cti_kill_CFLAGS				= $(CRAY_CTI_FE_CFLAGS) $(CRAY_CTI_BE_CFLAGS) $(AM_CFLAGS)
cti_kill_SOURCES			= cti_kill_test.c cti_fe_common.c
cti_kill_LDFLAGS			= -Wl,-rpath,$(libdir) -Wl,--as-needed $(AM_LDFLAGS)
cti_kill_LDADD				= $(SRC)/frontend/libcommontools_fe.la

cti_wlm_CFLAGS				= $(CRAY_CTI_FE_CFLAGS) $(CRAY_CTI_BE_CFLAGS) $(AM_CFLAGS)
cti_wlm_SOURCES				= cti_wlm_test.c cti_fe_common.c
cti_wlm_LDFLAGS				= -Wl,-rpath,$(libdir) -Wl,--as-needed $(AM_LDFLAGS)
cti_wlm_LDADD				= $(SRC)/frontend/libcommontools_fe.la

cti_barrier_SOURCES			= cti_barrier_test.c cti_fe_common.c
cti_barrier_CFLAGS			= $(CRAY_CTI_FE_CFLAGS) $(CRAY_CTI_BE_CFLAGS) $(AM_CFLAGS)
cti_barrier_LDFLAGS			= -Wl,-rpath,$(libdir) -Wl,--as-needed $(AM_LDFLAGS)
cti_barrier_LDADD			= $(SRC)/frontend/libcommontools_fe.la

cti_launch_SOURCES			= cti_launch_test.c cti_fe_common.c
cti_launch_CFLAGS			= $(CRAY_CTI_FE_CFLAGS) $(CRAY_CTI_BE_CFLAGS) $(AM_CFLAGS)
cti_launch_LDFLAGS			= -Wl,-rpath,$(libdir) -Wl,--as-needed $(AM_LDFLAGS)
cti_launch_LDADD			= $(SRC)/frontend/libcommontools_fe.la

cti_info_SOURCES			= cti_info_test.c cti_fe_common.c
cti_info_CFLAGS				= $(CRAY_CTI_FE_CFLAGS) $(CRAY_CTI_BE_CFLAGS) $(AM_CFLAGS)
cti_info_LDFLAGS			= -Wl,-rpath,$(libdir) -Wl,--as-needed $(AM_LDFLAGS)
cti_info_LDADD				= $(SRC)/frontend/libcommontools_fe.la

cti_mpmd_SOURCES			= cti_mpmd_test.c cti_fe_common.c
cti_mpmd_CFLAGS				= -I$(includedir) $(AM_CFLAGS)
cti_mpmd_LDFLAGS			= -Wl,-rpath,$(libdir) -Wl,--as-needed $(AM_LDFLAGS)
cti_mpmd_LDADD				= $(SRC)/frontend/libcommontools_fe.la

cti_link_SOURCES			= cti_linking_test.c
cti_link_CFLAGS				= $(CRAY_CTI_FE_CFLAGS) $(CRAY_CTI_BE_CFLAGS) $(AM_CFLAGS)
cti_link_LDFLAGS			= -Wl,-rpath,$(libdir) -Wl,--as-needed $(AM_LDFLAGS)
cti_link_LDADD				= $(SRC)/frontend/libcommontools_fe.la $(SRC)/backend/libcommontools_be.la

cti_transfer_SOURCES		= cti_transfer_test.c
cti_transfer_CFLAGS			= $(CRAY_CTI_FE_CFLAGS) $(CRAY_CTI_BE_CFLAGS) $(AM_CFLAGS)
cti_transfer_LDFLAGS		= -Wl,-rpath,$(libdir) -Wl,--as-needed $(AM_LDFLAGS)
cti_transfer_LDADD			= $(SRC)/frontend/libcommontools_fe.la

cti_callback_SOURCES		= cti_callback_test.c
cti_callback_CFLAGS			= $(CRAY_CTI_FE_CFLAGS) $(CRAY_CTI_BE_CFLAGS) $(AM_CFLAGS)
cti_callback_LDFLAGS		= -Wl,-rpath,$(libdir) -Wl,--as-needed -pthread $(AM_LDFLAGS)
cti_callback_LDADD			= $(SRC)/frontend/libcommontools_fe.la

cti_callback_daemon_SOURCES = cti_callback_daemon.c
cti_callback_daemon_CFLAGS	= $(CRAY_CTI_FE_CFLAGS) $(CRAY_CTI_BE_CFLAGS) $(AM_CFLAGS)
cti_callback_daemon_LDFLAGS = -Wl,-rpath,$(libdir) -Wl,--as-needed $(AM_LDFLAGS)
cti_callback_daemon_LDADD	= $(SRC)/backend/libcommontools_be.la

noinst_HEADERS				= cti_callback_test.h cti_fe_common.h
