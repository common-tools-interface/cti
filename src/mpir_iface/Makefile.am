#
# Makefile.am for the MPIR interface components of the craytool interface.
#
# Copyright2014 Cray Inc.  All Rights Reserved.
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

SRC             = @CRAYTOOL_DIR@/src
MPIR_IFACE		= $(SRC)/mpir_iface
FRONTEND		= $(SRC)/frontend
USEFUL          = $(SRC)/useful

AM_CFLAGS		= -Wall -I$(SRC)
AM_LIBTOOLFLAGS = --quiet

noinst_LTLIBRARIES      	=	libmpir_iface.la

libmpir_iface_la_SOURCES	=	gdb_MPIR.c gdb_MPIR.h gdb_MPIR_iface.c gdb_MPIR_iface.h
libmpir_iface_la_CFLAGS		=	$(AM_CFLAGS) -fPIC -I$(FRONTEND) -I$(USEFUL) $(MI_CFLAGS)
libmpir_iface_la_LDFLAGS    =	-static -Wl,--no-undefined -Wl,--as-needed

libexec_PROGRAMS    		=	cti_starter cti_attach

cti_starter_SOURCES			=	gdb_MPIR.c gdb_MPIR.h gdb_MPIR_starter.c
cti_starter_CFLAGS			=	$(AM_CFLAGS) -I$(USEFUL) $(MI_CFLAGS)
cti_starter_LDADD			=	$(USEFUL)/libuseful.la $(MI_LIBS)
cti_starter_LDFLAGS			=	-Wl,--no-undefined -Wl,-z,origin -Wl,-rpath,\$$ORIGIN/../lib \
								-Wl,-rpath,\$$ORIGIN $(MI_LDFLAGS)
								
cti_attach_SOURCES			=	gdb_MPIR.c gdb_MPIR.h gdb_MPIR_attach.c
cti_attach_CFLAGS			=	$(AM_CFLAGS) -I$(USEFUL) $(MI_CFLAGS)
cti_attach_LDADD			=	$(USEFUL)/libuseful.la $(MI_LIBS)
cti_attach_LDFLAGS			=	-Wl,--no-undefined -Wl,-z,origin -Wl,-rpath,\$$ORIGIN/../lib \
								-Wl,-rpath,\$$ORIGIN $(MI_LDFLAGS)
