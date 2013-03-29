dnl
dnl $ Id: $
dnl

PHP_ARG_WITH(sen-ctx, whether sen_ctx is available,[  --with-sen-ctx[=DIR] With sen_ctx support])


if test "$PHP_SEN_CTX" != "no"; then


  if test -r "$PHP_SEN_CTX/include/senna/senna.h"; then
	PHP_SEN_CTX_DIR="$PHP_SEN_CTX"
  else
	AC_MSG_CHECKING(for sen_ctx in default path)
	for i in /usr /usr/local; do
	  if test -r "$i/include/senna/senna.h"; then
		PHP_SEN_CTX_DIR=$i
		AC_MSG_RESULT(found in $i)
		break
	  fi
	done
	if test "x" = "x$PHP_SEN_CTX_DIR"; then
	  AC_MSG_ERROR(not found)
	fi
  fi

  PHP_ADD_INCLUDE($PHP_SEN_CTX_DIR/include)

  export OLD_CPPFLAGS="$CPPFLAGS"
  export CPPFLAGS="$CPPFLAGS $INCLUDES -DHAVE_SEN_CTX"
  AC_CHECK_HEADER([senna/senna.h], [], AC_MSG_ERROR('senna/senna.h' header not found))
  PHP_SUBST(SEN_CTX_SHARED_LIBADD)


  PHP_CHECK_LIBRARY(senna, sen_init,
  [
	PHP_ADD_LIBRARY_WITH_PATH(senna, $PHP_SEN_CTX_DIR/lib, SEN_CTX_SHARED_LIBADD)
  ],[
	AC_MSG_ERROR([wrong senna lib version or lib not found])
  ],[
	-L$PHP_SEN_CTX_DIR/lib
  ])
  export CPPFLAGS="$OLD_CPPFLAGS"

  export OLD_CPPFLAGS="$CPPFLAGS"
  export CPPFLAGS="$CPPFLAGS $INCLUDES -DHAVE_SEN_CTX"
  AC_CHECK_TYPE(sen_ctx *, [], [AC_MSG_ERROR(required payload type for resource sen_ctx not found)], [#include "$srcdir/php_sen_ctx.h"])
  AC_CHECK_TYPE(sen_db *, [], [AC_MSG_ERROR(required payload type for resource sen_db not found)], [#include "$srcdir/php_sen_ctx.h"])
  export CPPFLAGS="$OLD_CPPFLAGS"


  PHP_SUBST(SEN_CTX_SHARED_LIBADD)
  AC_DEFINE(HAVE_SEN_CTX, 1, [ ])
  PHP_NEW_EXTENSION(sen_ctx, sen_ctx.c , $ext_shared)

fi

