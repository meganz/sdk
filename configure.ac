##
# @file configure.ac
# @brief an input file for autoconf tool
#
# (c) 2013-2015 by Mega Limited, Auckland, New Zealand
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
m4_include([m4/curlchk.m4])
m4_include([m4/ax_check_compile_flag.m4])
m4_include([m4/ax_check_link_flag.m4])
m4_include([m4/ax_cxx_compile_stdcxx.m4])

AC_PREREQ([2.61])

# The Mega SDK version number is generated into config.h.
# The version in Git should reflect the *next* version planned.
m4_define([mega_major_version], [$(cat include/mega/version.h | grep "define MEGA_MAJOR" | cut -d" " -f 3)])
m4_define([mega_minor_version], [$(cat include/mega/version.h | grep "define MEGA_MINOR" | cut -d" " -f 3)])
m4_define([mega_micro_version], [$(cat include/mega/version.h | grep "define MEGA_MICRO" | cut -d" " -f 3)])

m4_define([mega_version],
          [mega_major_version.mega_minor_version.mega_micro_version])

# libtool interface versioning
m4_define([mega_lt_revision], [0])
m4_define([mega_lt_current], [$(grep define <include/mega/version.h | awk '{print @S|@3}' | awk 'BEGIN { RS = "" ; FS = "\n" } {printf @S|@1*10000+@S|@2*100+@S|@3}')])
m4_define([mega_lt_age], [0])

AC_INIT([libmega], m4_esyscmd([ grep define < include/mega/version.h | awk '{print $3}' | awk 'BEGIN { RS = "" ; FS = "\n" } {printf $1"."$2"."$3}']), [https://github.com/meganz/sdk])

#override default /usr/local prefix
AC_PREFIX_DEFAULT([/])

# Define _GNU_SOURCE
# AC_GNU_SOURCE
AC_USE_SYSTEM_EXTENSIONS

AM_INIT_AUTOMAKE([1.11 foreign silent-rules])
AC_CONFIG_HEADERS([include/mega/config.h])
LT_INIT([shared disable-static win32-dll])
AC_CONFIG_MACRO_DIR([m4])
# enable silent build
m4_ifndef([AM_SILENT_RULES], [m4_define([AM_SILENT_RULES],[])])
AM_SILENT_RULES([yes])

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

AC_CHECK_TYPES([ssize_t])

AC_CANONICAL_HOST
AC_DEFINE_UNQUOTED(OS, "${host}", [cpu-machine-OS])

# Check programs
AC_PROG_OBJCXX
AC_PROG_CXX
if test "$CXX" = no || test "$CXX:$GXX" = "g++:"; then
  AC_MSG_ERROR([C++ compiler not found !])
fi

AC_PROG_LIBTOOL
AM_SANITY_CHECK

# Check for cppcheck
AC_CHECK_PROG(CPPCHECK, cppcheck, yes)
AM_CONDITIONAL(CPPCHECK, test -n "$CPPCHECK")

# set C++
AC_LANG_CPLUSPLUS

# Check for C++11 standard
AX_CXX_COMPILE_STDCXX([11], [noext], [mandatory], [default])

# Check headers
AC_STDC_HEADERS
AC_HEADER_STDC
AC_HEADER_STDBOOL
AC_HEADER_DIRENT


AH_VERBATIM([__STDC_CONSTANT_MACROS], [/*Force definition of constant macros for C++*/
#ifndef __STDC_CONSTANT_MACROS
#define __STDC_CONSTANT_MACROS
#endif])

AH_VERBATIM([__STDC_FORMAT_MACROS], [/*Force definition of format macros for C++*/
#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif])

AH_VERBATIM([__STDC_LIMIT_MACROS], [/*Force definition of limit macros for C++*/
#ifndef __STDC_LIMIT_MACROS
#define __STDC_LIMIT_MACROS
#endif])


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

AC_CHECK_HEADERS([arpa/inet.h netdb.h netinet/in.h stddef.h stdint.h stdlib.h sys/socket.h sys/timeb.h htonl glob.h malloc.h sys/malloc.h malloc/malloc.h])

# Debug
AC_ARG_ENABLE(debug,
    AS_HELP_STRING([--enable-debug], [enable support for running in debug mode]),
    [], [enable_debug=no])

CXXFLAGS="$CXXFLAGS -std=c++11 -Wall -Wextra -Wconversion -Wno-unused-parameter"
# Enable debug flags / build
if test "x$enable_debug" = "xyes" ; then
    AM_CXXFLAGS="-Wall -g -ggdb3 -O0"
    CXXFLAGS="$CXXFLAGS -g -ggdb3 -O0"
    AC_SUBST([AM_CXXFLAGS])
    AC_DEFINE(DEBUG, 1, [Define to enable debug logging])

    # Limit -Werror to debug builds
    if test "x$enable_werror" = "xyes" ; then
       CXXFLAGS="$CXXFLAGS -Werror"
       # Warnings which should not be promoted to errors, but still appear as warnings
       CXXFLAGS="$CXXFLAGS -Wno-error=deprecated-declarations"
    fi
else
    CXXFLAGS="$CXXFLAGS -DNDEBUG=1"
fi

# Warnings as errors
AC_ARG_ENABLE(werror,
    AS_HELP_STRING([--enable-werror], [enable warnings promotion to errors]),
    [], [enable_werror=no])

# Check if we can use -fPIC flag
AX_CHECK_COMPILE_FLAG([-fPIC], [
  AX_CHECK_LINK_FLAG([-fPIC],
    [CXXFLAGS="$CXXFLAGS -fPIC"]
  )
])


AM_CONDITIONAL(ENABLE_STATIC, test x$enable_static = xyes)
AC_SUBST(ENABLE_STATIC)

AC_CHECK_LIB(dl, dlopen, [LIBS="-ldl $LIBS"])

# Check for inotify support.
AC_ARG_ENABLE(inotify,
    AS_HELP_STRING([--enable-inotify], [enable inotify support [default=yes]])],
    [enable_inotify=$enableval],
    [enable_inotify=yes]
)

AS_IF([test "x$enable_inotify" = "xyes"], [
    AC_CHECK_HEADERS([sys/inotify.h mcheck.h])
    AC_CHECK_FUNCS([inotify_init1], [AC_DEFINE([USE_INOTIFY], [1], [Use inotify API])])
])

# Check for particular functions
AC_CHECK_FUNCS(fdopendir select)
AC_CHECK_LIB([sendfile], [sendfile])
AC_CHECK_LIB([socket], [socket])
AC_CHECK_LIB([rt], [clock_gettime])

# check for malloc only on for native builds
# users report issues with cross-compiling builds
#AS_IF([test "x$cross_compiling" != "xyes"], [AC_FUNC_MALLOC AC_FUNC_REALLOC])

# Check if building for Win32, determine Win32 API libs
WIN32=no
DARWIN=no
ANDROID=no
LINUX=no
AC_MSG_CHECKING([platform options])
case $host in
  *-*-cygwin*)
    LIBS_EXTRA="-luser32 -lkernel32"
    CXXFLAGS="$CXXFLAGS -DNOMINMAX"
    WIN32=yes
    ;;
  *-*-mingw*)
    LIBS_EXTRA="-lws2_32 -lcrypt32 -lole32 -lwinmm -lshlwapi -lshell32"
    CXXFLAGS="$CXXFLAGS -DUNICODE -DWINVER=0x0501 -DHAVE_STRUCT_TIMESPEC -DNOMINMAX"
    WIN32=yes
    ;;
   *-*-msys*)
    LIBS_EXTRA="-lws2_32 -lcrypt32 -lole32 -lwinmm -lshlwapi -lshell32"
    CXXFLAGS="$CXXFLAGS -DUNICODE -DWINVER=0x0501 -DHAVE_STRUCT_TIMESPEC -DNOMINMAX -Wl,-no-undefined"
	LIBMEGA_EXTRALDFLAGS="-no-undefined"
	AC_SUBST(LIBMEGA_EXTRALDFLAGS)
    WIN32=yes
    ;;
 *-apple-darwin*)
    AC_DEFINE([_XOPEN_SOURCE], [500], [Define _XOPEN_SOURCE])
    AC_DEFINE([_DARWIN_C_SOURCE], [1], [Define _DARWIN_C_SOURCE])
    DARWIN=yes
    ;;
  *-android*)
    ANDROID=yes
    ;;
  *-linux*)
    LINUX=yes
    ;;
  *)
    LIBS_EXTRA=""
    ;;
esac
AC_MSG_RESULT([${WIN32}])
AM_CONDITIONAL([WIN32], [test "${WIN32}" = "yes"])
AM_CONDITIONAL([DARWIN], [test "${DARWIN}" = "yes"])
AM_CONDITIONAL([ANDROID], [test "${ANDROID}" = "yes"])
AM_CONDITIONAL([LINUX], [test "${LINUX}" = "yes"])

AC_SUBST(LIBS_EXTRA)

if test -n "$IOSC_TARGET"; then
#	bu=yes
    AC_DEFINE(USE_IOS, 1, [Use IOS])
fi
AM_CONDITIONAL([USE_IOS], [test  -n "$IOSC_TARGET"])



# check for Pthreads
AC_ARG_ENABLE(posix-threads,
    AS_HELP_STRING(--disable-posix-threads, disable POSIX threads support),
    [enable_posix_threads=no],
    [enable_posix_threads=yes])
if test x$enable_posix_threads != xno; then
    AC_CHECK_LIB(pthread, pthread_create, [HAVE_PTHREAD=yes], [HAVE_PTHREAD=no])
else
    HAVE_PTHREAD=no
fi
if test "$HAVE_PTHREAD" = "yes"; then
    AC_DEFINE(USE_PTHREAD, 1, [Defined if pthreads are available])
fi
AM_CONDITIONAL([HAVE_PTHREAD], [test "${HAVE_PTHREAD}" = "yes"])


# hardening options
AC_ARG_ENABLE(gcc-hardening,
    AS_HELP_STRING(--disable-gcc-hardening, disable compiler security checks),
    [],
    [enable_gcc_hardening=no])
if test x$enable_gcc_hardening != xno; then
    CXXFLAGS="$CXXFLAGS -D_FORTIFY_SOURCE=2"
    AX_CHECK_COMPILE_FLAG([-fstack-protector-all], [
        AX_CHECK_LINK_FLAG([-fstack-protector-all],
        [CXXFLAGS="$CXXFLAGS -fstack-protector-all"]
    )
])
fi

# sync subsystem

AC_ARG_ENABLE(sync,
    AS_HELP_STRING([--enable-sync], [include sync subsystem [default=yes]]),
    [enable_sync=$enableval
    if test "x$enable_sync" = "xyes" && (test "x$enable_java" = "xyes" || test "x$enable_python" = "xyes" || test "x$enable_php" = "xyes"); then
        AC_MSG_ERROR([--enable-sync not allowed when building bindings (php/python/java)!])
    fi
    ],
    [enable_sync=yes])

if test "x$enable_java" = "xyes" || test "x$enable_python" = "xyes" || test "x$enable_php" = "xyes" ; then
    enable_sync=no
fi

AS_IF([test "x$enable_sync" = "xyes" ], [
    AC_DEFINE(ENABLE_SYNC, 1, [Defined if sync subsystem is enabled])
])

AM_CONDITIONAL([ENABLE_SYNC], [test "x$enable_sync" = "xyes"])

# Drive Notifications
AC_ARG_ENABLE(drive-notifications,
    AS_HELP_STRING([--enable-drive-notifications], [enable Drive Notifications support [default=no]]),
    [enable_drive_notifications=${enableval}],
    [enable_drive_notifications=no])
AS_IF([test "x$enable_drive_notifications" = "xyes"], [
    # Add USE_DRIVE_NOTIFICATIONS preprocessor definition to config.h
    AC_DEFINE(USE_DRIVE_NOTIFICATIONS, 1, [Define to enable Drive Notifications])
    # Add -DUSE_DRIVE_NOTIFICATIONS to build command
    CXXFLAGS="$CXXFLAGS -DUSE_DRIVE_NOTIFICATIONS"
    # Add lib to linker command
    if test "x$LINUX" = "xyes"; then
        LDFLAGS="$LDFLAGS -ludev"
    elif test "x$WIN32" = "xyes"; then
        LDFLAGS="$LDFLAGS -lwbemuuid"
    elif test "x$DARWIN" = "xyes"; then
        LDFLAGS="$LDFLAGS -framework CoreFoundation -framework DiskArbitration"
    fi
])
AM_CONDITIONAL([USE_DRIVE_NOTIFICATIONS], [test "x$enable_drive_notifications" = "xyes"])


# MEGA_USE_C_ARES symbol for c-ares
AC_ARG_ENABLE(mega-c-ares,
    AS_HELP_STRING([--disable-mega-c-ares], [do not define MEGA_USE_C_ARES symbol]),
    [enable_mega_c_ares=${enableval}],
    [enable_mega_c_ares=yes])
AS_IF([test "x$enable_mega_c_ares" != "xno"], [
    # Add MEGA_USE_C_ARES preprocessor definition to config.h
    AC_DEFINE(MEGA_USE_C_ARES, 1, [Define MEGA_USE_C_ARES symbol])
    # Add -DMEGA_USE_C_ARES to build command
    CXXFLAGS="$CXXFLAGS -DMEGA_USE_C_ARES"
    # Reset it to a relevant and readable value, to be used in the final report
    enable_mega_c_ares=yes
])
AM_CONDITIONAL([MEGA_USE_C_ARES], [test "x$enable_mega_c_ares" = "xyes"])


# MEGA API
AC_ARG_ENABLE(megaapi,
    AS_HELP_STRING([--disable-megaapi], [disable MEGA API support]),
    [enable_megaapi=${enableval}],
    [enable_megaapi=yes])
if test "x$enable_megaapi" != "xno"; then
    [enable_megaapi=yes]
    AC_DEFINE(USE_MEGAAPI, 1, [Defined if MEGA API enabled])
fi
AM_CONDITIONAL([BUILD_MEGAAPI], [test "x$enable_megaapi" = "xyes"])

# MEGA API RPC
AC_ARG_ENABLE(megaapirpc,
    AS_HELP_STRING([--disable-megaapirpc], [disable MEGA API RPC support]),
    [enable_megaapirpc=${enableval}],
    [enable_megaapirpc=no])
if test "x$enable_megaapirpc" != "xno"; then
    [enable_megaapirpc=yes]
    AC_DEFINE(HAVE_MEGAAPI_RPC, 1, [Defined if MEGA API enabled])
fi

# Log performance
AC_ARG_ENABLE(log-performance,
    AS_HELP_STRING([--enable-log-performance], [enable log performance [default=no]]),
    [enable_log_performance=${enableval}],
    [enable_log_performance=no])
if test "x$enable_log_performance" = "xyes"; then
    AC_DEFINE(ENABLE_LOG_PERFORMANCE, 1, [Defined if log performance enabled])
fi

# Java
AC_MSG_CHECKING([if building Java bindings])
AC_ARG_ENABLE(java,
    AS_HELP_STRING([--enable-java], [build Java language bindings]),
    [AC_MSG_RESULT([yes])],
    [
     AC_MSG_RESULT([no])
     enable_java=no]
)
AC_ARG_WITH(java-include-dir,
    AS_HELP_STRING([--with-java-include-dir=DIR], [look in DIR for Java headers]),
	[JAVA_INCLUDE_DIR=$withval],)
AC_SUBST(JAVA_INCLUDE_DIR)

if test "x$enable_java" = "xyes" ; then
    AX_PKG_SWIG(2.0.0, [], [
        AC_MSG_ERROR([SWIG executable not found!])
    ])
    AX_SWIG_ENABLE_CXX
    AX_SWIG_MULTI_MODULE_SUPPORT

    if test -z "$SWIG"; then
        AC_MSG_ERROR([SWIG executable not found!])
    fi

    AC_CHECK_PROG(JAVA, java, yes, no)
    if test "x$JAVA" = "xno"; then
      AC_MSG_ERROR([no java binary in PATH])
    fi
    AC_CHECK_PROG(JAVAC, javac, yes, no)
    if test "x$JAVAC" = "xno"; then
      AC_MSG_ERROR([no javac binary in PATH])
    fi
    AC_CHECK_PROG(JAR, jar, yes, no)
    if test "x$JAR" = "xno"; then
      AC_MSG_ERROR([no jar binary in PATH])
    fi

    SAVE_CPPFLAGS=$CPPFLAGS
    CPPFLAGS="$CPPFLAGS -I$JAVA_INCLUDE_DIR"

    # on Fedora / CentOS jni_md.h header is not included
    if test -d "$JAVA_INCLUDE_DIR/linux"; then
        CPPFLAGS="$CPPFLAGS -I$JAVA_INCLUDE_DIR/linux"
        JAVA_INCLUDE_DIR_LINUX="$JAVA_INCLUDE_DIR/linux"
        AC_SUBST(JAVA_INCLUDE_DIR_LINUX)
    fi

    AC_CHECK_HEADER(jni.h,, [AC_MSG_ERROR([could not find jni.h, please specify JAVA include directory using --with-java-include-dir option.])])
    CPPFLAGS=$SAVE_CPPFLAGS
fi
AM_CONDITIONAL([BUILD_JAVA], [test "$enable_java" = "yes"])

# Chat
AC_ARG_ENABLE(chat,
    AS_HELP_STRING([--enable-chat], [enable chat support]),
    [], [enable_chat=no])
if test "x$enable_chat" = "xyes" || test "x$enable_java" = "xyes" || test "x$enable_python" = "xyes" || test "x$enable_php" = "xyes" ; then
    enable_chat=yes
    AC_DEFINE(ENABLE_CHAT, 1, [Define to enable chat])
fi


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

#libsodium
AC_MSG_CHECKING(for libsodium)
sodium=false
AC_ARG_WITH(sodium,
  AS_HELP_STRING(--with-sodium=PATH, base of libsodium installation),
  [
   case $with_sodium in
   no)
    sodium=false
     ;;
   yes)
    AC_CHECK_HEADERS([sodium.h],, [
        AC_MSG_ERROR([sodium.h header not found or not usable])
    ])
    AC_CHECK_LIB(sodium, [sodium_init], [SODIUM_LIBS="-lsodium"],[
            AC_MSG_ERROR([Could not find libsodium])
    ])
    sodium=true
     ;;
   *)

    # determine if library is installed
    if test -d "$with_sodium/lib"; then
        LDFLAGS="-L$with_sodium/lib $LDFLAGS"
        CXXFLAGS="-I$with_sodium/include $CXXFLAGS"
        CPPFLAGS="-I$with_sodium/include $CPPFLAGS"

        AC_CHECK_HEADERS(sodium.h,
         SODIUM_CXXFLAGS="-I$with_sodium/include"
         SODIUM_CPPFLAGS="-I$with_sodium/include"
         SODIUM_LDFLAGS="-L$with_sodium/lib",
         AC_MSG_ERROR([sodium/sodium.h header not found or not usable])
         )
    # assume we are using sodium source directory
    else
        LDFLAGS="-L$with_sodium/src/libsodium/.libs $LDFLAGS"
        CXXFLAGS="-I$with_sodium/src/libsodium/include $CXXFLAGS"
        CPPFLAGS="-I$with_sodium/src/libsodium/include $CPPFLAGS"

        AC_CHECK_HEADERS(sodium.h,
         SODIUM_CXXFLAGS="-I$with_sodium/src/libsodium/include"
         SODIUM_CPPFLAGS="-I$with_sodium/src/libsodium/include"
         SODIUM_LDFLAGS="-L$with_sodium/src/libsodium/.libs",
         AC_MSG_ERROR([sodium.h header not found or not usable])
         )
    fi

    AC_CHECK_LIB(sodium, [sodium_init], [SODIUM_LIBS="-lsodium"],[
            AC_MSG_ERROR([Could not find libsodium])
    ])

    sodium=true

    #restore
    LDFLAGS=$SAVE_LDFLAGS
    CXXFLAGS=$SAVE_CXXFLAGS
    CPPFLAGS=$SAVE_CPPFLAGS
    ;;
   esac
  ],
  [AC_MSG_RESULT([--with-sodium not specified])
    AC_CHECK_HEADERS([sodium.h], [sodium=true], [
        AC_MSG_ERROR([sodium.h header not found or not usable])
    ])
    AC_CHECK_LIB(sodium, [sodium_init], [SODIUM_LIBS="-lsodium"],[
        AC_MSG_ERROR([Could not find libsodium])
    ])
    ]
  )
AC_SUBST(SODIUM_CXXFLAGS)
AC_SUBST(SODIUM_CPPFLAGS)
AC_SUBST(SODIUM_LDFLAGS)
AC_SUBST(SODIUM_LIBS)

if test "x$sodium" = "xtrue" ; then
    AC_DEFINE(USE_SODIUM, [1], [Define to use libsodium])
fi
AM_CONDITIONAL([USE_SODIUM], [test "x$sodium" = "xtrue"])

#
# PCRE
#

#libpcre
AC_MSG_CHECKING(for libpcrecpp)
pcre=false
AC_ARG_WITH(pcre,
  AS_HELP_STRING(--with-pcre=PATH, base of libpcrecpp installation),
  [
   case $with_pcre in
   no)
    pcre=false
     ;;
   yes)
    AC_CHECK_HEADERS([pcrecpp.h],, [
        AC_MSG_ERROR([pcrecpp.h header not found or not usable])
    ])
    AC_CHECK_LIB(pcrecpp, [main], [PCRE_LIBS="-lpcrecpp"],[
            AC_MSG_ERROR([Could not find libpcrecpp])
    ])
    AC_CHECK_LIB(pcre, [pcre_compile], [PCRE_LIBS="$PCRE_LIBS -lpcre"],[
            AC_MSG_ERROR([Could not find libpcre])
    ])
    pcre=true
     ;;
   *)

    # determine if library is installed
    if test -d "$with_pcre/lib64"; then
        LDFLAGS="-L$with_pcre/lib64 $LDFLAGS"
        CXXFLAGS="-I$with_pcre/include $CXXFLAGS"
        CPPFLAGS="-I$with_pcre/include $CPPFLAGS"

        AC_CHECK_HEADERS(pcrecpp.h,
         PCRE_CXXFLAGS="-I$with_pcre/include"
         PCRE_CPPFLAGS="-I$with_pcre/include"
         PCRE_LDFLAGS="-L$with_pcre/lib64",
         AC_MSG_ERROR([pcrecpp.h header not found or not usable])
         )
    else
        LDFLAGS="-L$with_pcre/lib32 $LDFLAGS"
        CXXFLAGS="-I$with_pcre/include $CXXFLAGS"
        CPPFLAGS="-I$with_pcre/include $CPPFLAGS"

        AC_CHECK_HEADERS(pcrecpp.h,
         PCRE_CXXFLAGS="-I$with_pcre/include"
         PCRE_CPPFLAGS="-I$with_pcre/include"
         PCRE_LDFLAGS="-L$with_pcre/lib32",
         AC_MSG_ERROR([pcrecpp.h header not found or not usable])
         )
    fi

    AC_CHECK_LIB(pcrecpp, [main], [PCRE_LIBS="-lpcrecpp"],[
            AC_MSG_ERROR([Could not find libpcrecpp])
    ])
    AC_CHECK_LIB(pcre, [pcre_compile], [PCRE_LIBS="$PCRE_LIBS -lpcre"],[
            AC_MSG_ERROR([Could not find libpcre])
    ])

    pcre=true

    #restore
    LDFLAGS=$SAVE_LDFLAGS
    CXXFLAGS=$SAVE_CXXFLAGS
    CPPFLAGS=$SAVE_CPPFLAGS
    ;;
   esac
  ],
)
AC_SUBST(PCRE_CXXFLAGS)
AC_SUBST(PCRE_CPPFLAGS)
AC_SUBST(PCRE_LDFLAGS)
AC_SUBST(PCRE_LIBS)

if test "x$pcre" = "xtrue" ; then
    AC_DEFINE(USE_PCRE, [1], [Define to use libpcre])
fi
AM_CONDITIONAL([USE_PCRE], [test "x$pcre" = "xtrue"])

#
# LIBRAW
#

#libraw
SAVE_LIBS="$LIBS"
LIBS="$LIBS -fopenmp"
AC_MSG_CHECKING(for libraw)
libraw=undef
AC_ARG_WITH(libraw,
  AS_HELP_STRING(--with-libraw=PATH, base of libraw installation),
  [
   case $with_libraw in
   no)
    libraw=false
     ;;
   yes)
    AC_CHECK_HEADERS([libraw/libraw.h],, [
        AC_MSG_ERROR([libraw/libraw.h header not found or not usable])
    ])
    AC_CHECK_LIB(raw, [libraw_init], [LIBRAW_LIBS="$LIBRAW_LIBS -lraw -fopenmp"],[
            AC_MSG_ERROR([Could not find libraw])
    ])
    libraw=true
     ;;
   *)

    # determine if library is installed
    if test -d "$with_libraw/lib"; then
        LDFLAGS="-L$with_libraw/lib $LDFLAGS"
        CXXFLAGS="-I$with_libraw/include $CXXFLAGS"
        CPPFLAGS="-I$with_libraw/include $CPPFLAGS"

        AC_CHECK_HEADERS(libraw/libraw.h,
         LIBRAW_CXXFLAGS="-I$with_libraw/include"
         LIBRAW_CPPFLAGS="-I$with_libraw/include"
         LIBRAW_LDFLAGS="-L$with_libraw/lib",
         AC_MSG_ERROR([libraw/libraw.h header not found or not usable])
         )
    fi

    AC_CHECK_LIB(raw, [libraw_init], [LIBRAW_LIBS="$LIBRAW_LIBS -lraw -fopenmp"],[
            AC_MSG_ERROR([Could not find libraw])
    ])

    libraw=true

    #restore
    LDFLAGS=$SAVE_LDFLAGS
    CXXFLAGS=$SAVE_CXXFLAGS
    CPPFLAGS=$SAVE_CPPFLAGS
    ;;
   esac
  ],
)
AC_SUBST(LIBRAW_CXXFLAGS)
AC_SUBST(LIBRAW_CPPFLAGS)
AC_SUBST(LIBRAW_LDFLAGS)
AC_SUBST(LIBRAW_LIBS)

if test "x$libraw" = "xundef" ; then
    AC_CHECK_HEADERS([libraw/libraw.h],
    [
        AC_CHECK_LIB(raw, [libraw_init],
        [
        libraw=true
        LIBRAW_LIBS="$LIBRAW_LIBS -lraw -fopenmp"
        ],[
                AC_MSG_NOTICE([Could not find libraw])
        ])
    ]
    , [
        AC_MSG_NOTICE([libraw/libraw.h header not found or not usable])
    ])
fi

if test "x$libraw" = "xtrue" ; then
    AC_DEFINE(HAVE_LIBRAW, [1], [Define to use libraw])
fi
AM_CONDITIONAL([USE_LIBRAW], [test "x$libraw" = "xtrue"])
LIBS="$SAVE_LIBS"

#
# LIBUV
#

#libuv
LIBUV_LIBS=""
AC_MSG_CHECKING(for libuv)
libuv=false
AC_ARG_WITH(libuv,
  AS_HELP_STRING(--with-libuv=PATH, base of libuv installation),
  [
   case $with_libuv in
   no)
    libuv=false
     ;;
   yes)
    AC_CHECK_HEADERS([uv.h],, [
        AC_MSG_ERROR([uv.h header not found or not usable])
    ])
    AC_CHECK_LIB(uv, [uv_run], [LIBUV_LIBS="-luv"],[
            AC_MSG_ERROR([Could not find libuv])
    ])
    libuv=true
     ;;
   *)

    if test -f  "$with_libuv/lib/pkgconfig/libuv.pc"; then
        UV_LIBS=`cat $with_libuv/lib/pkgconfig/libuv.pc | grep "Libs:" | awk '{$1=""; $2=""; print $0}'`
    fi

    SAVE_LIBS=$LIBS
    # determine if library is installed
    if test -d "$with_libuv/lib"; then
        LIBS="-L$with_libuv/lib $UV_LIBS $LIBS" #TODO: These are actually lost: let's assume these library dependencies are included somewhere else
        LDFLAGS="-L$with_libuv/lib $UV_LDFLAGS $LDFLAGS"
        CXXFLAGS="-I$with_libuv/include $UV_CXXFLAGS $CXXFLAGS"
        CPPFLAGS="-I$with_libuv/include $UV_CPPFLAGS $CPPFLAGS"
        AC_CHECK_HEADERS(uv.h,
         LIBUV_CXXFLAGS="-I$with_libuv/include"
         LIBUV_CPPFLAGS="-I$with_libuv/include"
         LIBUV_LDFLAGS="-L$with_libuv/lib",
         AC_MSG_ERROR([uv.h header not found or not usable])
         )
    # assume we are using libuv source directory
    else
        LIBS="-L$with_libuv/uv $UV_LIBS $LIBS"
        LDFLAGS="-L$with_libuv/uv $UV_LDFLAGS $LDFLAGS"
        CXXFLAGS="-I$with_libuv $UV_CXXFLAGS $CXXFLAGS"
        CPPFLAGS="-I$with_libuv $UV_CPPFLAGS $CPPFLAGS"

        AC_CHECK_HEADERS(uv.h,
         LIBUV_CXXFLAGS="-I$with_libuv"
         LIBUV_CPPFLAGS="-I$with_libuv"
         LIBUV_LDFLAGS="-L$with_libuv/uv",
         AC_MSG_ERROR([uv.h header not found or not usable])
         )
    fi

    AC_CHECK_LIB(uv, [uv_run], [LIBUV_LIBS="-luv"],[
        SAVE_LIBS="$LIBS"
        LIBS="$LIBS -pthread"
        AC_CHECK_LIB(uv, [uv_close], [LIBUV_LIBS="-luv -pthread"],[ #different symbol to avoid cached value
            AC_MSG_ERROR([Could not find libuv])
            ])
        LIBS="$SAVE_LIBS"
    ])

    libuv=true

    #restore
    LIBS=$SAVE_LIBS
    LDFLAGS=$SAVE_LDFLAGS
    CXXFLAGS=$SAVE_CXXFLAGS
    CPPFLAGS=$SAVE_CPPFLAGS
    ;;
   esac
  ],
  [
      AC_MSG_RESULT([--with-libuv not specified])
      libuv=false
      AC_CHECK_HEADERS([uv.h],
        AC_CHECK_LIB(uv, [uv_run], [
          LIBUV_LIBS="-luv"
          libuv=true
          ],[
            if test "x$libuv" != "xtrue"; then
                SAVE_LIBS="$LIBS"
                LIBS="$LIBS -pthread"
                AC_CHECK_LIB(uv, [uv_close], [ #different symbol to avoid cached value
                LIBUV_LIBS="-luv -pthread"
                libuv=true
                ],
                [
                AC_MSG_RESULT([Could not find libuv])
                libuv=false
                ])
                LIBS="$SAVE_LIBS"
            fi
           ]
        )
      ,)
   ]
  )
AC_SUBST(LIBUV_CXXFLAGS)
AC_SUBST(LIBUV_CPPFLAGS)
AC_SUBST(LIBUV_LDFLAGS)
AC_SUBST(LIBUV_LIBS)
if test "x$libuv" = "xtrue" ; then
    AC_DEFINE(HAVE_LIBUV, [1], [Define to use libuv])
fi
AM_CONDITIONAL([USE_LIBUV], [test "x$libuv" = "xtrue"])


#
# Zlib
#

#zlib
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

    if test -d "$with_zlib/lib"; then
        LDFLAGS="-L$with_zlib/lib $LDFLAGS"
        CXXFLAGS="-I$with_zlib/include $CXXFLAGS"
        CPPFLAGS="-I$with_zlib/include $CPPFLAGS"

        AC_CHECK_HEADERS(zlib.h,
         ZLIB_CXXFLAGS="-I$with_zlib/include"
         ZLIB_CPPFLAGS="-I$with_zlib/include"
         ZLIB_LDFLAGS="-L$with_zlib/lib",
         AC_MSG_ERROR([zlib.h header not found or not usable])
         )

        AC_CHECK_LIB(z, [main], [ZLIB_LIBS="-lz"],[
                AC_MSG_ERROR([Could not find zlib])
        ])
    else
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
    fi

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
        DB_LIBS="-lsqlite3"
        AC_SUBST(DB_LIBS)
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

# check if no DB layer is selected, use SQLite by the default
if test "x$sqlite" = "xfalse" ; then
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

if test "x$sqlite" = "xtrue" ; then
    AC_DEFINE(USE_SQLITE, [1], [Define to use SQLite])
fi

#
# FFMPEG
#
AC_MSG_CHECKING(for ffmpeg)
AC_ARG_WITH(ffmpeg,
  #AS_HELP_STRING(--with-ffmpeg=PATH, base of ffmpeg installation), #TODO: allow PATH specification
  AS_HELP_STRING(--with-ffmpeg, enforce usage of ffmpeg),
  [
   case $with_ffmpeg in
   no)
    ffmpeg=false
     ;;
   yes)
    #ffmpeg-mega
    AC_CHECK_FILE([/usr/include/ffmpeg-mega/libavutil/macros.h],
       # LDFLAGS="-L$with_ffmpeg/lib $LDFLAGS"
        CXXFLAGS="-I/usr/include/ffmpeg-mega $CXXFLAGS"
        CPPFLAGS="-I/usr/include/ffmpeg-mega $CPPFLAGS"

        FFMPEG_CXXFLAGS="-I/usr/include/ffmpeg-mega"
        FFMPEG_CPPFLAGS="-I/usr/include/ffmpeg-mega"

        AC_CHECK_LIB(bz2, BZ2_bzCompressInit, [LIBBZ2FFMPEG="-lbz2"],)
        AC_CHECK_LIB(lzma, lzma_code, [LIBLZMAFFMPEG="-llzma"],)

        AC_CHECK_LIB(avformat, main, [FFMPEG_LIBS=["-lavformat -lavcodec -lavutil -lswscale $LIBBZ2FFMPEG $LIBLZMAFFMPEG"]]
        ,[
                AC_MSG_ERROR([Could not find ffmpeg])
        ])
    ,
        AC_CHECK_HEADERS([libavutil/macros.h],, [AC_MSG_ERROR([libavutil/macros.h (ffmpeg) header not found or not usable])]) #CONFLICTIVEOLDAUTOTOOLS
        AC_CHECK_LIB(avformat, main, [FFMPEG_LIBS="-lavcodec -lavformat -lavutil -lswscale"],[
                AC_MSG_ERROR([Could not find ffmpeg])
        ])
    )
    ffmpeg=true


    #restore
    LDFLAGS=$SAVE_LDFLAGS
    CXXFLAGS=$SAVE_CXXFLAGS
    CPPFLAGS=$SAVE_CPPFLAGS
    ;;
   esac
  ],
  [AC_MSG_RESULT([--with-ffmpeg not specified])

    AC_CHECK_FILE([/usr/include/ffmpeg-mega/libavutil/macros.h],
       # LDFLAGS="-L$with_ffmpeg/lib $LDFLAGS"
        CXXFLAGS="-I/usr/include/ffmpeg-mega $CXXFLAGS"
        CPPFLAGS="-I/usr/include/ffmpeg-mega $CPPFLAGS"

        FFMPEG_CXXFLAGS="-I/usr/include/ffmpeg-mega"
        FFMPEG_CPPFLAGS="-I/usr/include/ffmpeg-mega"

        AC_CHECK_LIB(bz2, BZ2_bzCompressInit, [LIBBZ2FFMPEG="-lbz2"],)
        AC_CHECK_LIB(lzma, lzma_code, [LIBLZMAFFMPEG="-llzma"],)

        AC_CHECK_LIB(avformat, main, [FFMPEG_LIBS=["-lavformat -lavcodec -lavutil -lswscale $LIBBZ2FFMPEG $LIBLZMAFFMPEG"]]
        ffmpeg=true
        ,)
    ,
        AC_CHECK_HEADERS([libavutil/macros.h],AC_CHECK_LIB(avformat, main, [FFMPEG_LIBS="-lavcodec -lavformat -lavutil -lswscale"]  #CONFLICTIVEOLDAUTOTOOLS
        ffmpeg=true,),)  #CONFLICTIVEOLDAUTOTOOLS
    )

  ]
  )

if test "x$ffmpeg" = "xtrue" ; then
AC_SUBST(FFMPEG_CXXFLAGS)
AC_SUBST(FFMPEG_CPPFLAGS)
AC_SUBST(FFMPEG_LDFLAGS)
AC_SUBST(FFMPEG_LIBS)
AC_DEFINE(HAVE_FFMPEG, [1], [Define to use FFMPEG])
fi

#
# LIBMediaInfo
#
#libzen
LIBZEN_LIBS=""
AC_MSG_CHECKING(for libzen)
libzen=false
AC_ARG_WITH(libzen,
  AS_HELP_STRING(--with-libzen=PATH, base of libzen installation),
  [
   case $with_libzen in
   no)
    libzen=false
     ;;
   yes)
    AC_CHECK_HEADERS([ZenLib/Ztring.h],, [
        AC_MSG_ERROR([ZenLib/Ztring.h header not found or not usable])
    ])
    AC_CHECK_LIB(zen, [_ZN6ZenLib15int8u2BigEndianEPch], [LIBZEN_LIBS="-lzen"],[
        SAVE_LIBS="$LIBS"
        LIBS="$LIBS -pthread"
        AC_CHECK_LIB(zen, [_ZN6ZenLib19int48s2LittleEndianEPcx], [LIBZEN_LIBS="-lzen -pthread"],[ #different symbol to avoid cached value
            AC_MSG_ERROR([Could not find libzen])
            ])
        LIBS="$SAVE_LIBS"
    ])
    libzen=true
     ;;
   *)

    # determine if library is installed
    if test -d "$with_libzen/lib"; then
        LDFLAGS="-L$with_libzen/lib $LDFLAGS"
        CXXFLAGS="-I$with_libzen/include $CXXFLAGS"
        CPPFLAGS="-I$with_libzen/include $CPPFLAGS"

        AC_CHECK_HEADERS(ZenLib/Ztring.h,
         LIBZEN_CXXFLAGS="-I$with_libzen/include"
         LIBZEN_CPPFLAGS="-I$with_libzen/include"
         LIBZEN_LDFLAGS="-L$with_libzen/lib",
         AC_MSG_ERROR([ZenLib/Ztring.h header not found or not usable])
         )
    # assume we are using libzen source directory
    else
        LDFLAGS="-L$with_libzen/zen $LDFLAGS"
        CXXFLAGS="-I$with_libzen $CXXFLAGS"
        CPPFLAGS="-I$with_libzen $CPPFLAGS"

        AC_CHECK_HEADERS(ZenLib/Ztring.h,
         LIBZEN_CXXFLAGS="-I$with_libzen"
         LIBZEN_CPPFLAGS="-I$with_libzen"
         LIBZEN_LDFLAGS="-L$with_libzen/zen",
         AC_MSG_ERROR([ZenLib/Ztring.h header not found or not usable])
         )
    fi

    AC_CHECK_LIB(zen, [_ZN6ZenLib15int8u2BigEndianEPch], [LIBZEN_LIBS="-lzen"],[
        SAVE_LIBS="$LIBS"
        LIBS="$LIBS -pthread"
        AC_CHECK_LIB(zen, [_ZN6ZenLib19int48s2LittleEndianEPcx], [LIBZEN_LIBS="-lzen -pthread"],[
            AC_MSG_ERROR([Could not find libzen])
            ])
        LIBS="$SAVE_LIBS"
    ])

    libzen=true

    #restore
    LDFLAGS=$SAVE_LDFLAGS
    CXXFLAGS=$SAVE_CXXFLAGS
    CPPFLAGS=$SAVE_CPPFLAGS
    ;;
   esac
  ],
  [AC_MSG_RESULT([--with-libzen not specified])
    AC_CHECK_HEADERS([ZenLib/Ztring.h],
        AC_CHECK_LIB(zen, [_ZN6ZenLib15int8u2BigEndianEPch], [
        LIBZEN_LIBS="-lzen"
        libzen=true
        ],)

	if test "x$libzen" != "xtrue"; then
	SAVE_LIBS="$LIBS"
	LIBS="$LIBS -pthread"
	AC_CHECK_LIB(zen, [_ZN6ZenLib19int48s2LittleEndianEPcx], [
	LIBZEN_LIBS="-lzen -pthread"
	libzen=true
	],)
	LIBS="$SAVE_LIBS"
	fi
    ,)
    
  ]
  )

AC_CHECK_LIB(pthread, pthread_create, [pthread_available=yes], [pthread_available=no])

if test "x$libzen" = "xtrue" && test "$pthread_available" = "yes"; then
    #libmediainfo
    LIBMEDIAINFO_LIBS=""
    AC_MSG_CHECKING(for libmediainfo)
    libmediainfo=false
    AC_ARG_WITH(libmediainfo,
      AS_HELP_STRING(--with-libmediainfo=PATH, base of libmediainfo installation),
      [
       case $with_libmediainfo in
       no)
        libmediainfo=false
         ;;
       yes)
        AC_CHECK_HEADERS([MediaInfo/MediaInfo.h],, [
            AC_MSG_ERROR([MediaInfo/MediaInfo.h header not found or not usable])
        ])

        LIBS="$LIBS $LIBZEN_LIBS $ZLIB_LIBS -pthread"

        AC_CHECK_LIB(mediainfo, [MediaInfo_Info_Version], [LIBMEDIAINFO_LIBS="-lmediainfo $LIBZEN_LIBS"],[
                AC_MSG_ERROR([Could not find libmediainfo])
        ])
        LIBS="$SAVE_LIBS"
        libmediainfo=true
         ;;
       *)

        # determine if library is installed
        if test -d "$with_libmediainfo/lib"; then
            LDFLAGS="$LIBZEN_LDFLAGS $ZLIB_LDFLAGS -L$with_libmediainfo/lib $LDFLAGS"
            CXXFLAGS="$LIBZEN_CXXFLAGS $ZLIB_CXXFLAGS -I$with_libmediainfo/include $CXXFLAGS"
            CPPFLAGS="$LIBZEN_CPPFLAGS $ZLIB_CPPFLAGS  -I$with_libmediainfo/include $CPPFLAGS"

            AC_CHECK_HEADERS(MediaInfo/MediaInfo.h,
             LIBMEDIAINFO_CXXFLAGS="$LIBZEN_CXXFLAGS -I$with_libmediainfo/include"
             LIBMEDIAINFO_CPPFLAGS="$LIBZEN_CPPFLAGS -I$with_libmediainfo/include"
             LIBMEDIAINFO_LDFLAGS="$LIBZEN_LDFLAGS -L$with_libmediainfo/lib",
             AC_MSG_ERROR([MediaInfo/MediaInfo.h header not found or not usable])
             )
        # assume we are using libmediainfo source directory
        else
            LDFLAGS="$LIBZEN_LDFLAGS $ZLIB_LDFLAGS -L$with_libmediainfo/mediainfo $LDFLAGS"
            CXXFLAGS="$LIBZEN_CXXFLAGS $ZLIB_CXXFLAGS -I$with_libmediainfo $CXXFLAGS"
            CPPFLAGS="$LIBZEN_CPPFLAGS $ZLIB_CPPFLAGS -I$with_libmediainfo $CPPFLAGS"

            AC_CHECK_HEADERS(MediaInfo/MediaInfo.h,
             LIBMEDIAINFO_CXXFLAGS="$LIBZEN_CXXFLAGS -I$with_libmediainfo"
             LIBMEDIAINFO_CPPFLAGS="$LIBZEN_CPPFLAGS -I$with_libmediainfo"
             LIBMEDIAINFO_LDFLAGS="$LIBZEN_LDFLAGS -L$with_libmediainfo/mediainfo",
             AC_MSG_ERROR([MediaInfo/MediaInfo.h header not found or not usable])
             )
        fi

        SAVE_LIBS="$LIBS"
        LIBS="$LIBS $LIBZEN_LIBS $ZLIB_LIBS -pthread"

        AC_CHECK_LIB(mediainfo, [MediaInfo_Info_Version], [LIBMEDIAINFO_LIBS="-lmediainfo $LIBZEN_LIBS"],[
                AC_MSG_ERROR([Could not find libmediainfo])
        ])
        LIBS="$SAVE_LIBS"

        libmediainfo=true

        #restore
        LDFLAGS=$SAVE_LDFLAGS
        CXXFLAGS=$SAVE_CXXFLAGS
        CPPFLAGS=$SAVE_CPPFLAGS
        ;;
       esac
      ],
      [AC_MSG_RESULT([--with-libmediainfo not specified])
        AC_CHECK_HEADERS([MediaInfo/MediaInfo.h],

            SAVE_LIBS="$LIBS"
            LIBS="$LIBS $LIBZEN_LIBS $ZLIB_LIBS -pthread"

            AC_CHECK_LIB(mediainfo, [MediaInfo_Info_Version], [
            LIBMEDIAINFO_LIBS="-lmediainfo $LIBZEN_LIBS"
            libmediainfo=true
            ],)

            LIBS="$SAVE_LIBS"
            
        ,)
        
      ]
      )

    if test "x$libmediainfo" = "xtrue" ; then
     SAVE_LIBS="$LIBS"
     LIBS="$LIBS $LIBMEDIAINFO_LIBS $LIBZEN_LIBS $ZLIB_LIBS -pthread"
     LDFLAGS="$LIBMEDIAINFO_LDFLAGS"
     CXXFLAGS="$LIBMEDIAINFO_CXXFLAGS"
     CPPFLAGS="$LIBMEDIAINFO_CPPFLAGS"
     lmediainfo_prog='#include <iostream> 
    #include "ZenLib/Ztring.h"
    #include "MediaInfo/MediaInfo.h"
    int main () { std::cout << ZenLib::Ztring(MediaInfoLib::MediaInfo::Option_Static(__T("Info_Version")).c_str()).To_Local().c_str() << std::endl; }
    '

      AC_LINK_IFELSE(
        [AC_LANG_SOURCE($lmediainfo_prog)],
         AC_MSG_RESULT([libmediainfo worked]),
         #Try again with -DUNICODE
         LIBMEDIAINFO_LDFLAGS="$LIBMEDIAINFO_LDFLAGS -DUNICODE"
         LDFLAGS="$LIBMEDIAINFO_LDFLAGS"
         AC_LINK_IFELSE(
        [AC_LANG_SOURCE($lmediainfo_prog)],
         AC_MSG_RESULT([libmediainfo worked worked with -DUNICODE])
         AC_DEFINE(UNICODE, [1], [Define to use UNICODE (for MediaInfo)])
         ,
         AC_MSG_RESULT([libmediainfo failed to compile. Discarded])
         libmediainfo=false
         unset LIBMEDIAINFO_CXXFLAGS
         unset LIBMEDIAINFO_CPPFLAGS
         unset LIBMEDIAINFO_LDFLAGS
         unset LIBMEDIAINFO_LIBS
         )
       )

      #restore
      LDFLAGS=$SAVE_LDFLAGS
      CXXFLAGS=$SAVE_CXXFLAGS
      CPPFLAGS=$SAVE_CPPFLAGS
      LIBS="$SAVE_LIBS"

    fi 
    if test "x$libmediainfo" = "xtrue" ; then
        AC_SUBST(LIBMEDIAINFO_CXXFLAGS)
        AC_SUBST(LIBMEDIAINFO_CPPFLAGS)
        AC_SUBST(LIBMEDIAINFO_LDFLAGS)
        AC_SUBST(LIBMEDIAINFO_LIBS)
        AC_DEFINE(USE_MEDIAINFO, [1], [Define to use libmediainfo])    
    fi

else
    AC_MSG_RESULT([libmediainfo requires libzen])
fi

AM_CONDITIONAL([USE_LIBMEDIAINFO], [test "x$libmediainfo" = "xtrue"])

#
# ** Posix dependent libraries **
#
openssl=false
cares=false
rt=undef
if test "x$WIN32" = "xno" ; then

    AC_ARG_WITH([poll],
      AS_HELP_STRING(--with-poll use poll instead of select in posix waiter),
      AC_DEFINE(USE_POLL, [1], [Define to use poll instead of select in posix waiter]),
    )

    if test "$HAVE_PTHREAD" = "yes"; then
        SAVE_LDFLAGS="-pthread $SAVE_LDFLAGS"
        LDFLAGS="-pthread $LDFLAGS"
    fi

    # librt
    AC_MSG_CHECKING(for rt)
    AC_ARG_WITH([rt],
      AS_HELP_STRING(--with-rt=PATH, base of Real-time Library installation),
      [
       case $with_rt in
       no)
            AC_MSG_RESULT([no])

            rt=false
         ;;
       yes)
            AC_CHECK_HEADERS([aio.h], [],
                AC_MSG_ERROR([aio.h header not found or not usable])
            )
            AC_CHECK_LIB(rt, [aio_write], [LIBRT_LIBS="$LIBRT_LIBS -lrt"],
                AC_MSG_ERROR([Could not find rt library!])
            )

            AC_SUBST(LIBRT_LIBS)
            rt=true
         ;;
       *)
        # determine if library is installed
        if test -d "$with_rt/lib"; then
            LDFLAGS="-L$with_rt/lib $LDFLAGS"
            CXXFLAGS="-I$with_rt/include $CXXFLAGS"

            AC_CHECK_HEADERS([aio.h], [
                LIBRT_LDFLAGS="-L$with_rt/lib"
                LIBRT_CXXFLAGS="-I$with_rt/include"],
                AC_MSG_ERROR([aio.h header not found or not usable])
            )
        
            AC_CHECK_LIB(rt, [aio_write], [LIBRT_LIBS="$LIBRT_LIBS -lrt"],
                AC_MSG_ERROR([Could not find rt library!])
            )
        else
            AC_MSG_ERROR([Could not find rt library in "$with_rt/lib"!])
        fi

        AC_SUBST(LIBRT_LDFLAGS)
        AC_SUBST(LIBRT_CXXFLAGS)
        AC_SUBST(LIBRT_LIBS)
        rt=true

        #restore
        LDFLAGS=$SAVE_LDFLAGS
        CXXFLAGS=$SAVE_CXXFLAGS
        ;;
       esac
      ],
      [
        AC_MSG_RESULT([--with-rt not specified])

        AC_CHECK_HEADERS([aio.h], [],
            AC_MSG_ERROR([aio.h header not found or not usable])
        )
        AC_CHECK_LIB(rt, [aio_write], [LIBRT_LIBS="$LIBRT_LIBS -lrt"],
            AC_MSG_NOTICE([Could not find librt])
        )

        AC_SUBST(LIBRT_LIBS)
        rt=true
      ]
    )

    # OpenSSL
    AC_MSG_CHECKING(for OpenSSL)
    AC_ARG_WITH([openssl],
      AS_HELP_STRING(--with-openssl=PATH, base of OpenSSL installation),
      [
       case $with_openssl in
       no)
         AC_MSG_ERROR([Please specify path to OpenSSL installation directory!])
         ;;
       yes)
            AC_CHECK_HEADERS([openssl/ssl.h], [],
                AC_MSG_ERROR([ssl.h header not found or not usable])
            )
            LIBS="-lcrypto $LIBS"
            AC_CHECK_LIB(ssl, [SSL_new], [LIBSSL_LIBS="-lssl -lcrypto"],
                AC_MSG_ERROR([Could not find OpenSSL library!])
            )
            LIBS=$SAVE_LIBS
            AC_SUBST(LIBSSL_LIBS)
            openssl=true
         ;;
       *)
        # determine if library is installed
        LDFLAGS="-L$with_openssl/lib $LDFLAGS"
        CXXFLAGS="-I$with_openssl/include $CXXFLAGS"

        LIBSSL_LDFLAGS="-L$with_openssl/lib"
        LIBSSL_FLAGS="-I$with_openssl/include"
        SAVE_LIBS=$LIBS
        LIBS="-lcrypto $LIBS"

        AC_CHECK_HEADERS([openssl/ssl.h], [],
            AC_MSG_ERROR([ssl.h header not found or not usable])
        )
        AC_CHECK_LIB(ssl, [SSL_new], [LIBSSL_LIBS="-lssl -lcrypto"],
            AC_MSG_ERROR([Could not find OpenSSL library!])
        )

        AC_SUBST(LIBSSL_FLAGS)
        AC_SUBST(LIBSSL_LDFLAGS)
        AC_SUBST(LIBSSL_LIBS)
        openssl=true

        #restore
        LIBS=$SAVE_LIBS
        LDFLAGS=$SAVE_LDFLAGS
        CXXFLAGS=$SAVE_CXXFLAGS
        CPPFLAGS=$SAVE_CPPFLAGS
        ;;
       esac
      ],
      [
        AC_MSG_RESULT([--with-openssl not specified])

        AC_CHECK_HEADERS([openssl/ssl.h], [],
            AC_MSG_ERROR([ssl.h header not found or not usable])
        )
        LIBS="-lcrypto $LIBS"
        AC_CHECK_LIB(ssl, [SSL_new], [LIBSSL_LIBS="-lssl -lcrypto"],
            AC_MSG_ERROR([Could not find OpenSSL library!])
        )
        LIBS=$SAVE_LIBS
        AC_SUBST(LIBSSL_LIBS)
        openssl=true
      ]
    )

    # c-ares
    AC_MSG_CHECKING(for c-ares)
    AC_ARG_WITH([cares],
      AS_HELP_STRING(--with-cares=PATH, base of c-ares installation),
      [
       case $with_cares in
       no)
         AC_MSG_ERROR([Please specify path to c-ares installation directory!])
         ;;
       yes)
            AC_CHECK_HEADERS([ares.h], [],
                AC_MSG_ERROR([ares.h header not found or not usable])
            )
            AC_CHECK_LIB(cares, [ares_library_init], [CARES_LIBS="-lcares"],
                AC_MSG_ERROR([Could not find c-ares library!])
            )

            AC_SUBST(CARES_LIBS)
            cares=true
         ;;
       *)
        # determine if library is installed
        LDFLAGS="-L$with_cares/lib $LDFLAGS"
        CXXFLAGS="-I$with_cares/include $CXXFLAGS"

        CARES_LDFLAGS="-L$with_cares/lib"
        CARES_FLAGS="-I$with_cares/include"

        AC_CHECK_HEADERS([ares.h], [],
            AC_MSG_ERROR([ares.h header not found or not usable])
        )
        AC_CHECK_LIB(cares, [ares_library_init], [CARES_LIBS="-lcares"],
            AC_MSG_ERROR([Could not find c-ares library!])
        )

        AC_SUBST(CARES_FLAGS)
        AC_SUBST(CARES_LDFLAGS)
        AC_SUBST(CARES_LIBS)
        cares=true

        #restore
        LDFLAGS=$SAVE_LDFLAGS
        CXXFLAGS=$SAVE_CXXFLAGS
        CPPFLAGS=$SAVE_CPPFLAGS
        ;;
       esac
      ],
      [
        AC_MSG_RESULT([--with-cares not specified])

        AC_CHECK_HEADERS([ares.h], [],
            AC_MSG_ERROR([ares.h header not found or not usable])
        )
        AC_CHECK_LIB(cares, [ares_library_init], [CARES_LIBS="-lcares"],
            AC_MSG_ERROR([Could not find c-ares library!])
        )

        AC_SUBST(CARES_LIBS)
        cares=true
      ]
    )

    # cURL
    AC_ARG_ENABLE(curl-checks,
        AS_HELP_STRING([--enable-curl-checks], [enable cURL checks [default=yes]])],
        [enable_curl_checks=$enableval],
        [enable_curl_checks=yes]
    )

    # check for cURL library
    AC_ARG_WITH([curl],
      AS_HELP_STRING(--with-curl=PATH, base of cURL installation),
      [
       case $with_curl in
       no)
         AC_MSG_ERROR([Please specify path to cURL installation directory!])
         ;;
       yes)
            LIBCURL_LIBS="-lcurl"
            SAVE_LIBS=$LIBS
            LIBS="-lcurl $LIBS"

            AC_CHECK_HEADERS([curl/curl.h], [],
                AC_MSG_ERROR([curl.h header not found or not usable])
            )
            AC_CHECK_LIB(curl, [main], [],
                AC_MSG_ERROR([Could not find libcurl!])
            )

            AC_SUBST(LIBCURL_LIBS)

            if test "x$enable_curl_checks" = "xyes" ; then
                CURL_CHK()
            else
                AC_MSG_NOTICE([skipping cURL checks.])
            fi

            LIBS=$SAVE_LIBS
         ;;
       *)

        # determine if library is installed
        LDFLAGS="-L$with_curl/lib $LDFLAGS $LIBSSL_LDFLAGS $LIBSSL_LIBS"

        CXXFLAGS="-I$with_curl/include $CXXFLAGS"
        SAVE_LIBS=$LIBS
        LIBS="-lcurl $LIBS"

        LIBCURL_LIBS="-L$with_curl/lib -lcurl"
        LIBCURL_FLAGS="-I$with_curl/include"

        AC_CHECK_HEADERS([curl/curl.h], [],
            AC_MSG_ERROR([curl.h header not found or not usable])
        )

        AC_CHECK_LIB(curl, [main], [],
            AC_MSG_ERROR([Could not find libcurl!])
        )

        AC_SUBST(LIBCURL_FLAGS)
        AC_SUBST(LIBCURL_LIBS)

        if test "x$enable_curl_checks" = "xyes" ; then
            CURL_CHK()
        else
            AC_MSG_NOTICE([skipping cURL checks.])
        fi

        #restore
        LIBS=$SAVE_LIBS
        LDFLAGS=$SAVE_LDFLAGS
        CXXFLAGS=$SAVE_CXXFLAGS
        CPPFLAGS=$SAVE_CPPFLAGS
         ;;
       esac
      ],
      [
        LIBCURL_LIBS="-lcurl"
        SAVE_LIBS=$LIBS
        LIBS="-lcurl $LIBS"

        AC_CHECK_HEADERS([curl/curl.h], [],
            AC_MSG_ERROR([curl.h header not found or not usable])
        )
        AC_CHECK_LIB(curl, [main], [],
            AC_MSG_ERROR([Could not find libcurl!])
        )

        AC_SUBST(LIBCURL_LIBS)

        if test "x$enable_curl_checks" = "xyes" ; then
            CURL_CHK()
        else
            AC_MSG_NOTICE([skipping cURL checks.])
        fi
        LIBS=$SAVE_LIBS
      ]
    )
fi

dnl FUSE functionality is only really meaningful on POSIX platforms.
AS_IF([test "x$LINUX" = xyes -o "x$DARWIN" = xyes], [
    dnl Let the user know they can enable FUSE functionality.
    AC_ARG_ENABLE(fuse,
                  [AS_HELP_STRING(
                    [--enable-fuse],
                    [Enable FUSE functionality [default=check]])],
                  [],
                  [enable_fuse=check])

    dnl Let the user specify where libfuse can be found.
    AC_ARG_WITH(fuse,
                [AS_HELP_STRING(
                  [--with-fuse=PATH],
                  [Where libfuse has been installed [default=PREFIX]])],
                [],
                [with_fuse=$prefix])

    dnl libfuse must be present to enable FUSE functionality.
    AS_IF([test "x$enable_fuse" != xno -a "x$with_fuse" != xno], [
        dnl Convenience.
        FUSE_CPPFLAGS="-I$with_fuse/include"
        FUSE_LDFLAGS="-L$with_fuse/lib"
        FUSE_LIBS="-lfuse"

        dnl Tell the system where it should search for libfuse.
        CPPFLAGS="$FUSE_CPPFLAGS $CPPFLAGS"
        LDFLAGS="$FUSE_LDFLAGS $LDFLAGS"

        dnl Check for libfuse headers.
        AC_CHECK_HEADERS([fuse/fuse.h], [], [
            dnl Compilation may fali if _FILE_OFFSET_BITS isn't defined.
            FUSE_CPPFLAGS="-D_FILE_OFFSET_BITS=64 $FUSE_CPPFLAGS"
            CPPFLAGS="$FUSE_CPPFLAGS $SAVE_CPPFLAGS"

            dnl See if this changes the result.
            AS_UNSET([ac_cv_header_fuse_fuse_h])

            AC_CHECK_HEADERS([fuse/fuse.h], [], [
                AS_IF([test "x$enable_fuse" != xcheck ], [
                    AC_MSG_ERROR([fuse/fuse.h not found or usable.])
                ], [
                    enable_fuse=no
                ])
            ])
        ])

        dnl Check for usable libfuse libraries.
        AS_IF([test "x$enable_fuse" != xno], [
            AC_CHECK_LIB([fuse], [fuse_main], [], [
                AS_IF([test "x$enable_fuse" != xcheck ], [
                    AC_MSG_ERROR([libfuse not found or usable.])
                ], [
                    enable_fuse=no
                ])
            ])
        ])

        dnl Make flags accessible from the makefiles.
        AS_IF([test "x$enable_fuse" = xno], [
            FUSE_CPPFLAGS=
            FUSE_LDFLAGS=
            FUSE_LIBS=
        ])

        AC_SUBST(FUSE_CPPFLAGS)
        AC_SUBST(FUSE_LDFLAGS)
        AC_SUBST(FUSE_LIBS)

        dnl Don't interfere with future feature checks.
        CPPFLAGS=$SAVE_CPPFLAGS
        LDFLAGS=$SAVE_LDFLAGS
        LIBS=$SAVE_LIBS
    ])
], [
  dnl Only POSIX platforms currently support FUSE.
  enable_fuse=no
])

dnl Let AM know whether we found a fuse backend.
AM_CONDITIONAL([WITH_FUSE], [test "x$enable_fuse" != xno])

# define on all platforms
AM_CONDITIONAL(HAVE_RT, test x$rt = xtrue)
if test "x$rt" = "xtrue" ; then
    AC_DEFINE(HAVE_AIO_RT, [1], [Define to indicate AIO presence in librt])
fi

# define on all platforms
AM_CONDITIONAL(HAVE_OPENSSL, test x$openssl = xtrue)
if test "x$openssl" = "xtrue" ; then
    AC_DEFINE(USE_OPENSSL, [1], [Define to use OpenSSL])
fi


# define on all platforms
AM_CONDITIONAL(HAVE_CARES, test x$cares = xtrue)
if test "x$cares" = "xtrue" ; then
    AC_DEFINE(USE_CARES, [1], [Define to use c-ares])
else
    AC_DEFINE(USE_CARES, [0], [Define to use c-ares])
fi

# determine platform include path
if test "x$WIN32" = "xyes" ; then
    platform_include="mega/win32"
else
    platform_include="mega/posix"
fi
AC_SUBST(platform_include)

# Examples
AC_MSG_CHECKING([if building example applications])
AC_ARG_ENABLE(examples,
    AS_HELP_STRING([--enable-examples], [build example applications [default=yes]]),
    [], [enable_examples=yes])
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
if test x$enable_static = xyes; then
    AC_DEFINE(FREEIMAGE_LIB, [1], [Define to use FreeImage as a static library.])
    AM_CONDITIONAL([FREEIMAGE_LIB], [test "x$freeimage" = "xtrue"])
else
    AM_CONDITIONAL([FREEIMAGE_LIB], [test "x$freeimage" = "xnone"])
fi
AC_SUBST(FI_CXXFLAGS)
AC_SUBST(FI_CPPFLAGS)
if test "x$freeimage" = "xtrue" ; then
    AC_DEFINE(USE_FREEIMAGE, [1], [Define to use FreeImage library.])
fi
AM_CONDITIONAL([USE_FREEIMAGE], [test "x$freeimage" = "xtrue"])

#pdfium
pdfium=false
AC_MSG_CHECKING(for PDFium)
AC_ARG_WITH(pdfium,
  AS_HELP_STRING(--with-pdfium=PATH, base of PDFium installation),
  [AC_MSG_RESULT($with_pdfium)
  case $with_pdfium in
    no)
      pdfium=false
    ;;
    yes)
      AC_CHECK_HEADERS([fpdfview.h],, [
          AC_MSG_ERROR([fpdfview.h header not found or not usable])
      ])
      AC_CHECK_LIB([pdfium], [main], [PDF_LIBS="-lpdfium"], [
          AC_MSG_ERROR([PDFium library is not found!])])

       pdfium=true
    ;;
    *)
    # determine if library is installed
    if test -d "$with_pdfium/lib"; then
        LDFLAGS="-L$with_pdfium/lib $LDFLAGS"
        CXXFLAGS="-I$with_pdfium/include $CXXFLAGS"
        CPPFLAGS="-I$with_pdfium/include $CPPFLAGS"

        AC_CHECK_HEADERS([fpdfview.h],[
         PDF_LDFLAGS="-L$with_pdfium/lib"
         PDF_CXXFLAGS="-I$with_pdfium/include"
         PDF_CPPFLAGS="-I$with_pdfium/include"],
         AC_MSG_ERROR([fpdfview.h header not found or not usable])
        )
    else
        LDFLAGS="-L$with_pdfium/Dist $LDFLAGS"
        CXXFLAGS="-I$with_pdfium/Dist $CXXFLAGS"
        CPPFLAGS="-I$with_pdfium/Dist $CPPFLAGS"

        AC_CHECK_HEADERS([fpdfview.h],[
         PDF_LDFLAGS="-L$with_pdfium/Dist"
         PDF_CXXFLAGS="-I$with_pdfium/Dist"
         PDF_CPPFLAGS="-I$with_pdfium/Dist"],
         AC_MSG_ERROR([fpdfview.h header not found or not usable])
        )
    fi
    # check and set PDFium library
    AC_CHECK_LIB([pdfium], [main], [PDF_LIBS="-lpdfium"], [
        AC_MSG_ERROR([PDFium library is not found!])])
    #restore
    LDFLAGS=$SAVE_LDFLAGS
    CXXFLAGS=$SAVE_CXXFLAGS
    CPPFLAGS=$SAVE_CPPFLAGS
    pdfium=true
    ;;
   esac
   ],
   [AC_MSG_RESULT([--with-pdfium not specified])
    AC_CHECK_HEADERS([fpdfview.h],AC_CHECK_LIB([pdfium], [main],
        [PDF_LIBS="-lpdfium"
        pdfium=true
        ],),)
   ]
   )

if test "x$pdfium" = "xtrue" ; then
AC_SUBST(PDF_LDFLAGS)
AC_SUBST(PDF_LIBS)
AC_SUBST(PDF_CXXFLAGS)
AC_SUBST(PDF_CPPFLAGS)
AC_DEFINE(HAVE_PDFIUM, [1], [Define to use PDFium library.])
fi
AM_CONDITIONAL([USE_PDFIUM], [test "x$pdfium" = "xtrue"])


# ICU
AC_MSG_CHECKING(for icu)
icu=false
AC_ARG_WITH(icu,
  AS_HELP_STRING(--with-icu=PATH, base of ICU installation),
  [
   case $with_icu in
   no)
    AC_MSG_ERROR([icu library is required. Please specify the installation path.])
    ;;
   yes)
    AC_CHECK_HEADERS([unicode/uchar.h],, [
        AC_MSG_ERROR([unicode/uchar.h header not found or not usable])
    ])
    AC_CHECK_LIB(icuuc, [main], [ICUUC_LIBS="-licuuc"], [
        AC_MSG_ERROR([Could not find ICU lib])])
    AC_CHECK_LIB(icudata, [main], [ICU_LIBS="-licudata $ICUUC_LIBS"], [
        AC_MSG_ERROR([Could not find ICU lib])])
    icu=true
     ;;
   *)
    # determine if library is installed in the provided path
    if test -d "$with_icu/lib"; then
        LDFLAGS="-L$with_icu/lib $LDFLAGS"
        CXXFLAGS="-I$with_icu/include $CXXFLAGS"
        CPPFLAGS="-I$with_icu/include $CPPFLAGS"

        AC_CHECK_HEADERS("unicode/uchar.h",
         ICU_CXXFLAGS="-I$with_icu/include"
         ICU_CPPFLAGS="-I$with_icu/include"
         ICU_LDFLAGS="-L$with_icu/lib",
         AC_MSG_ERROR([unicode/uchar.h header not found or not usable])
        )
    fi
    AC_CHECK_LIB(icuuc, [main], [ICUUC_LIBS="-licuuc"], [
        AC_MSG_ERROR([Could not find ICU lib])])
    AC_CHECK_LIB(icudata, [main], [ICU_LIBS="-licudata $ICUUC_LIBS"], [
        AC_MSG_ERROR([Could not find ICU lib])])

    #restore
    LDFLAGS=$SAVE_LDFLAGS
    CXXFLAGS=$SAVE_CXXFLAGS
    CPPFLAGS=$SAVE_CPPFLAGS

    icu=true
     ;;
   esac
  ],
   [AC_MSG_RESULT([--with-icu not specified])
    AC_CHECK_HEADERS([unicode/uchar.h], [icu=true], [
        AC_MSG_ERROR([unicode/uchar.h header not found or not usable])])
    AC_CHECK_LIB(icuuc, [main], [ICUUC_LIBS="-licuuc"], [
        AC_MSG_ERROR([Could not find ICU lib])])
    AC_CHECK_LIB(icudata, [main], [ICU_LIBS="-licudata $ICUUC_LIBS"], [
        AC_MSG_ERROR([Could not find ICU lib])])
   ]
)
AC_SUBST(ICU_CXXFLAGS)
AC_SUBST(ICU_CPPFLAGS)
AC_SUBST(ICU_LDFLAGS)
AC_SUBST(ICU_LIBS)

if test "x$icu" = "xtrue" ; then
    AC_DEFINE(HAVE_ICU, [1], [Define to have icu ])
fi

# if Examples are enables, check for specific libraries
if test "x$enable_examples" = "xyes" ; then
    #termcap
    AC_MSG_CHECKING(for termcap)
    AC_ARG_WITH(termcap,
      AS_HELP_STRING(--with-termcap=PATH, base of termcap installation),
      [AC_MSG_RESULT($with_termcap)
       case $with_termcap in
       no)
         ;;
       yes)
        AC_CHECK_LIB([termcap], [tputs], [TERMCAP_LIBS="-ltermcap"],
            [AC_MSG_NOTICE([termcap library not found or not usable.])]
        )
        ;;
       *)

        # determine if library is installed
        if test -d "$with_termcap/lib"; then
            LDFLAGS="-L$with_termcap/lib $LDFLAGS"
            CXXFLAGS="-I$with_termcap/include $CXXFLAGS"
            CPPFLAGS="-I$with_termcap/include $CPPFLAGS"

            AC_CHECK_HEADERS([termcap.h],[
                TERMCAP_LDFLAGS="-L$with_termcap/lib"
                TERMCAP_CXXFLAGS="-I$with_termcap/include"
                TERMCAP_CPPFLAGS="-I$with_termcap/include"],
                AC_MSG_NOTICE([termcap.h header not found or not usable])
            )
        else
            LDFLAGS="-L$with_termcap $LDFLAGS"
            CXXFLAGS="-I$with_termcap $CXXFLAGS"
            CPPFLAGS="-I$with_termcap $CPPFLAGS"

            AC_CHECK_HEADERS([termcap.h],[
                TERMCAP_LDFLAGS="-L$with_termcap"
                TERMCAP_CXXFLAGS="-I$with_termcap"
                TERMCAP_CPPFLAGS="-I$with_termcap"],
                AC_MSG_NOTICE([termcap.h header not found or not usable])
            )
        fi

        AC_CHECK_LIB([termcap], [tputs], [TERMCAP_LIBS="-ltermcap"], [
            AC_MSG_NOTICE([termcap library not found or not usable.])])

        #restore
        LDFLAGS=$SAVE_LDFLAGS
        CXXFLAGS=$SAVE_CXXFLAGS
        CPPFLAGS=$SAVE_CPPFLAGS

        ;;
       esac
      ],
      [AC_MSG_RESULT([--with-termcap not specified])
        AC_CHECK_LIB([termcap], [tputs], [TERMCAP_LIBS="-ltermcap"],
            [AC_MSG_NOTICE([termcap library not found or not usable.])]
        )
      ]
    )
    AC_SUBST(TERMCAP_LDFLAGS)
    AC_SUBST(TERMCAP_LIBS)
    AC_SUBST(TERMCAP_CXXFLAGS)
    AC_SUBST(TERMCAP_CPPFLAGS)

      # ReadLine
    AC_MSG_CHECKING(for Readline)
    AC_ARG_WITH(readline,
      AS_HELP_STRING(--with-readline=PATH, base of Readline installation),
      [AC_MSG_RESULT($with_readline)
       case $with_readline in
       no)
        AC_MSG_ERROR([readline library is required for the sample client.])
         ;;
       yes)
        AC_CHECK_HEADERS([readline/readline.h],, [
            AC_MSG_ERROR([readline/readline.h header not found or not usable])
        ])
        # readline requires termcap (or ncurses)
        SAVE_LIBS=$LIBS
        LIBS="$TERMCAP_LIBS $LIBS"
        AC_CHECK_LIB([readline], [rl_save_prompt], [RL_LIBS="-lreadline"], [
            AC_MSG_ERROR([readline library is required for the sample client.])])
        LIBS=$SAVE_LIBS
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

        # readline requires termcap (or ncurses)
        SAVE_LIBS=$LIBS
        LIBS="$TERMCAP_LIBS $LIBS"
        AC_CHECK_LIB([readline], [rl_save_prompt], [RL_LIBS="-lreadline"], [
            AC_MSG_ERROR([readline library is required for the sample client.])])
        LIBS=$SAVE_LIBS

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
        # readline requires termcap (or ncurses)
        SAVE_LIBS=$LIBS
        LIBS="$TERMCAP_LIBS $LIBS"
        AC_CHECK_LIB([readline], [rl_save_prompt], [RL_LIBS="-lreadline"], [
            AC_MSG_ERROR([readline library is required for the sample client.])])
        LIBS=$SAVE_LIBS
      ]
      )
      AC_SUBST(RL_LDFLAGS)
      AC_SUBST(RL_LIBS)
      AC_SUBST(RL_CXXFLAGS)
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
AC_ARG_ENABLE(tests,
    AS_HELP_STRING([--enable-tests], [build test applications]),
    [], [enable_tests=no])
if test "x$enable_tests" = "xyes" ; then
    AC_ARG_WITH(gtest,
        AS_HELP_STRING(--with-gtest=PATH, specify GTest location),
        [AC_SUBST([GTEST_DIR],[$with_gtest])],
        [AC_MSG_ERROR([GTest location must be specified])]
    )
fi
AM_CONDITIONAL([BUILD_TESTS], [test "$enable_tests" = "yes"])
AC_MSG_RESULT([$enable_tests])


## Bindings

# Python
AC_MSG_CHECKING([if building Python bindings])
AC_ARG_ENABLE(python,
    AS_HELP_STRING([--enable-python], [build Python language bindings]),
    [AC_MSG_RESULT([$enableval])],
    [
     AC_MSG_RESULT([$enableval])
     enable_python=no]
)

USE_PYTHON3=no
if test "x$enable_python" = "xyes" ; then

    AC_ARG_WITH(python3,
      AS_HELP_STRING([--with-python3], [build Python 3 language bindings]),
      [
       case $with_python3 in
       yes)
        USE_PYTHON3=yes
         ;;
       *)
        USE_PYTHON3=yes
        ;;
       esac
      ],
    )

    if test "x$USE_PYTHON3" = "xyes" ; then
        SWIG_FLAGS="-py3"
	    AC_SUBST([SWIG_FLAGS])

        AM_PATH_PYTHON(3)
        AX_PYTHON_DEVEL(>= '3.0.0')
    else
        AM_PATH_PYTHON
        AX_PYTHON_DEVEL
    fi

    AX_PKG_SWIG(2.0.0, [], [
        AC_MSG_ERROR([SWIG executable not found!])
    ])
    AX_SWIG_ENABLE_CXX
    AX_SWIG_MULTI_MODULE_SUPPORT
    AX_SWIG_PYTHON

    if test -z "$SWIG"; then
        AC_MSG_ERROR([SWIG executable not found!])
    fi

    if test -z "$PYTHON_VERSION"; then
        AC_MSG_ERROR([Python development files not found!])
    fi

fi
AM_CONDITIONAL([BUILD_PYTHON], [test "$enable_python" = "yes"])

# PHP
AC_MSG_CHECKING([if building PHP bindings])
AC_ARG_ENABLE(php,
    AS_HELP_STRING([--enable-php], [build PHP language bindings]),
    [AC_MSG_RESULT([yes])],
    [AC_MSG_RESULT([no])
     enable_php=no]
)
if test "x$enable_php" = "xyes" ; then
    AC_CHECK_PROGS(PHPCONFIG, php-config php5-config php-config5 php4-config php-config4)
    AC_MSG_CHECKING(for PHP header files)
    PHPINC="`$PHPCONFIG --includes 2>/dev/null`"
    if test "$PHPINC"; then
        AC_MSG_RESULT($PHPINC)
    else
        dirs="/usr/include/php /usr/local/include/php /usr/include/php5 /usr/local/include/php5 /usr/include/php4 /usr/local/include/php4 /usr/local/apache/php"
        for i in $dirs; do
            if test -r $i/main/php_config.h -o -r $i/main/php_version.h; then
                AC_MSG_RESULT($i is found)
                PHPEXT="$i"
                PHPINC="-I$PHPEXT -I$PHPEXT/main -I$PHPEXT/TSRM -I$PHPEXT/Zend"
                break;
            fi
        done
    fi
    if test -z "$PHPINC"; then
        AC_MSG_ERROR([PHP headers not found!])
    fi

    AC_MSG_CHECKING(for PHP extension-dir)
    PHPLIBDIR="`$PHPCONFIG --extension-dir 2>/dev/null | sed "s#/lib/#/lib${LIBPOSTFIX}/#g"`"
    if test ! -z "$PHPLIBDIR"; then
        AC_MSG_RESULT($PHPLIBDIR)
    else
        AC_MSG_ERROR([PHP development files not found!])
    fi

    AC_SUBST(PHPINC)
    AC_SUBST(PHPEXT)
    AC_SUBST(PHPLIBDIR)

    m4_define([php_version], [$(php-config --version | cut -d '.' -f 1)])
    if test "x"php_version = "x7" ; then
        AX_PKG_SWIG(3.0.11, [SWIG_FLAGS_PHP='-php7'], AC_MSG_ERROR([PHP 7 requires SWIG >=3.0.11]))
    else
        SWIG_FLAGS_PHP='-php'
    fi

    #do AX_PKG_SWIG again to have $SWIG correctly defined, no only for >=3.0.11
    AX_PKG_SWIG(2.0.0, [], [
        AC_MSG_ERROR([SWIG executable not found!])
    ])
    AX_SWIG_ENABLE_CXX
    AX_SWIG_MULTI_MODULE_SUPPORT

    if test -z "$SWIG"; then
        AC_MSG_ERROR([SWIG executable not found!])
    fi
    
    AC_SUBST([SWIG_FLAGS_PHP])

fi
AM_CONDITIONAL([BUILD_PHP], [test "$enable_php" = "yes"])

AC_CHECK_LIB([stdc++fs], [main], [LDFLAGS="-lstdc++fs $LDFLAGS"], [] )

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
 doc/Doxyfile
])
AC_OUTPUT

AC_MSG_NOTICE([Configured to build Mega SDK:

  SDK version:      mega_version
  Host setup:       ${host}
  Install prefix:   $prefix
  Compiler:         $CXX
  CXXFLAGS:         $CXXFLAGS
  LDFLAGS:          $LDFLAGS
  gcc hardening:    $enable_gcc_hardening
  debug:            $enable_debug
  static:           $enable_static
  sync subsystem:   $enable_sync
  chat:             $enable_chat
  MEGA API          $enable_megaapi
  MEGA API RPC      $enable_megaapirpc
  example apps:     $enable_examples
  Perf Logger:      $enable_log_performance
  Drive Notif:      $enable_drive_notifications
  FUSE:             $enable_fuse
  MEGA_USE_C_ARES:  $enable_mega_c_ares

  inotify:          $enable_inotify
  posix threads:    $enable_posix_threads

  Python bindings:  $enable_python
  Python3 bindings: $USE_PYTHON3
  PHP bindings:     $enable_php
  SWIG_FLAGS_PHP:   $SWIG_FLAGS_PHP
  
  Java bindings:    $enable_java
  
  OpenSSL:          $LIBSSL_FLAGS $LIBSSL_LDFLAGS $LIBSSL_LIBS
  Crypto++:         $CRYPTO_CXXFLAGS $CRYPTO_LDFLAGS $CRYPTO_LIBS
  Sodium:           $SODIUM_CXXFLAGS $SODIUM_LDFLAGS $SODIUM_LIBS
  Zlib:             $ZLIB_CXXFLAGS $ZLIB_LDFLAGS $ZLIB_LIBS
  SQLite3:          $DB_CXXFLAGS $DB_LDFLAGS $DB_LIBS
  c-ares:           $CARES_FLAGS $CARES_LDFLAGS $CARES_LIBS
  cURL:             $LIBCURL_FLAGS $LIBCURL_LIBS
  FreeImage:        $FI_CXXFLAGS $FI_LDFLAGS $FI_LIBS
  PDFium:           $PDF_CXXFLAGS $PDF_LDFLAGS $PDF_LIBS
  ICU:              $ICU_CXXFLAGS $ICU_LDFLAGS $ICU_LIBS $ICU_MSG
  Readline:         $RL_CXXFLAGS $RL_LDFLAGS $RL_LIBS
  Termcap:          $TERMCAP_CXXFLAGS $TERMCAP_LDFLAGS $TERMCAP_LIBS
  PCRE:             $PCRE_CXXFLAGS $PCRE_LDFLAGS $PCRE_LIBS
  LIBUV:            $LIBUV_CXXFLAGS $LIBUV_LDFLAGS $LIBUV_LIBS
  LIBRAW:           $LIBRAW_CXXFLAGS $LIBRAW_LDFLAGS $LIBRAW_LIBS
  LIBRT:            $LIBRT_CXXFLAGS $LIBRT_LDFLAGS $LIBRT_LIBS
  LIBMEDIAINFO:     $LIBMEDIAINFO_CXXFLAGS $LIBMEDIAINFO_LDFLAGS $LIBMEDIAINFO_LIBS
  FFMPEG:           $FFMPEG_CXXFLAGS $FFMPEG_LDFLAGS $FFMPEG_LIBS
  FUSE:             $FUSE_CPPFLAGS $FUSE_LDFLAGS $FUSE_LIBS
])
