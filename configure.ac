#
# configure.ac script for the common tools interface.
#
# Copyright 2011-2019 Cray Inc. All Rights Reserved
#

AC_PREREQ([2.63])

dnl We need to use the m4_esyscmd here because the Cray macro extensions are
dnl not pulled in until after AC_INIT. This is not portable...
AC_INIT([common_tool_interface], m4_esyscmd([source $PWD/release_versioning; echo $common_tool_major.$common_tool_minor | tr -d '\n']), [andrewg@cray.com])
AC_COPYRIGHT([Copyright 2011-2019 Cray Inc. All Rights Reserved.])

AC_CONFIG_SRCDIR([src/cti_defs.h])
AC_CONFIG_AUX_DIR([config])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADERS([config.h include/common_tools_version.h])

AC_ARG_ENABLE([CODE_COVERAGE_ENABLED],
[  --enable-code-coverage    Turn on code coverage],
[case "${enableval}" in
  yes) CODE_COVERAGE_ENABLED=true ;;
  no)  CODE_COVERAGE_ENABLED=false ;;
  *) AC_MSG_ERROR([bad value ${enableval} for --enable-code-coverage]) ;;
esac],[debug=false])
AM_CONDITIONAL([CODE_COVERAGE_ENABLED], [test x$CODE_COVERAGE_ENABLED = xtrue])

dnl Turn on Posix extensions
AC_USE_SYSTEM_EXTENSIONS

AM_INIT_AUTOMAKE([foreign -Wall])

AC_SUBST([COMMONTOOL_DIR], ["$(cd $srcdir && pwd)"])

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

AX_CXX_COMPILE_STDCXX_17([noext])

LT_INIT

dnl This macro runs check for --enable-code-coverage and sets CODE_COVERAGE_ENABLED, CODE_COVERAGE_CFLAGS, etc.
AX_CODE_COVERAGE

dnl Set external directory for libALPS headers (library will be dlopened at runtime)
AC_SUBST([LIBALPS_HEADERS], [$COMMONTOOL_DIR/external/libalps_headers])
SAVE_CPPFLAGS=$CPPFLAGS
CPPFLAGS="-I$LIBALPS_HEADERS $CPPFLAGS"
AC_CHECK_HEADERS([$LIBALPS_HEADERS/alps/apInfo.h],
[],
[AC_MSG_ERROR([Couldn't find libALPS headers])],
[#include "alps/apInfo.h"])

PKG_CHECK_MODULES([CDST_SUP], [cray-cdst-support])

############################
### Check for libraries. ###
############################

dnl Check for libc
AC_CHECK_LIB(   [c],
                [free],
                [],
                [AC_MSG_FAILURE([libc not found.])]
                )

dnl check for librl
AC_CHECK_LIB(   [rt],
                [clock_gettime],
                [AC_DEFINE( [HAVE_LIBRL],
                                [1],
                                [Define to 1 if you have the rt library (-lrt).]
                            )
                ],
                [AC_MSG_FAILURE([librt not found.])]
                )

dnl Check for libarchive
save_LDFLAGS="$LDFLAGS"
LDFLAGS="$LDFLAGS $CDST_SUP_LIBS"
AC_CHECK_LIB(   [archive],
                [archive_read_new],
                [AC_DEFINE( [HAVE_ARCHIVE],
                                [1],
                                [Define to 1 if you have the archive library (-larchive).]
                            )
                ],
                [AC_MSG_ERROR([libarchive not found.])]
                )
AC_SUBST([ARCHIVE_LIBS], ["-larchive"])
dnl restore flags
LDFLAGS="$save_LDFLAGS"

dnl Check for libssh2
save_LDFLAGS="$LDFLAGS"
LDFLAGS="$LDFLAGS $CDST_SUP_LIBS"
AC_CHECK_LIB(   [ssh2],
                [libssh2_init],
                [AC_DEFINE( [HAVE_SSH2],
                                [1],
                                [Define to 1 if you have the ssh2 library (-lssh2).]
                            )
                ],
                [AC_MSG_ERROR([libssh2 not found.])]
                )
AC_SUBST([SSH2_LIBS], ["-lssh2"])
dnl restore flags
LDFLAGS="$save_LDFLAGS"

dnl Check for dyninst
AC_LANG_PUSH([C++])
save_LDFLAGS="$LDFLAGS"
LDFLAGS="$LDFLAGS $CDST_SUP_LIBS"
AX_CXX_CHECK_LIB(   [symtabAPI],
                    [Dyninst::SymtabAPI::Symbol],
                    [AC_DEFINE( [HAVE_SYMTABAPI],
                                [1],
                                [Define to 1 if you have the symtabAPI library (-lsymtabAPI).]
                            )
                    ],
                    [AC_MSG_ERROR([libsymtabAPI not found.])]
                    )
AX_CXX_CHECK_LIB(   [pcontrol],
                    [Dyninst::ProcControlAPI::Process],
                    [AC_DEFINE( [HAVE_PCONTROL],
                                [1],
                                [Define to 1 if you have the pcontrol library (-lpcontrol).]
                            )
                    ],
                    [AC_MSG_ERROR([libpcontrol not found.])]
                    )
AC_LANG_POP([C++])
AC_SUBST([DYNINST_LIBS], ["-lsymtabAPI -lpcontrol"])
dnl restore flags
LDFLAGS="$save_LDFLAGS"

###############################
### Check for header files. ###
###############################
AC_CHECK_HEADERS([arpa/inet.h fcntl.h inttypes.h limits.h netdb.h netinet/in.h stddef.h stdint.h stdlib.h string.h strings.h sys/ioctl.h sys/param.h sys/socket.h sys/time.h unistd.h])

AC_HEADER_STDBOOL

dnl Check for libarchive headers
save_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS $CDST_SUP_CFLAGS"
AC_CHECK_HEADER([archive.h],
        [],
        [AC_MSG_FAILURE([archive.h not found.])],
        []
        )
dnl restore flags
CFLAGS="$save_CFLAGS"

dnl Check for libssh2 headers
save_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS $CDST_SUP_CFLAGS"
AC_CHECK_HEADER([libssh2.h],
                [],
                [AC_MSG_FAILURE([libssh2.h not found.])],
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
AC_CHECK_FUNCS([clock_gettime dup2 getcwd gethostname inet_ntoa memchr memmove memset mkdir mkfifo putenv realpath rmdir select setenv socket strchr strdup strerror strrchr strstr strtol strtoul strtoull])

########################
### Define constants ###
########################

dnl Define the release version so that the frontend can look in the right directory
AC_DEFINE_UNQUOTED([CTI_RELEASE_VERSION], ["$COMMONTOOL_RELEASE_VERSION"], [CTI release version string.])

dnl Define the dynamic name of the frontend and backend daemon that is version specific
AC_SUBST([COMMONTOOL_BE_DAEMON_BINARY], ["cti_be_daemon$COMMONTOOL_RELEASE_VERSION"])
AC_DEFINE_UNQUOTED([CTI_BE_DAEMON_BINARY], ["$COMMONTOOL_BE_DAEMON_BINARY"], [Name of CTI backend daemon binary.])
AC_SUBST([COMMONTOOL_FE_DAEMON_BINARY], ["cti_fe_daemon$COMMONTOOL_RELEASE_VERSION"])
AC_DEFINE_UNQUOTED([CTI_FE_DAEMON_BINARY], ["$COMMONTOOL_FE_DAEMON_BINARY"], [Name of CTI frontend daemon binary.])
AC_SUBST([COMMONTOOL_MPIR_SHIM_BINARY], ["mpir_shim$COMMONTOOL_RELEASE_VERSION"])
AC_DEFINE_UNQUOTED([CTI_MPIR_SHIM_BINARY], ["$COMMONTOOL_MPIR_SHIM_BINARY"], [Name of CTI MPIR shim binary.])

################
### Finalize ###
################

AC_CONFIG_FILES([
    Makefile
    pkgconfig/common_tools_be.pc
    pkgconfig/common_tools_fe.pc
    src/Makefile
    src/backend/Makefile
    src/backend/daemon/Makefile
    src/frontend/Makefile
    src/frontend/daemon/Makefile
    src/frontend/transfer/Makefile
    src/frontend/frontend_impl/Makefile
    src/frontend/frontend_impl/ALPS/Makefile
    src/frontend/frontend_impl/SLURM/Makefile
    src/frontend/frontend_impl/PALS/Makefile
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

cray_INIT_CHECKSUM([src/frontend/checksum])
cray_ADD_CHECKSUM([src/backend/daemon/${COMMONTOOL_BE_DAEMON_BINARY}], [CTI_BE_DAEMON])

AC_OUTPUT
