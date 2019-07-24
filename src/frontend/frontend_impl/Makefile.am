#
# Makefile.am for the frontend impl components of the common tools interface
#
# Copyright 2019 Cray Inc. All Rights Reserved.
#

@CODE_COVERAGE_RULES@

SUBDIRS				= SLURM GenericSSH

AM_LIBTOOLFLAGS 	= --quiet

noinst_LTLIBRARIES	= libfrontend_impl.la

libfrontend_impl_la_SOURCES		=
libfrontend_impl_la_LIBADD		= 	SLURM/libslurm.la \
									GenericSSH/libgenericssh.la $(CODE_COVERAGE_LIBS)
libfrontend_impl_la_LDFLAGS		=	-static -Wl,--no-undefined $(CODE_COVERAGE_LDFLAGS)
libfrontend_impl_la_CPPFLAGS	=	$(CODE_COVERAGE_CPPFLAGS)
libfrontend_impl_la_CXXFLAGS	=	$(CODE_COVERAGE_CXXFLAGS)
libfrontend_impl_la_CFLAGS		=	$(CODE_COVERAGE_CFLAGS)
noinst_HEADERS					=	Frontend_impl.hpp
