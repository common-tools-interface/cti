#
# Makefile.am for the ldval and libaudit components of the craytool interface.
#
# Copyright 2011-2019 Cray Inc.  All Rights Reserved.
#

AM_CFLAGS				= -Wall
AM_LIBTOOLFLAGS			= --quiet

noinst_LTLIBRARIES		= libld_val.la
lib_LTLIBRARIES			= libaudit.la

@CODE_COVERAGE_RULES@

libld_val_la_SOURCES	= ld_val.c ld_val_defs.h
libld_val_la_CFLAGS		= $(AM_CFLAGS) -fPIC $(CODE_COVERAGE_CFLAGS)
libld_val_la_LDFLAGS	= -static -Wl,--no-undefined $(CODE_COVERAGE_LDFLAGS)
libld_val_la_LIBADD	= $(CODE_COVERAGE_LIBS)
libld_val_la_CXXFLAGS	= $(CODE_COVEAGE_CXXFLAGS)
libld_val_la_CPPFLAGS	= $(CODE_COVERAGE_CPPFLAGS)

libaudit_la_SOURCES		= audit.c ld_val_defs.h
libaudit_la_CFLAGS		= $(AM_CFLAGS) -fPIC $(CODE_COVERAGE_CFLAGS)
libaudit_la_LDFLAGS		= -shared -Wl,--no-undefined -Wl,--as-needed -avoid-version $(CODE_COVERAGE_LDFLAGS)
libaudit_la_LIBADD	= $(CODE_COVERAGE_LIBS)
libaudit_la_CXXFLAGS	= $(CODE_COVEAGE_CXXFLAGS)
libaudit_la_CPPFLAGS	= $(CODE_COVERAGE_CPPFLAGS)

noinst_HEADERS			= ld_val.h

