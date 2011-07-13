
AM_CFLAGS = -DBINDIR=\"$(bindir)\" -fPIC -Wall

noinst_LTLIBRARIES 	= libld_val.la
lib_LTLIBRARIES		= libaudit.la

libld_val_la_SOURCES	= ld_val.c ld_val_defs.h ld_val.h
libld_val_la_LDFLAGS	=

libaudit_la_SOURCES	= audit.c ld_val_defs.h
libaudit_la_LDFLAGS	= -shared
