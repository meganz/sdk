##
# @file configure.ac
# @brief an input file for autoconf tool
#
# (c) 2013-2014 by Mega Limited, Wellsford, New Zealand
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

m4_include([m4/ax_prog_doxygen.m4])

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

# Define _GNU_SOURCE
# AC_GNU_SOURCE
AC_USE_SYSTEM_EXTENSIONS

AM_INIT_AUTOMAKE([1.11 foreign silent-rules])
AC_CONFIG_HEADERS([include/mega/config.h])
LT_INIT([shared static win32-dll])
AC_CONFIG_MACRO_DIR([m4])
# enable silent build
m4_ifndef([AM_SILENT_RULES], [m4_define([AM_SILENT_RULES],[])])
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
AC_TYPE_PID_T
AC_TYPE_OFF_T

AH_VERBATIM([__STDC_FORMAT_MACROS],
            [/* C99 says: define this to get the PRI... macros from stdint.h */
#ifndef __STDC_FORMAT_MACROS
# define __STDC_FORMAT_MACROS 1
#endif])

AC_CHECK_TYPES([ssize_t])

# Check programs
AC_PROG_CXX
if test "$CXX" = no || test "$CXX:$GXX" = "g++:"; then
  AC_MSG_ERROR([C++ compiler not found !])
fi

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

AC_DEFINE(__STDC_CONSTANT_MACROS, [], [Force definition of constant macros for C++])
AC_DEFINE(__STDC_FORMAT_MACROS, [], [Force definition of format macros for C++])
AC_DEFINE(__STDC_LIMIT_MACROS, [], [Force definition of limit macros for C++])

# Add 64-bits file support on some hosts
AC_SYS_LARGEFILE
# use new interfaces
AC_FUNC_FSEEKO
AC_TYPE_OFF_T

# Checks for typedefs, structures, and compiler characteristics.
AC_CHECK_SIZEOF([uint64_t])
AC_STRUCT_TM
AC_TYPE_OFF_T
AC_TYPE_SIZE_T

AC_CHECK_HEADERS([arpa/inet.h netdb.h netinet/in.h stddef.h stdint.h stdlib.h sys/socket.h sys/timeb.h htonl])

# Check for inotify support.
AC_ARG_ENABLE(
    [inotify],
    [AS_HELP_STRING(
        [--enable-inotify],
        [enable inotify support [default=yes]])],
    [enable_inotify=$enableval],
    [enable_inotify=yes]
)

AS_IF([test "x$enable_inotify" = "xyes"], [
    AC_CHECK_HEADERS([sys/inotify.h mcheck.h])
    AC_CHECK_FUNCS([inotify_init], [AC_DEFINE([USE_INOTIFY], [1], [Use inotify API])])
])

# Check for particular functions
AC_CHECK_FUNCS(fdopendir select)
AC_CHECK_LIB([sendfile], [sendfile])
AC_CHECK_LIB([socket], [socket])
AC_CHECK_LIB([rt], [clock_gettime])

AC_FUNC_MALLOC

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

SAVE_LDFLAGS=$LDFLAGS
SAVE_CXXFLAGS=$CXXFLAGS
SAVE_CPPFLAGS=$CPPFLAGS

#libcryptopp
CRYPTO_LIBS="-lcryptopp"
AC_MSG_CHECKING(for libcryptopp)
cryptopp=false
AC_ARG_WITH(cryptopp,
  AS_HELP_STRING(--with-cryptopp=PATH, base of libcrypto installation),
  [
   case $with_cryptopp in
   no)
    cryptopp=false
     ;;
   yes)
    AC_CHECK_HEADERS([cryptopp/cryptlib.h],, [
        AC_MSG_ERROR([cryptopp/cryptlib.h header not found or not usable])
    ])
    AC_CHECK_LIB(cryptopp, [main], [CRYPTO_LIBS="-lcryptopp"],[
            AC_MSG_ERROR([Could not find libcryptopp])
    ])
    cryptopp=true
     ;;
   *)

    # determine if library is installed
    if test -d "$with_cryptopp/lib"; then
        LDFLAGS="-L$with_cryptopp/lib $LDFLAGS"
        CXXFLAGS="-I$with_cryptopp/include $CXXFLAGS"
        CPPFLAGS="-I$with_cryptopp/include $CPPFLAGS"

        AC_CHECK_HEADERS(cryptopp/cryptlib.h,
         CRYPTO_CXXFLAGS="-I$with_cryptopp/include"
         CRYPTO_CPPFLAGS="-I$with_cryptopp/include"
         CRYPTO_LDFLAGS="-L$with_cryptopp/lib",
         AC_MSG_ERROR([cryptopp/cryptlib.h header not found or not usable])
         )
    # assume we are using crypto source directory
    else
        LDFLAGS="-L$with_cryptopp/cryptopp $LDFLAGS"
        CXXFLAGS="-I$with_cryptopp $CXXFLAGS"
        CPPFLAGS="-I$with_cryptopp $CPPFLAGS"

        AC_CHECK_HEADERS(cryptopp/cryptlib.h,
         CRYPTO_CXXFLAGS="-I$with_cryptopp"
         CRYPTO_CPPFLAGS="-I$with_cryptopp"
         CRYPTO_LDFLAGS="-L$with_cryptopp/cryptopp",
         AC_MSG_ERROR([cryptopp/cryptlib.h header not found or not usable])
         )
    fi

    AC_CHECK_LIB(cryptopp, [main], [CRYPTO_LIBS="-lcryptopp"],[
            AC_MSG_ERROR([Could not find libcryptopp])
    ])

    cryptopp=true

    #restore
    LDFLAGS=$SAVE_LDFLAGS
    CXXFLAGS=$SAVE_CXXFLAGS
    CPPFLAGS=$SAVE_CPPFLAGS
    ;;
   esac
  ],
  [AC_MSG_RESULT([--with-cryptopp not specified])
    AC_CHECK_HEADERS([cryptopp/cryptlib.h],, [
        AC_MSG_ERROR([cryptopp/cryptlib.h header not found or not usable])
    ])
    AC_CHECK_LIB(cryptopp, [main], [CRYPTO_LIBS="-lcryptopp"],[
            AC_MSG_ERROR([Could not find libcryptopp])
    ])
  ])
AC_SUBST(CRYPTO_CXXFLAGS)
AC_SUBST(CRYPTO_CPPFLAGS)
AC_SUBST(CRYPTO_LDFLAGS)
AC_SUBST(CRYPTO_LIBS)
AC_DEFINE(USE_CRYPTOPP, [1], [Define to use libcryptopp])

#
# Zlib
#

#zlib
ZLIB_LIBS="-lz"
AC_MSG_CHECKING(for zlib)
zlib=false
AC_ARG_WITH(zlib,
  AS_HELP_STRING(--with-zlib=PATH, base of zlib installation),
  [
   case $with_zlib in
   no)
    zlib=false
     ;;
   yes)
    AC_CHECK_HEADERS([zlib.h],, [
        AC_MSG_ERROR([zlib.h header not found or not usable])
    ])
    AC_CHECK_LIB(z, [main], [ZLIB_LIBS="-lz"],[
            AC_MSG_ERROR([Could not find zlib])
    ])
    zlib=true
     ;;
   *)

    LDFLAGS="-L$with_zlib $LDFLAGS"
    CXXFLAGS="-I$with_zlib $CXXFLAGS"
    CPPFLAGS="-I$with_zlib $CPPFLAGS"

    AC_CHECK_HEADERS(zlib.h,
     ZLIB_CXXFLAGS="-I$with_zlib"
     ZLIB_CPPFLAGS="-I$with_zlib"
     ZLIB_LDFLAGS="-L$with_zlib",
     AC_MSG_ERROR([zlib.h header not found or not usable])
     )

    AC_CHECK_LIB(z, [main], [ZLIB_LIBS="-lz"],[
            AC_MSG_ERROR([Could not find zlib])
    ])

    zlib=true

    #restore
    LDFLAGS=$SAVE_LDFLAGS
    CXXFLAGS=$SAVE_CXXFLAGS
    CPPFLAGS=$SAVE_CPPFLAGS
    ;;
   esac
  ],
  [AC_MSG_RESULT([--with-zlib not specified])
    AC_CHECK_HEADERS([zlib.h],, [
        AC_MSG_ERROR([zlib.h header not found or not usable])
    ])
    AC_CHECK_LIB(z, [main], [ZLIB_LIBS="-lz"],[
            AC_MSG_ERROR([Could not find zlib])
    ])
  ])
AC_SUBST(ZLIB_CXXFLAGS)
AC_SUBST(ZLIB_CPPFLAGS)
AC_SUBST(ZLIB_LDFLAGS)
AC_SUBST(ZLIB_LIBS)
AC_DEFINE(USE_ZLIB, [1], [Define to use zlib])

#
# ** DB layer **
#

# SQLite3
sqlite=false
AC_MSG_CHECKING(for SQLite)
AC_ARG_WITH(sqlite,
  AS_HELP_STRING(--with-sqlite=PATH, base of SQLite installation),
  [AC_MSG_RESULT($with_sqlite)
   case $with_sqlite in
   no)
    sqlite=false
     ;;
   yes)
    AC_CHECK_HEADERS([sqlite3.h],, [
        AC_MSG_ERROR([sqlite3.h header not found or not usable])
    ])
    AC_CHECK_LIB(sqlite3, [sqlite3_open], [DB_LIBS="-lsqlite3"],[
            AC_MSG_ERROR([Could not find libsqlite3])
    ])
    AC_SUBST(DB_LIBS)
    sqlite=true
     ;;
   *)

    # determine if library is installed
    if test -d "$with_sqlite/lib"; then
        LDFLAGS="-L$with_sqlite/lib $LDFLAGS"
        CXXFLAGS="-I$with_sqlite/include $CXXFLAGS"

        AC_CHECK_HEADERS(sqlite3.h,[
         DB_LDFLAGS="-L$with_sqlite/lib"
         DB_CXXFLAGS="-I$with_sqlite/include"
         DB_CPPFLAGS="-I$with_sqlite/include"],
         AC_MSG_ERROR([sqlite3.h header not found or not usable])
        )

        # use sqlite3 library
        AC_CHECK_LIB(sqlite3, [main], [DB_LIBS="-lsqlite3"],[
                AC_MSG_ERROR([Could not find libsqlite3])
        ])
        AC_SUBST(DB_LIBS)
    else
        LDFLAGS="-L$with_sqlite $LDFLAGS"
        CXXFLAGS="-I$with_sqlite $CXXFLAGS"

        AC_CHECK_HEADERS(sqlite3.h,[
         DB_LDFLAGS="-L$with_sqlite"
         DB_CXXFLAGS="-I$with_sqlite"
         DB_CPPFLAGS="-I$with_sqlite"],
         AC_MSG_ERROR([sqlite3.h header not found or not usable])
        )
    fi

    sqlite=true

    #restore
    LDFLAGS=$SAVE_LDFLAGS
    CXXFLAGS=$SAVE_CXXFLAGS
    CPPFLAGS=$SAVE_CPPFLAGS
    ;;
   esac
  ],
  [AC_MSG_RESULT([--with-sqlite not specified])]
  )
AM_CONDITIONAL(USE_SQLITE, test x$sqlite = xtrue)
AC_SUBST(DB_CXXFLAGS)
AC_SUBST(DB_CPPFLAGS)
AC_SUBST(DB_LDFLAGS)

# Berkeley DB
db=false
AC_MSG_CHECKING(for Berkeley DB)
AC_ARG_WITH(db,
  AS_HELP_STRING(--with-db=PATH, base of Berkeley DB installation),
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
    AC_SUBST(DB_LIBS)
    db=true
     ;;
   *)
    # set temp variables
    LDFLAGS="-L$with_db/lib $LDFLAGS"
    CXXFLAGS="-I$with_db/include $CXXFLAGS"

    AC_CHECK_HEADERS(db_cxx.h,
     DB_LDFLAGS="-L$with_db/lib"
     DB_CXXFLAGS="-I$with_db/include",
     AC_MSG_ERROR([db_cxx.h header not found or not usable])
     )
    AC_CHECK_LIB(db_cxx, [open], [DB_LIBS="-ldb_cxx"],[
            AC_MSG_ERROR([Could not find libdb_cxx])
    ])
    AC_SUBST(DB_LIBS)
    db=true

    #restore
    LDFLAGS=$SAVE_LDFLAGS
    CXXFLAGS=$SAVE_CXXFLAGS
    ;;
   esac
  ],
  [AC_MSG_RESULT([--with-db not specified])]
  )
AM_CONDITIONAL(USE_DB, test "x$db = xtrue")
AC_SUBST(DB_CXXFLAGS)
AC_SUBST(DB_LDFLAGS)

# check if both DB layers are selected
if test "x$sqlite" = "xtrue" ; then
    if test "x$db" = "xtrue" ; then
        AC_MSG_ERROR([Please provide exactly one DB access layer, either --with-sqlite or --with-db.])
    fi
fi

# check if no DB layer is selected, use SQLite by the default
if test "x$sqlite" = "xfalse" ; then
    if test "x$db" = "xfalse" ; then
        AC_MSG_NOTICE([Using SQLite3 as the default DB access layer.])

        AC_CHECK_HEADERS([sqlite3.h],, [
            AC_MSG_ERROR([sqlite3.h header not found or not usable])
        ])
        AC_CHECK_LIB(sqlite3, [sqlite3_open], [DB_LIBS="-lsqlite3"],[
                AC_MSG_ERROR([Could not find libsqlite3])
        ])
        sqlite=true

        AM_CONDITIONAL(USE_SQLITE, test x$sqlite = xtrue)
        AC_SUBST(DB_LIBS)
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
    # check for cURL library
    AC_MSG_CHECKING(for cURL)
    AC_ARG_WITH([curl],
      AS_HELP_STRING(--with-curl=PATH, path to curl),
      [
       case $with_curl in
       no)
         AC_MSG_ERROR([Please specify path to curl!])
         ;;
       yes)
            AC_CHECK_HEADERS([curl/curl.h],, [
                AC_MSG_ERROR([curl.h header not found or not usable])
            ])
            AC_CHECK_LIB(curl, [main], [LIBCURL_LIBS="-lcurl"],[
                    AC_MSG_ERROR([Could not find libcurl!])
            ])

            AC_SUBST(LIBCURL_LIBS)

            AC_MSG_CHECKING(for Curl compiled with c-ares support)
            if curl-config --configure  2>&1 | grep enable-ares > /dev/null;
            then
                AC_MSG_RESULT([yes])
            else
                AC_MSG_RESULT([no])
                AC_MSG_ERROR([cURL needs to be compiled with c-ares enabled!])
            fi
            AC_MSG_RESULT([yes])
         ;;
       *)

        # determine if library is installed
        LDFLAGS="-L$with_curl/lib $LDFLAGS"
        CXXFLAGS="-I$with_curl/include $CXXFLAGS"

        AC_CHECK_HEADERS([curl/curl.h],[
         LIBCURL_LIBS="-L$with_curl/lib"
         LIBCURL_FLAGS="-I$with_curl/include"],
         AC_MSG_ERROR([curl.h header not found or not usable])
        )

        AC_CHECK_LIB(curl, [main], [LIBCURL_LIBS="-lcurl"],[
            AC_MSG_ERROR([Could not find libcurl!])
        ])

        AC_SUBST(LIBCURL_FLAGS)
        AC_SUBST(LIBCURL_LIBS)

        # XXX: check for c-ares support !
     ;;
       esac
      ],
      [
        AC_CHECK_HEADERS([curl/curl.h],, [
            AC_MSG_ERROR([curl.h header not found or not usable])
        ])
        AC_CHECK_LIB(curl, [main], [LIBCURL_LIBS="-lcurl"],[
                AC_MSG_ERROR([Could not find libcurl!])
        ])

        AC_SUBST(LIBCURL_LIBS)

        AC_MSG_CHECKING(for Curl compiled with c-ares support)
        if curl-config --configure  2>&1 | grep enable-ares > /dev/null;
        then
            AC_MSG_RESULT([yes])
        else
            AC_MSG_RESULT([no])
            AC_MSG_ERROR([cURL needs to be compiled with c-ares enabled!])
        fi
      ]
    )
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
    AM_CXXFLAGS="${AM_CXXFLAGS} -Wall -g -ggdb3 -O0"
    AC_SUBST([AM_CXXFLAGS])
    AC_DEFINE(DEBUG, 1, [Define to enable debug logging])
fi

# Examples
AC_MSG_CHECKING([if building example applications])
AC_ARG_ENABLE([examples],
     AS_HELP_STRING(--enable-examples, build example applications),
	[], [enable_examples=no])
AM_CONDITIONAL([BUILD_EXAMPLES], [test "$enable_examples" = "yes"])
AC_MSG_RESULT([$enable_examples])

# FreeImage
freeimage=false
AC_MSG_CHECKING(for FreeImage)
AC_ARG_WITH(freeimage,
  AS_HELP_STRING(--with-freeimage=PATH, base of FreeImage installation),
  [AC_MSG_RESULT($with_freeimage)
   case $with_freeimage in
   no)
     freeimage=false
     ;;
   yes)
    AC_CHECK_HEADERS([FreeImage.h],, [
        AC_MSG_ERROR([FreeImage.h header not found or not usable])
    ])
    AC_CHECK_LIB([freeimage], [main], [FI_LIBS="-lfreeimage"], [
        AC_MSG_ERROR([FreeImage library is not found!])])

     freeimage=true
     ;;
   *)

    # determine if library is installed
    if test -d "$with_freeimage/lib"; then
        LDFLAGS="-L$with_freeimage/lib $LDFLAGS"
        CXXFLAGS="-I$with_freeimage/include $CXXFLAGS"
        CPPFLAGS="-I$with_freeimage/include $CPPFLAGS"

        AC_CHECK_HEADERS([FreeImage.h],[
         FI_LDFLAGS="-L$with_freeimage/lib"
         FI_CXXFLAGS="-I$with_freeimage/include"
         FI_CPPFLAGS="-I$with_freeimage/include"],
         AC_MSG_ERROR([FreeImage.h header not found or not usable])
        )
    else
        LDFLAGS="-L$with_freeimage/Dist $LDFLAGS"
        CXXFLAGS="-I$with_freeimage/Dist $CXXFLAGS"
        CPPFLAGS="-I$with_freeimage/Dist $CPPFLAGS"

        AC_CHECK_HEADERS([FreeImage.h],[
         FI_LDFLAGS="-L$with_freeimage/Dist"
         FI_CXXFLAGS="-I$with_freeimage/Dist"
         FI_CPPFLAGS="-I$with_freeimage/Dist"],
         AC_MSG_ERROR([FreeImage.h header not found or not usable])
        )
    fi

    # check and set FI library
    AC_CHECK_LIB([freeimage], [main], [FI_LIBS="-lfreeimage"], [
        AC_MSG_ERROR([FreeImage library is not found!])])

    #restore
    LDFLAGS=$SAVE_LDFLAGS
    CXXFLAGS=$SAVE_CXXFLAGS
    CPPFLAGS=$SAVE_CPPFLAGS
    freeimage=true
    ;;
   esac
  ],
  [AC_MSG_RESULT([--with-freeimage not specified])
    AC_CHECK_HEADERS([FreeImage.h],, [
        AC_MSG_ERROR([FreeImage.h header not found or not usable])
    ])
    AC_CHECK_LIB([freeimage], [main], [FI_LIBS="-lfreeimage"], [
        AC_MSG_ERROR([FreeImage library is not found!])])

     freeimage=true
  ]
  )
AC_SUBST(FI_LDFLAGS)
AC_SUBST(FI_LIBS)
AC_SUBST(FI_CXXFLAGS)
AC_SUBST(FI_CPPFLAGS)
if test "x$freeimage" = "xtrue" ; then
    AC_DEFINE(USE_FREEIMAGE, 1, [Define to use FreeImage library.])
fi
AM_CONDITIONAL([USE_FREEIMAGE], [test "x$freeimage" = "xtrue"])

# if Examples are enables, check for specific libraries
if test "x$enable_examples" = "xyes" ; then
      # ReadLine
    AC_MSG_CHECKING(for Readline)
    AC_ARG_WITH(readline,
      AS_HELP_STRING(--with-readline=PATH, base of Readline installation),
      [AC_MSG_RESULT($with_redline)
       case $with_readline in
       no)
         ;;
       yes)
        AC_CHECK_HEADERS([readline/readline.h],, [
            AC_MSG_ERROR([readline/readline.h header not found or not usable])
        ])
        AC_CHECK_LIB([readline], [rl_redisplay], [RL_LIBS="-lreadline"], [
            AC_MSG_ERROR([readline library is required for the sample client.])])

         ;;
       *)

        # determine if library is installed
        if test -d "$with_readline/lib"; then
            LDFLAGS="-L$with_readline/lib $LDFLAGS"
            CXXFLAGS="-I$with_readline/include $CXXFLAGS"
            CPPFLAGS="-I$with_readline/include $CPPFLAGS"
            AC_CHECK_HEADERS([readline/readline.h], [
             RL_LDFLAGS="-L$with_readline/lib "
             RL_CXXFLAGS="-I$with_readline/include "],
             AC_MSG_ERROR([readline/readline.h header not found or not usable])
            )
        else
            LDFLAGS="-L$with_readline $LDFLAGS"
            CXXFLAGS="-I$with_readline $CXXFLAGS"
            CPPFLAGS="-I$with_readline $CPPFLAGS"
            AC_CHECK_HEADERS([readline/readline.h], [
             RL_LDFLAGS="-L$with_readline"
             RL_CXXFLAGS="-I$with_readline"],
             AC_MSG_ERROR([readline/readline.h header not found or not usable])
            )
        fi

        AC_CHECK_LIB([readline], [rl_redisplay], [RL_LIBS="-lreadline"], [
            AC_MSG_ERROR([readline library is required for the sample client.])])

        #restore
        LDFLAGS=$SAVE_LDFLAGS
        CXXFLAGS=$SAVE_CXXFLAGS
        CPPFLAGS=$SAVE_CPPFLAGS
        ;;
       esac
      ],
      [AC_MSG_RESULT([--with-readline not specified])
        AC_CHECK_HEADERS([readline/readline.h],, [
            AC_MSG_ERROR([readline/readline.h header not found or not usable])
        ])
        AC_CHECK_LIB([readline], [rl_redisplay], [RL_LIBS="-lreadline"], [
            AC_MSG_ERROR([readline library is required for the sample client.])])
      ]
      )
      AC_SUBST(RL_LDFLAGS)
      AC_SUBST(RL_LIBS)
      AC_SUBST(RL_CXXFLAGS)

    if test "x$WIN32" = "xyes" ; then
        AC_MSG_CHECKING(for termcap)
        AC_ARG_WITH(termcap,
          AS_HELP_STRING(--with-termcap=PATH, base of termcap installation),
          [AC_MSG_RESULT($with_termcap)
           case $with_termcap in
           no)
             ;;
           *)
            TERMCAP_LDFLAGS="-L$with_termcap"
            TERMCAP_LIBS="-ltermcap"
            TERMCAP_CXXFLAGS="-I$with_termcap"
            TERMCAP_CPPFLAGS="-I$with_termcap"
            ;;
           esac
          ],
            AC_MSG_ERROR([termcap.h header not found or not usable])
          )
          AC_SUBST(TERMCAP_LDFLAGS)
          AC_SUBST(TERMCAP_LIBS)
          AC_SUBST(TERMCAP_CXXFLAGS)
          AC_SUBST(TERMCAP_CPPFLAGS)
    fi
fi

if test "x$WIN32" = "xyes" ; then
    AC_MSG_CHECKING(for WinHTTP)
    AC_ARG_WITH(winhttp,
      AS_HELP_STRING(--with-winhttp=PATH, base of WinHTTP installation),
      [AC_MSG_RESULT($with_winhttp)
       case $with_winhttp in
       no)
         ;;
       *)
        WINHTTP_LDFLAGS="-L$with_winhttp"
        WINHTTP_LIBS="-lwinhttp"
        WINHTTP_CXXFLAGS="-I$with_winhttp"
        WINHTTP_CPPFLAGS="-I$with_winhttp"
        ;;
       esac
      ],
        AC_MSG_ERROR([WinHTTP.h header not found or not usable])
      )
      AC_SUBST(WINHTTP_LDFLAGS)
      AC_SUBST(WINHTTP_LIBS)
      AC_SUBST(WINHTTP_CXXFLAGS)
      AC_SUBST(WINHTTP_CPPFLAGS)
fi

# Tests
AC_MSG_CHECKING([if building tests])
AC_ARG_ENABLE([tests],
     AS_HELP_STRING(--enable-tests, build test applications),
	[], [enable_tests=no])
if test "x$enable_tests" = "xyes" ; then
    AC_ARG_WITH(gtest,
        AS_HELP_STRING(--with-gtest, specify GTest location),
        [AC_SUBST([GTEST_DIR],[$with_gtest])],
        [AC_MSG_ERROR([GTest location must be specified])]
    )
fi
AM_CONDITIONAL([BUILD_TESTS], [test "$enable_tests" = "yes"])
AC_MSG_RESULT([$enable_tests])

dnl #########################################################################
dnl Doxygen settings
dnl #########################################################################

DX_INIT_DOXYGEN($PACKAGE_NAME, doc/Doxyfile, doc/api)
DX_DOXYGEN_FEATURE(ON)
DX_DOT_FEATURE(ON)
DX_HTML_FEATURE(ON)
DX_CHM_FEATURE(OFF)
DX_CHI_FEATURE(OFF)
DX_MAN_FEATURE(OFF)
DX_RTF_FEATURE(OFF)
DX_XML_FEATURE(OFF)
DX_PDF_FEATURE(OFF)
DX_PS_FEATURE(OFF)

# Output
AC_CONFIG_FILES([
 Makefile
 include/Makefile
 libmega.pc
])
AC_OUTPUT
