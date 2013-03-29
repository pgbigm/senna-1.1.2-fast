lib_LTLIBRARIES = libmyisenna.la
noinst_PROGRAMS = myistest

AM_CFLAGS = -Wall -O3 -fno-strict-aliasing

pkginclude_HEADERS = myisenna.h

INCLUDES = -I. -I../../.. $(SENNA_INCLUDEDIR) -I$(MYSQL_SRCDIR)/include -I$(MYSQL_SRCDIR)

DEFS = -D_REENTRANT -DSENNA_HOME=\"$(SENNA_HOME)\" $(SENNA_CFLAGS)

libmyisenna_la_SOURCES = myisenna.cc
libmyisenna_la_LDFLAGS = -version-info 0:0:0

myistest_SOURCES = myistest.c
myistest_LDADD = libmyisenna.la $(MYSQL_SRCDIR)/libmysqld/libmysqld.a $(top_builddir)/lib/libsenna.la -lpthread -lz  -lcrypt
