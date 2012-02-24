#
# Makefile.am for the libcraytool and dlaunch components of the craytool 
# interface.
#
# ©2011 Cray Inc.  All Rights Reserved.
#
# Unpublished Proprietary Information.
# This unpublished work is protected to trade secret, copyright and other laws.
# Except as permitted by contract or express written permission of Cray Inc.,
# no part of this work or its content may be used, reproduced or disclosed
# in any form.
#
# $HeadURL: http://svn/svn/debugger-support/tools_interface/trunk/src/Makefile.am $
# $Date: 2011-12-15 14:06:34 -0600 (Thu, 15 Dec 2011) $
# $Rev: 1452 $
# $Author: andrewg $
#

if USE_INTERNAL_CFLAGS
AM_CFLAGS       = -I$(includedir) -L$(libdir) -g -O2
else
AM_CFLAGS 	= -I$(includedir) -L$(libdir)
endif

EXTRA_PROGRAMS = aprunBarrier

aprunBarrier_CFLAGS = $(AM_CFLAGS)
aprunBarrier_SOURCES = alps_barrier_demo.c
aprunBarrier_LDFLAGS = $(LDFLAGS) -Wl,-rpath,$(libdir) -lcraytool_frontend

demos: aprunBarrier

.PHONY: demos
