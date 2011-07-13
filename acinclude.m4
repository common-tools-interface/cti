#
# acinclude.m4 configure extensions for the craytool interface.
#
# ©2011 Cray Inc.  All Rights Reserved.
#
# Unpublished Proprietary Information.
# This unpublished work is protected to trade secret, copyright and other laws.
# Except as permitted by contract or express written permission of Cray Inc.,
# no part of this work or its content may be used, reproduced or disclosed
# in any form.
#
# $HeadURL$
# $Date$
# $Rev$
# $Author$
#

dnl
dnl read release and library version information from disk
dnl
define([CRAYTOOL_RELEASE], [patsubst(esyscmd([. release_versioning; echo $craytool_major.$craytool_minor]), [
])])

define([CRAYTOOL_BE_CURRENT], [patsubst(esyscmd([. release_versioning; echo $be_current]), [
])])
define([CRAYTOOL_BE_REVISION], [patsubst(esyscmd([. release_versioning; echo $be_revision]), [
])])
define([CRAYTOOL_BE_AGE], [patsubst(esyscmd([. release_versioning; echo $be_age]), [
])])

define([CRAYTOOL_FE_CURRENT], [patsubst(esyscmd([. release_versioning; echo $fe_current]), [
])])
define([CRAYTOOL_FE_REVISION], [patsubst(esyscmd([. release_versioning; echo $fe_revision]), [
])])
define([CRAYTOOL_FE_AGE], [patsubst(esyscmd([. release_versioning; echo $fe_age]), [
])])

define([AUDIT_CURRENT], [patsubst(esyscmd([. release_versioning; echo $audit_current]), [
])])
define([AUDIT_REVISION], [patsubst(esyscmd([. release_versioning; echo $audit_revision]), [
])])
define([AUDIT_AGE], [patsubst(esyscmd([. release_versioning; echo $audit_age]), [
])])

