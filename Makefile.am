##
# @file Makefile.am
# @brief an input file for automake tool
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

AUTOMAKE_OPTIONS=foreign 1.9 subdir-objects
ACLOCAL_AMFLAGS=-I m4

AM_CPPFLAGS = \
	-I$(top_srcdir)/include


if WIN32
AM_CPPFLAGS+=-I$(top_srcdir)/include/mega/win32
else
AM_CPPFLAGS+=-I$(top_srcdir)/include/mega/posix
endif

VERSION_INFO=$(LT_CURRENT):$(LT_REVISION):$(LT_AGE)

CLEANFILES=
DISTCLEANFILES=
BUILT_SOURCES=
include src/include.am
include tests/include.am
include examples/include.am

SUBDIRS = include

EXTRA_DIST=autogen.sh
EXTRA_DIST+=libmega.pc.in

pkgconfigdir=$(libdir)/pkgconfig
pkgconfig_DATA=libmega.pc

DISTCLEANFILES+=*~ libmega.pc
