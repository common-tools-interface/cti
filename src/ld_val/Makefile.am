#
# Makefile.am for the ldval and libaudit components of the craytool interface.
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

AM_CFLAGS				= -fPIC -Wall
AM_LIBTOOLFLAGS				= --quiet

noinst_LTLIBRARIES		= libld_val.la
lib_LTLIBRARIES			= libaudit.la

libld_val_la_SOURCES	= ld_val.c

libld_val_la_CFLAGS		= $(AM_CFLAGS)
libld_val_la_LDFLAGS	= -static -no-undefined

libaudit_la_SOURCES		= audit.c
libaudit_la_CFLAGS		= $(AM_CFLAGS)
libaudit_la_LDFLAGS		= -shared -avoid-version

noinst_HEADERS			= ld_val.h ld_val_defs.h

