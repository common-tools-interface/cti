#
# cray_extensions.m4 Cray configure extensions.
#
# Copyright 2012-2019 Cray Inc. All Rights Reserved.
#
# This software is available to you under a choice of one of two
# licenses.  You may choose to be licensed under the terms of the GNU
# General Public License (GPL) Version 2, available from the file
# COPYING in the main directory of this source tree, or the
# BSD license below:
#
#     Redistribution and use in source and binary forms, with or
#     without modification, are permitted provided that the following
#     conditions are met:
#
#      - Redistributions of source code must retain the above
#        copyright notice, this list of conditions and the following
#        disclaimer.
#
#      - Redistributions in binary form must reproduce the above
#        copyright notice, this list of conditions and the following
#        disclaimer in the documentation and/or other materials
#        provided with the distribution.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
# BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
# ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
# CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#

dnl
dnl read release and library version information from disk
dnl This is not portable due to source...
dnl
AC_DEFUN([cray_INIT],
[
	dnl Pull in the revision information from the $PWD/release_versioning file
	m4_define([COMMONTOOL_REVISION], [m4_esyscmd_s([source $PWD/release_versioning; echo "$revision"])])

	m4_define([COMMONTOOL_MAJOR], [m4_esyscmd_s([source $PWD/release_versioning; echo "$common_tool_major"])])
	m4_define([COMMONTOOL_MINOR], [m4_esyscmd_s([source $PWD/release_versioning; echo "$common_tool_minor"])])

	m4_define([COMMONTOOL_BE_CURRENT], [m4_esyscmd_s([source $PWD/release_versioning; echo $be_current])])
	m4_define([COMMONTOOL_BE_AGE], [m4_esyscmd_s([source $PWD/release_versioning; echo $be_age])])

	m4_define([COMMONTOOL_FE_CURRENT], [m4_esyscmd_s([source $PWD/release_versioning; echo $fe_current])])
	m4_define([COMMONTOOL_FE_AGE], [m4_esyscmd_s([source $PWD/release_versioning; echo $fe_age])])

	AC_SUBST([COMMONTOOL_RELEASE_VERSION], [COMMONTOOL_MAJOR.COMMONTOOL_MINOR.COMMONTOOL_REVISION])
	AC_SUBST([COMMONTOOL_BE_VERSION], [COMMONTOOL_BE_CURRENT:COMMONTOOL_REVISION:COMMONTOOL_BE_AGE])
	AC_SUBST([COMMONTOOL_FE_VERSION], [COMMONTOOL_FE_CURRENT:COMMONTOOL_REVISION:COMMONTOOL_FE_AGE])

    AC_PREFIX_DEFAULT(["/opt/cray/pe/cti/COMMONTOOL_MAJOR.COMMONTOOL_MINOR.COMMONTOOL_REVISION"])

	AC_DEFINE_UNQUOTED([CTI_BE_VERSION], ["COMMONTOOL_BE_CURRENT.COMMONTOOL_REVISION.COMMONTOOL_BE_AGE"], [Version number of CTI backend.])
	AC_DEFINE_UNQUOTED([CTI_FE_VERSION], ["COMMONTOOL_FE_CURRENT.COMMONTOOL_REVISION.COMMONTOOL_FE_AGE"], [Version number of CTI frontend.])

	AC_SUBST([COMMONTOOL_EXTERNAL], [${COMMONTOOL_DIR}/external])
	AC_SUBST([COMMONTOOL_EXTERNAL_INSTALL], [${COMMONTOOL_DIR}/external/install])

	if test ! -d "${COMMONTOOL_EXTERNAL_INSTALL}"; then
		AS_MKDIR_P([${COMMONTOOL_EXTERNAL_INSTALL}])
	fi

	if [[ -z "${NUM_JOBS}" ]]; then
		NUM_JOBS=32
	fi
])

dnl support checksumming of critical files. generated header will be placed in
dnl $1/checksums.h
dnl
AC_DEFUN([cray_INIT_CHECKSUM],
[
	dnl enable checksum Makefile generation
	AC_CONFIG_FILES([$1/Makefile])

	AC_CHECK_PROG(SHA1SUM, sha1sum, yes)
	if test x"${SHA1SUM}" == x"yes"; then
		AC_DEFINE([HAVE_CHECKSUM], [1], [Define if checksumming support is activated.])
		AC_SUBST([CHECKSUM_PROG], ["sha1sum"])
		AC_DEFINE([CHECKSUM_BINARY], ["sha1sum"], [Define the name of the checksum binary])
	else
		AC_SUBST([CHECKSUM_PROG], ["true"])
		AC_MSG_WARN([sha1sum not found, checksumming disabled.])
	fi
])

dnl add the file $1 to CHECKSUM_FILES list, generated checksum macro will be named
dnl $2_CHECKSUM
dnl
AC_DEFUN([cray_ADD_CHECKSUM],
[
	filepath="$(pwd)/$1"
	macroname="$2"
	AC_SUBST([CHECKSUM_FILES], ["${filepath}@${macroname} ${CHECKSUM_FILES}"])
])

dnl
dnl create alps files in a staging location
dnl
AC_DEFUN([cray_SETUP_ALPS_RPMS],
[
	dnl Set this to the CLE base version we are buidling against. Note that this
	dnl does not include update level information.
	CLE_RPM_VERS="5.2"

	dnl Set this to the location of the alps RPMS we should build against
	ALPS_RPM_DIR="/cray/css/release/cray/build/xt/sles11sp3/x86_64/RB-5.2UP00-ari/working/latest-RB-5.2UP00/ALPS/x86_64"

	dnl Set this to the location of the xmlrpc-epi RPMS we should build against
	XMLRPC_RPM_DIR="/cray/css/release/cray/build/xt/sles11sp3/x86_64/RB-5.2UP00-ari/working/latest-RB-5.2UP00/3rd-party/x86_64/"

	dnl Create a temporary directory to extract the alps libraries to
	_cray_tmpdir="${COMMONTOOL_EXTERNAL}/alps_base"

	AC_MSG_CHECKING([for ALPS stage directory])

	AS_IF(	[test ! -d "$_cray_tmpdir"],
			[	AC_MSG_RESULT([no])

				dnl check required execs
				AC_PROG_MKDIR_P

				AC_ARG_VAR([RPM2CPIO], [Location of rpm2cpio])
				AC_ARG_VAR([CPIO], [Location of cpio])

				AC_PATH_PROG([RPM2CPIO], [rpm2cpio], [rpm2cpio])
				if test -z "$RPM2CPIO"; then
					AC_MSG_ERROR([rpm2cpio not found.])
				fi

				AC_PATH_PROG([CPIO], [cpio], [cpio])
				if test -z "$CPIO"; then
					AC_MSG_ERROR([cpio not found.])
				fi

				AC_MSG_NOTICE([Creating ALPS staging directory in $_cray_tmpdir])

				dnl resolve the names of the alps rpms
				if test -d "$ALPS_RPM_DIR"; then
					ALPS_RPM_1=$(find $ALPS_RPM_DIR/cray-libalps0-$CLE_RPM_VERS.[[0-9\.\-]]*.ari.x86_64.rpm 2>&AS_MESSAGE_LOG_FD)
					if test ! -e "$ALPS_RPM_1"; then
						AC_MSG_ERROR([ALPS RPM cray-libalps0-$CLE_RPM_VERS not found.])
					fi
					ALPS_RPM_2=$(find $ALPS_RPM_DIR/cray-libalps-devel-$CLE_RPM_VERS.[[0-9\.\-]]*.ari.x86_64.rpm 2>&AS_MESSAGE_LOG_FD)
					if test ! -e "$ALPS_RPM_2"; then
						AC_MSG_ERROR([ALPS RPM cray-libalps-devel-$CLE_RPM_VERS not found.])
					fi
					ALPS_RPM_3=$(find $ALPS_RPM_DIR/cray-libalpsutil0-$CLE_RPM_VERS.[[0-9\.\-]]*.ari.x86_64.rpm 2>&AS_MESSAGE_LOG_FD)
					if test ! -e "$ALPS_RPM_3"; then
						AC_MSG_ERROR([ALPS RPM cray-libalpsutil0-$CLE_RPM_VERS not found.])
					fi
					ALPS_RPM_4=$(find $ALPS_RPM_DIR/cray-libalpsutil-devel-$CLE_RPM_VERS.[[0-9\.\-]]*.ari.x86_64.rpm 2>&AS_MESSAGE_LOG_FD)
					if test ! -e "$ALPS_RPM_4"; then
						AC_MSG_ERROR([ALPS RPM alps-devel-$CLE_RPM_VERS not found.])
					fi
				else
					AC_MSG_ERROR([ALPS RPM directory $ALPS_RPM_DIR not found.])
				fi

				dnl resolve the names of the xmlrpc rpms
				if test -d "$XMLRPC_RPM_DIR"; then
					XMLRPC_RPM_1=$(find $XMLRPC_RPM_DIR/cray-libxmlrpc-epi0-[[0-9\.\-]]*.x86_64.rpm 2>&AS_MESSAGE_LOG_FD)
					if test ! -e "$XMLRPC_RPM_1"; then
						AC_MSG_ERROR([XMLRPC RPM cray-libxmlrpc-epi0 not found.])
					fi
					XMLRPC_RPM_2=$(find $XMLRPC_RPM_DIR/cray-libxmlrpc-epi-devel-[[0-9\.\-]]*.x86_64.rpm 2>&AS_MESSAGE_LOG_FD)
					if test ! -e "$XMLRPC_RPM_1"; then
						AC_MSG_ERROR([XMLRPC RPM cray-libxmlrpc-epi-devel not found.])
					fi
				else
					AC_MSG_ERROR([XMLRPC RPM directory $XMLRPC_RPM_DIR not found.])
				fi

				AS_MKDIR_P([$_cray_tmpdir])
				cd $_cray_tmpdir
				$RPM2CPIO $ALPS_RPM_1 | $CPIO -idv >&AS_MESSAGE_LOG_FD 2>&AS_MESSAGE_LOG_FD
				$RPM2CPIO $ALPS_RPM_2 | $CPIO -idv >&AS_MESSAGE_LOG_FD 2>&AS_MESSAGE_LOG_FD
				$RPM2CPIO $ALPS_RPM_3 | $CPIO -idv >&AS_MESSAGE_LOG_FD 2>&AS_MESSAGE_LOG_FD
				$RPM2CPIO $ALPS_RPM_4 | $CPIO -idv >&AS_MESSAGE_LOG_FD 2>&AS_MESSAGE_LOG_FD
				$RPM2CPIO $XMLRPC_RPM_1 | $CPIO -idv >&AS_MESSAGE_LOG_FD 2>&AS_MESSAGE_LOG_FD
				$RPM2CPIO $XMLRPC_RPM_2 | $CPIO -idv >&AS_MESSAGE_LOG_FD 2>&AS_MESSAGE_LOG_FD
				cd ${CRAYTOOL_DIR}
			],
			[AC_MSG_RESULT([yes])])

	AC_ARG_VAR([READLINK], [Location of readlink])

	AC_PATH_PROG([READLINK], [readlink], [readlink])
	if test -z "$READLINK"; then
		AC_MSG_ERROR([readlink not found.])
	fi

	dnl We need to resolve the stage location since this will change with each alps version.
	_cray_alps_inst=$($READLINK -m ${_cray_tmpdir}/opt/cray/alps/*)

	AC_MSG_CHECKING([for presence of ALPS libraries])

	if test ! -d "${_cray_alps_inst}/lib64/"; then
		AC_MSG_RESULT([not found])
		AC_MSG_ERROR([ALPS lib directory $_cray_alps_inst/lib64/ not found.])
	fi

	if test ! -d "${_cray_alps_inst}/include/"; then
		AC_MSG_RESULT([not found])
		AC_MSG_ERROR([ALPS include directory $_cray_alps_inst/include/ not found.])
	fi

	if test ! -d "${_cray_tmpdir}/usr/lib64"; then
		AC_MSG_RESULT([not found])
		AC_MSG_ERROR([XMLRPC lib directory $_cray_tmpdir/usr/lib64 not found.])
	fi

	AC_MSG_RESULT([yes])

	AC_SUBST([ALPS_BASE], [${CRAYTOOL_EXTERNAL}/alps_base])
	AC_SUBST([ALPS_LDFLAGS], ["-L${_cray_alps_inst}/lib64 -L${_cray_tmpdir}/usr/lib64"])
	AC_SUBST([ALPS_CFLAGS], ["-I${_cray_alps_inst}/include"])
])
