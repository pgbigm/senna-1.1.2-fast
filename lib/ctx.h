/* Copyright(C) 2004-2007 Brazil

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
#ifndef SEN_CTX_H
#define SEN_CTX_H

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif /* HAVE_ERRNO_H */

#ifndef SEN_STORE_H
#include "store.h"
#endif /* SEN_STORE_H */

#ifndef SEN_COM_H
#include "com.h"
#endif /* SEN_COM_H */

#ifdef __cplusplus
extern "C" {
#endif

/**** error handling ****/

#define SEN_OP_T0LVL 0
#define SEN_OP_ERR0  1

enum {
  SEN_EMERG = sen_log_emerg,
  SEN_ALERT = sen_log_alert,
  SEN_CRIT = sen_log_crit,
  SEN_ERROR = sen_log_error,
  SEN_WARN = sen_log_warning,
  SEN_OK = sen_log_notice
};

#define ERRCLR(ctx) do {\
  if (ctx) {\
    ((sen_ctx *)ctx)->errlvl = SEN_OK;\
    ((sen_ctx *)ctx)->rc = sen_success;\
  }\
  sen_gctx.errlvl = SEN_OK;\
  sen_gctx.rc = sen_success;\
} while (0)

#define ERRSET(ctx,lvl,r,...) do {\
  ((sen_ctx *)ctx)->errlvl = (lvl);\
  ((sen_ctx *)ctx)->rc = (r);\
  ((sen_ctx *)ctx)->errfile = __FILE__;\
  ((sen_ctx *)ctx)->errline = __LINE__;\
  ((sen_ctx *)ctx)->errfunc = __FUNCTION__;\
  ((sen_ctx *)ctx)->cur = ((sen_ctx *)ctx)->str_end;\
  ((sen_ctx *)ctx)->op = SEN_OP_ERR0;\
  SEN_LOG(lvl, __VA_ARGS__);\
  sen_ctx_log((sen_ctx *)ctx, __VA_ARGS__);\
} while (0)

#define ERRP(ctx,lvl) (((ctx) && ((sen_ctx *)(ctx))->errlvl <= (lvl)) || (sen_gctx.errlvl <= (lvl)))

#define QLERR(...) do {\
  ERRSET(ctx, SEN_WARN, sen_invalid_argument, __VA_ARGS__);\
  return F;\
} while (0)

#define QLASSERT(expr) do {\
  if (!(expr)) { QLERR("syntax error"); }\
} while (0)

#define ERR(rc,...) ERRSET(ctx, SEN_ERROR, (rc),  __VA_ARGS__)
#define MERR(...) ERRSET(ctx, SEN_ALERT, sen_memory_exhausted,  __VA_ARGS__)
#define SERR(str) ERR(sen_other_error, "syscall error '%s' (%s)", str, strerror(errno))

#define GERR(rc,...) ERRSET(&sen_gctx, SEN_ERROR, (rc),  __VA_ARGS__)
#define GMERR(...) ERRSET(&sen_gctx, SEN_ALERT, sen_memory_exhausted,  __VA_ARGS__)
#define GSERR(str) GERR(sen_other_error, "syscall error '%s' (%s)", str, strerror(errno))

#ifdef USE_FAIL_MALLOC
#define SEN_MALLOC(s) sen_fail_malloc(ctx,s,__FILE__,__LINE__,__FUNCTION__)
#define SEN_CALLOC(s) sen_fail_calloc(ctx,s,__FILE__,__LINE__,__FUNCTION__)
#define SEN_REALLOC(p,s) sen_fail_realloc(ctx,p,s,__FILE__,__LINE__,__FUNCTION__)
#define SEN_STRDUP(s) sen_fail_strdup(ctx,s,__FILE__,__LINE__,__FUNCTION__)
#define SEN_GMALLOC(s) sen_fail_malloc(&sen_gctx,s,__FILE__,__LINE__,__FUNCTION__)
#define SEN_GCALLOC(s) sen_fail_calloc(&sen_gctx,s,__FILE__,__LINE__,__FUNCTION__)
#define SEN_GREALLOC(p,s) sen_fail_realloc(&sen_gctx,p,s,__FILE__,__LINE__,__FUNCTION__)
#define SEN_GSTRDUP(s) sen_fail_strdup(&sen_gctx,s,__FILE__,__LINE__,__FUNTION__)
#else /* USE_FAIL_MALLOC */
#define SEN_MALLOC(s) sen_malloc(ctx,s,__FILE__,__LINE__)
#define SEN_CALLOC(s) sen_calloc(ctx,s,__FILE__,__LINE__)
#define SEN_REALLOC(p,s) sen_realloc(ctx,p,s,__FILE__,__LINE__)
#define SEN_STRDUP(s) sen_strdup(ctx,s,__FILE__,__LINE__)
#define SEN_GMALLOC(s) sen_malloc(&sen_gctx,s,__FILE__,__LINE__)
#define SEN_GCALLOC(s) sen_calloc(&sen_gctx,s,__FILE__,__LINE__)
#define SEN_GREALLOC(p,s) sen_realloc(&sen_gctx,p,s,__FILE__,__LINE__)
#define SEN_GSTRDUP(s) sen_strdup(&sen_gctx,s,__FILE__,__LINE__)
#endif /* USE_FAIL_MALLOC */
#define SEN_FREE(p) sen_free(ctx,p,__FILE__,__LINE__)
#define SEN_MALLOCN(t,n) ((t *)(SEN_MALLOC(sizeof(t) * (n))))
#define SEN_GFREE(p) sen_free(&sen_gctx,p,__FILE__,__LINE__)
#define SEN_GMALLOCN(t,n) ((t *)(SEN_GMALLOC(sizeof(t) * (n))))

#ifdef DEBUG
#define SEN_ASSERT(s) sen_assert((s),__FILE__,__LINE__,__FUNCTION__)
#else
#define SEN_ASSERT(s)
#endif

#ifdef USE_FAIL_MALLOC
int fail_malloc_check(size_t size, const char *file, int line, const char *func);
void *sen_fail_malloc(sen_ctx *ctx, size_t size, const char* file, int line, const char *func);
void *sen_fail_calloc(sen_ctx *ctx, size_t size, const char* file, int line, const char *func);
void *sen_fail_realloc(sen_ctx *ctx, void *ptr, size_t size, const char* file, int line, const char *func);
char *sen_fail_strdup(sen_ctx *ctx, const char *s, const char* file, int line, const char *func);
#endif /* USE_FAIL_MALLOC */
void *sen_malloc(sen_ctx *ctx, size_t size, const char* file, int line);
void *sen_calloc(sen_ctx *ctx, size_t size, const char* file, int line);
void *sen_realloc(sen_ctx *ctx, void *ptr, size_t size, const char* file, int line);
char *sen_strdup(sen_ctx *ctx, const char *s, const char* file, int line);
void sen_free(sen_ctx *ctx, void *ptr, const char* file, int line);

void sen_assert(int cond, const char* file, int line, const char* func);

void sen_index_expire(void);

/**** sen_obj ****/

typedef struct {
  int32_t tv_sec;
  int32_t tv_usec;
} sen_timeval;

typedef struct _sen_obj sen_obj;
typedef struct _sen_ql_co sen_ql_co;
typedef sen_obj *sen_ql_native_func(sen_ctx *, sen_obj *, sen_ql_co *);

struct _sen_obj {
  uint8_t type;
  uint8_t flags;
  uint16_t nrefs;
  sen_id class;
  union {
    struct {
      sen_id self;
      sen_ql_native_func *func;
    } o;
    struct {
      void *value;
      sen_ql_native_func *func;
    } p;
    struct {
      char *value;
      uint32_t size;
    } b;
    struct {
      sen_obj *car;
      sen_obj *cdr;
    } l;
    struct {
      int64_t i;
    } i;
    struct {
      double d;
    } d;
    struct {
      int8_t op;
      int8_t mode;
      int16_t weight;
      int32_t option;
    } op;
    sen_timeval tv;
  } u;
};

struct _sen_ql_co {
  uint16_t mode;
  uint16_t last;
  sen_ql_native_func *func;
  void *data;
};

/**** sen_ctx ****/

#define SEN_CTX_MSGSIZE 128

struct _sen_ctx {
  sen_rc rc;
  uint8_t errlvl;
  const char *errfile;
  int errline;
  const char *errfunc;
  char errbuf[SEN_CTX_MSGSIZE];

  uint32_t ncells;
  uint32_t seqno;
  uint32_t lseqno;
  uint32_t nbinds;
  uint32_t nunbinds;
  uint8_t feed_mode;
  uint8_t stat;

  uint8_t batchmode;
  uint8_t gc_verbose;

  sen_encoding encoding;

  int tok;
  char *cur;
  char *str_end;

  sen_obj **pht;       /* tail of placeholders */

  sen_obj arg;         /* wait_data container (for coroutine) */
  sen_db *db;
  sen_set *objects;    /* object table */
  sen_set *symbols;    /* symbol table */
  sen_obj *phs;        /* list of placeholders */
  sen_ql_co co;        /* coroutine info */

  sen_obj *args;       /* register for arguments of function */
  sen_obj *envir;      /* stack register for current environment */
  sen_obj *code;       /* register for current code */
  sen_obj *dump;       /* stack register for next evaluation */
  sen_obj *value;      /* evaluated value */
  sen_obj *global_env; /* global variables */
  uint8_t op;

  char *inbuf;
  sen_rbuf outbuf;
  sen_rbuf subbuf;
  unsigned int bufcur;

  void (*output)(sen_ctx *, int, void *);

  sen_com_sqtp *com;
  unsigned int com_status;
  unsigned int com_info;

  void *currec;        /* current recinfo (for slotexp) */
  sen_obj curobj;      /* current record container (for slotexp) */

  union {
    void *ptr;
    int fd;
    uint32_t u32;
    uint64_t u64;
  } data;
};

extern sen_ctx sen_gctx;

sen_obj *sen_get(const char *key);
sen_obj *sen_at(const char *key);
sen_rc sen_del(const char *key);

#ifndef SEN_TIMEVAL_STR_SIZE
#define SEN_TIMEVAL_STR_SIZE 0x100
#endif /* SEN_TIMEVAL_STR_SIZE */
#ifndef SEN_TIMEVAL_STR_FORMAT
#define SEN_TIMEVAL_STR_FORMAT "%04d-%02d-%02d %02d:%02d:%02d.%06d"
#endif /* SEN_TIMEVAL_STR_FORMAT */

sen_rc sen_timeval_now(sen_timeval *tv);
sen_rc sen_timeval2str(sen_timeval *tv, char *buf);
sen_rc sen_str2timeval(const char *str, uint32_t str_len, sen_timeval *tv);

void sen_ctx_log(sen_ctx *ctx, char *fmt, ...);

/**** receive handler ****/

void sen_ctx_recv_handler_set(sen_ctx *c, void (*func)(sen_ctx *, int, void *),
                              void *func_arg);

void sen_ctx_concat_func(sen_ctx *ctx, int flags, void *dummy);
void sen_ctx_stream_out_func(sen_ctx *c, int flags, void *stream);

#ifdef __cplusplus
}
#endif

#endif /* SEN_CTX_H */
