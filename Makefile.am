##
# @file Makefile.am
# @brief an input file for automake tool
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

AUTOMAKE_OPTIONS=foreign 1.9 subdir-objects
ACLOCAL_AMFLAGS=-I m4

AM_CPPFLAGS = \
	-I$(top_srcdir)/include -I$(top_srcdir)/third_party/utf8proc

include m4/aminclude.am

if WIN32
AM_CPPFLAGS+=-I$(top_srcdir)/include/mega/win32
else
AM_CPPFLAGS+=-I$(top_srcdir)/include/mega/posix
endif

if USE_IOS
AM_CPPFLAGS+=-I$(top_srcdir)/include/mega/gfx
AM_LIBTOOLFLAGS="--tag=CXX"
endif

if ANDROID
AM_LIBTOOLFLAGS="--tag=CXX"
endif

if LINUX
AM_LIBTOOLFLAGS="--tag=CXX"
endif

if DRAGONFLY
AM_LIBTOOLFLAGS="--tag=CXX"
endif

if NETBSD
AM_LIBTOOLFLAGS="--tag=CXX"
endif

VERSION_INFO=$(LT_CURRENT):$(LT_REVISION):$(LT_AGE)

CLEANFILES=
DISTCLEANFILES=
BUILT_SOURCES=
noinst_PROGRAMS=

bin_PROGRAMS=

include src/include.am
include examples/include.am
include tests/include.am

if BUILD_PYTHON
include bindings/python/include.am
endif

if BUILD_PHP
include bindings/php/include.am
endif

if BUILD_JAVA
include bindings/java/include.am
endif

SUBDIRS = include

EXTRA_DIST += autogen.sh
EXTRA_DIST += libmega.pc.in
EXTRA_DIST += $(DX_CONFIG)
EXTRA_DIST += m4/ax_prog_doxygen.m4
EXTRA_DIST += m4/aminclude.am

pkgconfigdir=$(libdir)/pkgconfig
pkgconfig_DATA=libmega.pc

DISTCLEANFILES+=*~ libmega.pc

if WIN32
platform_includes=$(top_srcdir)/include/mega/win32
else
platform_includes=$(top_srcdir)/include/mega/posix
endif

if CPPCHECK
CPPCHECK_CMD = cppcheck \
	--template='{file};{line};{severity};{id};{message}' \
	--enable=warning,portability,information,missingInclude \
	--std=c++03 --force \
	--quiet \
	-I $(top_srcdir)/include \
	-I $(platform_includes)

CPPCHECK_DIRS = \
	"$(top_srcdir)/src" \
    "$(top_srcdir)/examples"

cppcheck:
	$(CPPCHECK_CMD) $(CPPCHECK_DIRS)
endif

MOSTLYCLEANFILES = $(DX_CLEANFILES)

doc: doxygen-run
