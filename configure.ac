#
# configure.ac script for the craytool interface.
#
# Copyright 2010-2017 Cray Inc. All Rights Reserved
#
# Unpublished Proprietary Information.
# This unpublished work is protected to trade secret, copyright and other laws.
# Except as permitted by contract or express written permission of Cray Inc.,
# no part of this work or its content may be used, reproduced or disclosed
# in any form.
#
dnl $HeadURL$
dnl $Date$
dnl $Rev$
dnl $Author$

AC_PREREQ([2.63])

dnl We need to use the m4_esyscmd here because the Cray macro extensions are
dnl not pulled in until after AC_INIT. This is not portable...
AC_INIT([craytool_interface], m4_esyscmd([source $PWD/release_versioning; echo $craytool_major.$craytool_minor | tr -d '\n']), [andrewg@cray.com])
AC_COPYRIGHT([Copyright 2011-2015 Cray Inc. All Rights Reserved.])

AC_CONFIG_SRCDIR([src/cti_defs.h])
AC_CONFIG_AUX_DIR([config])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADERS([config.h])

dnl Turn on Posix extensions
AC_USE_SYSTEM_EXTENSIONS

AM_INIT_AUTOMAKE([foreign -Wall])
LT_INIT

AC_SUBST([CRAYTOOL_DIR], ["`cd $srcdir && pwd`"])

dnl Pull in Cray specific m4 extensions
cray_INIT

m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

###########################
### Check for programs. ###
###########################
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

#############################
### Check for --with tags ###
#############################
dnl Check for --with-libarchive
AC_ARG_WITH(	[libarchive],
				[AS_HELP_STRING([--with-libarchive],
								[Specify path to libarchive install.])
				],
				[libarchive_path=$withval],
				[libarchive_path=no]
			)

dnl This macro runs check for --with-boost
AX_BOOST_BASE([1.57],, [AC_MSG_ERROR([valgrind4hpc needs libboost >=1.57, but it was not found in your system])])
AX_BOOST_SYSTEM

dnl This macro runs check for --with-dyninst
AX_DYNINST
dnl Check for dyninst
if test -z "$DYNINST_VERS"; then
	AC_MSG_ERROR([dyninst not found (try --with-dyninst or fix build.sh)])
fi

dnl Check for --with-libssh
AC_ARG_WITH(	[libssh],
				[AS_HELP_STRING([--with-libssh],
								[Specify path to libssh install.])
				],
				[libssh_path=$withval],
				[libssh_path=no]
			)

#########################
### Setup env options ###
#########################

dnl This m4 macro will grab the required ALPS RPM from CSS and extract it with
dnl cpio. It will create varibales $ALPS_LIB_PATH and $ALPS_INC_PATH which point
dnl to the alps libraries and headers respectively. It will also modify LDFLAGS,
dnl CFLAGS, and CPPFLAGS.
cray_SETUP_ALPS_RPMS

dnl Ditto for the wlm_detect libraries
cray_SETUP_WLM_DETECT_RPMS

############################
### Optional build deps  ###
############################

dnl build libarchive if argument wasn't provided
AC_MSG_CHECKING([for libarchive argument])
AM_CONDITIONAL([BUILT_ARC], [test "x$libarchive_path" = xno])
if test "x$libarchive_path" = "xno"; then
	AC_MSG_RESULT([not found])
	AC_CACHE_VAL([cray_cv_lib_archive_build], [cray_BUILD_LIBARCHIVE])
	if test "x$cray_cv_lib_archive_build" = xno; then
		AC_MSG_ERROR([libarchive build failed.])
	fi
	AC_CACHE_SAVE
	cray_ENV_LIBARCHIVE
	libarchive_path=$INTERNAL_LIBARCHIVE
else
	AC_MSG_RESULT([provided])
fi

dnl build libssh if argument wasn't provided
AC_MSG_CHECKING([for libssh argument])
AM_CONDITIONAL([BUILT_SSH], [test "x$libssh_path" = xno])
if test "x$libssh_path" = "xno"; then
	AC_MSG_RESULT([not found])
	AC_CACHE_VAL([cray_cv_lib_ssh_build], [cray_BUILD_LIBSSH])
	if test "x$cray_cv_lib_ssh_build" = xno; then
		AC_MSG_ERROR([libssh build failed.])
	fi
	AC_CACHE_SAVE
	cray_ENV_LIBSSH
	libssh_path=$INTERNAL_LIBSSH
else
	AC_MSG_RESULT([provided])
fi

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
if test -f "$libarchive_path/lib/libarchive.a"; then
	AC_SUBST([ARCHIVE_LDFLAGS], ["-L$libarchive_path/lib"])
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

###############################
### Check for header files. ###
###############################
AC_CHECK_HEADERS([arpa/inet.h fcntl.h inttypes.h limits.h netdb.h netinet/in.h stddef.h stdint.h stdlib.h string.h strings.h sys/ioctl.h sys/param.h sys/socket.h sys/time.h unistd.h])

AC_HEADER_STDBOOL

dnl Check for alps.h
save_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS $ALPS_CFLAGS"
AC_CHECK_HEADER([alps/alps.h],
		[],
		[AC_MSG_FAILURE([alps/alps.h not found.])]
		)
dnl Check for alps_toolAssist.h
AC_CHECK_HEADER([alps/alps_toolAssist.h],
		[],
		[AC_MSG_FAILURE([alps/alps_toolAssist.h not found.])]
		)
dnl Check for apInfo.h
AC_CHECK_HEADER([alps/apInfo.h],
		[],
		[AC_MSG_FAILURE([alps/apInfo.h not found.])]
		)
dnl Check for libalps.h
AC_CHECK_HEADER([alps/libalps.h],
		[],
		[AC_MSG_FAILURE([alps/libalps.h not found.])],
		[#include <alps/alps_toolAssist.h>
		 #include <netinet/in.h>
		 ]
		)
dnl Check for libalpsutil.h
AC_CHECK_HEADER([alps/libalpsutil.h],
		[],
		[AC_MSG_FAILURE([alps/libalpsutil.h not found.])],
		[#include <alps/alps_toolAssist.h>
		 #include <netinet/in.h>
		 ]
		)
dnl restore flags
CFLAGS="$save_CFLAGS"

dnl restore flags
CFLAGS="$save_CFLAGS"
CPPFLAGS="$save_CPPFLAGS"

dnl Check for libwlm_detect headers
save_CFLAGS="$CFLAGS"
save_CPPFLAGS="$CPPFLAGS"
CFLAGS="$CFLAGS $WLM_DETECT_CFLAGS"
CPPFLAGS="$CPPFLAGS $WLM_DETECT_CFLAGS"
AC_CHECK_HEADER(	[wlm_detect.h],
					[],
					[AC_MSG_FAILURE([wlm_detect.h not found.])]
					)
dnl restore flags
CFLAGS="$save_CFLAGS"
CPPFLAGS="$save_CPPFLAGS"

dnl Check for libarchive headers
save_CFLAGS="$CFLAGS"
if test -f "$libarchive_path/include/archive.h"; then
	AC_SUBST([ARCHIVE_CFLAGS], ["-I$libarchive_path/include"])
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
if test -f "$libssh_path/include/libssh/libssh.h"; then
	AC_SUBST([SSH_CFLAGS], ["-I$libssh_path/include"])
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

dnl Define the dynamic name of the cti_dlaunch utility that is version specific
AC_SUBST([CRAYTOOL_LAUNCHER], ["cti_dlaunch$CRAYTOOL_RELEASE_VERSION"])
AC_DEFINE_UNQUOTED([CTI_LAUNCHER], ["$CRAYTOOL_LAUNCHER"], [Name of CTI daemon launcher binary.])

################
### Finalize ###
################

AC_CONFIG_FILES([	Makefile
				 	cray-cti_be.pc
					cray-cti_fe.pc
					external/Makefile
					src/Makefile
					src/backend/Makefile
					src/daemon/Makefile
					src/frontend/Makefile
					src/ld_val/Makefile
					src/mpir_iface/Makefile
					src/cti_transfer/Makefile
					src/slurm_util/Makefile
					src/useful/Makefile
					tests/Makefile
])

AC_OUTPUT
