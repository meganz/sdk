AC_PREREQ(2.61)

AC_INIT([libmega], [0.1], [https://mega.co.nz/])

AM_INIT_AUTOMAKE([1.10 foreign])
AC_CONFIG_HEADERS([config.h])
LT_INIT([shared static win32-dll])

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
