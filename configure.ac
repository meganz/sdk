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

AM_INIT_AUTOMAKE([1.10 foreign])
AC_CONFIG_HEADERS([include/mega/config.h])
LT_INIT([shared static win32-dll])
AC_CONFIG_MACRO_DIR([m4])

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


# Check programs
AC_PROG_CXX
AC_PROG_LIBTOOL
AM_SANITY_CHECK

# Check headers
AC_STDC_HEADERS
AC_HEADER_STDC
AC_HEADER_STDBOOL
AC_CHECK_HEADERS([arpa/inet.h netdb.h netinet/in.h stddef.h stdint.h stdlib.h sys/socket.h sys/timeb.h])

# Check dependencies
AC_CHECK_PROG(curl_config_prog, curl-config, yes, no)
if test "x$curl_config_prog" != "xyes"; then
    AC_MSG_ERROR([Unable to find libcurl configuration program curl-config in the path.])
fi
LIBCURL_FLAGS=`curl-config --cflags`
LIBCURL_LIBS=`curl-config --libs`

AC_SUBST(LIBCURL_FLAGS)
AC_SUBST(LIBCURL_LIBS)

#XXX: add detection of libcrypto++
PKG_CHECK_MODULES([CRYPTO], [libcrypto++])

# Check if building for Win32
AC_MSG_CHECKING([if building for Win32 platform])
case "$host" in
  *-*-mingw*|*-*-cygwin*)
    WIN32=yes
    ;;
  *)
    WIN32=no
    ;;
esac
AC_MSG_RESULT([${WIN32}])
AM_CONDITIONAL([WIN32], [test "${WIN32}" = "yes"])

# Enable debug
AC_ARG_ENABLE(debug,
     AS_HELP_STRING(--enable-debug, enable support for running in debug mode),
        [], [enable_debug=no])
if test x$enable_debug = xyes; then
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
if test "x$enable_examples" = "xyes"; then
EXAMPLES_LDFLAGS="-lfreeimage -lreadline -ltermcap"
AC_SUBST(EXAMPLES_LDFLAGS)
fi
AC_MSG_RESULT([$enable_examples])

# Tests
AC_MSG_CHECKING([if building test applications])
AC_ARG_ENABLE([tests],
     AS_HELP_STRING([--enable-tests, build test applications]),
	[], [enable_tests=no])
if test x$enable_debug = xyes; then
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
