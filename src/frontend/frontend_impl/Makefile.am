#
# Makefile.am for the frontend impl components of the common tools interface
#
# Copyright 2019-2020 Hewlett Packard Enterprise Development LP.
#

SUBDIRS				= ALPS SLURM PALS GenericSSH
if ENABLE_FLUX
SUBDIRS += Flux
endif

AM_LIBTOOLFLAGS 	= --quiet

noinst_LTLIBRARIES	= libfrontend_impl.la

libfrontend_impl_la_SOURCES		=
libfrontend_impl_la_LIBADD		= ALPS/libalps.la SLURM/libslurm.la PALS/libpals.la GenericSSH/libgenericssh.la
if ENABLE_FLUX
libfrontend_impl_la_LIBADD += Flux/libflux.la
endif
libfrontend_impl_la_LIBADD += $(CODE_COVERAGE_LIBS)
libfrontend_impl_la_LDFLAGS		= -Wl,--no-undefined $(AM_LDFLAGS)
libfrontend_impl_la_CPPFLAGS	= $(CODE_COVERAGE_CPPFLAGS) $(AM_CPPFLAGS)
libfrontend_impl_la_CXXFLAGS	= $(CODE_COVERAGE_CXXFLAGS) $(AM_CXXFLAGS)
noinst_HEADERS					= Frontend_impl.hpp
