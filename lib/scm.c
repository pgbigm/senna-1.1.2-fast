/* Copyright(C) 2006-2007 Brazil

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

/*  Senna Query Language is based on Mini-Scheme, original credits follow  */

/*
 *      ---------- Mini-Scheme Interpreter Version 0.85 ----------
 *
 *                coded by Atsushi Moriwaki (11/5/1989)
 *
 *            E-MAIL :  moriwaki@kurims.kurims.kyoto-u.ac.jp
 *
 *               THIS SOFTWARE IS IN THE PUBLIC DOMAIN
 *               ------------------------------------
 * This software is completely free to copy, modify and/or re-distribute.
 * But I would appreciate it if you left my name on the code as the author.
 *
 */
/*--
 *
 *  This version has been modified by R.C. Secrist.
 *
 *  Mini-Scheme is now maintained by Akira KIDA.
 *
 *  This is a revised and modified version by Akira KIDA.
 *  current version is 0.85k4 (15 May 1994)
 *
 *  Please send suggestions, bug reports and/or requests to:
 *    <SDI00379@niftyserve.or.jp>
 *--
 */

#include "senna_in.h"
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "ql.h"

#define InitFile "init.scm"

/* global variables */

sen_obj *sen_ql_nil;  /* special cell representing empty cell */
sen_obj *sen_ql_t;    /* special cell representing #t */
sen_obj *sen_ql_f;    /* special cell representing #f */

/* sen query language */

/* todo : update set-car! set-cdr!

inline static void
obj_ref(sen_obj *o)
{
  if (o->nrefs < 0xffff) { o->nrefs++; }
  if (PAIRP(o)) { // todo : check cycle
    if (CAR(o) != NIL) { obj_ref(CAR(o)); }
    if (CDR(o) != NIL) { obj_ref(CDR(o)); }
  }
}

inline static void
obj_unref(sen_obj *o)
{
  if (!o->nrefs) {
    SEN_LOG(sen_log_error, "o->nrefs corrupt");
    return;
  }
  if (o->nrefs < 0xffff) { o->nrefs--; }
  if (PAIRP(o)) { // todo : check cycle
    if (CAR(o) != NIL) { obj_unref(CAR(o)); }
    if (CDR(o) != NIL) { obj_unref(CDR(o)); }
  }
}

inline static void
rplaca(sen_ctx *ctx, sen_obj *a, sen_obj *b)
{
  if (a->nrefs) {
    ctx->nbinds++;
    if (a->u.l.car) {
      ctx->nunbinds++;
      obj_unref(a->u.l.car);
    }
    if (b) { obj_ref(b); }
  }
  a->u.l.car = b;
}

inline static void
rplacd(sen_ctx *ctx, sen_obj *a, sen_obj *b)
{
  if (a->nrefs) {
    ctx->nbinds++;
    if (a->u.l.cdr) {
      ctx->nunbinds++;
      obj_unref(a->u.l.cdr);
    }
    if (b) { obj_ref(b); }
  }
  a->u.l.cdr = b;
}

*/

sen_rc
sen_obj2int(sen_ctx *ctx, sen_obj *o)
{
  sen_rc rc = sen_invalid_argument;
  if (o) {
    switch (o->type) {
    case sen_ql_bulk :
      if (o->u.b.size) {
        const char *end = o->u.b.value + o->u.b.size, *rest;
        int64_t i = sen_atoll(o->u.b.value, end, &rest);
        if (rest == end) {
          sen_obj_clear(ctx, o);
          SETINT(o, i);
          rc = sen_success;
        }
      }
      break;
    case sen_ql_int :
      rc = sen_success;
      break;
    default :
      break;
    }
  }
  return rc;
}

/* get new symbol */
sen_obj *
sen_ql_mk_symbol(sen_ctx *ctx, const char *name)
{
  sen_obj *x;
  if (!sen_set_get(ctx->symbols, name, (void **) &x)) { return F; }
  if (!x->flags) {
    x->flags |= SEN_OBJ_SYMBOL;
    x->type = sen_ql_void;
  }
  if (x->type == sen_ql_void && ctx->db) {
    sen_db_store *slot = sen_db_store_open(ctx->db, SYMNAME(x));
    if (slot) { sen_ql_bind_symbol(slot, x); }
  }
  return x;
}

sen_obj *
sen_ql_at(sen_ctx *ctx, const char *key)
{
  sen_obj *o;
  if (!sen_set_at(ctx->symbols, key, (void **) &o)) {
    return NULL;
  }
  return o;
}

void
sen_ql_def_native_func(sen_ctx *ctx, const char *name, sen_ql_native_func *func)
{
  sen_obj *o = INTERN(name);
  if (o != F) {
    o->type = sen_ql_void;
    o->flags |= SEN_OBJ_NATIVE;
    o->u.o.func = func;
  }
}

inline static void
sen_ctx_igc(sen_ctx *ctx)
{
  uint32_t i;
  sen_obj *o;
  sen_set_eh *ep;
  for (i = ctx->lseqno; i != ctx->seqno; i++) {
    if ((ep = sen_set_at(ctx->objects, &i, (void **) &o))) {
      if (ctx->nbinds &&
          (o->nrefs ||
           (BULKP(o) && (o->flags & SEN_OBJ_ALLOCATED)))) { continue; }
      sen_obj_clear(ctx, o);
      sen_set_del(ctx->objects, ep);
    }
  }
  ctx->lseqno = ctx->seqno;
  ctx->nbinds = 0;
}

#define MARKP(p)        ((p)->flags & SEN_OBJ_MARKED)
#define REFERERP(p)     ((p)->flags & SEN_OBJ_REFERER)
#define SETREFERER(p)   ((p)->flags |= SEN_OBJ_REFERER)
#define UNSETREFERER(p) ((p)->flags &= ~SEN_OBJ_REFERER)

/*--
 *  We use algorithm E (Knuth, The Art of Computer Programming Vol.1,
 *  sec.3.5) for marking.
 */
inline static void
obj_mark(sen_ctx *ctx, sen_obj *o)
{
  sen_obj *t, *q, *p;
  t = NULL;
  p = o;
  // if (MARKP(o)) { return; }
E2:
  p->flags |= SEN_OBJ_MARKED;
  // if (!o->nrefs) { SEN_LOG(sen_log_error, "obj->nrefs corrupt"); }
  if (BULKP(o) && !(o->flags & SEN_OBJ_ALLOCATED)) {
    char *b = SEN_MALLOC(o->u.b.size + 1);
    if (b) {
      memcpy(b, o->u.b.value, o->u.b.size);
      b[o->u.b.size] = '\0';
      o->u.b.value = b;
      o->flags |= SEN_OBJ_ALLOCATED;
    }
  }
  if (!REFERERP(p)) { goto E6; }
  q = CAR(p);
  if (q && !MARKP(q)) {
    UNSETREFERER(p);
    CAR(p) = t;
    t = p;
    p = q;
    goto E2;
  }
E5:
  q = CDR(p);
  if (q && !MARKP(q)) {
    CDR(p) = t;
    t = p;
    p = q;
    goto E2;
  }
E6:
  if (!t) { return; }
  q = t;
  if (!REFERERP(q)) {
    SETREFERER(q);
    t = CAR(q);
    CAR(q) = p;
    p = q;
    goto E5;
  } else {
    t = CDR(q);
    CDR(q) = p;
    p = q;
    goto E6;
  }
}

inline static sen_rc
sen_ctx_mgc(sen_ctx *ctx)
{
  sen_set_cursor *sc;
  /*
  if (!(sc = sen_set_cursor_open(ctx->symbols))) { return sen_memory_exhausted; }
  {
    sen_obj *o;
    while (sen_set_cursor_next(sc, NULL, (void **) &o)) { obj_mark(o); }
    sen_set_cursor_close(sc);
  }
  */
  obj_mark(ctx, ctx->global_env);

  /* mark current registers */
  obj_mark(ctx, ctx->args);
  obj_mark(ctx, ctx->envir);
  obj_mark(ctx, ctx->code);
  obj_mark(ctx, ctx->dump);
  obj_mark(ctx, ctx->value);
  obj_mark(ctx, ctx->phs);

  if (!(sc = sen_set_cursor_open(ctx->objects))) { return sen_memory_exhausted; }
  {
    sen_obj *o;
    sen_set_eh *ep;
    while ((ep = sen_set_cursor_next(sc, NULL, (void **) &o))) {
      if (o->flags & SEN_OBJ_MARKED) {
        o->flags &= ~SEN_OBJ_MARKED;
      } else {
        sen_obj_clear(ctx, o);
        sen_set_del(ctx->objects, ep);
      }
    }
  }
  sen_set_cursor_close(sc);
  ctx->lseqno = ctx->seqno;
  ctx->nbinds = 0;
  ctx->nunbinds = 0;
  return sen_success;
}

inline static void Eval_Cycle(sen_ctx *ctx);

/* ========== Evaluation Cycle ========== */

/* operator code */

enum {
  OP_T0LVL = SEN_OP_T0LVL,
  OP_ERR0 = SEN_OP_ERR0,
  OP_LOAD,
  OP_T1LVL,
  OP_READ,
  OP_VALUEPRINT,
  OP_EVAL,
  OP_E0ARGS,
  OP_E1ARGS,
  OP_APPLY,
  OP_DOMACRO,
  OP_LAMBDA,
  OP_QUOTE,
  OP_DEF0,
  OP_DEF1,
  OP_BEGIN,
  OP_IF0,
  OP_IF1,
  OP_SET0,
  OP_SET1,
  OP_LET0,
  OP_LET1,
  OP_LET2,
  OP_LET0AST,
  OP_LET1AST,
  OP_LET2AST,
  OP_LET0REC,
  OP_LET1REC,
  OP_LET2REC,
  OP_COND0,
  OP_COND1,
  OP_DELAY,
  OP_AND0,
  OP_AND1,
  OP_OR0,
  OP_OR1,
  OP_C0STREAM,
  OP_C1STREAM,
  OP_0MACRO,
  OP_1MACRO,
  OP_CASE0,
  OP_CASE1,
  OP_CASE2,
  OP_PEVAL,
  OP_PAPPLY,
  OP_CONTINUATION,
  OP_SETCAR,
  OP_SETCDR,
  OP_FORCE,
  OP_ERR1,
  OP_PUT,
  OP_GET,
  OP_QUIT,
  OP_SDOWN,
  OP_RDSEXPR,
  OP_RDLIST,
  OP_RDDOT,
  OP_RDQUOTE,
  OP_RDQQUOTE,
  OP_RDUNQUOTE,
  OP_RDUQTSP,
  OP_NATIVE,
  OP_QQUOTE0,
  OP_QQUOTE1,
  OP_QQUOTE2
};

sen_obj *
sen_ql_feed(sen_ctx *ctx, char *str, uint32_t str_size, int mode)
{
  if (SEN_QL_WAITINGP(ctx)) {
    SEN_RBUF_REWIND(&ctx->outbuf);
    SEN_RBUF_REWIND(&ctx->subbuf);
    ctx->bufcur = 0;
  }
  for (;;) {
    switch (ctx->stat) {
    case SEN_QL_TOPLEVEL :
      ctx->co.mode &= ~SEN_CTX_HEAD;
      Eval_Cycle(ctx);
      break;
    case SEN_QL_WAIT_EXPR :
      ctx->co.mode = mode;
      ctx->cur = str;
      ctx->str_end = str + str_size;
      Eval_Cycle(ctx);
      break;
    case SEN_QL_WAIT_ARG :
      ctx->co.mode = mode;
      if ((mode & SEN_CTX_HEAD)) {
        ctx->cur = str;
        ctx->str_end = str + str_size;
      } else {
        char *buf;
        sen_obj *ph = CAR(ctx->phs);
        if (!(buf = SEN_MALLOC(str_size + 1))) {
          return NIL;
        }
        memcpy(buf, str, str_size);
        buf[str_size] = '\0';
        ph->flags |= SEN_OBJ_ALLOCATED;
        ph->u.b.value = buf;
        ph->u.b.size = str_size;
        ctx->phs = CDR(ctx->phs);
      }
      if ((ctx->phs == NIL) || (mode & (SEN_CTX_HEAD|SEN_CTX_TAIL))) {
        ctx->stat = SEN_QL_EVAL;
      }
      break;
    case SEN_QL_EVAL :
      Eval_Cycle(ctx);
      break;
    case SEN_QL_WAIT_DATA :
      ctx->co.mode = mode;
      if ((mode & SEN_CTX_HEAD)) {
        ctx->args = NIL;
        ctx->cur = str;
        ctx->str_end = str + str_size;
      } else {
        ctx->arg.u.b.value = str;
        ctx->arg.u.b.size = str_size;
        ctx->arg.type = sen_ql_bulk;
        ctx->args = &ctx->arg;
      }
      /* fall through */
    case SEN_QL_NATIVE :
      SEN_ASSERT(ctx->co.func);
      ctx->value = ctx->co.func(ctx, ctx->args, &ctx->co);
      if (ERRP(ctx, SEN_ERROR)) { ctx->stat = SEN_QL_QUITTING; return F; }
      ERRCLR(ctx);
      if (ctx->co.last && !(ctx->co.mode & (SEN_CTX_HEAD|SEN_CTX_TAIL))) {
        ctx->stat = SEN_QL_WAIT_DATA;
      } else {
        ctx->co.mode = 0;
        Eval_Cycle(ctx);
      }
      break;
    case SEN_QL_QUITTING:
      return NIL;
    }
    if (ERRP(ctx, SEN_ERROR)) { ctx->stat = SEN_QL_QUITTING; return F; }
    if (SEN_QL_WAITINGP(ctx)) { /* waiting input data */
      if (ctx->inbuf) {
        SEN_FREE(ctx->inbuf);
        ctx->inbuf = NULL;
      }
      break;
    }
    if ((ctx->stat & 0x40) && SEN_QL_GET_MODE(ctx) == sen_ql_step) {
      break;
    }
  }
  return NIL;
}

/**** sexp parser ****/

typedef sen_obj cell;

inline static void
skipline(sen_ctx *ctx)
{
  while (ctx->cur < ctx->str_end) {
    if (*ctx->cur++ == '\n') { break; }
  }
}

/*************** scheme interpreter ***************/

# define BACKQUOTE '`'

#include <stdio.h>
#include <ctype.h>

/* macros for cell operations */
#define HASPROP(p)       ((p)->flags & SEN_OBJ_SYMBOL)
#define SYMPROP(p)       CDR(p)
#define SYNTAXP(p)       ((p)->type == sen_ql_syntax)
#define PROCP(p)         ((p)->type == sen_ql_proc)
#define SYNTAXNAME(p)    SYMNAME(p)
#define SYNTAXNUM(p)     ((p)->class)
#define PROCNUM(p)       IVALUE(p)
#define CLOSUREP(p)      ((p)->type == sen_ql_closure)
#define MACROP(p)        ((p)->flags & SEN_OBJ_MACRO)
#define CLOSURE_CODE(p)  CAR(p)
#define CLOSURE_ENV(p)   CDR(p)
#define CONTINUATIONP(p) ((p)->type == sen_ql_continuation)
#define CONT_DUMP(p)     CDR(p)
#define PROMISEP(p)      ((p)->flags & SEN_OBJ_PROMISE)
#define SETPROMISE(p)    (p)->flags |= SEN_OBJ_PROMISE
#define LAMBDA           (INTERN("lambda"))
#define QUOTE            (INTERN("quote"))
#define QQUOTE           (INTERN("quasiquote"))
#define UNQUOTE          (INTERN("unquote"))
#define UNQUOTESP        (INTERN("unquote-splicing"))

/* get new cell.  parameter a, b is marked by gc. */
#define GET_CELL(ctx,a,b,o) SEN_OBJ_NEW(ctx, o)

/* get number atom */
inline static cell *
mk_number(sen_ctx *ctx, int64_t num)
{
  cell *x;
  SEN_OBJ_NEW(ctx, x);
  SETINT(x, num);
  return x;
}

/* get new string */
sen_obj *
sen_ql_mk_string(sen_ctx *ctx, const char *str, unsigned int len)
{
  cell *x = sen_obj_alloc(ctx, len);
  if (!x) { return F; }
  memcpy(x->u.b.value, str, len);
  x->u.b.value[len] = '\0';
  return x;
}

inline static cell *
mk_const_string(sen_ctx *ctx, const char *str)
{
  cell *x;
  SEN_OBJ_NEW(ctx, x);
  x->flags = 0;
  x->type = sen_ql_bulk;
  x->u.b.value = (char *)str;
  x->u.b.size = strlen(str);
  return x;
}

inline static cell *
sen_ql_mk_symbol2(sen_ctx *ctx, const char *q, unsigned int len, int kwdp)
{
  char buf[SEN_SYM_MAX_KEY_SIZE], *p = buf;
  if (len + 1 >= SEN_SYM_MAX_KEY_SIZE) { QLERR("too long symbol"); }
  if (kwdp) { *p++ = ':'; }
  memcpy(p, q, len);
  p[len] = '\0';
  return INTERN(buf);
}

inline static cell *
str2num(sen_ctx *ctx, char *str, unsigned int len)
{
  const char *cur, *str_end = str + len;
  int64_t i = sen_atoll(str, str_end, &cur);
  if (cur == str_end) { return mk_number(ctx, i); }
  if (cur != str) { /* todo : support #i notation */
    char *end, buf0[128], *buf = len < 128 ? buf0 : SEN_MALLOC(len + 1);
    if (buf) {
      double d;
      memcpy(buf, str, len);
      buf[len] = '\0';
      errno = 0;
      d = strtod(buf, &end);
      if (!(len < 128)) { SEN_FREE(buf); }
      if (!errno && buf + len == end) {
        cell *x;
        SEN_OBJ_NEW(ctx, x);
        SETFLOAT(x, d);
        return x;
      }
    }
  }
  return NIL;
}

/* make symbol or number atom from string */
inline static cell *
mk_atom(sen_ctx *ctx, char *str, unsigned int len, cell *v)
{
  cell **vp = &v, *p;
  const char *cur, *last, *str_end = str + len;
  if ((p = str2num(ctx, str, len)) != NIL) { return p; }
  for (last = cur = str; cur < str_end; cur += len) {
    if (!(len = sen_str_charlen_nonnull(cur, str_end, ctx->encoding))) { break; }
    if (*cur == '.') {
      if (last < cur) { *vp = sen_ql_mk_symbol2(ctx, last, cur - last, str != last); }
      v = CONS(v, CONS(NIL, NIL));
      vp = &CADR(v);
      last = cur + 1;
    }
  }
  if (last < cur) { *vp = sen_ql_mk_symbol2(ctx, last, cur - last, str != last); }
  return v;
}

/* make constant */
inline static cell *
mk_const(sen_ctx *ctx, char *name, unsigned int len)
{
  int64_t x;
  char    tmp[256];
  char    tmp2[256];
  /* todo : rewirte with sen_str_* functions */
  if (len == 1) {
    if (*name == 't') {
      return T;
    } else if (*name == 'f') {
      return F;
    }
  } else if (len > 1) {
    if (*name == 'p' && name[1] == '<' && name[12] == '>') {/* #p (sen_ql_object) */
      sen_id cls = sen_str_btoi(name + 2);
      if (cls) {
        sen_id self = sen_str_btoi(name + 7);
        if (self) {
          cell * v = sen_ql_mk_obj(ctx, cls, self);
          if (len > 13 && name[13] == '.') {
            return mk_atom(ctx, name + 13, len - 13, v);
          } else {
            return v;
          }
        }
      }
    } else if (*name == ':' && name[1] == '<') {/* #: (sen_ql_time) */
      cell *x;
      sen_timeval tv;
      const char *cur;
      tv.tv_sec = sen_atoi(name + 2, name + len, &cur);
      if (cur >= name + len || *cur != '.') {
        QLERR("illegal time format '%s'", name);
      }
      tv.tv_usec = sen_atoi(cur + 1, name + len, &cur);
      if (cur >= name + len || *cur != '>') {
        QLERR("illegal time format '%s'", name);
      }
      SEN_OBJ_NEW(ctx, x);
      SETTIME(x, &tv);
      return x;
    } else if (*name == 'o') {/* #o (octal) */
      len = (len > 255) ? 255 : len - 1;
      memcpy(tmp2, name + 1, len);
      tmp2[len] = '\0';
      sprintf(tmp, "0%s", tmp2);
      sscanf(tmp, "%Lo", &x);
      return mk_number(ctx, x);
    } else if (*name == 'd') {  /* #d (decimal) */
      sscanf(&name[1], "%Ld", &x);
      return mk_number(ctx, x);
    } else if (*name == 'x') {  /* #x (hex) */
      len = (len > 255) ? 255 : len - 1;
      memcpy(tmp2, name + 1, len);
      tmp2[len] = '\0';
      sprintf(tmp, "0x%s", tmp2);
      sscanf(tmp, "%Lx", &x);
      return mk_number(ctx, x);
    }
  }
  return NIL;
}

sen_rc
sen_ctx_load(sen_ctx *ctx, const char *filename)
{
  if (!filename) { filename = InitFile; }
  ctx->args = CONS(mk_const_string(ctx, filename), NIL);
  ctx->stat = SEN_QL_TOPLEVEL;
  ctx->op = OP_LOAD;
  return sen_ql_feed(ctx, "init", 4, 0) == F ? sen_success : sen_internal_error;
}

/* ========== Routines for Reading ========== */

#define TOK_LPAREN  0
#define TOK_RPAREN  1
#define TOK_DOT     2
#define TOK_ATOM    3
#define TOK_QUOTE   4
#define TOK_COMMENT 5
#define TOK_DQUOTE  6
#define TOK_BQUOTE  7
#define TOK_COMMA   8
#define TOK_ATMARK  9
#define TOK_SHARP   10
#define TOK_EOS     11
#define TOK_QUESTION 12

#define lparenp(c) ((c) == '(' || (c) == '[')
#define rparenp(c) ((c) == ')' || (c) == ']')

/* read chacters to delimiter */
inline static char
readstr(sen_ctx *ctx, char **str, unsigned int *size)
{
  char *start, *end;
  for (start = end = ctx->cur;;) {
    unsigned int len;
    /* null check and length check */
    if (!(len = sen_str_charlen_nonnull(end, ctx->str_end, ctx->encoding))) {
      ctx->cur = ctx->str_end;
      break;
    }
    if (sen_isspace(end, ctx->encoding) ||
        *end == ';' || lparenp(*end) || rparenp(*end)) {
      ctx->cur = end;
      break;
    }
    end += len;
  }
  if (start < end || ctx->cur < ctx->str_end) {
    *str = start;
    *size = (unsigned int)(end - start);
    return TOK_ATOM;
  } else {
    return TOK_EOS;
  }
}

/* read string expression "xxx...xxx" */
inline static char
readstrexp(sen_ctx *ctx, char **str, unsigned int *size)
{
  char *start, *src, *dest;
  for (start = src = dest = ctx->cur;;) {
    unsigned int len;
    /* null check and length check */
    if (!(len = sen_str_charlen_nonnull(src, ctx->str_end, ctx->encoding))) {
      ctx->cur = ctx->str_end;
      if (start < dest) {
        *str = start;
        *size = (unsigned int)(dest - start);
        return TOK_ATOM;
      }
      return TOK_EOS;
    }
    if (src[0] == '"' && len == 1) {
      ctx->cur = src + 1;
      *str = start;
      *size = (unsigned int)(dest - start);
      return TOK_ATOM;
    } else if (src[0] == '\\' && src + 1 < ctx->str_end && len == 1) {
      src++;
      *dest++ = *src++;
    } else {
      while (len--) { *dest++ = *src++; }
    }
  }
}

/* get token */
inline static char
token(sen_ctx *ctx)
{
  SKIPSPACE(ctx);
  if (ctx->cur >= ctx->str_end) { return TOK_EOS; }
  switch (*ctx->cur) {
  case '(':
  case '[':
    ctx->cur++;
    return TOK_LPAREN;
  case ')':
  case ']':
    ctx->cur++;
    return TOK_RPAREN;
  case '.':
    ctx->cur++;
    if (ctx->cur == ctx->str_end ||
        sen_isspace(ctx->cur, ctx->encoding) ||
        *ctx->cur == ';' || lparenp(*ctx->cur) || rparenp(*ctx->cur)) {
      return TOK_DOT;
    } else {
      ctx->cur--;
      return TOK_ATOM;
    }
  case '\'':
    ctx->cur++;
    return TOK_QUOTE;
  case ';':
    ctx->cur++;
    return TOK_COMMENT;
  case '"':
    ctx->cur++;
    return TOK_DQUOTE;
  case BACKQUOTE:
    ctx->cur++;
    return TOK_BQUOTE;
  case ',':
    ctx->cur++;
    if (ctx->cur < ctx->str_end && *ctx->cur == '@') {
      ctx->cur++;
      return TOK_ATMARK;
    } else {
      return TOK_COMMA;
    }
  case '#':
    ctx->cur++;
    return TOK_SHARP;
  case '?':
    ctx->cur++;
    return TOK_QUESTION;
  default:
    return TOK_ATOM;
  }
}

/* ========== Routines for Printing ========== */
#define  ok_abbrev(x)  (PAIRP(x) && CDR(x) == NIL)

void
sen_obj_inspect(sen_ctx *ctx, sen_obj *obj, sen_rbuf *buf, int flags)
{
  if (!obj) {
    SEN_RBUF_PUTS(buf, "NULL");
  } else if (obj == NIL) {
    SEN_RBUF_PUTS(buf, "()");
  } else if (obj == T) {
    SEN_RBUF_PUTS(buf, "#t");
  } else if (obj == F) {
    SEN_RBUF_PUTS(buf, "#f");
  } else {
    if (SYMBOLP(obj)) {
      const char *sym = SYMNAME(obj);
      if (sym) {
        if (flags & SEN_OBJ_INSPECT_SYM_AS_STR) {
          sen_rbuf_str_esc(buf, (*sym == ':') ? sym + 1 : sym, -1, ctx->encoding);
        } else {
          SEN_RBUF_PUTS(buf, sym);
        }
        return;
      }
    }
    switch (obj->type) {
    case sen_ql_void :
      SEN_RBUF_PUTS(buf, SYMBOLP(obj) ? SYMNAME(obj) : "#<VOID>");
      break;
    case sen_ql_object :
      if (flags & SEN_OBJ_INSPECT_ESC) {
        SEN_RBUF_PUTS(buf, "#p<");
        sen_rbuf_itob(buf, obj->class);
        sen_rbuf_itob(buf, obj->u.o.self);
        SEN_RBUF_PUTC(buf, '>');
      } else {
        const char *key = _sen_obj_key(ctx, obj);
        SEN_RBUF_PUTS(buf, key ? key : "");
      }
      break;
    case sen_ql_snip :
      SEN_RBUF_PUTS(buf, "#<SNIP>");
      break;
    case sen_ql_records :
      SEN_RBUF_PUTS(buf, "#<RECORDS>");
      break;
    case sen_ql_bulk :
      if (flags & SEN_OBJ_INSPECT_ESC) {
        sen_rbuf_str_esc(buf, obj->u.b.value, obj->u.b.size, ctx->encoding);
      } else {
        sen_rbuf_write(buf, obj->u.b.value, obj->u.b.size);
      }
      break;
    case sen_ql_int :
      sen_rbuf_lltoa(buf, IVALUE(obj));
      break;
    case sen_ql_float :
      sen_rbuf_ftoa(buf, FVALUE(obj));
      break;
    case sen_ql_time :
      SEN_RBUF_PUTS(buf, "#:<");
      sen_rbuf_itoa(buf, obj->u.tv.tv_sec);
      SEN_RBUF_PUTS(buf, ".");
      sen_rbuf_itoa(buf, obj->u.tv.tv_usec);
      SEN_RBUF_PUTC(buf, '>');
      break;
    case sen_ql_query :
      SEN_RBUF_PUTS(buf, "#<QUERY>");
      break;
    case sen_ql_op :
      SEN_RBUF_PUTS(buf, "#<OP>");
      break;
    case sen_ql_syntax :
      SEN_RBUF_PUTS(buf, "#<SYNTAX>");
      break;
    case sen_ql_proc :
      SEN_RBUF_PUTS(buf, "#<PROCEDURE ");
      sen_rbuf_itoa(buf, PROCNUM(obj));
      SEN_RBUF_PUTS(buf, ">");
      break;
    case sen_ql_closure :
      if (MACROP(obj)) {
        SEN_RBUF_PUTS(buf, "#<MACRO>");
      } else {
        SEN_RBUF_PUTS(buf, "#<CLOSURE>");
      }
      break;
    case sen_ql_continuation :
      SEN_RBUF_PUTS(buf, "#<CONTINUATION>");
      break;
    case sen_db_raw_class :
      SEN_RBUF_PUTS(buf, "#<RAW_CLASS>");
      break;
    case sen_db_class :
      SEN_RBUF_PUTS(buf, "#<CLASS>");
      break;
    case sen_db_obj_slot :
      SEN_RBUF_PUTS(buf, "#<OBJ_SLOT>");
      break;
    case sen_db_ra_slot :
      SEN_RBUF_PUTS(buf, "#<RA_SLOT>");
      break;
    case sen_db_ja_slot :
      SEN_RBUF_PUTS(buf, "#<JA_SLOT>");
      break;
    case sen_db_idx_slot :
      SEN_RBUF_PUTS(buf, "#<IDX_SLOT>");
      break;
    case sen_ql_list :
      /* todo : detect loop */
      if (CAR(obj) == QUOTE && ok_abbrev(CDR(obj))) {
        SEN_RBUF_PUTC(buf, '\'');
        sen_obj_inspect(ctx, CADR(obj), buf, flags);
      } else if (CAR(obj) == QQUOTE && ok_abbrev(CDR(obj))) {
        SEN_RBUF_PUTC(buf, '`');
        sen_obj_inspect(ctx, CADR(obj), buf, flags);
      } else if (CAR(obj) == UNQUOTE && ok_abbrev(CDR(obj))) {
        SEN_RBUF_PUTC(buf, ',');
        sen_obj_inspect(ctx, CADR(obj), buf, flags);
      } else if (CAR(obj) == UNQUOTESP && ok_abbrev(CDR(obj))) {
        SEN_RBUF_PUTS(buf, ",@");
        sen_obj_inspect(ctx, CADR(obj), buf, flags);
      } else {
        SEN_RBUF_PUTC(buf, '(');
        for (;;) {
          sen_obj_inspect(ctx, CAR(obj), buf, flags);
          if ((obj = CDR(obj)) && (obj != NIL)) {
            if (PAIRP(obj)) {
              SEN_RBUF_PUTC(buf, ' ');
            } else {
              SEN_RBUF_PUTS(buf, " . ");
              sen_obj_inspect(ctx, obj, buf, flags);
              SEN_RBUF_PUTC(buf, ')');
              break;
            }
          } else {
            SEN_RBUF_PUTC(buf, ')');
            break;
          }
        }
      }
      break;
    default :
      if (SYMBOLP(obj)) {
        SEN_RBUF_PUTS(buf, SYMNAME(obj));
      } else {
        SEN_RBUF_PUTS(buf, "#<?(");
        sen_rbuf_itoa(buf, obj->type);
        SEN_RBUF_PUTS(buf, ")?>");
      }
      break;
    }
  }
}

/* ========== Routines for Evaluation Cycle ========== */

/* make closure. c is code. e is environment */
inline static cell *
mk_closure(sen_ctx *ctx, cell *c, cell *e)
{
  cell *x;
  GET_CELL(ctx, c, e, x);
  x->type = sen_ql_closure;
  x->flags = SEN_OBJ_REFERER;
  CAR(x) = c;
  CDR(x) = e;
  return x;
}

/* make continuation. */
inline static cell *
mk_continuation(sen_ctx *ctx, cell *d)
{
  cell *x;
  GET_CELL(ctx, NIL, d, x);
  x->type = sen_ql_continuation;
  x->flags = SEN_OBJ_REFERER;
  CONT_DUMP(x) = d;
  return x;
}

/* reverse list -- make new cells */
inline static cell *
reverse(sen_ctx *ctx, cell *a) /* a must be checked by gc */
{
  cell *p = NIL;
  for ( ; PAIRP(a); a = CDR(a)) {
    p = CONS(CAR(a), p);
    if (ERRP(ctx, SEN_ERROR)) { return F; }
  }
  return p;
}

/* reverse list --- no make new cells */
inline static cell *
non_alloc_rev(cell *term, cell *list)
{
  cell *p = list, *result = term, *q;
  while (p != NIL) {
    q = CDR(p);
    CDR(p) = result;
    result = p;
    p = q;
  }
  return result;
}

/* append list -- make new cells */
inline static cell *
append(sen_ctx *ctx, cell *a, cell *b)
{
  cell *p = b, *q;
  if (a != NIL) {
    a = reverse(ctx, a);
    if (ERRP(ctx, SEN_ERROR)) { return F; }
    while (a != NIL) {
      q = CDR(a);
      CDR(a) = p;
      p = a;
      a = q;
    }
  }
  return p;
}

/* equivalence of atoms */
inline static int
eqv(sen_obj *a, sen_obj *b)
{
  if (a == b) { return 1; }
  if (a->type != b->type) { return 0; }
  switch (a->type) {
  case sen_ql_object :
    return (a->class == b->class && a->u.o.self == b->u.o.self);
    break;
  case sen_ql_bulk :
    return (a->u.b.size == b->u.b.size &&
            !memcmp(a->u.b.value, b->u.b.value, a->u.b.size));
    break;
  case sen_ql_int :
    return (IVALUE(a) == IVALUE(b));
    break;
  case sen_ql_float :
    return !islessgreater(FVALUE(a), FVALUE(b));
    break;
  case sen_ql_time :
    return (!memcmp(&a->u.tv, &b->u.tv, sizeof(sen_timeval)));
    break;
  default :
    /* todo : support other types */
    return 0;
    break;
  }
}

/* true or false value macro */
#define istrue(p)       ((p) != NIL && (p) != F)
#define isfalse(p)      ((p) == F)

/* control macros for Eval_Cycle */
#define s_goto(ctx,a) do {\
  ctx->op = (a);\
  return T;\
} while (0)

#define s_save(ctx,a,b,args) (\
    ctx->dump = CONS(ctx->envir, CONS((args), ctx->dump)),\
    ctx->dump = CONS((b), ctx->dump),\
    ctx->dump = CONS(mk_number(ctx, (int64_t)(a)), ctx->dump))

#define s_return(ctx,a) do {\
    ctx->value = (a);\
    ctx->op = IVALUE(CAR(ctx->dump));\
    ctx->args = CADR(ctx->dump);\
    ctx->envir = CADDR(ctx->dump);\
    ctx->code = CADDDR(ctx->dump);\
    ctx->dump = CDDDDR(ctx->dump);\
    return T;\
} while (0)

#define RTN_NIL_IF_HEAD(ctx) do {\
  if (((ctx)->co.mode & SEN_CTX_HEAD)) { s_goto(ctx, OP_T0LVL); }\
} while (0)

#define RTN_NIL_IF_TAIL(ctx) do {\
  if (((ctx)->co.mode & SEN_CTX_TAIL)) { s_return((ctx), NIL); } else { return NIL; }\
} while (0)

static cell *
list_deep_copy(sen_ctx *ctx, cell *c) {
  /* NOTE: only list is copied */
  if (PAIRP(c)) {
    /* TODO: convert recursion to loop */
    return CONS(list_deep_copy(ctx, CAR(c)), list_deep_copy(ctx, CDR(c)));
  } else {
    return c;
  }
}

static void
qquote_uquotelist(sen_ctx *ctx, cell *cl, cell *pcl, int level) {
  /* reverse list */
  cell *x, *y;
  while (PAIRP(cl)) {
    x = CAR(cl);
    if (PAIRP(x)) {
      y = CAR(x);
      if (y == UNQUOTE) {
        if (level) {
          qquote_uquotelist(ctx, CDR(x), x, level - 1);
        } else {
          CDR(ctx->args) = CONS(cl, CDR(ctx->args)); /* save (unquote ...) cell */
        }
      } else if (y == UNQUOTESP) {
        if (level) {
          qquote_uquotelist(ctx, CDR(x), x, level - 1);
        } else {
          CDR(ctx->args) = CONS(pcl, CDR(ctx->args)); /* save pre (unquote-splicing) cell */
        }
      } else {
        qquote_uquotelist(ctx, x, cl, level);
      }
    } else if (x == QQUOTE) {
      qquote_uquotelist(ctx, CDR(cl), cl, level + 1);
      return;
    }
    if (!level && CADR(cl) == UNQUOTE) {
      CDR(ctx->args) = CONS(cl, CDR(ctx->args)); /* save (a . ,b) cell */
      return;
    }
    pcl = cl;
    cl = CDR(cl);
  }
}

#define GC_THRESHOLD 1000000

inline static cell *
opexe(sen_ctx *ctx)
{
  register cell *x, *y;
  if (ctx->op == OP_T0LVL || ctx->objects->n_entries > ctx->ncells + GC_THRESHOLD) {
    if (ctx->gc_verbose) {
      sen_rbuf buf;
      sen_rbuf_init(&buf, 0);
      sen_obj_inspect(ctx, ctx->envir, &buf, SEN_OBJ_INSPECT_ESC);
      *buf.curr = '\0';
      SEN_LOG(sen_log_notice, "mgc > ncells=%d envir=<%s>", ctx->objects->n_entries, buf.head);
      sen_rbuf_fin(&buf);
    }
    sen_ctx_mgc(ctx);
    if (ctx->gc_verbose) {
      SEN_LOG(sen_log_notice, "mgc < ncells=%d", ctx->objects->n_entries);
    }
    ctx->ncells = ctx->objects->n_entries;
  }
  switch (ctx->op) {
  case OP_LOAD:    /* load */
    if (BULKP(CAR(ctx->args))) {
      struct stat st;
      char *fname = STRVALUE(CAR(ctx->args));
      if (fname && !stat(fname, &st)) {
        if (ctx->inbuf) { SEN_FREE(ctx->inbuf); }
        if ((ctx->inbuf = SEN_MALLOC(st.st_size))) {
          int fd;
          if ((fd = open(fname, O_RDONLY)) != -1) {
            if (read(fd, ctx->inbuf, st.st_size) == st.st_size) {
              SEN_RBUF_PUTS(&ctx->outbuf, "loading ");
              SEN_RBUF_PUTS(&ctx->outbuf, STRVALUE(CAR(ctx->args)));
              ctx->cur = ctx->inbuf;
              ctx->str_end = ctx->inbuf + st.st_size;
            }
            close(fd);
          }
          if (ctx->cur != ctx->inbuf) {
            SEN_FREE(ctx->inbuf);
            ctx->inbuf = NULL;
          }
        }
      }
    }
    s_goto(ctx, OP_T0LVL);

  case OP_T0LVL:  /* top level */
    ctx->dump = NIL;
    ctx->envir = ctx->global_env;
    if (ctx->batchmode) {
      s_save(ctx, OP_T0LVL, NIL, NIL);
    } else {
      s_save(ctx, OP_VALUEPRINT, NIL, NIL);
    }
    s_save(ctx, OP_T1LVL, NIL, NIL);
    // if (infp == stdin) printf("hoge>\n");
    ctx->pht = &ctx->phs;
    *ctx->pht = NIL;
    s_goto(ctx, OP_READ);

  case OP_T1LVL:  /* top level */
    // verbose check?
    if (ctx->phs != NIL &&
        !(ctx->co.mode & (SEN_CTX_HEAD|SEN_CTX_TAIL))) { RTN_NIL_IF_TAIL(ctx); }
    // SEN_RBUF_PUTC(&ctx->outbuf, '\n');
    ctx->code = ctx->value;
    s_goto(ctx, OP_EVAL);

  case OP_READ:    /* read */
    RTN_NIL_IF_HEAD(ctx);
    if ((ctx->tok = token(ctx)) == TOK_EOS) { RTN_NIL_IF_TAIL(ctx); }
    s_goto(ctx, OP_RDSEXPR);

  case OP_VALUEPRINT:  /* print evalution result */
    ctx->args = ctx->value;
    s_save(ctx, OP_T0LVL, NIL, NIL);
    sen_obj_inspect(ctx, ctx->args, &ctx->outbuf, SEN_OBJ_INSPECT_ESC);
    s_return(ctx, T);

  case OP_EVAL:    /* main part of evalution */
    // fixme : quick hack.
    if (SYMBOLP(ctx->code)) {  /* symbol */
      if (KEYWORDP(ctx->code)) { s_return(ctx, ctx->code); }
      for (x = ctx->envir; x != NIL; x = CDR(x)) {
        for (y = CAR(x); y != NIL; y = CDR(y))
          if (CAAR(y) == ctx->code)
            break;
        if (y != NIL)
          break;
      }
      if (x != NIL) {
        s_return(ctx, CDAR(y));
      } else {
        if (PROCP(ctx->code)) { s_return(ctx, ctx->code); }
        if (NATIVE_FUNCP(ctx->code)) { s_return(ctx, ctx->code); }
        QLERR("Unbounded variable %s", SYMNAME(ctx->code));
      }
    } else if (PAIRP(ctx->code)) {
      if (SYNTAXP(x = CAR(ctx->code))) {  /* SYNTAX */
        ctx->code = CDR(ctx->code);
        s_goto(ctx, SYNTAXNUM(x));
      } else {/* first, eval top element and eval arguments */
        s_save(ctx, OP_E0ARGS, NIL, ctx->code);
        ctx->code = CAR(ctx->code);
        // if (NATIVE_FUNCP(ctx->code)) { s_return(ctx, ctx->code); } /* call native funcs. fast */
        s_goto(ctx, OP_EVAL);
      }
    } else {
      s_return(ctx, ctx->code);
    }

  case OP_E0ARGS:  /* eval arguments */
    if (MACROP(ctx->value)) {  /* macro expansion */
      s_save(ctx, OP_DOMACRO, NIL, NIL);
      ctx->args = CONS(ctx->code, NIL);
      ctx->code = ctx->value;
      s_goto(ctx, OP_APPLY);
    } else {
      ctx->code = CDR(ctx->code);
      s_goto(ctx, OP_E1ARGS);
    }

  case OP_E1ARGS:  /* eval arguments */
    ctx->args = CONS(ctx->value, ctx->args);
    if (PAIRP(ctx->code)) {  /* continue */
      s_save(ctx, OP_E1ARGS, ctx->args, CDR(ctx->code));
      ctx->code = CAR(ctx->code);
      ctx->args = NIL;
      s_goto(ctx, OP_EVAL);
    } else {  /* end */
      ctx->args = non_alloc_rev(NIL, ctx->args);
      ctx->code = CAR(ctx->args);
      ctx->args = CDR(ctx->args);
      s_goto(ctx, OP_APPLY);
    }

  case OP_APPLY:    /* apply 'code' to 'args' */
    if (NATIVE_FUNCP(ctx->code)) {
      ctx->co.func = ctx->code->u.o.func;
      s_goto(ctx, OP_NATIVE);
    } else if (PROCP(ctx->code)) {
      s_goto(ctx, PROCNUM(ctx->code));  /* PROCEDURE */
    } else if (CLOSUREP(ctx->code)) {  /* CLOSURE */
      /* make environment */
      ctx->envir = CONS(NIL, CLOSURE_ENV(ctx->code));
      for (x = CAR(CLOSURE_CODE(ctx->code)), y = ctx->args;
           PAIRP(x); x = CDR(x), y = CDR(y)) {
        if (y == NIL) {
          QLERR("Few arguments");
        } else {
          CAR(ctx->envir) = CONS(CONS(CAR(x), CAR(y)), CAR(ctx->envir));
        }
      }
      if (x == NIL) {
        /*--
         * if (y != NIL) {
         *   QLERR("Many arguments");
         * }
         */
      } else if (SYMBOLP(x))
        CAR(ctx->envir) = CONS(CONS(x, y), CAR(ctx->envir));
      else {
        QLERR("Syntax error in closure");
      }
      ctx->code = CDR(CLOSURE_CODE(ctx->code));
      ctx->args = NIL;
      s_goto(ctx, OP_BEGIN);
    } else if (CONTINUATIONP(ctx->code)) {  /* CONTINUATION */
      ctx->dump = CONT_DUMP(ctx->code);
      s_return(ctx, ctx->args != NIL ? CAR(ctx->args) : NIL);
    } else {
      QLERR("Illegal function");
    }

  case OP_DOMACRO:  /* do macro */
    ctx->code = ctx->value;
    s_goto(ctx, OP_EVAL);

  case OP_LAMBDA:  /* lambda */
    s_return(ctx, mk_closure(ctx, ctx->code, ctx->envir));

  case OP_QUOTE:    /* quote */
    s_return(ctx, CAR(ctx->code));

  case OP_DEF0:  /* define */
    if (PAIRP(CAR(ctx->code))) {
      x = CAAR(ctx->code);
      ctx->code = CONS(LAMBDA, CONS(CDAR(ctx->code), CDR(ctx->code)));
    } else {
      x = CAR(ctx->code);
      ctx->code = CADR(ctx->code);
    }
    if (!SYMBOLP(x)) {
      QLERR("Variable is not symbol");
    }
    s_save(ctx, OP_DEF1, NIL, x);
    s_goto(ctx, OP_EVAL);

  case OP_DEF1:  /* define */
    for (x = CAR(ctx->envir); x != NIL; x = CDR(x))
      if (CAAR(x) == ctx->code)
        break;
    if (x != NIL)
      CDAR(x) = ctx->value;
    else
      CAR(ctx->envir) = CONS(CONS(ctx->code, ctx->value), CAR(ctx->envir));
    s_return(ctx, ctx->code);

  case OP_SET0:    /* set! */
    s_save(ctx, OP_SET1, NIL, CAR(ctx->code));
    ctx->code = CADR(ctx->code);
    s_goto(ctx, OP_EVAL);

  case OP_SET1:    /* set! */
    for (x = ctx->envir; x != NIL; x = CDR(x)) {
      for (y = CAR(x); y != NIL; y = CDR(y))
        if (CAAR(y) == ctx->code)
          break;
      if (y != NIL)
        break;
    }
    if (x != NIL) {
      CDAR(y) = ctx->value;
      s_return(ctx, ctx->value);
    } else {
      QLERR("Unbounded variable %s", SYMBOLP(ctx->code) ? SYMNAME(ctx->code) : "");
    }

  case OP_BEGIN:    /* begin */
    if (!PAIRP(ctx->code)) {
      s_return(ctx, ctx->code);
    }
    if (CDR(ctx->code) != NIL) {
      s_save(ctx, OP_BEGIN, NIL, CDR(ctx->code));
    }
    ctx->code = CAR(ctx->code);
    s_goto(ctx, OP_EVAL);

  case OP_IF0:    /* if */
    s_save(ctx, OP_IF1, NIL, CDR(ctx->code));
    ctx->code = CAR(ctx->code);
    s_goto(ctx, OP_EVAL);

  case OP_IF1:    /* if */
    if (istrue(ctx->value))
      ctx->code = CAR(ctx->code);
    else
      ctx->code = CADR(ctx->code);  /* (if #f 1) ==> () because
             * CAR(NIL) = NIL */
    s_goto(ctx, OP_EVAL);

  case OP_LET0:    /* let */
    ctx->args = NIL;
    ctx->value = ctx->code;
    ctx->code = SYMBOLP(CAR(ctx->code)) ? CADR(ctx->code) : CAR(ctx->code);
    s_goto(ctx, OP_LET1);

  case OP_LET1:    /* let (caluculate parameters) */
    ctx->args = CONS(ctx->value, ctx->args);
    if (PAIRP(ctx->code)) {  /* continue */
      QLASSERT(LISTP(CAR(ctx->code)) && LISTP(CDAR(ctx->code)));
      s_save(ctx, OP_LET1, ctx->args, CDR(ctx->code));
      ctx->code = CADAR(ctx->code);
      ctx->args = NIL;
      s_goto(ctx, OP_EVAL);
    } else {  /* end */
      ctx->args = non_alloc_rev(NIL, ctx->args);
      ctx->code = CAR(ctx->args);
      ctx->args = CDR(ctx->args);
      s_goto(ctx, OP_LET2);
    }

  case OP_LET2:    /* let */
    ctx->envir = CONS(NIL, ctx->envir);
    for (x = SYMBOLP(CAR(ctx->code)) ? CADR(ctx->code) : CAR(ctx->code), y = ctx->args;
         y != NIL; x = CDR(x), y = CDR(y))
      CAR(ctx->envir) = CONS(CONS(CAAR(x), CAR(y)), CAR(ctx->envir));
    if (SYMBOLP(CAR(ctx->code))) {  /* named let */
      for (x = CADR(ctx->code), ctx->args = NIL; PAIRP(x); x = CDR(x))
        ctx->args = CONS(CAAR(x), ctx->args);
      x = mk_closure(ctx, CONS(non_alloc_rev(NIL, ctx->args), CDDR(ctx->code)),
                     ctx->envir);
      CAR(ctx->envir) = CONS(CONS(CAR(ctx->code), x), CAR(ctx->envir));
      ctx->code = CDDR(ctx->code);
      ctx->args = NIL;
    } else {
      ctx->code = CDR(ctx->code);
      ctx->args = NIL;
    }
    s_goto(ctx, OP_BEGIN);

  case OP_LET0AST:  /* let* */
    if (CAR(ctx->code) == NIL) {
      ctx->envir = CONS(NIL, ctx->envir);
      ctx->code = CDR(ctx->code);
      s_goto(ctx, OP_BEGIN);
    }
    s_save(ctx, OP_LET1AST, CDR(ctx->code), CAR(ctx->code));
    QLASSERT(LISTP(CAR(ctx->code)) &&
             LISTP(CAAR(ctx->code)) && LISTP((CDR(CAAR(ctx->code)))));
    ctx->code = CADAAR(ctx->code);
    s_goto(ctx, OP_EVAL);

  case OP_LET1AST:  /* let* (make new frame) */
    ctx->envir = CONS(NIL, ctx->envir);
    s_goto(ctx, OP_LET2AST);

  case OP_LET2AST:  /* let* (caluculate parameters) */
    CAR(ctx->envir) = CONS(CONS(CAAR(ctx->code), ctx->value), CAR(ctx->envir));
    ctx->code = CDR(ctx->code);
    if (PAIRP(ctx->code)) {  /* continue */
      QLASSERT(LISTP(CAR(ctx->code)) && LISTP(CDAR(ctx->code)));
      s_save(ctx, OP_LET2AST, ctx->args, ctx->code);
      ctx->code = CADAR(ctx->code);
      ctx->args = NIL;
      s_goto(ctx, OP_EVAL);
    } else {  /* end */
      ctx->code = ctx->args;
      ctx->args = NIL;
      s_goto(ctx, OP_BEGIN);
    }

  case OP_LET0REC:  /* letrec */
    ctx->envir = CONS(NIL, ctx->envir);
    ctx->args = NIL;
    ctx->value = ctx->code;
    ctx->code = CAR(ctx->code);
    s_goto(ctx, OP_LET1REC);

  case OP_LET1REC:  /* letrec (caluculate parameters) */
    ctx->args = CONS(ctx->value, ctx->args);
    if (PAIRP(ctx->code)) {  /* continue */
      QLASSERT(LISTP(CAR(ctx->code)) && LISTP(CDAR(ctx->code)));
      s_save(ctx, OP_LET1REC, ctx->args, CDR(ctx->code));
      ctx->code = CADAR(ctx->code);
      ctx->args = NIL;
      s_goto(ctx, OP_EVAL);
    } else {  /* end */
      ctx->args = non_alloc_rev(NIL, ctx->args);
      ctx->code = CAR(ctx->args);
      ctx->args = CDR(ctx->args);
      s_goto(ctx, OP_LET2REC);
    }

  case OP_LET2REC:  /* letrec */
    for (x = CAR(ctx->code), y = ctx->args; y != NIL; x = CDR(x), y = CDR(y))
      CAR(ctx->envir) = CONS(CONS(CAAR(x), CAR(y)), CAR(ctx->envir));
    ctx->code = CDR(ctx->code);
    ctx->args = NIL;
    s_goto(ctx, OP_BEGIN);

  case OP_COND0:    /* cond */
    if (!PAIRP(ctx->code)) {
      QLERR("Syntax error in cond");
    }
    s_save(ctx, OP_COND1, NIL, ctx->code);
    ctx->code = CAAR(ctx->code);
    s_goto(ctx, OP_EVAL);

  case OP_COND1:    /* cond */
    if (istrue(ctx->value)) {
      if ((ctx->code = CDAR(ctx->code)) == NIL) {
        s_return(ctx, ctx->value);
      }
      s_goto(ctx, OP_BEGIN);
    } else {
      if ((ctx->code = CDR(ctx->code)) == NIL) {
        s_return(ctx, NIL);
      } else {
        s_save(ctx, OP_COND1, NIL, ctx->code);
        ctx->code = CAAR(ctx->code);
        s_goto(ctx, OP_EVAL);
      }
    }

  case OP_DELAY:    /* delay */
    x = mk_closure(ctx, CONS(NIL, ctx->code), ctx->envir);
    if (ERRP(ctx, SEN_ERROR)) { return F; }
    SETPROMISE(x);
    s_return(ctx, x);

  case OP_AND0:    /* and */
    if (ctx->code == NIL) {
      s_return(ctx, T);
    }
    s_save(ctx, OP_AND1, NIL, CDR(ctx->code));
    ctx->code = CAR(ctx->code);
    s_goto(ctx, OP_EVAL);

  case OP_AND1:    /* and */
    if (isfalse(ctx->value)) {
      s_return(ctx, ctx->value);
    } else if (ctx->code == NIL) {
      s_return(ctx, ctx->value);
    } else {
      s_save(ctx, OP_AND1, NIL, CDR(ctx->code));
      ctx->code = CAR(ctx->code);
      s_goto(ctx, OP_EVAL);
    }

  case OP_OR0:    /* or */
    if (ctx->code == NIL) {
      s_return(ctx, F);
    }
    s_save(ctx, OP_OR1, NIL, CDR(ctx->code));
    ctx->code = CAR(ctx->code);
    s_goto(ctx, OP_EVAL);

  case OP_OR1:    /* or */
    if (istrue(ctx->value)) {
      s_return(ctx, ctx->value);
    } else if (ctx->code == NIL) {
      s_return(ctx, ctx->value);
    } else {
      s_save(ctx, OP_OR1, NIL, CDR(ctx->code));
      ctx->code = CAR(ctx->code);
      s_goto(ctx, OP_EVAL);
    }

  case OP_C0STREAM:  /* cons-stream */
    s_save(ctx, OP_C1STREAM, NIL, CDR(ctx->code));
    ctx->code = CAR(ctx->code);
    s_goto(ctx, OP_EVAL);

  case OP_C1STREAM:  /* cons-stream */
    ctx->args = ctx->value;  /* save ctx->value to register ctx->args for gc */
    x = mk_closure(ctx, CONS(NIL, ctx->code), ctx->envir);
    if (ERRP(ctx, SEN_ERROR)) { return F; }
    SETPROMISE(x);
    s_return(ctx, CONS(ctx->args, x));

  case OP_0MACRO:  /* macro */
    x = CAR(ctx->code);
    ctx->code = CADR(ctx->code);
    if (!SYMBOLP(x)) {
      QLERR("Variable is not symbol");
    }
    s_save(ctx, OP_1MACRO, NIL, x);
    s_goto(ctx, OP_EVAL);

  case OP_1MACRO:  /* macro */
    ctx->value->flags |= SEN_OBJ_MACRO;
    for (x = CAR(ctx->envir); x != NIL; x = CDR(x))
      if (CAAR(x) == ctx->code)
        break;
    if (x != NIL)
      CDAR(x) = ctx->value;
    else
      CAR(ctx->envir) = CONS(CONS(ctx->code, ctx->value), CAR(ctx->envir));
    s_return(ctx, ctx->code);

  case OP_CASE0:    /* case */
    s_save(ctx, OP_CASE1, NIL, CDR(ctx->code));
    ctx->code = CAR(ctx->code);
    s_goto(ctx, OP_EVAL);

  case OP_CASE1:    /* case */
    for (x = ctx->code; x != NIL; x = CDR(x)) {
      if (!PAIRP(y = CAAR(x)))
        break;
      for ( ; y != NIL; y = CDR(y))
        if (eqv(CAR(y), ctx->value))
          break;
      if (y != NIL)
        break;
    }
    if (x != NIL) {
      if (PAIRP(CAAR(x))) {
        ctx->code = CDAR(x);
        s_goto(ctx, OP_BEGIN);
      } else {/* else */
        s_save(ctx, OP_CASE2, NIL, CDAR(x));
        ctx->code = CAAR(x);
        s_goto(ctx, OP_EVAL);
      }
    } else {
      s_return(ctx, NIL);
    }

  case OP_CASE2:    /* case */
    if (istrue(ctx->value)) {
      s_goto(ctx, OP_BEGIN);
    } else {
      s_return(ctx, NIL);
    }
  case OP_PAPPLY:  /* apply */
    ctx->code = CAR(ctx->args);
    ctx->args = CADR(ctx->args);
    s_goto(ctx, OP_APPLY);

  case OP_PEVAL:  /* eval */
    ctx->code = CAR(ctx->args);
    ctx->args = NIL;
    s_goto(ctx, OP_EVAL);

  case OP_CONTINUATION:  /* call-with-current-continuation */
    ctx->code = CAR(ctx->args);
    ctx->args = CONS(mk_continuation(ctx, ctx->dump), NIL);
    s_goto(ctx, OP_APPLY);

  case OP_SETCAR:  /* set-car! */
    if (PAIRP(CAR(ctx->args))) {
      CAAR(ctx->args) = CADR(ctx->args);
      s_return(ctx, CAR(ctx->args));
    } else {
      QLERR("Unable to set-car! for non-cons cell");
    }

  case OP_SETCDR:  /* set-cdr! */
    if (PAIRP(CAR(ctx->args))) {
      CDAR(ctx->args) = CADR(ctx->args);
      s_return(ctx, CAR(ctx->args));
    } else {
      QLERR("Unable to set-cdr! for non-cons cell");
    }

  case OP_FORCE:    /* force */
    ctx->code = CAR(ctx->args);
    if (PROMISEP(ctx->code)) {
      ctx->args = NIL;
      s_goto(ctx, OP_APPLY);
    } else {
      s_return(ctx, ctx->code);
    }

  case OP_ERR0:  /* error */
    SEN_RBUF_PUTS(&ctx->outbuf, "*** ERROR: ");
    SEN_RBUF_PUTS(&ctx->outbuf, ctx->errbuf);
    SEN_RBUF_PUTC(&ctx->outbuf, '\n');
    ctx->args = NIL;
    s_goto(ctx, OP_T0LVL);

  case OP_ERR1:  /* error */
    SEN_RBUF_PUTS(&ctx->outbuf, "*** ERROR:");
    while (ctx->args != NIL) {
      SEN_RBUF_PUTC(&ctx->outbuf, ' ');
      sen_obj_inspect(ctx, CAR(ctx->args), &ctx->outbuf, SEN_OBJ_INSPECT_ESC);
      ctx->args = CDR(ctx->args);
    }
    SEN_RBUF_PUTC(&ctx->outbuf, '\n');
    s_goto(ctx, OP_T0LVL);

  case OP_PUT:    /* put */
    if (!HASPROP(CAR(ctx->args)) || !HASPROP(CADR(ctx->args))) {
      QLERR("Illegal use of put");
    }
    for (x = SYMPROP(CAR(ctx->args)), y = CADR(ctx->args); x != NIL; x = CDR(x))
      if (CAAR(x) == y)
        break;
    if (x != NIL)
      CDAR(x) = CADDR(ctx->args);
    else
      SYMPROP(CAR(ctx->args)) = CONS(CONS(y, CADDR(ctx->args)),
              SYMPROP(CAR(ctx->args)));
    s_return(ctx, T);

  case OP_GET:    /* get */
    if (!HASPROP(CAR(ctx->args)) || !HASPROP(CADR(ctx->args))) {
      QLERR("Illegal use of get");
    }
    for (x = SYMPROP(CAR(ctx->args)), y = CADR(ctx->args); x != NIL; x = CDR(x))
      if (CAAR(x) == y)
        break;
    if (x != NIL) {
      s_return(ctx, CDAR(x));
    } else {
      s_return(ctx, NIL);
    }

  case OP_SDOWN:   /* shutdown */
    SEN_LOG(sen_log_notice, "shutting down..");
    sen_gctx.stat = SEN_CTX_QUIT;
    s_goto(ctx, OP_QUIT);

  case OP_RDSEXPR:
    {
      char tok, *str;
      unsigned len;
      RTN_NIL_IF_HEAD(ctx);
      switch (ctx->tok) {
      case TOK_COMMENT:
        skipline(ctx);
        if ((ctx->tok = token(ctx)) == TOK_EOS) { RTN_NIL_IF_TAIL(ctx); }
        s_goto(ctx, OP_RDSEXPR);
      case TOK_LPAREN:
        if ((tok = token(ctx)) == TOK_EOS) { RTN_NIL_IF_TAIL(ctx); }
        ctx->tok = tok;
        if (ctx->tok == TOK_RPAREN) {
          s_return(ctx, NIL);
        } else if (ctx->tok == TOK_DOT) {
          QLERR("syntax error: illegal dot expression");
        } else {
          s_save(ctx, OP_RDLIST, NIL, NIL);
          s_goto(ctx, OP_RDSEXPR);
        }
      case TOK_QUOTE:
        s_save(ctx, OP_RDQUOTE, NIL, NIL);
        if ((ctx->tok = token(ctx)) == TOK_EOS) { RTN_NIL_IF_TAIL(ctx); }
        s_goto(ctx, OP_RDSEXPR);
      case TOK_BQUOTE:
        s_save(ctx, OP_RDQQUOTE, NIL, NIL);
        if ((ctx->tok = token(ctx)) == TOK_EOS) { RTN_NIL_IF_TAIL(ctx); }
        s_goto(ctx, OP_RDSEXPR);
      case TOK_COMMA:
        s_save(ctx, OP_RDUNQUOTE, NIL, NIL);
        if ((ctx->tok = token(ctx)) == TOK_EOS) { RTN_NIL_IF_TAIL(ctx); }
        s_goto(ctx, OP_RDSEXPR);
      case TOK_ATMARK:
        s_save(ctx, OP_RDUQTSP, NIL, NIL);
        if ((ctx->tok = token(ctx)) == TOK_EOS) { RTN_NIL_IF_TAIL(ctx); }
        s_goto(ctx, OP_RDSEXPR);
      case TOK_ATOM:
        if (readstr(ctx, &str, &len) == TOK_EOS) { ctx->tok = TOK_EOS; RTN_NIL_IF_TAIL(ctx); }
        s_return(ctx, mk_atom(ctx, str, len, NIL));
      case TOK_DQUOTE:
        if (readstrexp(ctx, &str, &len) == TOK_EOS) {
          QLERR("unterminated string");
        }
        s_return(ctx, sen_ql_mk_string(ctx, str, len));
      case TOK_SHARP:
        if ((readstr(ctx, &str, &len) == TOK_EOS) ||
            (x = mk_const(ctx, str, len)) == NIL) {
          QLERR("Undefined sharp expression");
        } else {
          s_return(ctx, x);
        }
      case TOK_EOS :
        if ((ctx->tok = token(ctx)) == TOK_EOS) { RTN_NIL_IF_TAIL(ctx); }
        s_goto(ctx, OP_RDSEXPR);
      case TOK_QUESTION:
        {
          cell *o, *p;
          SEN_OBJ_NEW(ctx, o);
          p = CONS(o, NIL);
          o->type = sen_ql_bulk;
          o->flags = 0;
          o->u.b.size = 1;
          o->u.b.value = "?";
          *ctx->pht = p;
          ctx->pht = &CDR(p);
          s_return(ctx, o);
        }
      default:
        QLERR("syntax error: illegal token");
      }
    }
    break;

  case OP_RDLIST:
    RTN_NIL_IF_HEAD(ctx);
    if ((ctx->tok = token(ctx)) == TOK_EOS) { RTN_NIL_IF_TAIL(ctx); }
    if (ctx->tok == TOK_COMMENT) {
      skipline(ctx);
      s_goto(ctx, OP_RDLIST);
    }
    ctx->args = CONS(ctx->value, ctx->args);
    if (ctx->tok == TOK_RPAREN) {
      cell *v = non_alloc_rev(NIL, ctx->args);
      if (ctx->cur < ctx->str_end && *ctx->cur == '.') {
        char *str = NULL;
        unsigned len = 0;
        if (readstr(ctx, &str, &len) != TOK_ATOM) { /* error */ }
        s_return(ctx, mk_atom(ctx, str, len, v));
      } else {
        s_return(ctx, v);
      }
    } else if (ctx->tok == TOK_DOT) {
      s_save(ctx, OP_RDDOT, ctx->args, NIL);
      if ((ctx->tok = token(ctx)) == TOK_EOS) {
        ctx->op = OP_RDSEXPR; RTN_NIL_IF_TAIL(ctx);
      }
      s_goto(ctx, OP_RDSEXPR);
    } else {
      s_save(ctx, OP_RDLIST, ctx->args, NIL);;
      s_goto(ctx, OP_RDSEXPR);
    }

  case OP_RDDOT:
    RTN_NIL_IF_HEAD(ctx);
    if ((ctx->tok = token(ctx)) == TOK_EOS) { RTN_NIL_IF_TAIL(ctx); }
    if (ctx->tok != TOK_RPAREN) {
      QLERR("syntax error: illegal dot expression");
    } else {
      cell *v = non_alloc_rev(ctx->value, ctx->args);
      if (ctx->cur < ctx->str_end && *ctx->cur == '.') {
        char *str = NULL;
        unsigned len = 0;
        if (readstr(ctx, &str, &len) != TOK_ATOM) { /* error */ }
        s_return(ctx, mk_atom(ctx, str, len, v));
      } else {
        s_return(ctx, v);
      }
    }

  case OP_RDQUOTE:
    s_return(ctx, CONS(QUOTE, CONS(ctx->value, NIL)));

  case OP_RDQQUOTE:
    s_return(ctx, CONS(QQUOTE, CONS(ctx->value, NIL)));

  case OP_RDUNQUOTE:
    s_return(ctx, CONS(UNQUOTE, CONS(ctx->value, NIL)));

  case OP_RDUQTSP:
    s_return(ctx, CONS(UNQUOTESP, CONS(ctx->value, NIL)));

  case OP_NATIVE:
    s_return(ctx, ctx->value);
  case OP_QQUOTE0:
    ctx->code = list_deep_copy(ctx, ctx->code);
    ctx->args = CONS(ctx->code, NIL);
    qquote_uquotelist(ctx, ctx->code, ctx->code, 0);
    ctx->code = CDR(ctx->args);
    s_goto(ctx, OP_QQUOTE1);
  case OP_QQUOTE1:
    while (PAIRP(ctx->code)) {
      x = CAR(ctx->code);
      if (PAIRP(x) && LISTP(CDR(x))) {
        s_save(ctx, OP_QQUOTE2, ctx->args, ctx->code);
        y = CADR(x);
        if (y == UNQUOTE) {
          QLASSERT(LISTP(CDDR(x)));
          ctx->code = CADDR(x);
        } else if (CAR(y) == UNQUOTESP) {
          QLASSERT(LISTP(CDR(y)));
          ctx->code = CADR(y);
        } else {
          y = CAR(x);
          if (CAR(y) == UNQUOTE) {
            ctx->code = CADR(y);
          } else if (CAAR(y) == UNQUOTESP) {
            ctx->code = CADAR(y);
          } else {
            /* error */
          }
        }
        s_goto(ctx, OP_EVAL);
      }
      ctx->code = CDR(ctx->code);
    }
    s_return(ctx, CAAR(ctx->args));
  case OP_QQUOTE2:
    x = CAR(ctx->code);
    y = CADR(x);
    if (y == UNQUOTE) {
      CDR(x) = ctx->value;
    } else if (CAR(y) == UNQUOTESP) {
      if (ctx->value == NIL) {
        CDR(x) = CDDR(x);
      } else if (!PAIRP(ctx->value) ) {
        /* error */
      } else {
        ctx->value = list_deep_copy(ctx, ctx->value);
        for (y = ctx->value; CDR(y) != NIL; y = CDR(y)) {}
        CDR(y) = CDDR(x);
        CDR(x) = ctx->value;
      }
    } else {
      y = CAAR(x);
      if (y == UNQUOTE) {
        CAR(x) = ctx->value;
      } else if (CAR(y) == UNQUOTESP) {
        if (ctx->value == NIL) {
          CAR(x) = CDAR(x);
        } else if (!PAIRP(ctx->value) ) {
          /* error */
        } else {
          ctx->value = list_deep_copy(ctx, ctx->value);
          for (y = ctx->value; CDR(y) != NIL; y = CDR(y)) {}
          CDR(y) = CDAR(x);
          CAR(x) = ctx->value;
        }
      } else {
        /* error */
      }
    }
    ctx->code = CDR(ctx->code);
    s_goto(ctx, OP_QQUOTE1);
  }
  SEN_LOG(sen_log_error, "illegal op (%d)", ctx->op);
  return NIL;
}

/* kernel of this intepreter */
inline static void
Eval_Cycle(sen_ctx *ctx)
{
  ctx->co.func = NULL;
  ctx->co.last = 0;
  while (opexe(ctx) != NIL) {
    switch (ctx->op) {
    case OP_NATIVE :
      ctx->stat = SEN_QL_NATIVE;
      return;
    case OP_T0LVL :
      ctx->stat = SEN_QL_TOPLEVEL;
      return;
    case OP_T1LVL :
      ctx->stat = (ctx->phs != NIL) ? SEN_QL_WAIT_ARG : SEN_QL_EVAL;
      return;
    case OP_QUIT :
      ctx->stat = SEN_QL_QUITTING;
      return;
    default :
      break;
    }
    if (ERRP(ctx, SEN_ERROR)) { return; }
  }
  ctx->stat = SEN_QL_WAIT_EXPR;
}

sen_obj *
sen_ql_eval(sen_ctx *ctx, sen_obj *code, sen_obj *objs)
{
  sen_ql_co co;
  uint8_t op = ctx->op;
  uint8_t stat = ctx->stat;
  uint8_t feed_mode = ctx->feed_mode;
  sen_obj *o, *code_ = ctx->code;
  o = CONS(objs, ctx->envir);
  memcpy(&co, &ctx->co, sizeof(sen_ql_co));
  s_save(ctx, OP_QUIT, ctx->args, o);
  ctx->op = OP_EVAL;
  ctx->stat = SEN_QL_EVAL;
  ctx->code = code;
  ctx->feed_mode = sen_ql_atonce;
  sen_ql_feed(ctx, NULL, 0, 0);
  ctx->feed_mode = feed_mode;
  ctx->stat = stat;
  ctx->op = op;
  ctx->envir = CDR(o);
  ctx->code = code_;
  memcpy(&ctx->co, &co, sizeof(sen_ql_co));
  return ctx->value;
}

/* ========== native functions ========== */

#define s_retbool(tf)  do { return (tf) ? T : F; } while (0)

#define do_op(x,y,op) do {\
  switch ((x)->type) {\
  case sen_ql_int :\
    switch ((y)->type) {\
    case sen_ql_int :\
      IVALUE(x) = IVALUE(x) op IVALUE(y);\
      break;\
    case sen_ql_float :\
      SETFLOAT(x, ((double) IVALUE(x)) op FVALUE(y));\
      break;\
    default :\
      if (sen_obj2int(ctx, y)) { QLERR("can't convert into numeric value"); }\
      IVALUE(x) = IVALUE(x) op IVALUE(y);\
    }\
    break;\
  case sen_ql_float :\
    switch ((y)->type) {\
    case sen_ql_int :\
      FVALUE(x) = FVALUE(x) op IVALUE(y);\
      break;\
    case sen_ql_float :\
      FVALUE(x) = FVALUE(x) op FVALUE(y);\
      break;\
    default :\
      if (sen_obj2int(ctx, y)) { QLERR("can't convert into numeric value"); }\
      FVALUE(x) = FVALUE(x) op IVALUE(y);\
    }\
    break;\
  default :\
    QLERR("can't convert into numeric");\
  }\
} while (0)

#define do_compare(x,y,r,op) do {\
  switch (x->type) {\
  case sen_ql_int :\
    switch (y->type) {\
    case sen_ql_int :\
      r = (IVALUE(x) op IVALUE(y));\
      break;\
    case sen_ql_float :\
      r = (IVALUE(x) op FVALUE(y));\
      break;\
    default :\
      if (sen_obj2int(ctx, y)) { QLERR("can't convert into numeric value"); }\
      r = (IVALUE(x) op IVALUE(y));\
    }\
    break;\
  case sen_ql_float :\
    switch (y->type) {\
    case sen_ql_int :\
      r = (FVALUE(x) op IVALUE(y));\
      break;\
    case sen_ql_float :\
      r = (FVALUE(x) op FVALUE(y));\
      break;\
    default :\
      if (sen_obj2int(ctx, y)) { QLERR("can't convert into numeric value"); }\
      r = (FVALUE(x) op IVALUE(y));\
    }\
    break;\
  case sen_ql_bulk :\
    if (y->type == sen_ql_bulk) {\
      int r_;\
      uint32_t la = x->u.b.size, lb = y->u.b.size;\
      if (la > lb) {\
        if (!(r_ = memcmp(x->u.b.value, y->u.b.value, lb))) {\
          r_ = 1;\
        }\
      } else {\
        if (!(r_ = memcmp(x->u.b.value, y->u.b.value, la))) {\
          r_ = la == lb ? 0 : -1;\
        }\
      }\
      r = (r_ op 0);\
    } else {\
      QLERR("can't compare");\
    }\
    break;\
  case sen_ql_time :\
    if (y->type == sen_ql_time) {\
      if (x->u.tv.tv_sec != y->u.tv.tv_sec) {\
        r = (x->u.tv.tv_sec op y->u.tv.tv_sec);\
      } else {\
        r = (x->u.tv.tv_usec op y->u.tv.tv_usec);\
      }\
    } else {\
      QLERR("can't compare");\
    }\
  default :\
    r = (memcmp(&x->u.tv, &y->u.tv, sizeof(sen_timeval)) op 0);\
  }\
} while (0)

#define time_op(x,y,v,op) {\
  switch (y->type) {\
  case sen_ql_time :\
    {\
      double dv= x->u.tv.tv_sec op y->u.tv.tv_sec;\
      dv += (x->u.tv.tv_usec op y->u.tv.tv_usec) / 1000000.0;\
      SETFLOAT(v, dv);\
    }\
    break;\
  case sen_ql_int :\
    {\
      sen_timeval tv;\
      int64_t sec = x->u.tv.tv_sec op IVALUE(y);\
      if (sec < INT32_MIN || INT32_MAX < sec) { QLERR("time val overflow"); }\
      tv.tv_sec = (int)sec;\
      tv.tv_usec = x->u.tv.tv_usec;\
      SETTIME(v, &tv);\
    }\
    break;\
  case sen_ql_float :\
    {\
      sen_timeval tv;\
      double sec = x->u.tv.tv_sec op (int)FVALUE(y);\
      int32_t usec = x->u.tv.tv_usec op (int)((FVALUE(y) - (int)FVALUE(y)) * 1000000);\
      if (sec < INT32_MIN || INT32_MAX < sec) { QLERR("time val overflow"); }\
      tv.tv_sec = (int)sec;\
      if (usec < 0) {\
        tv.tv_sec--;\
        usec += 1000000;\
      } else if (usec >= 1000000) {\
        tv.tv_sec++;\
        usec -= 1000000;\
      }\
      tv.tv_usec = usec;\
      SETTIME(v, &tv);\
    }\
    break;\
  default :\
    QLERR("can't convert into numeric value");\
    break;\
  }\
} while (0)

static sen_obj *
nf_add(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  register cell *x, *v;
  if (!PAIRP(args)) { QLERR("list required"); }
  switch (CAR(args)->type) {
  case sen_ql_bulk :
    {
      sen_rbuf buf;
      sen_rbuf_init(&buf, 0);
      while (PAIRP(args)) {
        POP(x, args);
        sen_obj_inspect(ctx, x, &buf, 0);
      }
      SEN_RBUF2OBJ(ctx, &buf, v);
    }
    break;
  case sen_ql_time :
    if (PAIRP(CDR(args)) && NUMBERP(CADR(args))) {
      SEN_OBJ_NEW(ctx, v);
      time_op(CAR(args), CADR(args), v, +);
    } else {
      QLERR("can't convert into numeric value");
    }
    break;
  default :
    v = mk_number(ctx, 0);
    while (PAIRP(args)) {
      POP(x, args);
      do_op(v, x, +);
    }
    break;
  }
  return v;
}

static sen_obj *
nf_sub(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  register cell *v = mk_number(ctx, 0);
  register cell *x;
  if (PAIRP(args) && CDR(args) != NIL) {
    if (CAR(args)->type == sen_ql_time) {
      time_op(CAR(args), CADR(args), v, -);
      return v;
    }
    POP(x, args);
    do_op(v, x, +);
  }
  while (PAIRP(args)) {
    POP(x, args);
    do_op(v, x, -);
  }
  return v;
}

static sen_obj *
nf_mul(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  register cell *v, *x;
  if (CAR(args)->type == sen_ql_bulk && CADR(args)->type == sen_ql_int) {
    int i, n = (int)IVALUE(CADR(args));
    sen_rbuf buf;
    sen_rbuf_init(&buf, 0);
    POP(x, args);
    for (i = 0; i < n; i++) {
      sen_obj_inspect(ctx, x, &buf, 0);
    }
    SEN_RBUF2OBJ(ctx, &buf, v);
  } else {
    v = mk_number(ctx, 1);
    while (PAIRP(args)) {
      POP(x, args);
      do_op(v, x, *);
    }
  }
  return v;
}

static sen_obj *
nf_div(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  register cell *v;
  register cell *x;
  if (PAIRP(args) && CDR(args) != NIL) {
    v = mk_number(ctx, 0);
    POP(x, args);
    do_op(v, x, +);
  } else {
    v = mk_number(ctx, 1);
  }
  while (PAIRP(args)) {
    POP(x, args);
    if (x->type == sen_ql_int && IVALUE(x) == 0 && v->type == sen_ql_int) {
      SETFLOAT(v, (double)IVALUE(v));
    }
    do_op(v, x, /);
  }
  return v;
}
static sen_obj *
nf_rem(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  register int64_t v;
  register cell *x;
  x = args;
  if (sen_obj2int(ctx, CAR(x))) {
    QLERR("can't convert into integer");
  }
  v = IVALUE(CAR(x));
  while (CDR(x) != NIL) {
    x = CDR(x);
    if (sen_obj2int(ctx, CAR(x))) {
      QLERR("can't convert into integer");
    }
    if (IVALUE(CAR(x)) != 0)
      v %= IVALUE(CAR(x));
    else {
      QLERR("Divided by zero");
    }
  }
  return mk_number(ctx, v);
}
static sen_obj *
nf_car(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  if (PAIRP(CAR(args))) {
    return CAAR(args);
  } else {
    QLERR("Unable to car for non-cons cell");
  }
}
static sen_obj *
nf_cdr(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  if (PAIRP(CAR(args))) {
    return CDAR(args);
  } else {
    QLERR("Unable to cdr for non-cons cell");
  }
}
static sen_obj *
nf_cons(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  CDR(args) = CADR(args);
  return args;
}
static sen_obj *
nf_not(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  s_retbool(isfalse(CAR(args)));
}
static sen_obj *
nf_bool(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  s_retbool(CAR(args) == F || CAR(args) == T);
}
static sen_obj *
nf_null(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  s_retbool(CAR(args) == NIL);
}
static sen_obj *
nf_zerop(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  register cell *x = CAR(args);
  switch (x->type) {
  case sen_ql_int :
    s_retbool(IVALUE(x) == 0);
    break;
  case sen_ql_float :
    s_retbool(!(islessgreater(FVALUE(x), 0.0)));
    break;
  default :
    QLERR("can't convert into numeric value");
  }
}
static sen_obj *
nf_posp(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  register cell *x = CAR(args);
  switch (x->type) {
  case sen_ql_int :
    s_retbool(IVALUE(x) > 0);
    break;
  case sen_ql_float :
    s_retbool(!(isgreater(FVALUE(x), 0.0)));
    break;
  default :
    QLERR("can't convert into numeric value");
  }
}
static sen_obj *
nf_negp(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  register cell *x = CAR(args);
  switch (x->type) {
  case sen_ql_int :
    s_retbool(IVALUE(x) < 0);
    break;
  case sen_ql_float :
    s_retbool(!(isless(FVALUE(x), 0.0)));
    break;
  default :
    QLERR("can't convert into numeric value");
  }
}
static sen_obj *
nf_neq(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  int r = 1;
  register cell *x, *y;
  POP(x, args);
  if (!PAIRP(args)) { QLERR("Few arguments"); }
  do {
    POP(y, args);
    switch (x->type) {
    case sen_ql_int :
      switch (y->type) {
      case sen_ql_int :
        r = (IVALUE(x) == IVALUE(y));
        break;
      case sen_ql_float :
        r = (IVALUE(x) <= FVALUE(y) && IVALUE(x) >= FVALUE(y));
        break;
      default :
        if (sen_obj2int(ctx, y)) { QLERR("can't convert into numeric value"); }
        r = (IVALUE(x) == IVALUE(y));
      }
      break;
    case sen_ql_float :
      switch (y->type) {
      case sen_ql_int :
        r = (FVALUE(x) <= IVALUE(y) && FVALUE(x) >= IVALUE(y));
        break;
      case sen_ql_float :
        r = (FVALUE(x) <= FVALUE(y) && FVALUE(x) >= FVALUE(y));
        break;
      default :
        if (sen_obj2int(ctx, y)) { QLERR("can't convert into numeric value"); }
        r = (FVALUE(x) <= IVALUE(y) && FVALUE(x) >= IVALUE(y));
      }
      break;
    case sen_ql_bulk :
      if (y->type == sen_ql_bulk) {
        int r_;
        uint32_t la = x->u.b.size, lb = y->u.b.size;
        if (la > lb) {
          if (!(r_ = memcmp(x->u.b.value, y->u.b.value, lb))) {
            r_ = 1;
          }
        } else {
          if (!(r_ = memcmp(x->u.b.value, y->u.b.value, la))) {
            r_ = la == lb ? 0 : -1;
          }
        }
        r = (r_ == 0);
      } else {
        QLERR("can't compare");
      }
      break;
    case sen_ql_time :
      if (y->type == sen_ql_time) {
        if (x->u.tv.tv_sec != y->u.tv.tv_sec) {
          r = (x->u.tv.tv_sec == y->u.tv.tv_sec);
        } else {
          r = (x->u.tv.tv_usec == y->u.tv.tv_usec);
        }
      } else {
        QLERR("can't compare");
      }
    default :
      r = (memcmp(&x->u.tv, &y->u.tv, sizeof(sen_timeval)) == 0);
    }
    x = y;
  } while (PAIRP(args) && r);
  return r ? T : F;
}
static sen_obj *
nf_less(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  int r = 1;
  register cell *x, *y;
  POP(x, args);
  if (!PAIRP(args)) { QLERR("Few arguments"); }
  do {
    POP(y, args);
    do_compare(x, y, r, <);
    x = y;
  } while (PAIRP(args) && r);
  return r ? T : F;
}
static sen_obj *
nf_gre(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  int r = 1;
  register cell *x, *y;
  POP(x, args);
  if (!PAIRP(args)) { QLERR("Few arguments"); }
  do {
    POP(y, args);
    do_compare(x, y, r, >);
    x = y;
  } while (PAIRP(args) && r);
  return r ? T : F;
}
static sen_obj *
nf_leq(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  int r = 1;
  register cell *x, *y;
  POP(x, args);
  if (!PAIRP(args)) { QLERR("Few arguments"); }
  do {
    POP(y, args);
    do_compare(x, y, r, <=);
    x = y;
  } while (PAIRP(args) && r);
  return r ? T : F;
}
static sen_obj *
nf_geq(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  int r = 1;
  register cell *x, *y;
  POP(x, args);
  if (!PAIRP(args)) { QLERR("Few arguments"); }
  do {
    POP(y, args);
    do_compare(x, y, r, >=);
    x = y;
  } while (PAIRP(args) && r);
  return r ? T : F;
}
static sen_obj *
nf_symbol(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  s_retbool(SYMBOLP(CAR(args)));
}
static sen_obj *
nf_number(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  s_retbool(NUMBERP(CAR(args)));
}
static sen_obj *
nf_string(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  s_retbool(BULKP(CAR(args)));
}
static sen_obj *
nf_proc(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  /*--
   * continuation should be procedure by the example
   * (call-with-current-continuation procedure?) ==> #t
   * in R^3 report sec. 6.9
   */
  s_retbool(PROCP(CAR(args)) || CLOSUREP(CAR(args)) || CONTINUATIONP(CAR(args)));
}
static sen_obj *
nf_pair(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  s_retbool(PAIRP(CAR(args)));
}
static sen_obj *
nf_eq(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  s_retbool(CAR(args) == CADR(args));
}
static sen_obj *
nf_eqv(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  s_retbool(eqv(CAR(args), CADR(args)));
}
static sen_obj *
nf_write(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  args = CAR(args);
  sen_obj_inspect(ctx, args, &ctx->outbuf, SEN_OBJ_INSPECT_ESC);
  return T;
}
static sen_obj *
nf_display(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  args = CAR(args);
  sen_obj_inspect(ctx, args, &ctx->outbuf, 0);
  return T;
}
static sen_obj *
nf_newline(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  SEN_RBUF_PUTC(&ctx->outbuf, '\n');
  return T;
}
static sen_obj *
nf_reverse(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  return reverse(ctx, CAR(args));
}
static sen_obj *
nf_append(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  return append(ctx, CAR(args), CADR(args));
}
static sen_obj *
nf_gc(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  sen_ctx_mgc(ctx);
  sen_index_expire();
  // gc(NIL, NIL);
  return T;
}
static sen_obj *
nf_gcverb(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  int  was = ctx->gc_verbose;
  ctx->gc_verbose = (CAR(args) != F);
  s_retbool(was);
}
static sen_obj *
nf_nativep(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  s_retbool(NATIVE_FUNCP(CAR(args)));
}
static sen_obj *
nf_length(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  register long v;
  register cell *x;
  for (x = CAR(args), v = 0; PAIRP(x); x = CDR(x)) { ++v; }
  return mk_number(ctx, v);
}
static sen_obj *
nf_assq(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  register cell *x, *y;
  x = CAR(args);
  for (y = CADR(args); PAIRP(y); y = CDR(y)) {
    if (!PAIRP(CAR(y))) {
      QLERR("Unable to handle non pair element");
    }
    if (x == CAAR(y))
      break;
  }
  if (PAIRP(y)) {
    return CAR(y);
  } else {
    return F;
  }
}
static sen_obj *
nf_get_closure(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  args = CAR(args);
  if (args == NIL) {
    return F;
  } else if (CLOSUREP(args)) {
    return CONS(LAMBDA, CLOSURE_CODE(ctx->value));
  } else if (MACROP(args)) {
    return CONS(LAMBDA, CLOSURE_CODE(ctx->value));
  } else {
    return F;
  }
}
static sen_obj *
nf_closurep(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  /*
   * Note, macro object is also a closure.
   * Therefore, (closure? <#MACRO>) ==> #t
   */
  if (CAR(args) == NIL) {
      return F;
  }
  s_retbool(CLOSUREP(CAR(args)));
}
static sen_obj *
nf_macrop(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  if (CAR(args) == NIL) {
      return F;
  }
  s_retbool(MACROP(CAR(args)));
}
static sen_obj *
nf_voidp(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  s_retbool(CAR(args)->type == sen_ql_void);
}
static sen_obj *
nf_list(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  if (PAIRP(args)) {
    return args;
  } else {
    QLERR("Unable to handle non-cons argument");
  }
}
static sen_obj *
nf_batchmode(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  if (CAR(args) == F) {
    ctx->batchmode = 0;
    return F;
  } else {
    ctx->batchmode = 1;
    return T;
  }
}
static sen_obj *
nf_loglevel(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  static sen_logger_info info;
  cell *x = CAR(args);
  if (sen_obj2int(ctx, x)) { QLERR("can't convert into integer"); }
  info.max_level = IVALUE(x);
  info.flags = SEN_LOG_TIME|SEN_LOG_MESSAGE;
  info.func = NULL;
  info.func_arg = NULL;
  return (sen_logger_info_set(&info)) ? F : T;
}
static sen_obj *
nf_now(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  cell *x;
  sen_timeval tv;
  if (sen_timeval_now(&tv)) { QLERR("sysdate failed"); }
  SEN_OBJ_NEW(ctx, x);
  SETTIME(x, &tv);
  return x;
}
static sen_obj *
nf_timestr(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  sen_timeval tv;
  char buf[SEN_TIMEVAL_STR_SIZE];
  cell *x = CAR(args);
  switch (x->type) {
  case sen_ql_bulk :
    if (sen_obj2int(ctx, x)) { QLERR("can't convert into integer"); }
    /* fallthru */
  case sen_ql_int :
    tv.tv_sec = IVALUE(x);
    tv.tv_usec = 0;
    break;
  case sen_ql_float :
    tv.tv_sec = (int32_t) FVALUE(x);
    tv.tv_usec = (int32_t) ((FVALUE(x) - tv.tv_sec) * 1000000);
    break;
  case sen_ql_time :
    memcpy(&tv, &x->u.tv, sizeof(sen_timeval));
    break;
  default :
    QLERR("can't convert into time");
  }
  if (sen_timeval2str(&tv, buf)) { QLERR("timeval2str failed"); }
  return sen_ql_mk_string(ctx, buf, strlen(buf));
}
static sen_obj *
nf_tonumber(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  sen_obj *x, *v;
  if (!PAIRP(args)) { QLERR("list required"); }
  x = CAR(args);
  switch (x->type) {
  case sen_ql_bulk :
    if ((v = str2num(ctx, STRVALUE(x), x->u.b.size)) == NIL) { v = mk_number(ctx, 0); }
    break;
  case sen_ql_int :
  case sen_ql_float :
    v = x;
    break;
  case sen_ql_time :
    {
      double dv= x->u.tv.tv_sec;
      dv += x->u.tv.tv_usec / 1000000.0;
      SEN_OBJ_NEW(ctx, v);
      SETFLOAT(v, dv);
    }
    break;
  default :
    QLERR("can't convert into number");
  }
  return v;
}
static sen_obj *
nf_totime(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  sen_timeval tv;
  sen_obj *x, *v;
  if (!PAIRP(args)) { QLERR("list required"); }
  x = CAR(args);
  switch (x->type) {
  case sen_ql_bulk :
    {
      /*
      if (PAIRP(CDR(args)) && BULKP(CADR(args))) { fmt = STRVALUE(CADR(args)); }
      */
      if (sen_str2timeval(STRVALUE(x), x->u.b.size, &tv)) {
        QLERR("cast error");
      }
      SEN_OBJ_NEW(ctx, v);
      SETTIME(v, &tv);
    }
    break;
  case sen_ql_int :
    tv.tv_sec = (int32_t) IVALUE(x);
    tv.tv_usec = 0;
    SEN_OBJ_NEW(ctx, v);
    SETTIME(v, &tv);
    break;
  case sen_ql_float :
    tv.tv_sec = (int32_t) FVALUE(x);
    tv.tv_usec = (int32_t) ((FVALUE(x) - tv.tv_sec) * 1000000);
    SEN_OBJ_NEW(ctx, v);
    SETTIME(v, &tv);
    break;
  case sen_ql_time :
    v = x;
    break;
  default :
    QLERR("can't convert into number");
  }
  return v;
}
static sen_obj *
nf_substrb(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  sen_obj *str, *s, *e;
  int64_t is, ie;
  if (!PAIRP(args)) { QLERR("list required"); }
  POP(str, args);
  if (!BULKP(str)) { QLERR("string required"); }
  POP(s, args);
  if (!INTP(s)) { QLERR("integer required"); }
  POP(e, args);
  if (!INTP(e)) { QLERR("integer required"); }
  is = IVALUE(s);
  ie = IVALUE(e) + 1;
  if (ie <= 0) {
    ie = str->u.b.size + ie;
    if (ie < 0) { ie = 0; }
  } else if (ie > str->u.b.size) {
    ie = str->u.b.size;
  }
  if (is < 0) {
    is = str->u.b.size + is + 1;
    if (is < 0) { is = 0; }
  } else if (is > str->u.b.size) {
    is = str->u.b.size;
  }
  if (is < ie) {
    return sen_ql_mk_string(ctx, STRVALUE(str) + is, ie - is);
  } else {
    sen_obj *o;
    SEN_OBJ_NEW(ctx, o);
    o->flags = 0;
    o->type = sen_ql_bulk;
    o->u.b.size = 0;
    o->u.b.value = NULL;
    return o;
  }
}

/* ========== Initialization of internal keywords ========== */

inline static void
mk_syntax(sen_ctx *ctx, uint8_t op, char *name)
{
  cell *x;
  if ((x = INTERN(name)) != F) {
    x->type = sen_ql_syntax;
    SYNTAXNUM(x) = op;
  }
}

inline static void
mk_proc(sen_ctx *ctx, uint8_t op, char *name)
{
  cell *x;
  if ((x = INTERN(name)) != F) {
    x->type = sen_ql_proc;
    IVALUE(x) = (int64_t) op;
  }
}

void
sen_ql_init_const(void)
{
  static sen_obj _NIL, _T, _F;
  /* init NIL */
  NIL = &_NIL;
  NIL->type = sen_ql_void;
  CAR(NIL) = CDR(NIL) = NIL;
  /* init T */
  T = &_T;
  T->type = sen_ql_void;
  CAR(T) = CDR(T) = T;
  /* init F */
  F = &_F;
  F->type = sen_ql_void;
  CAR(F) = CDR(F) = F;
}

inline static void
init_vars_global(sen_ctx *ctx)
{
  cell *x;
  /* init global_env */
  ctx->global_env = CONS(NIL, NIL);
  /* init else */
  if ((x = INTERN("else")) != F) {
    CAR(ctx->global_env) = CONS(CONS(x, T), CAR(ctx->global_env));
  }
}

inline static void
init_syntax(sen_ctx *ctx)
{
  /* init syntax */
  mk_syntax(ctx, OP_LAMBDA, "lambda");
  mk_syntax(ctx, OP_QUOTE, "quote");
  mk_syntax(ctx, OP_DEF0, "define");
  mk_syntax(ctx, OP_IF0, "if");
  mk_syntax(ctx, OP_BEGIN, "begin");
  mk_syntax(ctx, OP_SET0, "set!");
  mk_syntax(ctx, OP_LET0, "let");
  mk_syntax(ctx, OP_LET0AST, "let*");
  mk_syntax(ctx, OP_LET0REC, "letrec");
  mk_syntax(ctx, OP_COND0, "cond");
  mk_syntax(ctx, OP_DELAY, "delay");
  mk_syntax(ctx, OP_AND0, "and");
  mk_syntax(ctx, OP_OR0, "or");
  mk_syntax(ctx, OP_C0STREAM, "cons-stream");
  mk_syntax(ctx, OP_0MACRO, "define-macro");
  mk_syntax(ctx, OP_CASE0, "case");
  mk_syntax(ctx, OP_QQUOTE0, "quasiquote");
}

inline static void
init_procs(sen_ctx *ctx)
{
  /* init procedure */
  mk_proc(ctx, OP_PEVAL, "eval");
  mk_proc(ctx, OP_PAPPLY, "apply");
  mk_proc(ctx, OP_CONTINUATION, "call-with-current-continuation");
  mk_proc(ctx, OP_FORCE, "force");
  mk_proc(ctx, OP_SETCAR, "set-car!");
  mk_proc(ctx, OP_SETCDR, "set-cdr!");
  mk_proc(ctx, OP_READ, "read");
  mk_proc(ctx, OP_LOAD, "load");
  mk_proc(ctx, OP_ERR1, "error");
  mk_proc(ctx, OP_PUT, "put");
  mk_proc(ctx, OP_GET, "get");
  mk_proc(ctx, OP_QUIT, "quit");
  mk_proc(ctx, OP_SDOWN, "shutdown");
  sen_ql_def_native_func(ctx, "+", nf_add);
  sen_ql_def_native_func(ctx, "-", nf_sub);
  sen_ql_def_native_func(ctx, "*", nf_mul);
  sen_ql_def_native_func(ctx, "/", nf_div);
  sen_ql_def_native_func(ctx, "remainder", nf_rem);
  sen_ql_def_native_func(ctx, "car", nf_car);
  sen_ql_def_native_func(ctx, "cdr", nf_cdr);
  sen_ql_def_native_func(ctx, "cons", nf_cons);
  sen_ql_def_native_func(ctx, "not", nf_not);
  sen_ql_def_native_func(ctx, "boolean?", nf_bool);
  sen_ql_def_native_func(ctx, "symbol?", nf_symbol);
  sen_ql_def_native_func(ctx, "number?", nf_number);
  sen_ql_def_native_func(ctx, "string?", nf_string);
  sen_ql_def_native_func(ctx, "procedure?", nf_proc);
  sen_ql_def_native_func(ctx, "pair?", nf_pair);
  sen_ql_def_native_func(ctx, "eqv?", nf_eqv);
  sen_ql_def_native_func(ctx, "eq?", nf_eq);
  sen_ql_def_native_func(ctx, "null?", nf_null);
  sen_ql_def_native_func(ctx, "zero?", nf_zerop);
  sen_ql_def_native_func(ctx, "positive?", nf_posp);
  sen_ql_def_native_func(ctx, "negative?", nf_negp);
  sen_ql_def_native_func(ctx, "=", nf_neq);
  sen_ql_def_native_func(ctx, "<", nf_less);
  sen_ql_def_native_func(ctx, ">", nf_gre);
  sen_ql_def_native_func(ctx, "<=", nf_leq);
  sen_ql_def_native_func(ctx, ">=", nf_geq);
  sen_ql_def_native_func(ctx, "write", nf_write);
  sen_ql_def_native_func(ctx, "display", nf_display);
  sen_ql_def_native_func(ctx, "newline", nf_newline);
  sen_ql_def_native_func(ctx, "reverse", nf_reverse);
  sen_ql_def_native_func(ctx, "append", nf_append);
  sen_ql_def_native_func(ctx, "gc", nf_gc);
  sen_ql_def_native_func(ctx, "gc-verbose", nf_gcverb);
  sen_ql_def_native_func(ctx, "native?", nf_nativep);
  sen_ql_def_native_func(ctx, "length", nf_length);  /* a.k */
  sen_ql_def_native_func(ctx, "assq", nf_assq);  /* a.k */
  sen_ql_def_native_func(ctx, "get-closure-code", nf_get_closure);  /* a.k */
  sen_ql_def_native_func(ctx, "closure?", nf_closurep);  /* a.k */
  sen_ql_def_native_func(ctx, "macro?", nf_macrop);  /* a.k */
  sen_ql_def_native_func(ctx, "void?", nf_voidp);
  sen_ql_def_native_func(ctx, "list", nf_list);
  sen_ql_def_native_func(ctx, "batchmode", nf_batchmode);
  sen_ql_def_native_func(ctx, "loglevel", nf_loglevel);
  sen_ql_def_native_func(ctx, "now", nf_now);
  sen_ql_def_native_func(ctx, "timestr", nf_timestr);
  sen_ql_def_native_func(ctx, "x->time", nf_totime);
  sen_ql_def_native_func(ctx, "x->number", nf_tonumber);
  sen_ql_def_native_func(ctx, "substrb", nf_substrb);
}

/* initialize several globals */
void
sen_ql_init_globals(sen_ctx *ctx)
{
  init_vars_global(ctx);
  init_syntax(ctx);
  init_procs(ctx);
  ctx->output = sen_ctx_concat_func;
  /* intialization of global pointers to special symbols */
}
