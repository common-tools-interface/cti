#
# Makefile.am for the frontend impl components of the common tools interface
#
# Copyright 2019-2023 Hewlett Packard Enterprise Development LP.
#

SUBDIRS				= SLURM GenericSSH Localhost
if ENABLE_ALPS
SUBDIRS += ALPS
endif
if ENABLE_PALS
SUBDIRS += PALS
endif
if ENABLE_FLUX
SUBDIRS += Flux
endif

AM_LIBTOOLFLAGS 	= --quiet

noinst_LTLIBRARIES	= libfrontend_impl.la

libfrontend_impl_la_SOURCES		=
libfrontend_impl_la_LIBADD		= SLURM/libslurm.la GenericSSH/libgenericssh.la
if ENABLE_ALPS
libfrontend_impl_la_LIBADD += ALPS/libalps.la
endif
if ENABLE_PALS
libfrontend_impl_la_LIBADD += PALS/libpals.la
endif
if ENABLE_FLUX
libfrontend_impl_la_LIBADD += Flux/libflux.la
endif

libfrontend_impl_la_LIBADD += Localhost/liblocalhost.la


libfrontend_impl_la_LIBADD += $(CODE_COVERAGE_LIBS)
libfrontend_impl_la_LDFLAGS		= -Wl,--no-undefined $(AM_LDFLAGS)
libfrontend_impl_la_CPPFLAGS	= $(CODE_COVERAGE_CPPFLAGS) $(AM_CPPFLAGS)
libfrontend_impl_la_CXXFLAGS	= $(CODE_COVERAGE_CXXFLAGS) $(AM_CXXFLAGS)
noinst_HEADERS					= Frontend_impl.hpp
