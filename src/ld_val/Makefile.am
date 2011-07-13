#
# Makefile.am for the ldval and libaudit components of the craytool interface.
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

AM_CFLAGS = -DBINDIR=\"$(bindir)\" -fPIC -Wall

noinst_LTLIBRARIES 	= libld_val.la
lib_LTLIBRARIES		= libaudit.la

libld_val_la_SOURCES	= ld_val.c ld_val_defs.h ld_val.h
libld_val_la_LDFLAGS	= -static -no-undefined

libaudit_la_SOURCES	= audit.c ld_val_defs.h
libaudit_la_LDFLAGS	= -shared -no-undefined -version-info $(AUDIT_VERSION)
