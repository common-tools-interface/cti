#
# Makefile.am for user-facing CTI utilities
#
# Copyright 2021 Hewlett Packard Enterprise Development LP.
#

SRC = @COMMONTOOL_DIR@/src
INCLUDE	= @COMMONTOOL_DIR@/include

AM_LIBTOOLFLAGS = --quiet
AM_CXXFLAGS		= -Wall

libexec_PROGRAMS = cti_diagnostics cti_diagnostics_backend cti_diagnostics_target

cti_diagnostics_SOURCES = cti_diagnostics.cpp
cti_diagnostics_CXXFLAGS = -I$(SRC) -I. -I$(INCLUDE) $(CDST_SUP_CFLAGS) $(AM_CXXFLAGS)
cti_diagnostics_LDFLAGS	= -Wl,--no-undefined -Wl,--as-needed -pthread $(CDST_SUP_LIBS)
cti_diagnostics_LDADD = $(SRC)/frontend/libcommontools_fe.la

cti_diagnostics_backend_SOURCES = cti_diagnostics_backend.c
cti_diagnostics_backend_CXXFLAGS = -I$(SRC) -I. -I$(INCLUDE)  $(AM_CFLAGS)
cti_diagnostics_backend_LDFLAGS	= -Wl,--no-undefined -Wl,--as-needed -pthread
cti_diagnostics_backend_LDADD = $(SRC)/backend/libcommontools_be.la

cti_diagnostics_target_SOURCES = cti_diagnostics_target.c
