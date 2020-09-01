#
# cray_extensions.m4 Cray configure extensions.
#
# Copyright 2011-2020 Hewlett Packard Enterprise Development LP.
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
	m4_define([COMMONTOOL_BE_MAJOR], [m4_eval( COMMONTOOL_BE_CURRENT - COMMONTOOL_BE_AGE)])

	m4_define([COMMONTOOL_FE_CURRENT], [m4_esyscmd_s([source $PWD/release_versioning; echo $fe_current])])
	m4_define([COMMONTOOL_FE_AGE], [m4_esyscmd_s([source $PWD/release_versioning; echo $fe_age])])
	m4_define([COMMONTOOL_FE_MAJOR], [m4_eval( COMMONTOOL_FE_CURRENT - COMMONTOOL_FE_AGE)])

	AC_SUBST([COMMONTOOL_RELEASE_VERSION], [COMMONTOOL_MAJOR].[COMMONTOOL_MINOR].[COMMONTOOL_REVISION])
	AC_SUBST([COMMONTOOL_BE_VERSION], [COMMONTOOL_BE_CURRENT:COMMONTOOL_REVISION:COMMONTOOL_BE_AGE])
	AC_SUBST([COMMONTOOL_FE_VERSION], [COMMONTOOL_FE_CURRENT:COMMONTOOL_REVISION:COMMONTOOL_FE_AGE])

    AC_PREFIX_DEFAULT(["/opt/cray/pe/cti/COMMONTOOL_MAJOR.COMMONTOOL_MINOR.COMMONTOOL_REVISION"])

	AC_DEFINE_UNQUOTED([CTI_PACKAGE_VERSION], ["COMMONTOOL_MAJOR.COMMONTOOL_MINOR.COMMONTOOL_REVISION"], [Version number of CTI package.])
	AC_DEFINE_UNQUOTED([CTI_BE_VERSION], ["COMMONTOOL_BE_MAJOR.COMMONTOOL_BE_AGE.COMMONTOOL_REVISION"], [Version number of CTI backend.])
	AC_DEFINE_UNQUOTED([CTI_FE_VERSION], ["COMMONTOOL_FE_MAJOR.COMMONTOOL_FE_AGE.COMMONTOOL_REVISION"], [Version number of CTI frontend.])

	AC_DEFINE([CTI_PACKAGE_MAJOR], [COMMONTOOL_MAJOR], [Package major version])
	AC_DEFINE([CTI_PACKAGE_MINOR], [COMMONTOOL_MINOR], [Package minor version])
	AC_DEFINE([CTI_PACKAGE_REVISION], [COMMONTOOL_REVISION], [Package revision])
	AC_DEFINE([CTI_BE_CURRENT], [COMMONTOOL_BE_CURRENT], [Backend current version])
	AC_DEFINE([CTI_BE_AGE], [COMMONTOOL_BE_AGE], [Backend age])
	AC_DEFINE([CTI_BE_REVISION], [COMMONTOOL_REVISION], [Backend revision])
	AC_DEFINE([CTI_FE_CURRENT], [COMMONTOOL_FE_CURRENT], [Frontend current version])
	AC_DEFINE([CTI_FE_AGE], [COMMONTOOL_FE_AGE], [Frontend age])
	AC_DEFINE([CTI_FE_REVISION], [COMMONTOOL_REVISION], [Frontend revision])

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
