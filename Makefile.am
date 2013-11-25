AUTOMAKE_OPTIONS=foreign 1.9 subdir-objects
ACLOCAL_AMFLAGS=-I m4

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
