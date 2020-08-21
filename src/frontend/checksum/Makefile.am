#
# Makefile.am for the CTI frontend checksum utility
#
# (C) Copyright 2019-2020 Hewlett Packard Enterprise Development LP.
#

SRC = @COMMONTOOL_DIR@/src

noinst_LTLIBRARIES = libchecksum.la

libchecksum.cpp: checksums.h

libchecksum_la_SOURCES = checksums.h libchecksum.cpp
libchecksum_la_LDFLAGS = -Wl,--no-undefined
libchecksum_la_CXXFLAGS = -Wall -I$(SRC) -fPIC $(AM_CXXFLAGS)

CS_FILE_NAME_PAIRS = @CHECKSUM_FILES@
CS_FILES = $(basename $(subst @,.,$(CS_FILE_NAME_PAIRS)))
CS_TARGETS = $(patsubst %, %_CHECKSUM, $(CS_FILE_NAME_PAIRS))
#.PHONY: $(CS_TARGETS)

clean-local:
	rm -f checksums.h checksums.h.sums

checksums.h: checksums.h.sums
	cp checksums.h.in checksums.h
	cat checksums.h.sums >> checksums.h
	printf "\n#endif\n" >> checksums.h
	rm checksums.h.sums

checksums.h.sums: $(CS_FILES) $(CS_TARGETS)
	touch checksums.h.sums

%_CHECKSUM:
	printf "#define %s \"%s\"\n" \
	$(subst .,,$(suffix $(subst @,.,$@))) \
	$(firstword $(shell @CHECKSUM_PROG@ $(basename $(subst @,.,$@)))) \
	>> checksums.h.sums
