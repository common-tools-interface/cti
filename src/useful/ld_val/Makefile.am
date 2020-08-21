#
# Makefile.am for the ldval and libaudit components of the common tools interface
#
# (C) Copyright 2011-2020 Hewlett Packard Enterprise Development LP.
#

AM_CFLAGS				= -Wall
AM_LIBTOOLFLAGS			= --quiet

noinst_LTLIBRARIES		= libld_val.la
lib_LTLIBRARIES			= libaudit.la

libld_val_la_SOURCES	= ld_val.c ld_val_defs.h
libld_val_la_CFLAGS		= -fPIC $(CODE_COVERAGE_CFLAGS) $(AM_CFLAGS)
libld_val_la_LDFLAGS	= -Wl,--no-undefined $(AM_LDFLAGS)
libld_val_la_LIBADD		= $(CODE_COVERAGE_LIBS)
libld_val_la_CPPFLAGS	= $(CODE_COVERAGE_CPPFLAGS) $(AM_CPPFLAGS)

libaudit_la_SOURCES		= audit.c ld_val_defs.h
libaudit_la_CFLAGS		= -fPIC $(CODE_COVERAGE_CFLAGS) $(AM_CFLAGS)
libaudit_la_LDFLAGS		= -Wl,--no-undefined -Wl,--as-needed -avoid-version $(AM_LDFLAGS)
libaudit_la_LIBADD		= $(CODE_COVERAGE_LIBS)
libaudit_la_CPPFLAGS	= $(CODE_COVERAGE_CPPFLAGS) $(AM_CPPFLAGS)

noinst_HEADERS			= ld_val.h

if CODE_COVERAGE_ENABLED
clean-local: code-coverage-clean
distclean-local: code-coverage-dist-clean
endif

