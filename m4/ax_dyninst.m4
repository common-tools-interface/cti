################################################################################
# Copyright (c) 2010-2014 Krell Institute. All Rights Reserved.
# Copyright 2019 Cray Inc. All Rights Reserved.
#
# This program is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation; either version 2 of the License, or (at your option) any later
# version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
# details.
#
# You should have received a copy of the GNU General Public License along with
# this program; if not, write to the Free Software Foundation, Inc., 59 Temple
# Place, Suite 330, Boston, MA  02111-1307  USA
################################################################################


################################################################################
# Check for Dyninst (http://www.dyninst.org)
################################################################################

AC_DEFUN([AX_DYNINST], [

    AS_IF(  [ test -n "$INTERNAL_DYNINST" ],
            [ dyninst_dir="$INTERNAL_DYNINST" ],
            [ dyninst_dir="/usr" ])

    DYNINST_CPPFLAGS="-I$dyninst_dir/include -I$dyninst_dir/include/dyninst"
    DYNINST_LDFLAGS="-L$dyninst_dir/lib"
    DYNINST_LIBDIR="$dyninst_dir/lib"

    DYNINST_DIR="$dyninst_dir"

    DYNINST_CPPFLAGS="$DYNINST_CPPFLAGS"
    DYNINST_LIBS="-lsymtabAPI -lpcontrol"
    BOOST_LIBS="$BOOST_SYSTEM_LIB $BOOST_THREAD_LIB $BOOST_CHRONO_LIB"

    AC_LANG_PUSH(C++)
    AC_REQUIRE_CPP

    dyninst_saved_LD_LIBRARY_PATH=$LD_LIBRARY_PATH
    dyninst_saved_CPPFLAGS=$CPPFLAGS
    dyninst_saved_LDFLAGS=$LDFLAGS
    dyninst_saved_LIBS=$LIBS

    LD_LIBRARY_PATH="$DYNINST_LIBDIR"
    CPPFLAGS="$CPPFLAGS $DYNINST_CPPFLAGS $BOOST_CPPFLAGS"
    LDFLAGS="$LDFLAGS $DYNINST_LDFLAGS $BINUTILS_LDFLAGS $LIBDWARF_LDFLAGS $LIBELF_LDFLAGS $BOOST_LDFLAGS"
    LIBS="$DYNINST_LIBS $BINUTILS_IBERTY_LIB $LIBDWARF_LIBS $LIBELF_LIBS $BOOST_SYSTEM_LIB $BOOST_THREAD_LIB $BOOST_CHRONO_LIB"

    AC_MSG_CHECKING([for Dyninst API library and headers])

    AC_LINK_IFELSE([AC_LANG_PROGRAM([[
	    #include <Symbol.h>
        ]], [[
	    Dyninst::SymtabAPI::Symbol symbol;
        ]])], [ AC_MSG_RESULT(yes)

            AM_CONDITIONAL(HAVE_DYNINST, true)
            AC_DEFINE(HAVE_DYNINST, 1, [Define to 1 if you have Dyninst.])

        ], [ AC_MSG_RESULT(no)
            AC_MSG_ERROR[Required dyninst libraries not found!.]
        ]
    )

    LD_LIBRARY_PATH=$dyninst_saved_LD_LIBRARY_PATH
    CPPFLAGS=$dyninst_saved_CPPFLAGS
    LDFLAGS=$dyninst_saved_LDFLAGS
    LIBS=$dyninst_saved_LIBS

    AC_LANG_POP(C++)

    AC_SUBST(DYNINST_CPPFLAGS)
    AC_SUBST(DYNINST_LDFLAGS)
    AC_SUBST(DYNINST_LIBS)
    AC_SUBST(DYNINST_LIBDIR)
    AC_SUBST(DYNINST_DIR)

])

