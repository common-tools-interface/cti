#
# Makefile.am for the ldval and libaudit components of the craytool interface.
#
# Copyright 2011-2019 Cray Inc.  All Rights Reserved.
#
# Unpublished Proprietary Information.
# This unpublished work is protected to trade secret, copyright and other laws.
# Except as permitted by contract or express written permission of Cray Inc.,
# no part of this work or its content may be used, reproduced or disclosed
# in any form.
#

AM_CFLAGS				= -Wall
AM_LIBTOOLFLAGS			= --quiet

noinst_LTLIBRARIES		= libld_val.la
lib_LTLIBRARIES			= libaudit.la

libld_val_la_SOURCES	= ld_val.c ld_val_defs.h
libld_val_la_CFLAGS		= $(AM_CFLAGS) -fPIC
libld_val_la_LDFLAGS	= -static -Wl,--no-undefined

libaudit_la_SOURCES		= audit.c ld_val_defs.h
libaudit_la_CFLAGS		= $(AM_CFLAGS) -fPIC
libaudit_la_LDFLAGS		= -shared -Wl,--no-undefined -Wl,--as-needed -avoid-version

noinst_HEADERS			= ld_val.h

