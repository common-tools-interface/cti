#
# Makefile.am for the demo utilities
#
# ©2011-2013 Cray Inc.  All Rights Reserved.
#
# Unpublished Proprietary Information.
# This unpublished work is protected to trade secret, copyright and other laws.
# Except as permitted by contract or express written permission of Cray Inc.,
# no part of this work or its content may be used, reproduced or disclosed
# in any form.
#
## $HeadURL$
## $Date$
## $Rev$
## $Author$
#

AM_CFLAGS 	= -I$(includedir) -L$(libdir)

EXTRA_PROGRAMS = aprunBarrier aprunInfo aprunTransfer aprunCallback callback_daemon

aprunBarrier_CFLAGS = $(AM_CFLAGS)

aprunBarrier_SOURCES = alps_barrier_demo.c
aprunBarrier_LDFLAGS = $(LDFLAGS) -Wl,-rpath,$(libdir) -lcraytool_frontend

aprunInfo_CFLAGS = $(AM_CFLAGS)
aprunInfo_SOURCES = alps_info_demo.c
aprunInfo_LDFLAGS = $(LDFLAGS) -Wl,-rpath,$(libdir) -lcraytool_frontend

aprunTransfer_CFLAGS = $(AM_CFLAGS)
aprunTransfer_SOURCES = alps_transfer_demo.c
aprunTransfer_LDFLAGS = $(LDFLAGS) -Wl,-rpath,$(libdir) -lcraytool_frontend

aprunCallback_CFLAGS = $(AM_CFLAGS)
aprunCallback_SOURCES = alps_callback_demo.c
aprunCallback_LDFLAGS = $(LDFLAGS) -Wl,-rpath,$(libdir) -lcraytool_frontend -pthread

callback_daemon_CFLAGS = $(AM_CFLAGS)
callback_daemon_SOURCES = alps_callback_daemon.c
callback_daemon_LDFLAGS = $(LDFLAGS) -Wl,-rpath,$(libdir) -lcraytool_backend

demos: aprunBarrier aprunInfo aprunTransfer aprunCallback callback_daemon
	mkdir -p demo_binaries
	cp testing.info demo_binaries
	mv aprunBarrier demo_binaries
	mv aprunInfo demo_binaries
	mv aprunTransfer demo_binaries
	mv aprunCallback demo_binaries
	mv callback_daemon demo_binaries

.PHONY: demos
