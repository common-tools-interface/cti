# configure.ac for cti tests.
#
# Copyright 2020 Cray Inc.
#

AC_PREREQ([2.63])
AC_INIT([cti_tests], [1.0])
AM_INIT_AUTOMAKE([subdir-objects foreign -Wall])

AC_SUBST([COMMONTOOL_DIR], ["$(cd $srcdir/.. && pwd)"])

dnl Check if this is really needed
AC_ARG_ENABLE([CODE_COVERAGE_ENABLED],
[  --enable-code-coverage    Turn on code coverage],
[case "${enableval}" in
  yes) CODE_COVERAGE_ENABLED=true ;;
  no)  CODE_COVERAGE_ENABLED=false ;;
  *) AC_MSG_ERROR([bad value ${enableval} for --enable-code-coverage]) ;;
esac],[debug=false])
AM_CONDITIONAL([CODE_COVERAGE_ENABLED], [test x$CODE_COVERAGE_ENABLED = xtrue])

dnl This macro runs check for --enable-code-coverage and sets CODE_COVERAGE_ENABLED, CODE_COVERAGE_CFLAGS, etc.
AX_CODE_COVERAGE

: ${CXXFLAGS="-g -O0"}
: ${CFLAGS="-g -O0"}

AM_PROG_AR
AC_PROG_CXX([CC])
AC_PROG_CC([cc])
AC_COPYRIGHT([Copyright 2020 Cray Inc.])

PKG_PROG_PKG_CONFIG

PKG_CHECK_MODULES([CRAY_CTI_FE], 	[common_tools_fe],[],[AC_MSG_ERROR([libcommontools_fe.so not found.])])
PKG_CHECK_MODULES([CRAY_CTI_BE], 	[common_tools_be],[],[AC_MSG_ERROR([libcommontools_be.so not found.])])

dnl Define the release version so that the frontend can look in the right directory
AC_DEFINE_UNQUOTED([CTI_RELEASE_VERSION], ["$COMMONTOOL_RELEASE_VERSION"], [CTI release version string.])

LT_INIT

AC_CONFIG_FILES([
    Makefile
    test_support/Makefile
    mock/Makefile
    unit/Makefile
    examples/Makefile
    function/Makefile
])

AC_OUTPUT
