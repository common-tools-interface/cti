#
# configure.ac script for the craytool interface.
#
# Copyright 2010-2019 Cray Inc. All Rights Reserved
#
# Unpublished Proprietary Information.
# This unpublished work is protected to trade secret, copyright and other laws.
# Except as permitted by contract or express written permission of Cray Inc.,
# no part of this work or its content may be used, reproduced or disclosed
# in any form.
#

AC_PREREQ([2.63])

dnl We need to use the m4_esyscmd here because the Cray macro extensions are
dnl not pulled in until after AC_INIT. This is not portable...
AC_INIT([craytool_interface], m4_esyscmd([source $PWD/release_versioning; echo $craytool_major.$craytool_minor | tr -d '\n']), [andrewg@cray.com])
AC_COPYRIGHT([Copyright 2011-2019 Cray Inc. All Rights Reserved.])

AC_CONFIG_SRCDIR([src/cti_defs.h])
AC_CONFIG_AUX_DIR([config])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADERS([config.h])

dnl Turn on Posix extensions
AC_USE_SYSTEM_EXTENSIONS

AM_INIT_AUTOMAKE([foreign -Wall])

AC_SUBST([CRAYTOOL_DIR], ["$(cd $srcdir && pwd)"])

dnl Pull in Cray specific m4 extensions
cray_INIT

m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

###########################
### Check for programs. ###
###########################
AM_PROG_AR
AC_PROG_CXX
AC_PROG_AWK
AC_PROG_SED
AC_PROG_CC
AC_PROG_CC_C99
AM_PROG_CC_C_O
AC_PROG_CPP
AC_PROG_INSTALL
AC_LIBTOOL_DLOPEN
AC_PROG_LIBTOOL
AC_PROG_LN_S
AC_PROG_MAKE_SET

AX_CXX_COMPILE_STDCXX_11([noext])

LT_INIT

############################
### Optional build deps  ###
############################

dnl build libarchive
AC_CACHE_CHECK([for libarchive], [cray_cv_lib_archive_build], [cray_BUILD_LIBARCHIVE])
if test "x$cray_cv_lib_archive_build" = xno; then
	AC_MSG_ERROR([libarchive build failed.])
fi
AC_CACHE_SAVE
cray_ENV_LIBARCHIVE

dnl build libssh
AC_CACHE_CHECK([for libssh], [cray_cv_lib_ssh_build], [cray_BUILD_LIBSSH])
if test "x$cray_cv_lib_ssh_build" = xno; then
	AC_MSG_ERROR([libssh build failed.])
fi
AC_CACHE_SAVE
cray_ENV_LIBSSH

dnl build elfutils
AC_CACHE_CHECK([for elfutils], [cray_cv_elfutils_conf], [cray_CONF_ELFUTILS])
if test "x$cray_cv_elfutils_conf" = xno; then
	AC_MSG_ERROR([elfutils conf failed.])
fi
AC_CACHE_SAVE
cray_ENV_ELFUTILS

dnl build boost
AC_CACHE_CHECK([for boost], [cray_cv_boost_build], [cray_BUILD_BOOST])
if test "x$cray_cv_boost_build" = xno; then
	AC_MSG_ERROR([boost build failed.])
fi
AC_CACHE_SAVE
cray_ENV_BOOST

dnl build tbb
AC_CACHE_CHECK([for tbb], [cray_cv_tbb_build], [cray_BUILD_TBB])
if test "x$cray_cv_tbb_build" = xno; then
	AC_MSG_ERROR([tbb build failed.])
fi
AC_CACHE_SAVE
cray_ENV_TBB

dnl build dyninst
AC_CACHE_CHECK([for dyninst], [cray_cv_dyninst_build], [cray_BUILD_DYNINST])
if test "x$cray_cv_dyninst_build" = xno; then
	AC_MSG_ERROR([dyninst build failed.])
fi
AC_CACHE_SAVE
cray_ENV_DYNINST

dnl This macro runs check for --with-dyninst
AX_DYNINST

############################
### Check for libraries. ###
############################
dnl Check for libc
AC_CHECK_LIB(	[c],
				[free],
				[],
				[AC_MSG_FAILURE([libc not found.])]
				)

dnl check for librl
AC_CHECK_LIB(	[rt],
				[clock_gettime],
				[AC_DEFINE(	[HAVE_LIBRL],
								[1],
								[Define to 1 if you have the rt library (-lrt).]
							)
				],
				[AC_MSG_FAILURE([librt not found.])]
				)

dnl Check for libarchive
save_LDFLAGS="$LDFLAGS"
if test -d "$INTERNAL_LIBARCHIVE/lib"; then
	AC_SUBST([ARCHIVE_LDFLAGS], ["-L$INTERNAL_LIBARCHIVE/lib"])
	LDFLAGS="$LDFLAGS $ARCHIVE_LDFLAGS"
fi
AC_CHECK_LIB(	[archive],
				[archive_read_new],
				[AC_DEFINE(	[HAVE_ARCHIVE],
								[1],
								[Define to 1 if you have the archive library (-larchive).]
							)
				],
				[AC_MSG_ERROR([libarchive.a not found.])]
				)
AC_SUBST([ARCHIVE_LIBS], ["-larchive"])
dnl restore flags
LDFLAGS="$save_LDFLAGS"

dnl Check for libssh - we dlopen this, just make sure the library exists.
save_LDFLAGS="$LDFLAGS"
if test -d "$INTERNAL_LIBSSH/lib"; then
	LDFLAGS="$LDFLAGS -L$INTERNAL_LIBSSH/lib"
fi
AC_CHECK_LIB(	[ssh],
				[ssh_init],
				[AC_DEFINE(	[HAVE_SSH],
								[1],
								[Define to 1 if you have the ssh library (-lssh).]
							)
				],
				[AC_MSG_ERROR([libssh not found.])]
				)
dnl restore flags
LDFLAGS="$save_LDFLAGS"

###############################
### Check for header files. ###
###############################
AC_CHECK_HEADERS([arpa/inet.h fcntl.h inttypes.h limits.h netdb.h netinet/in.h stddef.h stdint.h stdlib.h string.h strings.h sys/ioctl.h sys/param.h sys/socket.h sys/time.h unistd.h])

AC_HEADER_STDBOOL

dnl Check for libarchive headers
save_CFLAGS="$CFLAGS"
if test -f "$INTERNAL_LIBARCHIVE/include/archive.h"; then
	AC_SUBST([ARCHIVE_CFLAGS], ["-I$INTERNAL_LIBARCHIVE/include"])
	CFLAGS="$CFLAGS $ARCHIVE_CFLAGS"
fi
AC_CHECK_HEADER([archive.h],
		[],
		[AC_MSG_FAILURE([archive.h not found.])],
		[]
		)
dnl restore flags
CFLAGS="$save_CFLAGS"

dnl Check for libssh headers
save_CFLAGS="$CFLAGS"
if test -f "$INTERNAL_LIBSSH/include/libssh/libssh.h"; then
	AC_SUBST([SSH_CFLAGS], ["-I$INTERNAL_LIBSSH/include"])
	CFLAGS="$CFLAGS $SSH_CFLAGS"
fi
AC_CHECK_HEADER([libssh/libssh.h],
				[],
				[AC_MSG_FAILURE([libssh.h not found.])],
				[]
				)
dnl restore flags
CFLAGS="$save_CFLAGS"

#####################################################################
### Check for typedefs, structures, and compiler characteristics. ###
#####################################################################
AC_TYPE_UID_T
AC_C_INLINE
AC_TYPE_INT32_T
AC_TYPE_INT64_T
AC_TYPE_OFF_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T

####################################
### Check for library functions. ###
####################################
AC_FUNC_FORK
AC_FUNC_GETGROUPS
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_CHECK_FUNCS([clock_gettime dup2 getcwd inet_ntoa memchr memmove memset mkdir putenv realpath select setenv socket strchr strdup strerror strrchr strstr strtol strtoul strtoull])

########################
### Define constants ###
########################

dnl Define the release version so that the frontend can look in the right directory
AC_DEFINE_UNQUOTED([CTI_RELEASE_VERSION], ["$CRAYTOOL_RELEASE_VERSION"], [CTI release version string.])

dnl Define the dynamic name of the cti_dlaunch utility that is version specific
AC_SUBST([CRAYTOOL_DLAUNCH_BINARY], ["cti_dlaunch$CRAYTOOL_RELEASE_VERSION"])
AC_DEFINE_UNQUOTED([CTI_DLAUNCH_BINARY], ["$CRAYTOOL_DLAUNCH_BINARY"], [Name of CTI daemon launcher binary.])

################
### Finalize ###
################

AC_CONFIG_FILES([	Makefile
				 	craytools_be.pc
					craytools_fe.pc
					external/Makefile
					src/Makefile
					src/backend/Makefile
					src/daemon/Makefile
					src/frontend/Makefile
					src/frontend/cti_transfer/Makefile
					src/frontend/frontend_impl/Makefile
					src/frontend/frontend_impl/CraySLURM/Makefile
					src/frontend/frontend_impl/GenericSSH/Makefile
					src/frontend/mpir_iface/Makefile
					src/useful/Makefile
					src/useful/ld_val/Makefile
					tests/Makefile
					tests/test_support/Makefile
					tests/mock/Makefile
					tests/unit/Makefile
					tests/examples/Makefile
					tests/function/Makefile
])

AC_OUTPUT
