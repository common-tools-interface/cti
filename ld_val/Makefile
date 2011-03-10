#
# Main project makefile for ld_val program
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
AR = ar

prefix = $(CURDIR)/install
exec_prefix = $(prefix)

libdir = $(exec_prefix)/lib

libaudit_CFLAGS = -fPIC -Wall
libaudit_SOURCES = audit.c
libaudit_OBJECTS = $(libaudit_SOURCES:.c=.o)
LIBS = libaudit.so

ld_val_CFLAGS = -fPIC -Wall
ld_val_SOURCES = ld_val.c
ld_val_OBJECTS = $(ld_val_SOURCES:.c=.o)
ld_val_ARFLAGS = -cq
A_LIBS = libld_val.a

OBJECTS = $(libaudit_OBJECTS) $(ld_val_OBJECTS)

.PHONY: all
all: LIBS_SOURCES $(LIBS) A_LIBS_SOURCES $(A_LIBS)

.PHONY: LIBS_SOURCES
LIBS_SOURCES : $(libaudit_SOURCES)
	$(CC) $(libaudit_CFLAGS) -c $(libaudit_SOURCES)

libaudit.so : $(libaudit_OBJECTS)
	$(CC) -shared -Wl,-soname,$(LIBS) -o $@ $(libaudit_OBJECTS) -lc

.PHONY: A_LIBS_SOURCES
A_LIBS_SOURCES : $(ld_val_SOURCES)
	$(CC) $(ld_val_CFLAGS) -c $(ld_val_SOURCES)

libld_val.a : $(ld_val_OBJECTS)
	$(AR) $(ld_val_ARFLAGS) $@ $(ld_val_OBJECTS)

.PHONY: install
install : $(LIBS)
	mkdir -p $(libdir)
	cp $(LIBS) $(libdir)

.PHONY: clean
clean : 
	rm -f $(OBJECTS)

.PHONY: distclean
distclean :
	rm -f $(OBJECTS) $(LIBS) $(A_LIBS)
