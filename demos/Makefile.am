#
# Makefile.am for the demo utilities
#
# ©2011 Cray Inc.  All Rights Reserved.
#
# Unpublished Proprietary Information.
# This unpublished work is protected to trade secret, copyright and other laws.
# Except as permitted by contract or express written permission of Cray Inc.,
# no part of this work or its content may be used, reproduced or disclosed
# in any form.
#
# $HeadURL$
# $Date$
# $Rev$
# $Author$
#

if USE_INTERNAL_CFLAGS
AM_CFLAGS       = -I$(includedir) -L$(libdir) -g -O2
else
AM_CFLAGS 	= -I$(includedir) -L$(libdir)
endif

EXTRA_PROGRAMS = aprunBarrier aprunTransfer

aprunBarrier_CFLAGS = $(AM_CFLAGS)
aprunBarrier_SOURCES = alps_barrier_demo.c
aprunBarrier_LDFLAGS = $(LDFLAGS) -Wl,-rpath,$(libdir) -lcraytool_frontend

aprunTransfer_CFLAGS = $(AM_CFLAGS)
aprunTransfer_SOURCES = alps_transfer_demo.c
aprunTransfer_LDFLAGS = $(LDFLAGS) -Wl,-rpath,$(libdir) -lcraytool_frontend

demos: aprunBarrier aprunTransfer

.PHONY: demos
