#
# Makefile.am for the libcraytool and dlaunch components of the craytool 
# interface.
#
# ©2011-2014 Cray Inc.  All Rights Reserved.
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

ARCHIVE_LIBS    = @archive_libs@
SRC				= @craytool_dir@/src
LDVAL			= $(SRC)/ld_val
USEFUL			= $(SRC)/useful
BE				= $(SRC)/backend
DAEMON			= $(SRC)/daemon
FE				= $(SRC)/frontend

AM_CFLAGS 		= -fPIC -Wall -I$(SRC)
AM_LIBTOOLFLAGS = --quiet


lib_LTLIBRARIES	= libcraytools_fe.la libcraytools_be.la

libcraytools_fe_la_SOURCES	=	$(FE)/alps_application.c $(FE)/alps_run.c $(FE)/alps_transfer.c \
				 				$(FE)/cti_error.c $(FE)/alps_application.h $(FE)/alps_run.h \
								$(FE)/alps_transfer.h $(FE)/cti_error.h
libcraytools_fe_la_CFLAGS	=	$(AM_CFLAGS) -I$(LDVAL) -I$(USEFUL)
libcraytools_fe_la_LIBADD	=	$(USEFUL)/libuseful.la $(LDVAL)/libld_val.la $(ARCHIVE_LIBS)
libcraytools_fe_la_LDFLAGS	=	-lalps -version-info $(CRAYTOOL_FE_VERSION)

libcraytools_be_la_SOURCES	=	$(BE)/alps_backend.c $(BE)/pmi_attribs_parser.c \
								$(BE)/alps_backend.h $(BE)/pmi_attribs_parser.h
libcraytools_be_la_CFLAGS	=	$(AM_CFLAGS)
libcraytools_be_la_LDFLAGS	=	-lalpsutil -version-info $(CRAYTOOL_BE_VERSION)


bin_PROGRAMS	= dlaunch

dlaunch_SOURCES				=	$(DAEMON)/daemon_launcher.c
dlaunch_CFLAGS				=	$(AM_CFLAGS) -I$(USEFUL)
dlaunch_LDADD				=	$(USEFUL)/libuseful.la $(ARCHIVE_LIBS)
dlaunch_LDFLAGS				=	-static


noinst_HEADERS  = alps_defs.h
