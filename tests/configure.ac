# configure.ac for cti tests.
#
# Copyright 2020 Cray Inc.
#

AC_PREREQ([2.63])
AC_INIT([cti_tests], [1.0])
AM_INIT_AUTOMAKE([subdir-objects foreign -Wall])
AC_COPYRIGHT([Copyright 2020 Cray Inc.])
AC_CONFIG_MACRO_DIRS([m4])

: ${CXXFLAGS="-g -O0"}
: ${CFLAGS="-g -O0"}

AC_SUBST([COMMONTOOL_DIR], ["$(cd $srcdir/.. && pwd)"])
AC_SUBST([TESTS_DIR_NAME], ["$(cd $srcdir && basename `pwd`)"])

m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_PROG_CC([cc])
AC_PROG_CXX([CC])
AM_PROG_AR
LT_INIT
AX_PTHREAD

PKG_PROG_PKG_CONFIG
PKG_CHECK_MODULES([CRAY_CTI_FE], 	[common_tools_fe],[],[AC_MSG_ERROR([libcommontools_fe.so not found.])])
PKG_CHECK_MODULES([CRAY_CTI_BE], 	[common_tools_be],[],[AC_MSG_ERROR([libcommontools_be.so not found.])])

AC_CONFIG_FILES([
    Makefile
    test_support/Makefile
    function/Makefile
    function/tests/Makefile
])

AC_OUTPUT
