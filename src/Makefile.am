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
# $HeadURL$
# $Date$
# $Rev$
# $Author$
#

LDVAL 	= ld_val
JOBLIBS	= @JOB_LIBS@

if USE_INTERNAL_CFLAGS
AM_CFLAGS       = -I$(LDVAL) -fPIC -Wall -g -O2
else
AM_CFLAGS 	= -I$(LDVAL) -fPIC -Wall
endif

lib_LTLIBRARIES		= libcraytool_frontend.la libcraytool_backend.la

libcraytool_frontend_la_SOURCES = alps_application.c alps_transfer.c alps_run.c useful/path.c \
				  useful/stringList.c alps_application.h alps_transfer.h \
				  alps_run.h useful.h
libcraytool_frontend_la_LIBADD	= $(LDVAL)/libld_val.la
libcraytool_frontend_la_LDFLAGS	= -lalps -lxmlrpc -version-info $(CRAYTOOL_FE_VERSION)

libcraytool_backend_la_SOURCES	= alps_backend.c pmi_attribs_parser.c alps_backend.h pmi_attribs_parser.h
libcraytool_backend_la_LDFLAGS	= $(JOBLIBS) -lalpsutil -version-info $(CRAYTOOL_BE_VERSION)

bin_PROGRAMS	= dlaunch
dlaunch_SOURCES	= daemon_launcher.c useful/path.c useful/log.c useful/useful.h
dlaunch_CFLAGS = $(AM_CFLAGS)

