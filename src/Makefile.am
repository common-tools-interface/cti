#
# Makefile.am for the libcraytool and dlaunch components of the craytool 
# interface.
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

LDVAL		= ld_val
ARCHIVE_LIBS	= @archive_libs@

AM_CFLAGS 	= -I$(LDVAL) -fPIC -Wall
AM_LIBTOOLFLAGS = --quiet

lib_LTLIBRARIES	= libcraytools_fe.la libcraytools_be.la

libcraytools_fe_la_SOURCES	= alps_application.c alps_run.c alps_transfer.c \
				  cti_error.c useful/path.c useful/stringList.c 
libcraytools_fe_la_LIBADD	= $(LDVAL)/libld_val.la $(ARCHIVE_LIBS)
libcraytools_fe_la_LDFLAGS	= -lalps -version-info $(CRAYTOOL_FE_VERSION)

libcraytools_be_la_SOURCES	= alps_backend.c pmi_attribs_parser.c
libcraytools_be_la_LDFLAGS	= -lalpsutil -version-info $(CRAYTOOL_BE_VERSION)

bin_PROGRAMS	= dlaunch
dlaunch_SOURCES	= daemon_launcher.c useful/path.c useful/log.c
dlaunch_CFLAGS = $(AM_CFLAGS)
dlaunch_LDADD = $(ARCHIVE_LIBS)
dlaunch_LDFLAGS = -static

noinst_HEADERS  = alps_application.h alps_backend.h alps_defs.h alps_run.h \
		  alps_transfer.h cti_error.h pmi_attribs_parser.h \
		  useful/log.h useful/path.h useful/stringList.h useful/useful.h

