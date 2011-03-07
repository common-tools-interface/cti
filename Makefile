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

libtransfer_CFLAGS 	= -fPIC -g -Wall -Ialps/include -Ild_val
libtransfer_SOURCES 	= alps_application.c alps_transfer.c alps_run.c useful/path.c
libtransfer_HEADERS	= alps_application.h alps_transfer.h alps_run.h
libtransfer_OBJECTS 	= $(libtransfer_SOURCES:.c=.o)
libtransfer_LDFLAGS 	= -Lalps/lib/alps 
libtransfer_LDADD 	= ld_val/libld_val.a -lalps -lxmlrpc

libbackend_CFLAGS	= -fPIC -g -Wall -Ialps/include -I/opt/cray/job/1.5.5-0.1_2.0301.24546.5.1.gem/include
libbackend_SOURCES	= alps_backend.c
libbackend_HEADERS	= alps_backend.h
libbackend_OBJECTS	= $(libbackend_SOURCES:.c=.o)
libbackend_LDFLAGS	= -Lalps/lib/alps -L/opt/cray/job/1.5.5-0.1_2.0301.24546.5.1.gem/lib64
libbackend_LDADD	= -lalpsutil -ljob

daemon_launcher_CFLAGS  = -g -Wall
daemon_launcher_SOURCES = daemon_launcher.c useful/path.c

demo_CFLAGS		= -g -Wall
demo_LDFLAGS		= -L. -Lalps/lib/alps
demo_SOURCES		= alps_transfer_demo.c
demo_LIBADD		= -ltransfer -lalps -lxmlrpc
demo_DATA		= demos/*

OBJECTS                 = $(libtransfer_OBJECTS) $(libbackend_OBJECTS)
HEADERS			= $(libtransfer_HEADERS) $(libbackend_HEADERS)
LIBS			= libtransfer.so libbackend.so
EXECUTABLE		= dlaunch
DEMO			= demo

.PHONY: all
all: ld_val $(OBJECTS) $(LIBS) $(EXECUTABLE)

.PHONY: ld_val
ld_val :
	$(MAKE) -C ld_val

$(libtransfer_OBJECTS) : %.o: %.c
	$(CC) $(libtransfer_CFLAGS) -c $< -o $@

$(libbackend_OBJECTS) : %.o: %.c
	$(CC) $(libbackend_CFLAGS) -c $< -o $@

libtransfer.so : $(libtransfer_OBJECTS)
	$(CC) -shared -Wl,-soname,$@ -o $@ $(libtransfer_OBJECTS) $(libtransfer_LDFLAGS) $(libtransfer_LDADD) -lc

libbackend.so : $(libbackend_OBJECTS)
	$(CC) -shared -Wl,-soname,$@ -o $@ $(libbackend_OBJECTS) $(libbackend_LDFLAGS) $(libbackend_LDADD) -lc

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
