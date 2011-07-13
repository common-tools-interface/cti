#
# configure.ac script for the craytool interface.
#
# © 2010-2011 Cray Inc. All Rights Reserved
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

AC_PREREQ([2.68])

AC_INIT([craytool_interface], [1.0], [andrewg@cray.com])
AC_COPYRIGHT([© 2011 Cray Inc. All Rights Reserved.])

AC_CONFIG_SRCDIR([src/alps_application.c])
AC_CONFIG_AUX_DIR([config])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADERS([config.h])

dnl Turn on Posix extensions
AC_USE_SYSTEM_EXTENSIONS

AC_SUBST([craytool_srcdir], ["`cd $srcdir && pwd`"])

AM_INIT_AUTOMAKE([foreign -Wall])
LT_INIT

#########################
### Setup env options ###
#########################
dnl Add the local alps libs to the library search path
if test -d "$craytool_srcdir/alps/lib/alps/"; then
	LDFLAGS="$LDFLAGS -L$craytool_srcdir/alps/lib/alps/"
fi

dnl Add the local alps include to the header search path
if test -d "$craytool_srcdir/alps/include/"; then
	CPPFLAGS="$CPPFLAGS -I$craytool_srcdir/alps/include/"
	CFLAGS="$CFLAGS -I$craytool_srcdir/alps/include/"
fi

###########################
### Check for programs. ###
###########################
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_LIBTOOL
AC_PROG_LN_S
AC_PROG_SED
AM_PROG_CC_C_O

############################
### Check for libraries. ###
############################
dnl Check for libc
AC_CHECK_LIB([c],
	     [free],
	     [],
	     [AC_MSG_ERROR([libc not found!!!])]
	     )

dnl Check for libxmlrpc
AC_CHECK_LIB([xmlrpc], 
	     [XMLRPC_Free], 
	     [AC_DEFINE([HAVE_LIBXMLRPC],
			[1],
			[Define to 1 if you have the xmlrpc library (-lxmlrpc).]
			)
	      ],
	     [AC_MSG_ERROR([libxmlrpc not found.])]
	     )

dnl Check for libalps
AC_CHECK_LIB([alps], 
	     [alpsGetMyNid],
	     [AC_DEFINE([HAVE_LIBALPS],
			[1],
			[Define to 1 if you have the alps library (-lalps).]
			)
	      ],
	     [AC_MSG_ERROR([libalps not found.])],
	     [-lxmlrpc]
	     )

dnl Check for libalpsutil
AC_CHECK_LIB([alpsutil], 
	     [alps_get_placement_info],
	     [AC_DEFINE([HAVE_LIBALPSUTIL],
			[1],
			[Define to 1 if you have the alpsutil library (-lalpsutil).]
			)
	      ],
	     [AC_MSG_ERROR([libalpsutil not found.])]
	     [-lxmlrpc]
	     )

dnl Note that the job include path is not in the normal system location.
dnl So we need to use pkg-config to get at the info for this.
PKG_CHECK_MODULES([JOB], [cray-job])

dnl Define the substitution var for later
AC_SUBST([JOB_LIBS], ["$JOB_LIBS"])

dnl Check for libjob
AC_CHECK_LIB([job], 
	     [job_getapjid],
	     [AC_DEFINE([HAVE_LIBJOB],
			[1],
			[Define to 1 if you have the job library (-ljob).]
			)
	      ],
	     [AC_MSG_ERROR([libjob not found. Ensure the job module is loaded.])],
	     [$JOB_LIBS]
	     )

###############################
### Check for header files. ###
###############################
AC_CHECK_HEADERS([fcntl.h inttypes.h limits.h netinet/in.h stdint.h stdlib.h string.h strings.h unistd.h])

dnl Check for alps.h
AC_CHECK_HEADER([alps/alps.h],
		[],
		[AC_MSG_ERROR([alps/alps.h not found.])]
		)

dnl Check for alps_toolAssist.h
AC_CHECK_HEADER([alps/alps_toolAssist.h],
		[],
		[AC_MSG_ERROR([alps/alps_toolAssist.h not found.])]
		)

dnl Check for apInfo.h
AC_CHECK_HEADER([alps/apInfo.h],
		[],
		[AC_MSG_ERROR([alps/apInfo.h not found.])]
		)

dnl Check for libalps.h
AC_CHECK_HEADER([alps/libalps.h],
		[],
		[AC_MSG_ERROR([alps/libalps.h not found.])],
		[#include <alps/alps_toolAssist.h>
		 #include <netinet/in.h>
		 ]
		)

dnl Check for libalpsutil.h
AC_CHECK_HEADER([alps/libalpsutil.h],
		[],
		[AC_MSG_ERROR([alps/libalpsutil.h not found.])],
		[#include <alps/alps_toolAssist.h>
		 #include <netinet/in.h>
		 ]
		)

dnl add the $job_path/include location to the header search paths
CFLAGS="$CFLAGS $JOB_CFLAGS"
CPPFLAGS="$CPPFLAGS $JOB_CFLAGS"

dnl Now check for the job.h header
AC_CHECK_HEADER([job.h],
		[],
		[AC_MSG_ERROR([job.h header not found. Ensure proper job module is loaded.])]
		)

#####################################################################
### Check for typedefs, structures, and compiler characteristics. ###
#####################################################################
AC_TYPE_UID_T
AC_TYPE_INT32_T
AC_TYPE_INT64_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T

####################################
### Check for library functions. ###
####################################
AC_FUNC_FORK
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_CHECK_FUNCS([dup2 memset setenv strdup strrchr strstr])

################
### Finalize ###
################
AC_CONFIG_FILES([Makefile
		 src/Makefile
                 src/ld_val/Makefile])
AC_OUTPUT
