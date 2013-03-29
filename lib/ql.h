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
#ifndef SEN_QL_H
#define SEN_QL_H

#ifndef SEN_STORE_H
#include "store.h"
#endif /* SEN_STORE_H */

#ifndef SEN_COM_H
#include "com.h"
#endif /* SEN_COM_H */

#ifndef SEN_CTX_H
#include "ctx.h"
#endif /* SEN_CTX_H */

#define __USE_ISOC99
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

/**** query language ****/

#define sen_ql_void          0x10
#define sen_ql_object        0x11
#define sen_ql_records       0x12
#define sen_ql_bulk          0x13
#define sen_ql_int           0x14
#define sen_ql_time          0x15
#define sen_ql_float         0x17
#define sen_ql_snip          0x18
#define sen_ql_query         0x19
#define sen_ql_op            0x1a
#define sen_ql_syntax        0x1b
#define sen_ql_proc          0x1c
#define sen_ql_promise       0x1d
#define sen_ql_closure       0x1e
#define sen_ql_continuation  0x1f
#define sen_ql_index         0x30
#define sen_ql_list          0x40

#define SEN_QL_CO_BEGIN(c) if (c) { switch((c)->last) { case 0:
#define SEN_QL_CO_WAIT(c,d) \
  (c)->last = __LINE__; (c)->data = (d); return NULL;\
  case __LINE__: (d) = (c)->data;
#define SEN_QL_CO_END(c) (c)->last = 0; }}

#define SEN_QL_PLACEHOLDER_CHAR '?'

#define SEN_QL_SET_MODE(c,m) ((c)->feed_mode = (m))
#define SEN_QL_GET_MODE(c) ((c)->feed_mode)
#define SEN_QL_GET_STAT(c) ((c)->stat)

enum {
  sen_ql_atonce = 0,
  sen_ql_step
};

#define SEN_QL_TOPLEVEL  0x00
#define SEN_QL_QUITTING  0x0f
#define SEN_QL_EVAL      0x40
#define SEN_QL_NATIVE    0x41
#define SEN_QL_WAIT_EXPR 0xc0
#define SEN_QL_WAIT_ARG  0xc1
#define SEN_QL_WAIT_DATA 0xc2

#define SEN_QL_WAITINGP(c) ((c)->stat & 0x80)

extern sen_obj *sen_ql_nil;  /* special cell representing empty cell */
extern sen_obj *sen_ql_t;    /* special cell representing #t */
extern sen_obj *sen_ql_f;    /* special cell representing #f */

#define NIL sen_ql_nil
#define T sen_ql_t
#define F sen_ql_f

// FIXME : should be private
#define SEN_OBJ_ALLOCATED 1
#define SEN_OBJ_MARKED 2
#define SEN_OBJ_SYMBOL 4
#define SEN_OBJ_MACRO  8
#define SEN_OBJ_PROMISE 16
#define SEN_OBJ_REFERER 32
#define SEN_OBJ_NATIVE 64
#define SEN_OBJ_FROMJA 128

void sen_ql_init_globals(sen_ctx *c);

sen_obj *sen_ql_at(sen_ctx *c, const char *key);
void sen_ql_def_db_funcs(sen_ctx *c);

#define SEN_OBJ_INSPECT_ESC 1
#define SEN_OBJ_INSPECT_SYM_AS_STR 2

void sen_obj_inspect(sen_ctx *c, sen_obj *obj, sen_rbuf *buf, int flags);
void sen_ql_def_native_func(sen_ctx *c, const char *name,
                                  sen_ql_native_func *func);
const char *_sen_obj_key(sen_ctx *c, sen_obj *obj);
sen_obj *sen_ql_feed(sen_ctx *c, char *str, uint32_t str_size, int mode);
sen_obj *sen_ql_eval(sen_ctx *c, sen_obj *code, sen_obj *objs);
sen_rc sen_obj2int(sen_ctx *c, sen_obj *o);

sen_obj *sen_ql_mk_symbol(sen_ctx *c, const char *name);
sen_obj *sen_ql_mk_obj(sen_ctx *c, sen_id cls, sen_id self);
void sen_ql_bind_symbol(sen_db_store *dbs, sen_obj *symbol);
sen_obj *sen_ql_mk_string(sen_ctx *c, const char *str, unsigned int len);

sen_obj *sen_obj_new(sen_ctx *c);
sen_obj *sen_obj_alloc(sen_ctx *c, uint32_t size);
void sen_obj_clear(sen_ctx *c, sen_obj *o);
sen_obj *sen_obj_cons(sen_ctx *ctx, sen_obj *a, sen_obj *b);
char *sen_obj_copy_bulk_value(sen_ctx *ctx, sen_obj *o);
void sen_ql_init_const(void);

#define SEN_OBJ2VALUE(o,v,s) ((v) = (o)->u.b.value, (s) = (o)->u.b.size)
#define SEN_VALUE2OBJ(o,v,s) ((o)->u.b.value = (v), (o)->u.b.size = (s))

#define VOIDP(c) ((c) == NIL || !(c) || (c)->type == sen_ql_void)
#define OBJECTP(c) ((c)->type == sen_ql_object)
#define RECORDSP(c) ((c)->type == sen_ql_records)
#define SNIPP(c) ((c)->type == sen_ql_snip)
#define BULKP(c) ((c)->type == sen_ql_bulk)
#define PAIRP(c) (((c)->type & sen_ql_list))
#define LISTP(c) (PAIRP(c) || (c) == NIL)
#define INTP(c) ((c)->type == sen_ql_int)
#define CLASSP(c) ((c)->type == sen_db_class)
#define RAW_CLASSP(c) ((c)->type == sen_db_raw_class)
#define NATIVE_FUNCP(c) ((c)->flags & SEN_OBJ_NATIVE)
#define QUERYP(c) ((c)->type == sen_ql_query)
#define OBJ_SLOTP(c) ((c)->type == sen_db_obj_slot)
#define RA_SLOTP(c) ((c)->type == sen_db_ra_slot)
#define JA_SLOTP(c) ((c)->type == sen_db_ja_slot)
#define IDX_SLOTP(c) ((c)->type == sen_db_idx_slot)
#define SLOTP(c) (sen_db_obj_slot <= (c)->type && (c)->type <= sen_db_pslot)
#define OPP(c) ((c)->type == sen_ql_op)
#define STRVALUE(c) ((c)->u.b.value)
#define NUMBERP(c) ((c)->type == sen_ql_int || (c)->type == sen_ql_float)
#define SYMBOLP(c) ((c)->flags & SEN_OBJ_SYMBOL)
#define SYMNAME(c) (SEN_SET_STRKEY_BY_VAL(c))
#define KEYWORDP(c) (*SYMNAME(c) == ':')

#define SETINT(c,v) ((c)->type = sen_ql_int, (c)->u.i.i = (v))
#define SETFLOAT(c,v) ((c)->type = sen_ql_float, (c)->u.d.d = (v))
#define SETBULK(c,v,s) ((c)->type = sen_ql_bulk, (c)->u.b.value = (v), (c)->u.b.size = (s))
#define SETTIME(c,v) ((c)->type = sen_ql_time, memcpy(&(c)->u.tv, v, sizeof(sen_timeval)))

#define CAR(c)          ((c)->u.l.car)
#define CDR(c)          ((c)->u.l.cdr)
#define CAAR(c)         CAR(CAR(c))
#define CADR(c)         CAR(CDR(c))
#define CDAR(c)         CDR(CAR(c))
#define CDDR(c)         CDR(CDR(c))
#define CADDR(c)        CAR(CDDR(c))
#define CADAR(p)        CAR(CDAR(p))
#define CADAAR(p)       CAR(CDR(CAAR(p)))
#define CADDDR(p)       CAR(CDR(CDDR(p)))
#define CDDDDR(p)       CDR(CDR(CDDR(p)))

#define IVALUE(p)       ((p)->u.i.i)
#define FVALUE(p)       ((p)->u.d.d)

#define SEN_OBJ_NEW(ctx,o) do {\
  if (!((o) = sen_obj_new(ctx))) { QLERR("obj_new failed"); }\
} while(0)

#define SEN_RBUF2OBJ(ctx,rbuf,o) do {\
  SEN_RBUF_PUTC((rbuf), '\0');\
  SEN_OBJ_NEW(ctx, (o));\
  (o)->flags = SEN_OBJ_ALLOCATED;\
  (o)->type = sen_ql_bulk;\
  (o)->u.b.value = (rbuf)->head;\
  (o)->u.b.size = SEN_RBUF_VSIZE(rbuf) - 1;\
} while(0)

#define CONS(a,b) (sen_obj_cons(ctx, a, b))

#define INTERN(s) (sen_ql_mk_symbol(ctx, s))

#define POP(x,c) (PAIRP(c) ? ((x) = CAR(c), (c) = CDR(c), (x)) : (x = NIL))

#define SKIPSPACE(c) do {\
  unsigned int len;\
  while ((c)->cur < (c)->str_end && sen_isspace((c)->cur, (c)->encoding)) {\
    if (!(len = sen_str_charlen_nonnull((c)->cur, (c)->str_end, (c)->encoding))) {\
      (c)->cur = (c)->str_end;\
      break;\
    }\
    (c)->cur += len;\
  }\
} while (0)

#ifdef __cplusplus
}
#endif

#endif /* SEN_QL_H */
