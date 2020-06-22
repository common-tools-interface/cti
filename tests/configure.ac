# configure.ac for cti tests.
#
# Copyright 2020 Cray Inc.
#

AC_PREREQ([2.63])
AC_INIT([cti_tests], [1.0])
AM_INIT_AUTOMAKE([subdir-objects foreign -Wall])
AC_COPYRIGHT([Copyright 2020 Cray Inc.])

: ${CXXFLAGS="-g -O0"}
: ${CFLAGS="-g -O0"}

AC_CONFIG_MACRO_DIRS([../m4])

AC_ARG_ENABLE([CODE_COVERAGE_ENABLED],
[  --enable-code-coverage    Turn on code coverage],
[case "${enableval}" in
  yes) CODE_COVERAGE_ENABLED=true ;;
  no)  CODE_COVERAGE_ENABLED=false ;;
  *) AC_MSG_ERROR([bad value ${enableval} for --enable-code-coverage]) ;;
esac],[debug=false])
AM_CONDITIONAL([CODE_COVERAGE_ENABLED], [test x$CODE_COVERAGE_ENABLED = xtrue])

AC_SUBST([COMMONTOOL_DIR], ["$(cd $srcdir/.. && pwd)"])

dnl This macro runs check for --enable-code-coverage and sets CODE_COVERAGE_ENABLED, CODE_COVERAGE_CFLAGS, etc.
AX_CODE_COVERAGE

m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

cray_INIT
AC_PROG_CC([cc])
AC_PROG_CXX([CC])
AM_PROG_AR
LT_INIT

PKG_PROG_PKG_CONFIG
PKG_CHECK_MODULES([CRAY_CTI_FE], 	[common_tools_fe],[],[AC_MSG_ERROR([libcommontools_fe.so not found.])])
PKG_CHECK_MODULES([CRAY_CTI_BE], 	[common_tools_be],[],[AC_MSG_ERROR([libcommontools_be.so not found.])])
PKG_CHECK_MODULES([CDST_SUP], [cray-cdst-support],[],AC_MSG_ERROR([cray-cdst-support not found.]))

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

dnl Define the release version so that the frontend can look in the right directory
AC_DEFINE_UNQUOTED([CTI_RELEASE_VERSION], ["$COMMONTOOL_RELEASE_VERSION"], [CTI release version string.])

dnl Define the release version so that the frontend can look in the right directory
AC_DEFINE_UNQUOTED([CTI_RELEASE_VERSION], ["$COMMONTOOL_RELEASE_VERSION"], [CTI release version string.])

dnl Define the dynamic name of the frontend and backend daemon that is version specific
AC_SUBST([COMMONTOOL_BE_DAEMON_BINARY], ["cti_be_daemon$COMMONTOOL_RELEASE_VERSION"])
AC_DEFINE_UNQUOTED([CTI_BE_DAEMON_BINARY], ["$COMMONTOOL_BE_DAEMON_BINARY"], [Name of CTI backend daemon binary.])
AC_SUBST([COMMONTOOL_FE_DAEMON_BINARY], ["cti_fe_daemon$COMMONTOOL_RELEASE_VERSION"])
AC_DEFINE_UNQUOTED([CTI_FE_DAEMON_BINARY], ["$COMMONTOOL_FE_DAEMON_BINARY"], [Name of CTI frontend daemon binary.])

AC_CONFIG_FILES([
    Makefile
    test_support/Makefile
    function/Makefile
    function/tests/Makefile
])

AC_OUTPUT
