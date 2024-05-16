#
# Makefile.am for the CTI frontend checksum utility
#
# Copyright 2019-2020 Hewlett Packard Enterprise Development LP.
# SPDX-License-Identifier: Linux-OpenIB
#

SRC = @COMMONTOOL_DIR@/src

noinst_LTLIBRARIES = libchecksum.la

libchecksum.cpp: checksums.h

libchecksum_la_SOURCES = checksums.h libchecksum.cpp
libchecksum_la_LDFLAGS = -Wl,--no-undefined
libchecksum_la_CXXFLAGS = -Wall -I$(SRC) -fPIC $(AM_CXXFLAGS)

CS_FILE_NAME_PAIRS = @CHECKSUM_FILES@
CS_FILES = $(basename $(subst @,.,$(CS_FILE_NAME_PAIRS)))

clean-local:
	rm -f checksums.h

checksums.h: $(CS_FILES)
	cp checksums.h.in checksums.h
	for path_target in $(CS_FILE_NAME_PAIRS); do \
		printf "#define %s_CHECKSUM \"%s\"\n" \
		$$(cut -d '@' -f 2 <<< "$$path_target") \
		$$(@CHECKSUM_PROG@ $$(cut -d '@' -f 1 <<< "$$path_target") | cut -d ' ' -f 1) \
		>> checksums.h; \
	done
	printf "\n#endif\n" >> checksums.h

