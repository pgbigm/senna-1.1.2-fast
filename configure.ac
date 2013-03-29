AC_INIT([senna],1.1.2,[senna@razil.jp])
AM_CONFIG_HEADER(config.h)

AM_INIT_AUTOMAKE(AC_PACKAGE_NAME, AC_PACKAGE_VERSION)

AC_PROG_CC
AC_DEFUN([CHECK_CFLAG], [
  old_CFLAGS=$CFLAGS
  CFLAGS="$CFLAGS $1 -Werror"
  AC_COMPILE_IFELSE([[
#include <stdio.h>
int main(int argc, char **argv)
{
  printf("test cflags, argc:%d argv[0]:%s", argc, argv[0]);
  return 0;
}
    ]],
    [check_cflag=yes],
    [check_cflag=no])
  CFLAGS="$old_CFLAGS"
  if test "x$check_cflag" = "xyes"; then
    CFLAGS="$CFLAGS $1"
  fi
])
if test "$GCC" = "yes"; then
  CHECK_CFLAG([-Wall])
  CHECK_CFLAG([-Wextra])
  if test "x$check_cflag" = "xno"; then
    CHECK_CFLAG([-W])
  fi
  CHECK_CFLAG([-Wno-unused-parameter])
  CHECK_CFLAG([-Wno-sign-compare])
  CHECK_CFLAG([-Wno-pointer-sign])
  CHECK_CFLAG([-Wno-missing-field-initializers])

  CHECK_CFLAG([-Wformat=2])
  CHECK_CFLAG([-Wstrict-aliasing=2])
  CHECK_CFLAG([-Wdisabled-optimization])
  CHECK_CFLAG([-Wfloat-equal])
  CHECK_CFLAG([-Wpointer-arith])
  CHECK_CFLAG([-Wdeclaration-after-statement])
  CHECK_CFLAG([-Wbad-function-cast])
  CHECK_CFLAG([-Wcast-align])
  CHECK_CFLAG([-Wredundant-decls])
#  CHECK_CFLAG([-Wunsafe-loop-optimizations])
#  CHECK_CFLAG([-Wunreachable-code])
#  CHECK_CFLAG([-Wswitch-enum])
#  CHECK_CFLAG([-Wshadow])
#  CHECK_CFLAG([-Wconversion])
#  CHECK_CFLAG([-Wwrite-strings])
  CHECK_CFLAG([-Winline])
  CHECK_CFLAG([--param max-inline-insns-single=12000])
  CHECK_CFLAG([--param large-function-growth=28000])
  CHECK_CFLAG([--param inline-unit-growth=400])
fi
AM_PROG_LIBTOOL
AC_CONFIG_FILES([Makefile test/Makefile src/Makefile lib/Makefile bindings/mysql/myisenna/Makefile])
AC_CHECK_HEADERS(sys/mman.h sys/time.h sys/param.h sys/types.h pthread.h sys/resource.h)
AC_CHECK_HEADERS(netdb.h sys/wait.h sys/socket.h netinet/in.h netinet/tcp.h)
AC_CHECK_HEADERS(ucontext.h signal.h errno.h)
AC_CHECK_FUNCS(localtime_r)
AC_SYS_LARGEFILE
AC_TYPE_OFF_T
AC_TYPE_SIZE_T
AC_CHECK_SIZEOF(off_t)

# will deprecated
AC_ARG_WITH(sennahome,
  [AC_HELP_STRING([--with-sennahome=DIR],
    [specify Senna home directory(will deprecated)])],
  SENNA_HOME="$withval",
  SENNA_HOME="/var/senna")
AC_DEFINE_UNQUOTED(SENNA_HOME, "$SENNA_HOME", "specified home directory")

# log directory
AC_ARG_WITH(log,
  [AC_HELP_STRING([--with-log=PATH],
    [specify Senna log path.])],
  SENNA_LOG_PATH="$withval",
  SENNA_LOG_PATH="$SENNA_HOME/log/senna.log")
AC_DEFINE_UNQUOTED(SENNA_LOG_PATH, "$SENNA_LOG_PATH", "specified log directory")

# default encoding
AC_ARG_WITH(encoding,
  [AC_HELP_STRING([--with-encoding=ENCODING],
    [specify Senna default encoding(euc/sjis/utf8/latin1/koi8r/none)])],
  SENNA_DEFAULT_ENCODING="$withval",
  SENNA_DEFAULT_ENCODING="euc_jp")
AC_DEFINE_UNQUOTED(SENNA_DEFAULT_ENCODING, "$SENNA_DEFAULT_ENCODING", "specified default encoding")

# default initial_n_segments
AC_ARG_WITH(initial_n_segments,
  [AC_HELP_STRING([--with-initial-n-segments=NUMBER],
    [specify Senna default initial_n_segments])],
  SENNA_DEFAULT_INITIAL_N_SEGMENTS="$withval",
  SENNA_DEFAULT_INITIAL_N_SEGMENTS="512")
AC_DEFINE_UNQUOTED(SENNA_DEFAULT_INITIAL_N_SEGMENTS, $SENNA_DEFAULT_INITIAL_N_SEGMENTS, "specified default initial_n_segments")

# default query escalation threshold
AC_ARG_WITH(query_escalation_threshold,
  [AC_HELP_STRING([--with-query-escalation-threshold=NUMBER],
    [specify Senna default query escalation threshold])],
  SENNA_DEFAULT_QUERY_ESCALATION_THRESHOLD="$withval",
  SENNA_DEFAULT_QUERY_ESCALATION_THRESHOLD="0")
AC_DEFINE_UNQUOTED(SENNA_DEFAULT_QUERY_ESCALATION_THRESHOLD, $SENNA_DEFAULT_QUERY_ESCALATION_THRESHOLD, "specified query escalation threshold")

AC_CHECK_LIB(m, log, [M_LIBS="-lm"], [AC_MSG_ERROR("No libm found")])
AC_CHECK_LIB(pthread, pthread_mutex_init, [PTHREAD_LIBS="-lpthread"], [AC_MSG_ERROR("No libpthread found")])

# aio
AC_ARG_ENABLE(aio,
  [AC_HELP_STRING([--enable-aio],
    [use aio/dio based cache. [default=no]])],,
  [enable_aio="no"])
if test "x$enable_aio" != "xno"; then
 	AC_CHECK_LIB(rt, nanosleep, [], [AC_MSG_ERROR("No librt found")])
  AC_DEFINE(USE_AIO, [1], [use AIO/DIO])
fi

# nfkc
AC_ARG_ENABLE(nfkc,
  [AC_HELP_STRING([--enable-nfkc],
    [use nfkc based utf8 normalization. [default=yes]])],,
  [enable_nfkc="yes"])
if test "x$enable_nfkc" != "xyes"; then
  AC_DEFINE(NO_NFKC, [1], [compile without nfkc.c])
fi

# gcov
AC_ARG_ENABLE(gcov,
  [AC_HELP_STRING([--enable-gcov],
    [use gcov for coverage analysis. [default=no]])],,
  [enable_gcov="no"])
if test "x$enable_gcov" = "xno"; then
 	OPT_CFLAGS=""
else
 	OPT_CFLAGS="-O0"
  GCOV_CFLAGS="-fprofile-arcs -ftest-coverage"
 	AC_CHECK_LIB(gcov, __gcov_init, [], [AC_MSG_ERROR("No libgcov found")])
  GCOV_LIBS="-lgcov"
  AC_SUBST(GCOV_LIBS)

  AC_PATH_PROG(LCOV, lcov, no)
  if test "x$LCOV" = "xno"; then
    AC_MSG_ERROR([lcov is missing.])
  fi

  AC_PATH_PROG(GENHTML, genhtml, no)
  if test "x$GENHTML" = "xno"; then
    AC_MSG_ERROR([genhtml is missing.])
  fi
  AC_SUBST(GCOV_CFLAGS)
  AC_SUBST(LCOV)
  AC_SUBST(GENHTML)
fi
AM_CONDITIONAL(GCOV_ENABLED, test "x$enable_gcov" = "xyes")

# failmalloc
AC_ARG_ENABLE(fmalloc,
  [AC_HELP_STRING([--enable-fmalloc],
    [make memory allocation failed in specified condition for debug. [default=no]])],,
  [enable_fail_malloc="no"])
if test "x$enable_fail_malloc" != "xno"; then
  AC_DEFINE(USE_FAIL_MALLOC, [1], [use fmalloc])
fi

# libmyisenna
AC_ARG_WITH(mysqlsrc,
  [AC_HELP_STRING([--with-mysqlsrc=DIR],
    [specify MySQL source dictionary.])],
  MYSQL_SRCDIR="$withval")
AC_SUBST(MYSQL_SRCDIR)

# epoll/poll/select check
AC_CHECK_HEADER(sys/epoll.h, [
  AC_CHECK_FUNC(epoll_create, [
    AC_TRY_RUN([
#include <sys/epoll.h>
int main(int argc, char **argv) { return (epoll_create(16) < 0); }
    ],
    [
      have_epoll="yes"
      AC_DEFINE(USE_EPOLL, [1], [use epoll])
    ]
    )
  ])
])

if test "x$have_epoll" != "xyes"; then
  AC_CHECK_HEADER(sys/poll.h, [
    AC_CHECK_FUNC(poll, [
      have_poll="yes"
      AC_DEFINE(USE_POLL, [1], [use poll])
    ])
  ])
  if test "x$have_poll" != "xyes"; then
    AC_CHECK_FUNC(select, [
      have_select="yes"
      AC_CHECK_HEADERS(sys/select.h)
      AC_DEFINE(USE_SELECT, [1], [use select])
    ],[
      AC_MSG_ERROR([epoll/poll/select is missing.])
    ])
  fi
fi

# check MSG_NOSIGNAL defined
AC_MSG_CHECKING([whether MSG_NOSIGNAL defined])
AC_COMPILE_IFELSE([[
#include <sys/types.h>
#include <sys/socket.h>

#if __FreeBSD__ >= 2 && __FreeBSD_version >= 600020
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0x20000
#endif
#endif

int main(int argc, char **argv)
{
  return MSG_NOSIGNAL;
}
    ]],
    [
      AC_MSG_RESULT(yes)
      AC_DEFINE(USE_MSG_NOSIGNAL, [1], [use MSG_SIGNAL])
    ],
    [
      AC_MSG_RESULT(no)
    ])

# mecab
# NOTE: MUST be checked last
AC_ARG_WITH(mecab,
  [AC_HELP_STRING([--with-mecab],
    [use mecab for morphological analysis. [default=yes]])],
  [with_mecab="$withval"],
  [with_mecab="yes"])
if test "x$with_mecab" = "xno"; then
  AC_DEFINE(NO_MECAB, [1], [without MeCab])
else
  # mecab-config
  AC_ARG_WITH(mecab-config,
    [AC_HELP_STRING([--with-mecab-config=PATH],
      [set mecab-config location. [search path]])],
    [AC_CHECK_FILE("$withval", MECAB_CONFIG="$withval", MECAB_CONFIG=no)],
    [AC_PATH_PROG(MECAB_CONFIG, mecab-config, no)])
  if test "x$MECAB_CONFIG" = "xno"; then
    AC_MSG_ERROR([mecab-config is missing.])
  fi

  MECAB_CFLAGS="-I`$MECAB_CONFIG --inc-dir`"
  LDFLAGS="$LDFLAGS -L`$MECAB_CONFIG --libs-only-L`"
 	AC_CHECK_LIB(mecab, mecab_new, [], [AC_MSG_ERROR("No libmecab found")])
 	AC_CHECK_LIB(mecab, mecab_dictionary_info,
               [AC_DEFINE(USE_MECAB_DICINFO, [1], [check mecab encodings])], [])
  CPPFLAGS="$CPPFLAGS $MECAB_CFLAGS"
  AC_CHECK_HEADER(mecab.h, , [AC_MSG_ERROR("No mecab.h found")])
fi

# flags for compile senna
CFLAGS="$CFLAGS $MECAB_CFLAGS $OPT_CFLAGS $GCOV_CFLAGS"
LIBS="$LIBS $PTHREAD_LIBS $M_LIBS"
AC_DEFINE_UNQUOTED(CONFIGURE_OPTIONS, "$ac_configure_args", "specified configure options")

# flags for senna-cfg
SENNA_LIBDIR='-L${libdir}'
SENNA_INCLUDEDIR="-I${includedir}/senna"
SENNA_CFLAGS="$GCOV_CFLAGS"
SENNA_LIBS="-lsenna $GCOV_LIBS $PTHREAD_LIBS $M_LIBS"

AC_SUBST(SENNA_LIBDIR)
AC_SUBST(SENNA_INCLUDEDIR)
AC_SUBST(SENNA_CFLAGS)
AC_SUBST(SENNA_LIBS)

AC_OUTPUT([
  senna-cfg
  senna.spec
  vcc/config.h
  ],
  chmod +x senna-cfg)

echo "
$PACKAGE_NAME $PACKAGE_VERSION configuration:
-----------------------
  Compiler:              ${CC}
  CFLAGS:                ${CFLAGS}
  Libraries:             ${LIBS}

  Install path prefix:   ${prefix}

Now type 'make' to build $PACKAGE_NAME $PACKAGE_VERSION!
"
