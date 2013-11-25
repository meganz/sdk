AC_PREREQ(2.61)

m4_define([mega_major_version], [0])
m4_define([mega_minor_version], [1])
m4_define([mega_micro_version], [0])
m4_define([mega_version],
          [mega_major_version.mega_minor_version.mega_micro_version])

# libtool interface versioning
m4_define([mega_lt_revision], [0])
m4_define([mega_lt_current], [m4_eval(100 * mega_minor_version + mega_micro_version)])
m4_define([mega_lt_age], [0])

AC_INIT([libmega], [mega_version], [https://mega.co.nz/])

AM_INIT_AUTOMAKE([1.10 foreign])
AC_CONFIG_HEADERS([config.h])
LT_INIT([shared static win32-dll])

MEGA_MAJOR_VERSION=mega_major_version
MEGA_MINOR_VERSION=mega_minor_version
MEGA_MICRO_VERSION=mega_micro_version

AC_SUBST(MEGA_MAJOR_VERSION)
AC_SUBST(MEGA_MINOR_VERSION)
AC_SUBST(MEGA_MICRO_VERSION)

AC_DEFINE(MEGA_MAJOR_VERSION, [mega_major_version],
	  [MEGA SDK major version])
AC_DEFINE(MEGA_MINOR_VERSION, [mega_minor_version],
	  [MEGA SDK minor version])
AC_DEFINE(MEGA_MICRO_VERSION, [mega_micro_version],
	  [MEGA SDK micro version])


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

PKG_CHECK_MODULES([CRYPTO], [libcrypto++])

# examples
AC_ARG_ENABLE([examples],
     AS_HELP_STRING([--enable-examples build example applications]),
	[], [enable_examples=no])
AM_CONDITIONAL([BUILD_EXAMPLES], [test "$enable_examples" = "yes"])

# check if building for Win32
AC_MSG_CHECKING([if building for Win32 platform])
case "$host" in
  *-*-mingw*|*-*-cygwin*)
    platform_win32=yes
    ;;
  *)
    platform_win32=no
    ;;
esac
AC_MSG_RESULT($platform_win32)
AM_CONDITIONAL(PLATFORM_WIN32, test x$platform_win32 = xyes)


CXXFLAGS="-g"

AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_FILES([
 Makefile
 include/Makefile
 libmega.pc
 tests/Makefile
])

AC_OUTPUT
