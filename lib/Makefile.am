lib_LTLIBRARIES = libsenna.la

AM_CFLAGS = -fno-strict-aliasing
AM_INCLUDES = -I. -I..
DEFS=-D_REENTRANT

libsenna_la_SOURCES = io.c sym.c set.c inv.c str.c lex.c index.c nfkc.c snip.c cache.c query.c store.c inv08.c sym08.c com.c ql.c scm.c ctx.c

libsenna_la_LDFLAGS = -version-info 0:0:0
noinst_HEADERS = cache.h com.h inv.h io.h lex.h ql.h senna_in.h set.h snip.h store.h str.h sym.h ctx.h

CLEANFILES = *.gcno *.gcda
