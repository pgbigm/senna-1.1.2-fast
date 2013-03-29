/* Copyright(C) 2007 Brazil

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "senna_in.h"
#include <string.h>
#include "lex.h"
#include "ql.h"
#include "sym.h"
#include "snip.h"
#include <stdarg.h>
#include <stdio.h>
#include <time.h>

sen_ctx sen_gctx;

/* fixme by 2038 */

sen_rc
sen_timeval_now(sen_timeval *tv)
{
#ifdef WIN32
  time_t t;
  struct _timeb tb;
  time(&t);
  _ftime(&tb);
  tv->tv_sec = (int32_t) t;
  tv->tv_usec = tb.millitm * 1000;
  return sen_success;
#else /* WIN32 */
  struct timeval t;
  sen_rc rc = gettimeofday(&t, NULL) ? sen_external_error : sen_success;
  if (!rc) {
    tv->tv_sec = (int32_t) t.tv_sec;
    tv->tv_usec = t.tv_usec;
  }
  return rc;
#endif /* WIN32 */
}

sen_rc
sen_timeval2str(sen_timeval *tv, char *buf)
{
  struct tm *ltm;
#ifdef WIN32
  time_t tvsec = (time_t) tv->tv_sec;
  ltm = localtime(&tvsec);
#else /* WIN32 */
  struct tm tm;
  time_t t = tv->tv_sec;
  ltm = localtime_r(&t, &tm);
#endif /* WIN32 */
  if (!ltm) { return sen_external_error; }
  snprintf(buf, SEN_TIMEVAL_STR_SIZE - 1, SEN_TIMEVAL_STR_FORMAT,
           ltm->tm_year + 1900, ltm->tm_mon + 1, ltm->tm_mday,
           ltm->tm_hour, ltm->tm_min, ltm->tm_sec, (int) tv->tv_usec);
  buf[SEN_TIMEVAL_STR_SIZE - 1] = '\0';
  return sen_success;
}

sen_rc
sen_str2timeval(const char *str, uint32_t str_len, sen_timeval *tv)
{
  struct tm tm;
  const char *r1, *r2, *rend = str + str_len;
  uint32_t uv;
  memset(&tm, 0, sizeof(struct tm));

  tm.tm_year = (int)sen_atoui(str, rend, &r1) - 1900;
  if ((r1 + 1) >= rend || (*r1 != '/' && *r1 != '-') ||
      tm.tm_year < 0) { return sen_invalid_argument; }
  tm.tm_mon = (int)sen_atoui(++r1, rend, &r1) - 1;
  if ((r1 + 1) >= rend || (*r1 != '/' && *r1 != '-') ||
      tm.tm_mon < 0 || tm.tm_mon >= 12) { return sen_invalid_argument; }
  tm.tm_mday = (int)sen_atoui(++r1, rend, &r1);
  if ((r1 + 1) >= rend || *r1 != ' ' ||
      tm.tm_mday < 1 || tm.tm_mday > 31) { return sen_invalid_argument; }

  tm.tm_hour = (int)sen_atoui(++r1, rend, &r2);
  if ((r2 + 1) >= rend || r1 == r2 || *r2 != ':' ||
      tm.tm_hour < 0 || tm.tm_hour >= 24) {
    return sen_invalid_argument;
  }
  r1 = r2 + 1;
  tm.tm_min = (int)sen_atoui(r1, rend, &r2);
  if ((r2 + 1) >= rend || r1 == r2 || *r2 != ':' ||
      tm.tm_min < 0 || tm.tm_min >= 60) {
    return sen_invalid_argument;
  }
  r1 = r2 + 1;
  tm.tm_sec = (int)sen_atoui(r1, rend, &r2);
  if (r1 == r2 ||
      tm.tm_sec < 0 || tm.tm_sec > 61 /* leap 2sec */) {
    return sen_invalid_argument;
  }
  r1 = r2;

  if ((tv->tv_sec = (int32_t) mktime(&tm)) == -1) { return sen_invalid_argument; }
  if ((r1 + 1) < rend && *r1 == '.') { r1++; }
  uv = sen_atoi(r1, rend, &r2);
  while (r2 < r1 + 6) {
    uv *= 10;
    r2++;
  }
  if (uv >= 1000000) { return sen_invalid_argument; }
  tv->tv_usec = uv;
  return sen_success;
}

#ifdef USE_FAIL_MALLOC
double sen_fmalloc_prob = 1.0;
char *sen_fmalloc_func = NULL;
char *sen_fmalloc_file = NULL;
int sen_fmalloc_line = 0;
#endif /* USE_FAIL_MALLOC */

void
sen_ctx_init(sen_ctx *ctx)
{
  ERRCLR(ctx);
  ctx->db = NULL;
  ctx->phs = NIL;
  ctx->code = NIL;
  ctx->dump = NIL;
  ctx->op = SEN_OP_T0LVL;
  ctx->args = NIL;
  ctx->envir = NIL;
  ctx->value = NIL;
  ctx->ncells = 0;
  ctx->seqno = 0;
  ctx->lseqno = 0;
  ctx->nbinds = 0;
  ctx->nunbinds = 0;
  ctx->feed_mode = sen_ql_atonce;
  ctx->stat = SEN_QL_WAIT_EXPR;
  ctx->cur = NULL;
  ctx->str_end = NULL;
  ctx->batchmode = 0;
  ctx->gc_verbose = 0;
  ctx->inbuf = NULL;
  ctx->co.mode = 0;
  ctx->co.func = NULL;
  ctx->objects = NULL;
  ctx->symbols = NULL;
  ctx->com = NULL;
  sen_rbuf_init(&ctx->outbuf, 0);
  sen_rbuf_init(&ctx->subbuf, 0);
}

sen_rc
sen_ctx_fin(sen_ctx *ctx)
{
  sen_rc rc = sen_success;
  if (ctx->objects) {
    sen_obj *o;
    sen_set_cursor *sc;
    if ((sc = sen_set_cursor_open(ctx->objects))) {
      while (sen_set_cursor_next(sc, NULL, (void **) &o)) { sen_obj_clear(ctx, o); }
      sen_set_cursor_close(sc);
    } else {
      return sen_memory_exhausted;
    }
    sen_set_close(ctx->objects);
  }
  if (ctx->symbols) {
    sen_set_close(ctx->symbols);
  }
  if (ctx->com) {
    if (ctx->stat != SEN_CTX_QUIT) {
      int flags;
      char *str;
      unsigned int str_len;
      sen_ctx_send(ctx, "(quit)", 6, SEN_CTX_HEAD);
      sen_ctx_recv(ctx, &str, &str_len, &flags);
    }
    sen_ctx_send(ctx, "ACK", 3, SEN_CTX_HEAD);
    rc = sen_com_sqtp_close(NULL, ctx->com);
  }
  rc = sen_rbuf_fin(&ctx->outbuf);
  rc = sen_rbuf_fin(&ctx->subbuf);
  return rc;
}

void
sen_ctx_initql(sen_ctx *ctx)
{
  if (!ctx->objects && !ctx->symbols) {
    if ((ctx->objects = sen_set_open(sizeof(int), sizeof(sen_obj), 0))) {
      if ((ctx->symbols = sen_set_open(0, sizeof(sen_obj), 0))) {
        if (ctx->db) { sen_ql_def_db_funcs(ctx); }
        if (!ERRP(ctx, SEN_ERROR)) {
          sen_ql_init_globals(ctx);
          if (!ERRP(ctx, SEN_ERROR)) {
            return;
          }
        }
        sen_set_close(ctx->symbols);
        ctx->symbols = NULL;
      } else {
        MERR("ctx->symbols init failed");
      }
      sen_set_close(ctx->objects);
      ctx->objects = NULL;
    } else {
      MERR("ctx->objects init failed");
    }
  } else {
    MERR("invalid ctx assigned");
  }
}

static void
expand_stack(void)
{
#ifndef WIN32
  struct rlimit rlim;
  if (!getrlimit(RLIMIT_STACK, &rlim)) {
    SEN_LOG(sen_log_notice, "RLIMIT_STACK is %d (%d)", rlim.rlim_cur, getpid());
    if (rlim.rlim_cur != RLIM_INFINITY && rlim.rlim_cur < SEN_STACK_SIZE) {
      rlim.rlim_cur = SEN_STACK_SIZE;
      if (!setrlimit(RLIMIT_STACK, &rlim)) {
        SEN_LOG(sen_log_notice, "expanded RLIMIT_STACK to %d", rlim.rlim_cur);
      }
    }
  }
#endif /* WIN32 */
}

sen_rc
sen_init(void)
{
  sen_rc rc;
  sen_ql_init_const();
  sen_ctx_init(&sen_gctx);
  sen_gctx.encoding = sen_strtoenc(SENNA_DEFAULT_ENCODING);
  expand_stack();
#ifdef USE_AIO
  if (getenv("SEN_DEBUG_PRINT")) {
    sen_debug_print = atoi(getenv("SEN_DEBUG_PRINT"));
  } else {
    sen_debug_print = 0;
  }
  if (getenv("SEN_AIO_ENABLED")) {
    sen_aio_enabled = atoi(getenv("SEN_AIO_ENABLED"));
  } else {
    sen_aio_enabled = 0;
  }
  if (sen_aio_enabled) {
    SEN_LOG(sen_log_notice, "AIO and DIO enabled");
    sen_cache_open();
  }
#endif /* USE_AIO */
#ifdef USE_FAIL_MALLOC
  if (getenv("SEN_FMALLOC_PROB")) {
    sen_fmalloc_prob = strtod(getenv("SEN_FMALLOC_PROB"), 0);
    if (getenv("SEN_FMALLOC_SEED")) {
      srand((unsigned int)atoi(getenv("SEN_FMALLOC_SEED")));
    } else {
      srand((unsigned int)time(NULL));
    }
  }
  if (getenv("SEN_FMALLOC_FUNC")) {
    sen_fmalloc_func = getenv("SEN_FMALLOC_FUNC");
  }
  if (getenv("SEN_FMALLOC_FILE")) {
    sen_fmalloc_file = getenv("SEN_FMALLOC_FILE");
  }
  if (getenv("SEN_FMALLOC_LINE")) {
    sen_fmalloc_line = atoi(getenv("SEN_FMALLOC_LINE"));
  }
#endif /* USE_FAIL_MALLOC */
  if ((rc = sen_lex_init())) {
    SEN_LOG(sen_log_alert, "sen_lex_init failed (%d)", rc);
    return rc;
  }
  if ((rc = sen_com_init())) {
    SEN_LOG(sen_log_alert, "sen_com_init failed (%d)", rc);
    return rc;
  }
  sen_ctx_initql(&sen_gctx);
  if ((rc = sen_gctx.rc)) {
    SEN_LOG(sen_log_alert, "gctx initialize failed (%d)", rc);
    return rc;
  }
  SEN_LOG(sen_log_notice, "sen_init");
  return rc;
}

static int alloc_count = 0;

sen_rc
sen_fin(void)
{
  sen_ctx_fin(&sen_gctx);
  sen_lex_fin();
  sen_str_fin();
  sen_com_fin();
  SEN_LOG(sen_log_notice, "sen_fin (%d)", alloc_count);
  sen_logger_fin();
  return sen_success;
}

sen_ctx *
sen_ctx_open(sen_db *db, int flags)
{
  sen_ctx *ctx = SEN_GMALLOCN(sen_ctx, 1);
  if (ctx) {
    sen_ctx_init(ctx);
    if ((ctx->db = db)) { ctx->encoding = db->keys->encoding; }
    if (flags & SEN_CTX_USEQL) {
      sen_ctx_initql(ctx);
      if (ERRP(ctx, SEN_ERROR)) {
        sen_ctx_close(ctx);
        return NULL;
      }
    }
    if (flags & SEN_CTX_BATCHMODE) { ctx->batchmode = 1; }
  }
  return ctx;
}

sen_ctx *
sen_ctx_connect(const char *host, int port, int flags)
{
  sen_ctx *ctx;
  sen_com_sqtp *com = sen_com_sqtp_copen(NULL, host, port);
  if (!com) { return NULL; }
  if ((ctx = SEN_GMALLOCN(sen_ctx, 1))) {
    sen_ctx_init(ctx);
    ctx->com = com;
  } else {
    sen_com_sqtp_close(NULL, com);
  }
  return ctx;
}

sen_rc
sen_ctx_close(sen_ctx *ctx)
{
  sen_rc rc = sen_ctx_fin(ctx);
  SEN_GFREE(ctx);
  return rc;
}

sen_rc
sen_ctx_send(sen_ctx *ctx, char *str, unsigned int str_len, int flags)
{
  ERRCLR(ctx);
  if (ctx->com) {
    sen_rc rc;
    static uint32_t info = 0;
    sen_com_sqtp_header sheader;
    if ((flags & SEN_CTX_MORE)) { flags |= SEN_CTX_QUIET; }
    sheader.flags = flags;
    sheader.size = str_len;
    sheader.qtype = 0;
    sheader.level = 0;
    sheader.status = 0;
    sheader.info = info++; /* for debug */
    if ((rc = sen_com_sqtp_send(ctx->com, &sheader, (char *)str))) {
      ERR(rc, "sen_com_sqtp_send failed");
    }
    goto exit;
  } else {
    if (ctx->objects) {
      sen_ql_feed(ctx, str, str_len, flags);
      if (ctx->stat == SEN_QL_QUITTING) { ctx->stat = SEN_CTX_QUIT; }
      if (!ERRP(ctx, SEN_CRIT)) {
        if (!(flags & SEN_CTX_QUIET) && ctx->output) {
          ctx->output(ctx, 0, ctx->data.ptr);
        }
      }
      goto exit;
    }
  }
  ERR(sen_invalid_argument, "invalid ctx assigned");
exit :
  return ctx->rc;
}

sen_rc
sen_ctx_recv(sen_ctx *ctx, char **str, unsigned int *str_len, int *flags)
{
  ERRCLR(ctx);
  if (ctx->stat == SEN_CTX_QUIT) {
    *flags = SEN_CTX_QUIT;
    return ctx->rc;
  }
  if (ctx->com) {
    if (sen_com_sqtp_recv(ctx->com, &ctx->com->msg, &ctx->com_status, &ctx->com_info)) {
      *str = NULL;
      *str_len = 0;
      *flags = 0;
    } else {
      sen_com_sqtp_header *rheader = SEN_COM_SQTP_MSG_HEADER(&ctx->com->msg);
      *str = SEN_COM_SQTP_MSG_BODY(&ctx->com->msg);
      *str_len = rheader->size;
      if (rheader->flags & SEN_CTX_QUIT) {
        ctx->stat = SEN_CTX_QUIT;
        *flags = SEN_CTX_QUIT;
      } else {
        *flags = (rheader->flags & SEN_CTX_TAIL) ? 0 : SEN_CTX_MORE;
      }
    }
    if (ctx->com->rc) {
      ERR(ctx->com->rc, "sen_com_sqtp_recv failed!");
    }
    goto exit;
  } else {
    if (ctx->objects) {
      sen_rbuf *buf = &ctx->outbuf;
      unsigned int head, tail;
      int *offsets = (unsigned int *) ctx->subbuf.head;
      int npackets = SEN_RBUF_VSIZE(&ctx->subbuf) / sizeof(unsigned int);
      if (npackets < ctx->bufcur) { return sen_invalid_argument; }
      head = ctx->bufcur ? offsets[ctx->bufcur - 1] : 0;
      tail = ctx->bufcur < npackets ? offsets[ctx->bufcur] : SEN_RBUF_VSIZE(buf);
      *str = buf->head + head;
      *str_len = tail - head;
      *flags = ctx->bufcur++ < npackets ? SEN_CTX_MORE : 0;
      goto exit;
    }
  }
  ERR(sen_invalid_argument, "invalid ctx assigned");
exit :
  return ctx->rc;
}

void
sen_ctx_concat_func(sen_ctx *ctx, int flags, void *dummy)
{
  if (flags & SEN_CTX_MORE) {
    unsigned int size = SEN_RBUF_VSIZE(&ctx->outbuf);
    sen_rbuf_write(&ctx->subbuf, (char *) &size, sizeof(unsigned int));
  }
}

void
sen_ctx_stream_out_func(sen_ctx *ctx, int flags, void *stream)
{
  sen_rbuf *buf = &ctx->outbuf;
  uint32_t size = SEN_RBUF_VSIZE(buf);
  if (size) {
    fwrite(buf->head, 1, size, (FILE *)stream);
    fputc('\n', (FILE *)stream);
    fflush((FILE *)stream);
    SEN_RBUF_REWIND(buf);
  }
}

void
sen_ctx_recv_handler_set(sen_ctx *ctx, void (*func)(sen_ctx *, int, void *), void *func_arg)
{
  ctx->output = func;
  ctx->data.ptr = func_arg;
}

sen_rc
sen_ctx_info_get(sen_ctx *ctx, sen_ctx_info *info)
{
  if (!ctx) { return sen_invalid_argument; }
  if (ctx->com) {
    info->fd = ctx->com->com.fd;
    info->com_status = ctx->com_status;
    info->com_info = ctx->com_info;
    info->outbuf = &ctx->com->msg;
    info->stat = ctx->stat;
  } else {
    info->fd = -1;
    info->com_status = 0;
    info->com_info = 0;
    info->outbuf = &ctx->outbuf;
    info->stat = ctx->stat;
  }
  return sen_success;
}


sen_obj *
sen_get(const char *key)
{
  sen_obj *obj;
  if (!sen_set_get(sen_gctx.symbols, key, (void **) &obj)) { return F; }
  if (!obj->flags) {
    obj->flags |= SEN_OBJ_SYMBOL;
    obj->type = sen_ql_void;
  }
  return obj;
}

sen_obj *
sen_at(const char *key)
{
  sen_obj *obj;
  if (!sen_set_at(sen_gctx.symbols, key, (void **) &obj)) { return F; }
  return obj;
}

sen_rc
sen_del(const char *key)
{
  sen_obj *obj;
  sen_set_eh *eh;
  if (!(eh = sen_set_get(sen_gctx.symbols, key, (void **) &obj))) {
    return sen_invalid_argument;
  }
  /* todo : do something */
  return sen_set_del(sen_gctx.symbols, eh);
}

/**** memory allocation ****/

void *
sen_malloc(sen_ctx *ctx, size_t size, const char* file, int line)
{
  void *res = malloc(size);
  if (res) {
    alloc_count++;
  } else {
    sen_index_expire();
    if (!(res = malloc(size))) {
      MERR("malloc fail (%d)=%p (%s:%d) <%d>", size, res, file, line, alloc_count);
    }
  }
  return res;
}

void *
sen_calloc(sen_ctx *ctx, size_t size, const char* file, int line)
{
  void *res = calloc(size, 1);
  if (res) {
    alloc_count++;
  } else {
    sen_index_expire();
    if (!(res = calloc(size, 1))) {
      MERR("calloc fail (%d)=%p (%s:%d) <%d>", size, res, file, line, alloc_count);
    }
  }
  return res;
}

void
sen_free(sen_ctx *ctx, void *ptr, const char* file, int line)
{
  free(ptr);
  if (ptr) {
    alloc_count--;
  } else {
    SEN_LOG(sen_log_alert, "free fail (%p) (%s:%d) <%d>", ptr, file, line, alloc_count);
  }
}

void *
sen_realloc(sen_ctx *ctx, void *ptr, size_t size, const char* file, int line)
{
  void *res;
  if (!size) {
    alloc_count--;
#if defined __FreeBSD__
    free(ptr);
    return NULL;
#endif /* __FreeBSD__ */
  }
  res = realloc(ptr, size);
  if (!ptr && res) { alloc_count++; }
  if (size && !res) {
    sen_index_expire();
    if (!(res = realloc(ptr, size))) {
      MERR("realloc fail (%p,%zu)=%p (%s:%d) <%d>", ptr, size, res, file, line, alloc_count);
    }
  }
  if (!size && res) {
    SEN_LOG(sen_log_alert, "realloc(%p,%zu)=%p (%s:%d) <%d>", ptr, size, res, file, line, alloc_count);
    // sen_free(ctx, res, file, line);
  }
  return res;
}

char *
sen_strdup(sen_ctx *ctx, const char *s, const char* file, int line)
{
  char *res = strdup(s);
  if (res) {
    alloc_count++;
  } else  {
    sen_index_expire();
    if (!(res = strdup(s))) {
      MERR("strdup(%p)=%p (%s:%d) <%d>", s, res, file, line, alloc_count);
    }
  }
  return res;
}

#ifdef USE_FAIL_MALLOC
int
fail_malloc_check(size_t size, const char *file, int line, const char *func)
{
  if ((sen_fmalloc_file && strcmp(file, sen_fmalloc_file)) ||
      (sen_fmalloc_line && line != sen_fmalloc_line) ||
      (sen_fmalloc_func && strcmp(func, sen_fmalloc_func))) {
    return 1;
  }
  if (sen_fmalloc_prob * RAND_MAX >= rand()) {
    return 0;
  }
  return 1;
}

void *
sen_fail_malloc(sen_ctx *ctx, size_t size, const char* file, int line, const char *func)
{
  if (fail_malloc_check(size, file, line, func)) {
    return sen_malloc(ctx, size, file, line);
  } else {
    sen_index_expire();
    MERR("fail_malloc (%d) (%s:%d@%s) <%d>", size, file, line, func, alloc_count);
    return NULL;
  }
}

void *
sen_fail_calloc(sen_ctx *ctx, size_t size, const char* file, int line, const char *func)
{
  if (fail_malloc_check(size, file, line, func)) {
    return sen_calloc(ctx, size, file, line);
  } else {
    sen_index_expire();
    MERR("fail_calloc (%d) (%s:%d@%s) <%d>", size, file, line, func, alloc_count);
    return NULL;
  }
}

void *
sen_fail_realloc(sen_ctx *ctx, void *ptr, size_t size, const char* file, int line,
                 const char *func)
{
  if (fail_malloc_check(size, file, line, func)) {
    return sen_realloc(ctx, ptr, size, file, line);
  } else {
    sen_index_expire();
    MERR("fail_realloc (%p,%zu) (%s:%d@%s) <%d>", ptr, size
                                                   , file, line, func, alloc_count);
    return NULL;
  }
}

char *
sen_fail_strdup(sen_ctx *ctx, const char *s, const char* file, int line, const char *func)
{
  if (fail_malloc_check(strlen(s), file, line, func)) {
    return sen_strdup(ctx, s, file, line);
  } else {
    sen_index_expire();
    MERR("fail_strdup(%p) (%s:%d@%s) <%d>", s, file, line, func, alloc_count);
    return NULL;
  }
}
#endif /* USE_FAIL_MALLOC */

sen_obj *
sen_obj_new(sen_ctx *ctx)
{
  sen_obj *o;
  do {
    if (!sen_set_get(ctx->objects, &ctx->seqno, (void **) &o)) {
      MERR("sen_set_get failed");
      return NULL;
    }
    // SEN_SET_INT_ADD(ctx->objects, &ctx->seqno, o);
    ctx->seqno++;
  } while (o->type);
  o->flags = 0;
  o->nrefs = 0;
  return o;
}

sen_obj *
sen_obj_cons(sen_ctx *ctx, sen_obj *a, sen_obj *b)
{
  sen_obj *o;
  SEN_OBJ_NEW(ctx, o);
  o->type = sen_ql_list;
  o->flags = SEN_OBJ_REFERER;
  o->u.l.car = a;
  o->u.l.cdr = b;
  return o;
}

sen_obj *
sen_obj_alloc(sen_ctx *ctx, uint32_t size)
{
  void *value = SEN_MALLOC(size + 1);
  if (value) {
    sen_obj *o = sen_obj_new(ctx);
    if (!ERRP(ctx, SEN_ERROR)) {
      o->flags = SEN_OBJ_ALLOCATED;
      o->type = sen_ql_bulk;
      o->u.b.size = size;
      o->u.b.value = value;
      return o;
    }
    SEN_FREE(value);
  } else {
    MERR("malloc(%d) failed", size + 1);
  }
  return NULL;
}

char *
sen_obj_copy_bulk_value(sen_ctx *ctx, sen_obj *o)
{
  if (o->type != sen_ql_bulk) { return NULL; }
  if (o->flags & SEN_OBJ_ALLOCATED) {
    o->flags &= ~SEN_OBJ_ALLOCATED;
    return o->u.b.value;
  } else {
    char *v = SEN_MALLOC(o->u.b.size + 1);
    if (!v) {
      MERR("malloc(%d) failed", o->u.b.size + 1);
      return NULL;
    }
    memcpy(v, o->u.b.value, o->u.b.size);
    v[o->u.b.size] = '\0';
    return v;
  }
}

void
sen_obj_clear(sen_ctx *ctx, sen_obj *o)
{
  if (o->flags & SEN_OBJ_ALLOCATED) {
    switch (o->type) {
    case sen_ql_snip :
      if (o->u.p.value) { sen_snip_close(o->u.p.value); }
      break;
    case sen_ql_records :
      if (o->u.p.value) { sen_records_close(o->u.p.value); }
      break;
    case sen_ql_bulk :
      if (o->u.b.value) {
        if (o->flags & SEN_OBJ_FROMJA) {
          sen_ja_unref(ctx->db->values, 0, o->u.b.value, o->u.b.size);
        } else {
          SEN_FREE(o->u.b.value);
        }
      }
      break;
    case sen_ql_query :
      if (o->u.p.value) { sen_query_close(o->u.p.value); }
      break;
    default :
      break;
    }
  }
  o->flags = 0;
}

/* don't handle error inside logger functions */

void
sen_ctx_log(sen_ctx *ctx, char *fmt, ...)
{
  va_list argp;
  va_start(argp, fmt);
  vsnprintf(ctx->errbuf, SEN_CTX_MSGSIZE - 1, fmt, argp);
  va_end(argp);
  ctx->errbuf[SEN_CTX_MSGSIZE - 1] = '\0';
}

static FILE *default_logger_fp = NULL;

static void
default_logger_func(int level, const char *time, const char *title,
                    const char *msg, const char *location, void *func_arg)
{
  const char slev[] = " EACewnid-";
  if (!default_logger_fp) {
    // mutex_lock
    default_logger_fp = fopen(SENNA_LOG_PATH, "a");
    // mutex_unlock
  }
  if (default_logger_fp) {
    fprintf(default_logger_fp, "%s|%c|%s %s %s\n",
            time, *(slev + level), title, msg, location);
    fflush(default_logger_fp);
  }
}

void
sen_logger_fin(void)
{
  if (default_logger_fp) {
    fclose(default_logger_fp);
    default_logger_fp = NULL;
  }
}

static sen_logger_info default_logger = {
  SEN_LOG_DEFAULT_LEVEL,
  SEN_LOG_TIME|SEN_LOG_MESSAGE,
  default_logger_func
};

static const sen_logger_info *sen_logger = &default_logger;

sen_rc
sen_logger_info_set(const sen_logger_info *info)
{
  if (info) {
    sen_logger = info;
  } else {
    sen_logger = &default_logger;
  }
  return sen_success;
}

int
sen_logger_pass(sen_log_level level)
{
  return level <= sen_logger->max_level;
}

#define TBUFSIZE SEN_TIMEVAL_STR_SIZE
#define MBUFSIZE 0x1000
#define LBUFSIZE 0x400

void
sen_logger_put(sen_log_level level,
               const char *file, int line, const char *func, char *fmt, ...)
{
  if (level <= sen_logger->max_level) {
    char tbuf[TBUFSIZE];
    char mbuf[MBUFSIZE];
    char lbuf[LBUFSIZE];
    if (sen_logger->flags & SEN_LOG_TIME) {
      sen_timeval tv;
      if (sen_timeval_now(&tv) || sen_timeval2str(&tv, tbuf)) { tbuf[0] = '\0'; }
    } else {
      tbuf[0] = '\0';
    }
    if (sen_logger->flags & SEN_LOG_MESSAGE) {
      va_list argp;
      va_start(argp, fmt);
      vsnprintf(mbuf, MBUFSIZE - 1, fmt, argp);
      va_end(argp);
      mbuf[MBUFSIZE - 1] = '\0';
    } else {
      mbuf[0] = '\0';
    }
    if (sen_logger->flags & SEN_LOG_LOCATION) {
      snprintf(lbuf, LBUFSIZE - 1, "%04x %s:%d %s()",
               getpid(), file, line, func);
      lbuf[LBUFSIZE - 1] = '\0';
    } else {
      lbuf[0] = '\0';
    }
    if (sen_logger->func) {
      sen_logger->func(level, tbuf, "", mbuf, lbuf, sen_logger->func_arg);
    } else {
      default_logger_func(level, tbuf, "", mbuf, lbuf, sen_logger->func_arg);
    }
  }
}

void
sen_assert(int cond, const char* file, int line, const char* func)
{
  if (!cond) {
    SEN_LOG(sen_log_warning, "ASSERT fail on %s %s:%d", func, file, line);
  }
}
