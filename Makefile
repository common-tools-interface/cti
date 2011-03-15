#
# Main project makefile for the alps_transfer interface
#
# © 2011 Cray Inc.  All Rights Reserved.  
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

CC = gcc

prefix			= $(CURDIR)/install
exec_prefix		= $(prefix)

bindir			= $(exec_prefix)/bin
libdir			= $(exec_prefix)/lib
includedir		= $(exec_prefix)/include
demodir			= $(exec_prefix)/demos

libcraytool_frontend_CFLAGS 	= -fPIC -Wall -Ialps/include -Ild_val
libcraytool_frontend_SOURCES 	= alps_application.c alps_transfer.c alps_run.c useful/path.c useful/stringList.c
libcraytool_frontend_HEADERS	= tool_frontend.h 
libcraytool_frontend_OBJECTS 	= $(libcraytool_frontend_SOURCES:.c=.o)
libcraytool_frontend_LDFLAGS 	= -Lalps/lib/alps 
libcraytool_frontend_LDADD 	= ld_val/libld_val.a -lalps -lxmlrpc

libcraytool_backend_CFLAGS	= -fPIC -Wall -Ialps/include -I/opt/cray/job/1.5.5-0.1_2.0301.24546.5.1.gem/include
libcraytool_backend_SOURCES	= alps_backend.c
libcraytool_backend_HEADERS	= tool_backend.h
libcraytool_backend_OBJECTS	= $(libcraytool_backend_SOURCES:.c=.o)
libcraytool_backend_LDFLAGS	= -Lalps/lib/alps -L/opt/cray/job/1.5.5-0.1_2.0301.24546.5.1.gem/lib64
libcraytool_backend_LDADD	= -lalpsutil -ljob

daemon_launcher_CFLAGS  = -Wall
daemon_launcher_SOURCES = daemon_launcher.c useful/path.c

demo_CFLAGS		= -Wall
demo_LDFLAGS		= -L. -Lalps/lib/alps
demo_SOURCES		= alps_transfer_demo.c
demo_LIBADD		= -ltransfer -lalps -lxmlrpc
demo_DATA		= demos/*

OBJECTS                 = $(libcraytool_frontend_OBJECTS) $(libcraytool_backend_OBJECTS)
HEADERS			= $(libcraytool_frontend_HEADERS) $(libcraytool_backend_HEADERS)
LIBS			= libcraytool_frontend.so libcraytool_backend.so
EXECUTABLE		= dlaunch
DEMO			= demo

.PHONY: all
all: ld_val $(OBJECTS) $(LIBS) $(EXECUTABLE)

.PHONY: ld_val
ld_val :
	$(MAKE) -C ld_val

$(libcraytool_frontend_OBJECTS) : %.o: %.c
	$(CC) $(libcraytool_frontend_CFLAGS) -c $< -o $@

$(libcraytool_backend_OBJECTS) : %.o: %.c
	$(CC) $(libcraytool_backend_CFLAGS) -c $< -o $@

libcraytool_frontend.so : $(libcraytool_frontend_OBJECTS)
	$(CC) -shared -Wl,-soname,$@ -o $@ $(libcraytool_frontend_OBJECTS) $(libcraytool_frontend_LDFLAGS) $(libcraytool_frontend_LDADD) -lc

libcraytool_backend.so : $(libcraytool_backend_OBJECTS)
	$(CC) -shared -Wl,-soname,$@ -o $@ $(libcraytool_backend_OBJECTS) $(libcraytool_backend_LDFLAGS) $(libcraytool_backend_LDADD) -lc

dlaunch : $(daemon_launcher_SOURCES)
	$(CC) $(daemon_launcher_CFLAGS) -o $@ $(daemon_launcher_SOURCES)

demo : $(demo_SOURCES) $(LIBS)
	$(CC) $(demo_CFLAGS) -o $@ $(demo_SOURCES) $(demo_LDFLAGS) $(demo_LIBADD)

.PHONY: install
install : ld_val $(HEADERS) $(LIBS) $(EXECUTABLE)
	mkdir -p $(exec_prefix)
	mkdir -p $(bindir)
	mkdir -p $(libdir)
	mkdir -p $(includedir)
	cp $(HEADERS) $(includedir)
	cp $(LIBS) $(libdir)
	cp $(EXECUTABLE) $(bindir)
	$(MAKE) -C ld_val prefix=$(exec_prefix) install

.PHONY: install-demo
install-demo : $(DEMO)
	mkdir -p $(demodir)
	cp -r $(demo_DATA) $(demodir)
	cp -r $(DEMO) $(bindir)

.PHONY: clean
clean : 
	$(MAKE) -C ld_val clean
	rm -f $(OBJECTS)

.PHONY: distclean
distclean :
	$(MAKE) -C ld_val distclean
	rm -f $(OBJECTS) $(LIBS) $(EXECUTABLE) $(DEMO)
