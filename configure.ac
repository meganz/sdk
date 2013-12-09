##
# @file configure.ac
# @brief an input file for autoconf tool
#
# (c) 2013 by Mega Limited, Wellsford, New Zealand
#
# This file is part of the MEGA SDK - Client Access Engine.
#
# Applications using the MEGA API must present a valid application key
# and comply with the the rules set forth in the Terms of Service.
#
# The MEGA SDK is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
#
# @copyright Simplified (2-clause) BSD License.
#
# You should have received a copy of the license along with this
# program.
##

#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.61])

# The Mega SDK version number is generated into config.h.
# The version in Git should reflect the *next* version planned.
m4_define([mega_major_version], [0])
m4_define([mega_minor_version], [2])
m4_define([mega_micro_version], [3])
m4_define([mega_version],
          [mega_major_version.mega_minor_version.mega_micro_version])

# libtool interface versioning
m4_define([mega_lt_revision], [0])
m4_define([mega_lt_current], [m4_eval(100 * mega_minor_version + mega_micro_version)])
m4_define([mega_lt_age], [0])

AC_INIT([libmega], [mega_version], [https://github.com/megaprivacy])

AM_INIT_AUTOMAKE([1.10 foreign silent-rules])
AC_CONFIG_HEADERS([include/mega/config.h])
LT_INIT([shared static win32-dll])
AC_CONFIG_MACRO_DIR([m4])
# enable silent build
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES])
AM_SILENT_RULES([yes])

MEGA_MAJOR_VERSION=mega_major_version
MEGA_MINOR_VERSION=mega_minor_version
MEGA_MICRO_VERSION=mega_micro_version

AC_SUBST(MEGA_MAJOR_VERSION)
AC_SUBST(MEGA_MINOR_VERSION)
AC_SUBST(MEGA_MICRO_VERSION)

AC_DEFINE(MEGA_MAJOR_VERSION, [mega_major_version],
	  [MEGA SDK major version.])
AC_DEFINE(MEGA_MINOR_VERSION, [mega_minor_version],
	  [MEGA SDK minor version.])
AC_DEFINE(MEGA_MICRO_VERSION, [mega_micro_version],
	  [MEGA SDK micro version.])

LT_CURRENT=mega_lt_current
LT_REVISION=mega_lt_revision
LT_AGE=mega_lt_age

AC_SUBST(LT_CURRENT)
AC_SUBST(LT_REVISION)
AC_SUBST(LT_AGE)


# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T
AC_TYPE_UINT8_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T

# Check programs
AC_PROG_CXX
AC_PROG_LIBTOOL
AM_SANITY_CHECK

# Check for cppcheck
AC_CHECK_PROG(HAVE_CPPCHECK, cppcheck, yes)
AM_CONDITIONAL(CPPCHECK, test -n "$HAVE_CPPCHECK")

# set C++
AC_LANG_CPLUSPLUS

# Check headers
AC_STDC_HEADERS
AC_HEADER_STDC
AC_HEADER_STDBOOL
AC_CHECK_HEADERS([arpa/inet.h netdb.h netinet/in.h stddef.h stdint.h stdlib.h sys/socket.h sys/timeb.h])

# Checks for inotify support.
AC_CHECK_HEADERS([sys/inotify.h mcheck.h])
AC_CHECK_FUNCS([inotify_init12], [AC_DEFINE([USE_INOTIFY], [1], [Use inotify API])])

#XXX: add detection of libcrypto++
PKG_CHECK_MODULES([CRYPTO], [libcrypto++])
AC_DEFINE(USE_CRYPTOPP, [1], [Define to use libcrypto++])

# Check if building for Win32, determine Win32 API libs
AC_MSG_CHECKING([if building for Win32 platform])
case $host in
  *-*-cygwin*)
	LIBS_EXTRA="-luser32 -lkernel32"
    WIN32=yes
    ;;
  *-*-mingw*)
	LIBS_EXTRA="-lws2_32 -lole32 -lwinmm -lshlwapi"
    WIN32=yes
    ;;
  *)
	LIBS_EXTRA=""
    WIN32=no
    ;;
esac
AC_MSG_RESULT([${WIN32}])
AM_CONDITIONAL([WIN32], [test "${WIN32}" = "yes"])
AC_SUBST(LIBS_EXTRA)

#
# ** DB layer **
#
SAVE_LDFLAGS=$LDFLAGS
SAVE_CPPFLAGS=$CPPFLAGS

# SQLite3
sqlite=false
AC_MSG_CHECKING(for SQLite)
AC_ARG_WITH(sqlite,
  [  --with-sqlite=PATH       base of SQLite installation],
  [AC_MSG_RESULT($with_sqlite)
   case $with_sqlite in
   no)
    sqlite=false
     ;;
   yes)
    AC_CHECK_HEADERS([sqlite3.h],, [
        AC_MSG_ERROR([sqlite3.h header not found or not usable])
    ])
    AC_CHECK_LIB(sqlite3, [sqlite3_open], [DB_LDFLAGS="-lsqlite3"],[
            AC_MSG_ERROR([Could not find libsqlite3])
    ])
    sqlite=true
     ;;
   *)
    # set temp variables
    LDFLAGS="-L$with_sqlite/lib $LDFLAGS"
    CPPFLAGS="-I$with_sqlite/include $CPPFLAGS"

    AC_CHECK_HEADERS(sqlite3.h,
     EXT_LIB_PATH="-L$with_sqlite/lib $EXT_LIB_PATH"
     DB_LDFLAGS="-L$with_sqlite/lib"
     DB_CPPFLAGS="-I$with_sqlite/include",
     AC_MSG_ERROR([sqlite3.h header not found or not usable])
     )
    AC_CHECK_LIB(sqlite3, [sqlite3_open], [DB_LDFLAGS="-lsqlite3"],[
            AC_MSG_ERROR([Could not find libsqlite3])
    ])

    sqlite=true

    #restore
    LDFLAGS=$SAVE_LDFLAGS
    CPPFLAGS=$SAVE_CPPFLAGS
    ;;
   esac
  ],
  [AC_MSG_RESULT([--with-sqlite not specified])]
  )
AM_CONDITIONAL(USE_SQLITE, test x$sqlite = xtrue)
AC_SUBST(DB_CPPFLAGS)
AC_SUBST(DB_LDFLAGS)

# Berkeley DB
db=false
AC_MSG_CHECKING(for Berkeley DB)
AC_ARG_WITH(db,
  [  --with-db=PATH       base of Berkeley DB installation],
  [AC_MSG_RESULT($with_db)
   case $with_db in
   no)
     db=false
     ;;
   yes)
    AC_CHECK_HEADERS([db_cxx.h],, [
        AC_MSG_ERROR([db_cxx.h header not found or not usable])
    ])

    AC_CHECK_LIB(db_cxx, [open], [DB_LIBS="-ldb_cxx"],[
            AC_MSG_ERROR([Could not find libdb_cxx])
    ])
    db=true
     ;;
   *)
    # set temp variables
    LDFLAGS="-L$with_db/lib $LDFLAGS"
    CPPFLAGS="-I$with_db/include $CPPFLAGS"

    AC_CHECK_HEADERS(db_cxx.h,
     EXT_LIB_PATH="-L$with_db/lib $EXT_LIB_PATH"
     DB_LDFLAGS="-L$with_db/lib"
     DB_CPPFLAGS="-I$with_db/include",
     AC_MSG_ERROR([db_cxx.h header not found or not usable])
     )
    AC_CHECK_LIB(db_cxx, [open], [DB_LIBS="-ldb_cxx"],[
            AC_MSG_ERROR([Could not find libdb_cxx])
    ])
    db=true

    #restore
    LDFLAGS=$SAVE_LDFLAGS
    CPPFLAGS=$SAVE_CPPFLAGS
    ;;
   esac
  ],
  [AC_MSG_RESULT([--with-db not specified])]
  )
AM_CONDITIONAL(USE_DB, test "x$db = xtrue")
AC_SUBST(DB_CPPFLAGS)
AC_SUBST(DB_LDFLAGS)

# check if both DB layers are selected
if test "x$sqlite" = "xtrue" ; then
    if test "x$db" = "xtrue" ; then
        AC_MSG_ERROR([Please provide exactly one DB access layer, either --enable-sqlite or --enable-db.])
    fi
fi

# check if no DB layer is selected, use SQLite by the default
if test "x$sqlite" = "xfalse" ; then
    if test "x$db" = "xfalse" ; then
        AC_MSG_NOTICE([Using SQLite3 as the default DB access layer.])

        AC_CHECK_HEADERS([sqlite3.h],, [
            AC_MSG_ERROR([sqlite3.h header not found or not usable])
        ])
        AC_CHECK_LIB(sqlite3, [sqlite3_open], [DB_LDFLAGS="-lsqlite3"],[
                AC_MSG_ERROR([Could not find libsqlite3])
        ])
        sqlite=true

        AM_CONDITIONAL(USE_SQLITE, test x$sqlite = xtrue)
        AC_SUBST(DB_CPPFLAGS)
        AC_SUBST(DB_LDFLAGS)
    fi
fi

if test "x$sqlite" = "xtrue" ; then
    AC_DEFINE(USE_SQLITE, [1], [Define to use SQLite])
    AC_DEFINE(USE_DB, [0], [Define to use Berkeley DB])
else
    AC_DEFINE(USE_SQLITE, [0], [Define to use SQLite])
    AC_DEFINE(USE_DB, [1], [Define to use Berkeley DB])
fi
#
# ** Posix dependent libraries **
#
if test "x$WIN32" = "xno" ; then
    # check for cURL configuration script
    AC_CHECK_PROG(curl_config_prog, curl-config, yes, no)
    if test "x$curl_config_prog" != "xyes" ; then
        AC_MSG_ERROR([Unable to find libcurl configuration program curl-config in the path.])
    fi
    LIBCURL_FLAGS=`curl-config --cflags`
    LIBCURL_LIBS=`curl-config --libs`

    AC_SUBST(LIBCURL_FLAGS)
    AC_SUBST(LIBCURL_LIBS)
fi

# determine platform include path
if test "x$WIN32" = "xyes" ; then
    platform_include="mega/win32"
else
    platform_include="mega/posix"
fi
AC_SUBST(platform_include)

# Debug
AC_ARG_ENABLE(debug,
     AS_HELP_STRING(--enable-debug, enable support for running in debug mode),
        [], [enable_debug=no])

# Enable debug flags / build
if test "x$enable_debug" = "xyes" ; then
    CXXFLAGS="$CXXFLAGS -Wall -g -ggdb3 -O0"
else
    CXXFLAGS="$CXXFLAGS"
fi

# Examples
AC_MSG_CHECKING([if building example applications])
AC_ARG_ENABLE([examples],
     AS_HELP_STRING([--enable-examples build example applications]),
	[], [enable_examples=no])
AM_CONDITIONAL([BUILD_EXAMPLES], [test "$enable_examples" = "yes"])
AC_MSG_RESULT([$enable_examples])

# if Examples are enables, check for specific libraries
if test "x$enable_examples" = "xyes" ; then
    # FreeImage
    AC_MSG_CHECKING(for FreeImage)
    AC_ARG_WITH(freeimage,
      [  --with-freeimage=PATH       base of FreeImage installation],
      [AC_MSG_RESULT($with_freeimage)
       case $with_freeimage in
       no)
         ;;
       yes)
        AC_CHECK_HEADERS([FreeImage.h],, [
            AC_MSG_ERROR([FreeImage.h header not found or not usable])
        ])
        AC_CHECK_LIB([freeimage], [FreeImage_Load], [], [
            AC_MSG_ERROR([FreeImage library is required for the sample client.])])

         ;;
       *)
        # set temp variables
        LDFLAGS="-L$with_freeimage/lib $LDFLAGS"
        CPPFLAGS="-I$with_freeimage/include $CPPFLAGS"

        AC_CHECK_HEADERS([FreeImage.h],
         EXT_LIB_PATH="-L$with_freeimage/lib $EXT_LIB_PATH"
         FI_LDFLAGS="-L$with_freeimage/lib"
         FI_CPPFLAGS="-I$with_freeimage/include",
         AC_MSG_ERROR([FreeImage.h header not found or not usable])
         )
        AC_CHECK_LIB([freeimage], [FreeImage_Load], [], [
            AC_MSG_ERROR([FreeImage library is required for the sample client.])])

        #restore
        LDFLAGS=$SAVE_LDFLAGS
        CPPFLAGS=$SAVE_CPPFLAGS
        ;;
       esac
      ],
      [AC_MSG_RESULT([--with-freeimage not specified])
        AC_CHECK_HEADERS([FreeImage.h],, [
            AC_MSG_ERROR([FreeImage.h header not found or not usable])
        ])
        AC_CHECK_LIB([freeimage], [FreeImage_Load], [], [
            AC_MSG_ERROR([FreeImage library is required for the sample client.])])
      ]
      )
      AC_SUBST(FI_LDFLAGS)
      AC_SUBST(FI_CPPFLAGS)

      # ReadLine
    AC_MSG_CHECKING(for Readline)
    AC_ARG_WITH(redline,
      [  --with-redline=PATH       base of Readline installation],
      [AC_MSG_RESULT($with_redline)
       case $with_readline in
       no)
         ;;
       yes)
        AC_CHECK_HEADERS([readline/readline.h],, [
            AC_MSG_ERROR([readline/readline.h header not found or not usable])
        ])
        AC_CHECK_LIB([readline], [rl_redisplay], [], [
            AC_MSG_ERROR([readline library is required for the sample client.])])

         ;;
       *)
        # set temp variables
        LDFLAGS="-L$with_readline/lib $LDFLAGS"
        CPPFLAGS="-I$with_readline/include $CPPFLAGS"

        AC_CHECK_HEADERS([readline/readline.h],
         EXT_LIB_PATH="-L$with_readline/lib $EXT_LIB_PATH"
         RL_LDFLAGS="-L$with_readline/lib"
         RL_CPPFLAGS="-I$with_readline/include",
         AC_MSG_ERROR([readline/readline.h header not found or not usable])
         )
        AC_CHECK_LIB([readline], [rl_redisplay], [], [
            AC_MSG_ERROR([readline library is required for the sample client.])])

        #restore
        LDFLAGS=$SAVE_LDFLAGS
        CPPFLAGS=$SAVE_CPPFLAGS
        ;;
       esac
      ],
      [AC_MSG_RESULT([--with-readline not specified])
        AC_CHECK_HEADERS([readline/readline.h],, [
            AC_MSG_ERROR([readline/readline.h header not found or not usable])
        ])
        AC_CHECK_LIB([readline], [rl_redisplay], [], [
            AC_MSG_ERROR([readline library is required for the sample client.])])
      ]
      )
      AC_SUBST(RL_LDFLAGS)
      AC_SUBST(RL_CPPFLAGS)
fi

# Tests
AC_MSG_CHECKING([if building test applications])
AC_ARG_ENABLE([tests],
     AS_HELP_STRING([--enable-tests, build test applications]),
	[], [enable_tests=no])
if test "x$enable_debug" = "xyes" ; then
    GTEST_FLAGS="$GTEST_FLAGS"
    GTEST_LIBS="$GTEST_LIBS"
    AC_SUBST(GTEST_FLAGS)
    AC_SUBST(GTEST_LIBS)
fi
AM_CONDITIONAL([BUILD_TEST_APPS], [test "$enable_tests" = "yes"])
AC_MSG_RESULT([$enable_tests])

# Output
AC_CONFIG_FILES([
 Makefile
 include/Makefile
 libmega.pc
])
AC_OUTPUT
