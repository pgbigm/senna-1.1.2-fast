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

#include "senna_in.h"
#include <string.h>
#include <ctype.h>
#include "sym.h"
#include "ql.h"
#include "snip.h"

static sen_obj *nf_records(sen_ctx *ctx, sen_obj *args, sen_ql_co *co);
static sen_obj *nf_object(sen_ctx *ctx, sen_obj *args, sen_ql_co *co);
static sen_obj *nf_void(sen_ctx *ctx, sen_obj *args, sen_ql_co *co);
static sen_obj *nf_snip(sen_ctx *ctx, sen_obj *args, sen_ql_co *co);

#define SYM_DO(sym,key,block) do {\
  if (sym->flags & SEN_INDEX_NORMALIZE) {\
    sen_nstr *nstr;\
    if (!(nstr = sen_nstr_open(key, strlen(key), sym->encoding, 0))) {\
      QLERR("nstr open failed");\
    }\
    {\
      char *key = nstr->norm;\
      block\
    }\
    sen_nstr_close(nstr);\
  } else {\
    block\
  }\
} while (0)

#define PVALUE(obj,type) ((type *)((obj)->u.p.value))
#define RVALUE(obj) PVALUE(obj, sen_records)

inline static void
rec_obj_bind(sen_obj *obj, sen_records *rec, sen_id cls)
{
  obj->type = sen_ql_records;
  obj->flags = SEN_OBJ_NATIVE|SEN_OBJ_ALLOCATED;
  obj->class = cls;
  obj->u.p.value = rec;
  obj->u.p.func = nf_records;
}

inline static void
snip_obj_bind(sen_obj *obj, sen_snip *snip)
{
  obj->type = sen_ql_snip;
  obj->flags = SEN_OBJ_NATIVE|SEN_OBJ_ALLOCATED;
  obj->u.p.value = snip;
  obj->u.p.func = nf_snip;
}

inline static void
obj_obj_bind(sen_obj *obj, sen_id cls, sen_id self)
{
  obj->type = sen_ql_object;
  obj->flags = SEN_OBJ_NATIVE;
  obj->class = cls;
  obj->u.o.self = self;
  obj->u.o.func = nf_object;
}

sen_obj *
sen_ql_mk_obj(sen_ctx *ctx, sen_id cls, sen_id self)
{
  sen_obj *o;
  SEN_OBJ_NEW(ctx, o);
  obj_obj_bind(o, cls, self);
  return o;
}

inline static sen_obj *
slot_value_obj(sen_ctx *ctx, sen_db_store *slot, sen_id id, const sen_obj *args, sen_obj *res)
{
  sen_id *ip;
  ip = (VOIDP(args) || (PAIRP(args) && VOIDP(CAR(args))))
    ? sen_ra_at(slot->u.o.ra, id)
    : sen_ra_get(slot->u.o.ra, id);
  if (!ip) { return F; }
  if (!VOIDP(args)) {
    sen_obj *car;
    POP(car, args);
    switch (car->type) {
    case sen_ql_object :
      if (car->class != slot->u.o.class) { return F; }
      *ip = car->u.o.self;
      break;
    case sen_ql_bulk :
      {
        char *name = car->u.b.value;
        sen_db_store *cls = sen_db_store_by_id(slot->db, slot->u.o.class);
        if (!cls) { return F; }
        SYM_DO(cls->u.c.keys, name, { *ip = sen_sym_get(cls->u.c.keys, name); });
      }
      break;
    default :
      if (*ip && VOIDP(car)) {
        sen_db_store *cls;
        if (!(cls = sen_db_store_by_id(slot->db, slot->u.o.class))) { return F; }
        /* todo : use sen_sym_del_with_sis if cls->u.c.keys->flags & SEN_SYM_WITH_SIS */
        /* disable cascade delete */
        // sen_sym_del(cls->u.c.keys, _sen_sym_key(cls->u.c.keys, *ip));
        *ip = SEN_SYM_NIL;
      }
      return F;
      break;
    }
    // todo : trigger
  }
  if (!*ip) { return F; }
  if (!res) { SEN_OBJ_NEW(ctx, res); }
  obj_obj_bind(res, slot->u.o.class, *ip);
  return res;
}

#define STR2DBL(str,len,val) do {\
  char *end, buf0[128], *buf = (len) < 128 ? buf0 : SEN_MALLOC((len) + 1);\
  if (buf) {\
    double d;\
    memcpy(buf, (str), (len));\
    buf[len] = '\0';\
    errno = 0;\
    d = strtod(buf, &end);\
    if (!((len) < 128)) { SEN_FREE(buf); }\
    if (!errno && buf + (len) == end) {\
      (val) = d;\
    } else { QLERR("cast failed"); }\
  } else { QLERR("buf alloc failed"); }\
} while (0)

inline static sen_obj *
slot_value_ra(sen_ctx *ctx, sen_db_store *slot, sen_id id, const sen_obj *args, sen_obj *res)
{
  void *vp;
  vp = (VOIDP(args) || (PAIRP(args) && VOIDP(CAR(args))))
    ? sen_ra_at(slot->u.f.ra, id)
    : sen_ra_get(slot->u.f.ra, id);
  if (!vp) { return F; }
  if (!VOIDP(args)) {
    sen_obj *car;
    POP(car, args);
    switch (car->type) {
    case sen_ql_bulk :
      switch (slot->u.f.class) {
      case 1 : /* <int> */
        {
          int64_t iv = sen_atoll(STRVALUE(car), STRVALUE(car) + car->u.b.size, NULL);
          *(int32_t *)vp = (int32_t) iv;
        }
        break;
      case 2 : /* <uint> */
        {
          int64_t iv = sen_atoll(STRVALUE(car), STRVALUE(car) + car->u.b.size, NULL);
          *(uint32_t *)vp = (uint32_t) iv;
        }
        break;
      case 3 : /* <int64> */
        {
          int64_t iv = sen_atoll(STRVALUE(car), STRVALUE(car) + car->u.b.size, NULL);
          *(int64_t *)vp = iv;
        }
        break;
      case 4 : /* <float> */
        { /* todo : support #i notation */
          char *str = STRVALUE(car);
          int len = car->u.b.size;
          STR2DBL(str, len, *(double *)vp);
        }
        break;
      case 8 : /* <time> */
        {
          sen_timeval tv;
          if (!sen_str2timeval(STRVALUE(car), car->u.b.size, &tv)) {
            memcpy(vp, &tv, sizeof(sen_timeval));
          } else {
            double dval;
            char *str = STRVALUE(car);
            int len = car->u.b.size;
            STR2DBL(str, len, dval);
            tv.tv_sec = (int32_t) dval;
            tv.tv_usec = (int32_t) ((dval - tv.tv_sec) * 1000000);
            memcpy(vp, &tv, sizeof(sen_timeval));
          }
        }
        break;
      default :
        if (car->u.b.size != slot->u.f.ra->header->element_size) { return F; }
        memcpy(vp, car->u.b.value, car->u.b.size);
      }
      break;
    case sen_ql_int :
      switch (slot->u.f.class) {
      case 1 : /* <int> */
        *(int32_t *)vp = (int32_t) IVALUE(car);
        break;
      case 2 : /* <uint> */
        *(uint32_t *)vp = (uint32_t) IVALUE(car);
        break;
      case 3 : /* <int64> */
        *(int64_t *)vp = IVALUE(car);
        break;
      case 4 : /* <float> */
        *(double *)vp = (double) IVALUE(car);
        break;
      case 8 : /* <time> */
        {
          sen_timeval tv;
          tv.tv_sec = (int32_t) IVALUE(car);
          tv.tv_usec = 0;
          memcpy(vp, &tv, sizeof(sen_timeval));
        }
        break;
      default :
        if (slot->u.f.ra->header->element_size > sizeof(int64_t)) { return F; }
        memcpy(vp, &IVALUE(car), slot->u.f.ra->header->element_size);
        break;
      }
      break;
    case sen_ql_float :
      switch (slot->u.f.class) {
      case 1 : /* <int> */
        *(int32_t *)vp = (int32_t) FVALUE(car);
        break;
      case 2 : /* <uint> */
        *(uint32_t *)vp = (uint32_t) FVALUE(car);
        break;
      case 3 : /* <int64> */
        *(int64_t *)vp = (int64_t) FVALUE(car);
        break;
      case 4 : /* <float> */
        *(double *)vp = FVALUE(car);
        break;
      case 8 : /* <time> */
        {
          sen_timeval tv;
          tv.tv_sec = (int32_t) FVALUE(car);
          tv.tv_usec = (int32_t) ((FVALUE(car) - tv.tv_sec) * 1000000);
          memcpy(vp, &tv, sizeof(sen_timeval));
        }
        break;
      default :
        return F;
      }
      break;
    case sen_ql_time :
      switch (slot->u.f.class) {
      case 1 : /* <int> */
        *(int32_t *)vp = (int32_t) car->u.tv.tv_usec;
        break;
      case 2 : /* <uint> */
        *(uint32_t *)vp = (uint32_t) car->u.tv.tv_usec;
        break;
      case 3 : /* <int64> */
        *(int64_t *)vp = (int64_t) car->u.tv.tv_usec;
        break;
      case 4 : /* <float> */
        *(double *)vp = ((double) car->u.tv.tv_usec) / 1000000 + car->u.tv.tv_sec;
        break;
      case 8 : /* <time> */
        memcpy(vp, &car->u.tv, sizeof(sen_timeval));
        break;
      default :
        return F;
      }
      break;
    default :
      if (VOIDP(car)) {
        memset(vp, 0, slot->u.f.ra->header->element_size);
      }
      return F;
    }
  // todo : trigger
  }
  if (!res) { SEN_OBJ_NEW(ctx, res); }
  switch (slot->u.f.class) {
  case 1 : /* <int> */
    SETINT(res, *(int32_t *)vp);
    break;
  case 2 : /* <uint> */
    SETINT(res, *(uint32_t *)vp);
    break;
  case 3 : /* <int64> */
    SETINT(res, *(int64_t *)vp);
    break;
  case 4 : /* <float> */
    SETFLOAT(res, *(double *)vp);
    break;
  case 8 : /* <time> */
    SETTIME(res, vp);
    break;
  default :
    res->type = sen_ql_bulk;
    res->u.b.size = slot->u.f.ra->header->element_size;
    res->u.b.value = vp;
  }
  return res;
}

inline static sen_obj *
slot_value_ja(sen_ctx *ctx, sen_db_store *slot, sen_id id, const sen_obj *args, sen_obj *res)
{
  void *vp;
  uint32_t vs;
  vp = (void *)sen_ja_ref(slot->u.v.ja, id, &vs);
  // todo : unref
  if (VOIDP(args)) {
    if (!vp) { return F; }
    if (!res) { SEN_OBJ_NEW(ctx, res); }
    res->flags = SEN_OBJ_ALLOCATED|SEN_OBJ_FROMJA;
    res->type = sen_ql_bulk;
    res->u.b.size = vs;
    res->u.b.value = vp;
    return res;
  } else {
    sen_db_trigger *t;
    char *nvp;
    uint32_t nvs;
    sen_obj *car;
    POP(car, args);
    // todo : support append and so on..
    if (BULKP(car)) {
      unsigned int max_element_size;
      nvs = car->u.b.size;
      nvp = car->u.b.value;
      if (sen_ja_info(slot->u.v.ja, &max_element_size) ||
          nvs > max_element_size) {
        QLERR("too long value(%d) > max_element_size(%d)", nvs, max_element_size);
      }
    } else if (VOIDP(car)) {
      nvs = 0;
      nvp = NULL;
    } else {
      if (vp) { sen_ja_unref(slot->u.v.ja, id, vp, vs); }
      return F;
    }
    if (vs == nvs && (!vs || (vp && nvp && !memcmp(vp, nvp, vs)))) {
      if (vp) { sen_ja_unref(slot->u.v.ja, id, vp, vs); }
      return car;
    }
    for (t = slot->triggers; t; t = t->next) {
      if (t->type == sen_db_before_update_trigger) {
        sen_db_store *index = sen_db_store_by_id(slot->db, t->target);
        const char *key = _sen_sym_key(index->u.i.index->keys, id);
        if (key && sen_index_upd(index->u.i.index, key, vp, vs, nvp, nvs)) {
          SEN_LOG(sen_log_error, "sen_index_upd failed. id=%d key=(%s) id'=%d", id, _sen_sym_key(index->u.i.index->keys, id), sen_sym_at(index->u.i.index->keys, _sen_sym_key(index->u.i.index->keys, id)));
        }
      }
    }
    if (vp) { sen_ja_unref(slot->u.v.ja, id, vp, vs); }
    return sen_ja_put(slot->u.v.ja, id, nvp, nvs, 0) ? F : car;
  }
}

inline static sen_obj *
slot_value(sen_ctx *ctx, sen_db_store *slot, sen_id obj, sen_obj *args, sen_obj *res)
{
  switch (slot->type) {
  case sen_db_obj_slot :
    return slot_value_obj(ctx, slot, obj, args, res);
    break;
  case sen_db_ra_slot :
    return slot_value_ra(ctx, slot, obj, args, res);
    break;
  case sen_db_ja_slot :
    return slot_value_ja(ctx, slot, obj, args, res);
    break;
  case sen_db_idx_slot :
    {
      sen_records *rec;
      const char *key = _sen_sym_key(slot->u.i.index->lexicon, obj);
      if (!key) { return F; }
      if (!(rec = sen_index_sel(slot->u.i.index, key, strlen(key)))) {
        return F;
      }
      if (!res) { SEN_OBJ_NEW(ctx, res); }
      rec_obj_bind(res, rec, slot->u.i.class);
      return res;
    }
    break;
  default :
    return F;
    break;
  }
}

inline static sen_obj *
int2strobj(sen_ctx *ctx, int64_t i)
{
  char buf[32], *rest;
  if (sen_str_lltoa(i, buf, buf + 32, &rest)) { return NULL; }
  return sen_ql_mk_string(ctx, buf, rest - buf);
}

inline static char *
str_value(sen_ctx *ctx, sen_obj *o)
{
  if (o->flags & SEN_OBJ_SYMBOL) {
    char *r = SEN_SET_STRKEY_BY_VAL(o);
    return *r == ':' ? r + 1 : r;
  } else if (o->type == sen_ql_bulk) {
    return o->u.b.value;
  } else if (o->type == sen_ql_int) {
    sen_obj *p = int2strobj(ctx, IVALUE(o));
    return p ? p->u.b.value : NULL;
  }
  return NULL;
}

inline static sen_obj *
obj2oid(sen_ctx *ctx, sen_obj *obj, sen_obj *res)
{
  char buf[32];
  sen_rbuf bogus_buf = { /*.head = */buf, /*.curr = */buf, /*.tail = */buf + 32 };
  if (obj->type != sen_ql_object) { return F; }
  sen_obj_inspect(ctx, obj, &bogus_buf, SEN_OBJ_INSPECT_ESC);
  if (res) {
    uint32_t size = SEN_RBUF_VSIZE(&bogus_buf);
    char *value = SEN_MALLOC(size + 1);
    if (!value) { return F; }
    sen_obj_clear(ctx, res);
    res->flags = SEN_OBJ_ALLOCATED;
    res->type = sen_ql_bulk;
    res->u.b.size = size;
    res->u.b.value = value;
    memcpy(res->u.b.value, buf, res->u.b.size + 1);
  } else {
    if (!(res = sen_ql_mk_string(ctx, buf, SEN_RBUF_VSIZE(&bogus_buf)))) { return F; }
  }
  return res;
}

#define SET_KEY_VALUE(ctx,v_,o_) do {\
  const char *key;\
  if (o_->class) {\
    sen_db_store *cls = sen_db_store_by_id(ctx->db, o_->class);\
    if (!cls) { QLERR("Invalid Object"); }\
    switch (cls->type) {\
    case sen_db_class :\
      if (!(key = _sen_sym_key(cls->u.c.keys, o_->u.o.self))) { QLERR("Invalid Object"); }\
      v_->flags = 0;\
      v_->type = sen_ql_bulk;\
      v_->u.b.value = (char *)key;\
      v_->u.b.size = strlen(key);\
      break;\
    case sen_db_rel1 :\
      v_->u.i.i = o_->u.o.self;\
      v_->type = sen_ql_int;\
      break;\
    default :\
      break;\
    }\
  } else {\
    if (!(key = _sen_sym_key(ctx->db->keys, o_->u.o.self))) { QLERR("Invalid Object"); }\
    v_->flags = 0;\
    v_->type = sen_ql_bulk;\
    v_->u.b.value = (char *)key;\
    v_->u.b.size = strlen(key);\
  }\
} while(0)

// from index.c
typedef struct {
  int score;
  sen_id subid;
} subrec;

typedef struct {
  int score;
  int n_subrecs;
  subrec subrecs[1];
} recinfo;

typedef struct {
  sen_id rid;
  uint32_t sid;
  uint32_t pos;
} posinfo;

static sen_obj *
nf_object(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  char *msg;
  sen_db_store *slot;
  sen_obj *obj, *car, *res;
  if (!(obj = res = ctx->code)) { QLERR("invalid receiver"); }
  POP(car, args);
  if (!(msg = str_value(ctx, car))) { QLERR("invalid message"); }
  if (*msg == ':') {
    switch (msg[1]) {
    case 'i' : /* :id */
    case 'I' :
      res = obj2oid(ctx, obj, NULL);
      break;
    case 'k' : /* :key */
    case 'K' :
      SEN_OBJ_NEW(ctx, res);
      SET_KEY_VALUE(ctx, res, obj);
      break;
    case 'S' : /* :score */
    case 's' :
      if (ctx->currec) {
        SEN_OBJ_NEW(ctx, res);
        (res)->type = sen_ql_int;
        (res)->u.i.i = ((recinfo *)(ctx->currec))->score;
      } else {
        res = F;
      }
      break;
    case 'N' : /* :nsubrecs */
    case 'n' :
      if (ctx->currec) {
        SEN_OBJ_NEW(ctx, res);
        (res)->type = sen_ql_int;
        (res)->u.i.i = ((recinfo *)(ctx->currec))->n_subrecs;
      } else {
        res = F;
      }
      break;
    }
  } else {
    if (!(slot = sen_db_class_slot(ctx->db, obj->class, msg))) {
      QLERR("Invalid slot %s", msg);
    }
    if (VOIDP(args)) {
      res = slot_value(ctx, slot, obj->u.o.self, args, NULL);
    } else {
      if (sen_db_lock(ctx->db, -1)) {
        SEN_LOG(sen_log_crit, "clear_all_slot_values: lock failed");
      } else {
        res = slot_value(ctx, slot, obj->u.o.self, args, NULL);
        sen_db_unlock(ctx->db);
      }
    }
  }
  return res;
}

sen_obj *
sen_ql_class_at(sen_ctx *ctx, sen_db_store *cls, const void *key, int flags, sen_obj *res)
{
  sen_id id;
  SYM_DO(cls->u.c.keys, key, {
    id = flags ? sen_sym_get(cls->u.c.keys, key) : sen_sym_at(cls->u.c.keys, key);
  });
  if (id) {
    if (!res) { SEN_OBJ_NEW(ctx, res); }
    obj_obj_bind(res, cls->id, id);
    return res;
  } else {
    return F;
  }
}

static sen_obj *
nf_void(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  if (!ctx->code) { return F; }
  return ctx->code;
}

#define DEF_COMPAR_FUNC(funcname,expr) \
static int funcname(sen_records *ra, const sen_recordh *a, sen_records *rb, const sen_recordh *b, void *arg)\
{\
  void *va, *vb;\
  sen_id *pa, *pb;\
  sen_ra *raa = (sen_ra *)ra->userdata, *rab = (sen_ra *)rb->userdata;\
  sen_set_element_info(ra->records, a, (void **)&pa, NULL);\
  sen_set_element_info(rb->records, b, (void **)&pb, NULL);\
  va = sen_ra_at(raa, *pa);\
  vb = sen_ra_at(rab, *pb);\
  if (va) {\
    if (vb) {\
      return expr;\
    } else {\
      return 1;\
    }\
  } else {\
    return vb ? -1 : 0;\
  }\
}

DEF_COMPAR_FUNC(compar_ra, (memcmp(va, vb, raa->header->element_size)));
DEF_COMPAR_FUNC(compar_int, (*((int32_t *)va) - *((int32_t *)vb)));
DEF_COMPAR_FUNC(compar_uint, (*((uint32_t *)va) - *((uint32_t *)vb)));
DEF_COMPAR_FUNC(compar_int64, (*((int64_t *)va) - *((int64_t *)vb)));
DEF_COMPAR_FUNC(compar_float,
 (isgreater(*((double *)va), *((double *)vb)) ? 1 :
  (isless(*((double *)va), *((double *)vb)) ? -1 : 0)));
DEF_COMPAR_FUNC(compar_time,
 ((((sen_timeval *)va)->tv_sec != ((sen_timeval *)vb)->tv_sec) ?
  (((sen_timeval *)va)->tv_sec - ((sen_timeval *)vb)->tv_sec) :
  (((sen_timeval *)va)->tv_usec - ((sen_timeval *)vb)->tv_usec)));

static int
compar_ja(sen_records *ra, const sen_recordh *a, sen_records *rb, const sen_recordh *b, void *arg)
{
  int r;
  void *va, *vb;
  uint32_t la, lb;
  sen_id *pa, *pb;
  sen_ja *jaa = (sen_ja *)ra->userdata, *jab = (sen_ja *)rb->userdata;
  sen_set_element_info(ra->records, a, (void **)&pa, NULL);
  sen_set_element_info(rb->records, b, (void **)&pb, NULL);
  va = sen_ja_ref(jaa, *pa, &la);
  vb = sen_ja_ref(jab, *pb, &lb);
  if (va) {
    if (vb) {
      if (la > lb) {
        if (!(r = memcmp(va, vb, lb))) { r = 1; }
      } else {
        if (!(r = memcmp(va, vb, la))) { r = la == lb ? 0 : -1; }
      }
      sen_ja_unref(jab, *pb, vb, lb);
    } else {
      r = 1;
    }
    sen_ja_unref(jaa, *pa, va, la);
  } else {
    if (vb) {
      sen_ja_unref(jab, *pb, vb, lb);
      r = -1;
    } else {
      r = 0;
    }
  }
  return r;
}

static int
compar_key(sen_records *ra, const sen_recordh *a, sen_records *rb, const sen_recordh *b, void *arg)
{
  const char *va, *vb;
  sen_id *pa, *pb;
  sen_sym *ka = ra->userdata, *kb = rb->userdata;
  sen_set_element_info(ra->records, a, (void **)&pa, NULL);
  sen_set_element_info(rb->records, b, (void **)&pb, NULL);
  va = _sen_sym_key(ka, *pa);
  vb = _sen_sym_key(kb, *pb);
  // todo : if (key_size)..
  if (va) {
    return vb ? strcmp(va, vb) : 1;
  } else {
    return vb ? -1 : 0;
  }
}

static sen_obj sen_db_pslot_key = {
  sen_db_pslot, SEN_OBJ_NATIVE, 0, 0, { { SEN_DB_PSLOT_FLAG, NULL } }
};

static sen_obj sen_db_pslot_id = {
  sen_db_pslot, SEN_OBJ_NATIVE, 0, 0, { { SEN_DB_PSLOT_FLAG|SEN_DB_PSLOT_ID, NULL } }
};
static sen_obj sen_db_pslot_score = {
  sen_db_pslot, SEN_OBJ_NATIVE, 0, 0, { { SEN_DB_PSLOT_FLAG|SEN_DB_PSLOT_SCORE, NULL } }
};
static sen_obj sen_db_pslot_nsubrecs = {
  sen_db_pslot, SEN_OBJ_NATIVE, 0, 0, { { SEN_DB_PSLOT_FLAG|SEN_DB_PSLOT_NSUBRECS, NULL } }
};

inline static sen_obj *
class_slot(sen_ctx *ctx, sen_id base, char *msg, sen_records *records, int *recpslotp)
{
  *recpslotp = 0;
  if (*msg == ':') {
    switch (msg[1]) {
    case 'i' : /* :id */
    case 'I' :
      return &sen_db_pslot_id;
    case 'K' : /* :key */
    case 'k' :
      return &sen_db_pslot_key;
    case 'S' : /* :score */
    case 's' :
      if (records) {
        *recpslotp = 1;
        return &sen_db_pslot_score;
      }
      return F;
    case 'N' : /* :nsubrecs */
    case 'n' :
      if (records) {
        *recpslotp = 1;
        return &sen_db_pslot_nsubrecs;
      }
      return F;
    default :
      return F;
    }
  } else {
    sen_db_store *slot;
    char buf[SEN_SYM_MAX_KEY_SIZE];
    if (sen_db_class_slotpath(ctx->db, base, msg, buf)) {
      QLERR("Invalid slot %s", msg);
    }
    if (!(slot = sen_db_store_open(ctx->db, buf))) {
      QLERR("store open failed %s", buf);
    }
    return INTERN(buf);
  }
}

static sen_obj *
slotexp_prepare(sen_ctx *ctx, sen_id base, sen_obj *e, sen_records *records)
{
  char *str;
  const char *key;
  int recpslotp;
  sen_obj *slot, *r;
  if (PAIRP(e)) {
    for (r = NIL; PAIRP(CAR(e)); e = CAR(e)) {
      if (PAIRP(CDR(e))) { r = CONS(CDR(e), r); }
    }
    if (CAR(e) == NIL) {
      e = CDR(e);
    } else {
      if (CDR(e) != NIL) { QLERR("invalid slot expression"); }
    }
    if (e == NIL) {
      r = CONS(CONS(T, NIL), r);
      goto exit;
    }
    if (!(str = str_value(ctx, CAR(e)))) {
      QLERR("invalid slotname");
    }
    if (*str == '\0') {
      if (!records) {
        QLERR(" ':' assigned without records");
      }
      base = records->subrec_id;
      if (!(key = _sen_sym_key(ctx->db->keys, base))) { QLERR("invalid base class"); }
      slot = INTERN(key);
      if (!CLASSP(slot)) { QLERR("invalid class"); }
      r = CONS(CONS(slot, CDR(e)), r);
    } else {
      if ((slot = class_slot(ctx, base, str, records, &recpslotp)) == F) {
        QLERR("invalid slot");
      }
      if (recpslotp) { r = slot; goto exit; }
      r = CONS(CONS(slot, CDR(e)), r);
      base = slot->class;
    }
    for (e = CDR(r); PAIRP(e); e = CDR(e)) {
      if (!(str = str_value(ctx, CAAR(e))) ||
          (slot = class_slot(ctx, base, str, records, &recpslotp)) == F) {
        QLERR("invalid slot");
      }
      if (recpslotp) { r = slot; goto exit; }
      e->u.l.car = CONS(slot, CDAR(e));
      base = slot->class;
    }
  } else {
    if (!(str = str_value(ctx, e))) {
      QLERR("invalid expr");
    }
    r = class_slot(ctx, base, str, records, &recpslotp);
  }
exit :
  return r;
}

/* SET_SLOT_VALUE doesn't update slot value */
#define SET_SLOT_VALUE(ctx,slot,value,args,ri) do {\
  sen_id id = (slot)->u.o.self;\
  if (id & SEN_DB_PSLOT_FLAG) {\
    uint8_t pslot_type = id & SEN_DB_PSLOT_MASK;\
    switch (pslot_type) {\
    case 0 : /* SEN_DB_PSLOT_KEY */\
      SET_KEY_VALUE((ctx), (value), (value));\
      break;\
    case SEN_DB_PSLOT_ID :\
      obj2oid((ctx), (value), (value));\
      break;\
    case SEN_DB_PSLOT_SCORE :\
      (value)->type = sen_ql_int;\
      (value)->u.i.i = (ri)->score;\
      break;\
    case SEN_DB_PSLOT_NSUBRECS :\
      (value)->type = sen_ql_int;\
      (value)->u.i.i = (ri)->n_subrecs;\
      break;\
    }\
  } else {\
    sen_db_store *dbs = sen_db_store_by_id((ctx)->db, id);\
    value = slot_value((ctx), dbs, (value)->u.o.self, /*(args)*/ NIL, (value));\
  }\
} while(0)

static sen_obj *
slotexp_exec(sen_ctx *ctx, sen_obj *expr, sen_obj *value, recinfo *ri)
{
  sen_obj *t, *car;
  if (PAIRP(expr)) {
    POP(t, expr);
    car = CAR(t);
    if (CLASSP(car)) {
      int i = 0;
      if (INTP(CADR(t))) { i = CADR(t)->u.i.i; }
      obj_obj_bind(value, car->u.o.self, ri->subrecs[i].subid);
    } else {
      SET_SLOT_VALUE(ctx, car, value, CDR(t), ri);
    }
  } else if (SLOTP(expr)) {
    SET_SLOT_VALUE(ctx, expr, value, NIL, ri);
  }
  while (value != NIL && PAIRP(expr)) {
    POP(t, expr);
    if (!PAIRP(t)) { break; }
    car = CAR(t);
    SET_SLOT_VALUE(ctx, car, value, CDR(t), ri);
  }
  return value;
}

static void
ses_check(sen_obj *e, int *ns, int *ne)
{
  if (PAIRP(e)) {
    sen_obj *x;
    POP(x, e);
    if (x == NIL) {
      (*ns)++;
    } else {
      ses_check(x, ns, ne);
    }
    while (PAIRP(e)) {
      POP(x, e);
      ses_check(x, ns, ne);
    }
  } else {
    if (SYMBOLP(e) && !KEYWORDP(e)) { (*ne)++; }
  }
}

static sen_obj *
ses_copy(sen_ctx *ctx, sen_obj *e)
{
  if (PAIRP(e)) {
    sen_obj *x, *r, **d;
    POP(x, e);
    r = CONS(x == NIL ? &ctx->curobj : ses_copy(ctx, x), NIL);
    d = &CDR(r);
    while (PAIRP(e)) {
      POP(x, e);
      *d = CONS(ses_copy(ctx, x), NIL);
      d = &CDR(*d);
    }
    return r;
  } else {
    return e;
  }
}

static sen_obj *
ses_prepare(sen_ctx *ctx, sen_id base, sen_obj *e, sen_records *records)
{
  int ns = 0, ne = 0;
  ses_check(e, &ns, &ne);
  if (ne) {
    obj_obj_bind(&ctx->curobj, base, 0);
    return CONS(T, ns ? ses_copy(ctx, e) : e);
  } else {
    if (ns) {
      return CONS(NIL, slotexp_prepare(ctx, base, e, records));
    } else {
      return CONS(F, e);
    }
  }
}

static sen_obj *
ses_exec(sen_ctx *ctx, sen_obj *e, recinfo *ri, sen_obj *objs)
{
  sen_obj *x = CAR(e);
  if (x == T) {
    ctx->currec = ri;
    return sen_ql_eval(ctx, CDR(e), objs);
  } else if (x == F) {
    return CDR(e);
  } else {
    return slotexp_exec(ctx, CDR(e), &ctx->curobj, ri);
  }
}

static void
ses_clear(sen_ctx *ctx)
{
  sen_obj_clear(ctx, &ctx->curobj);
}

typedef struct {
  sen_id base;
  sen_obj *se;
} compar_expr_userdata;

static int
compar_expr(sen_records *ra, const sen_recordh *a, sen_records *rb, const sen_recordh *b, void *arg)
{
  int r;
  sen_obj oa, ob, *va, *vb;
  sen_id *pa, *pb;
  recinfo *ria, *rib;
  sen_ctx *ctx = (sen_ctx *) arg;
  compar_expr_userdata *ceuda = (compar_expr_userdata *)ra->userdata;
  compar_expr_userdata *ceudb = (compar_expr_userdata *)rb->userdata;
  sen_obj *exa = ceuda->se, *exb = ceudb->se;
  sen_set_element_info(ra->records, a, (void **)&pa, (void **)&ria);
  sen_set_element_info(rb->records, b, (void **)&pb, (void **)&rib);
  /*
  oa.u.o.self = *pa;
  ob.u.o.self = *pb;
  va = slotexp_exec(ctx, exa, &oa, ria);
  vb = slotexp_exec(ctx, exb, &ob, rib);
  */
  obj_obj_bind(&ctx->curobj, ceuda->base, *pa);
  va = ses_exec(ctx, exa, ria, exa);
  if (va != NIL) { memcpy(&oa, va, sizeof(sen_obj)); va = &oa; }
  obj_obj_bind(&ctx->curobj, ceudb->base, *pb);
  vb = ses_exec(ctx, exa, rib, exb);
  if (vb != NIL) { memcpy(&ob, vb, sizeof(sen_obj)); vb = &ob; }

  if (va == NIL) {
    r = (vb == NIL) ? 0 : -1;
  } else if (vb == NIL) {
    r = 1;
  } else {
    if (va->type != vb->type) {
      SEN_LOG(sen_log_error, "obj type unmatch in compar_expr");
      r = 0;
    } else {
      switch (va->type) {
      case sen_ql_object :
        {
          sen_db_store *ca, *cb;
          if (!(ca = sen_db_store_by_id(ctx->db, va->class)) ||
               (cb = sen_db_store_by_id(ctx->db, vb->class))) {
            SEN_LOG(sen_log_error, "clas open failed in compar_expr");
            r = 0;
          } else {
            const char *ka = _sen_sym_key(ca->u.c.keys, va->u.o.self);
            const char *kb = _sen_sym_key(cb->u.c.keys, vb->u.o.self);
            r = (ka && kb) ? strcmp(ka, kb) : 0;
          }
        }
        break;
      case sen_ql_bulk :
        {
          uint32_t la = va->u.b.size, lb = vb->u.b.size;
          if (la > lb) {
            if (!(r = memcmp(va->u.b.value, vb->u.b.value, lb))) { r = 1; }
          } else {
            if (!(r = memcmp(va->u.b.value, vb->u.b.value, la))) { r = la == lb ? 0 : -1; }
          }
        }
        break;
      case sen_ql_int :
        r = IVALUE(va) - IVALUE(vb);
        break;
      case sen_ql_float :
        if (isgreater(FVALUE(va), FVALUE(vb))) {
          r = 1;
        } else {
          r = (isless(FVALUE(va), FVALUE(vb))) ? -1 : 0;
        }
        break;
      case sen_ql_time :
        if (va->u.tv.tv_sec != vb->u.tv.tv_sec) {
          r = va->u.tv.tv_sec - vb->u.tv.tv_sec;
        } else {
          r = va->u.tv.tv_usec - vb->u.tv.tv_usec;
        }
        break;
      default :
        SEN_LOG(sen_log_error, "invalid value in compar_expr");
        r = 0;
        break;
      }
    }
  }
  sen_obj_clear(ctx, va);
  sen_obj_clear(ctx, vb);
  return r;
}

static int
compar_obj(sen_records *ra, const sen_recordh *a, sen_records *rb, const sen_recordh *b, void *arg)
{
  const char *va, *vb;
  sen_id *pa, *pb, *oa, *ob;
  sen_sym *key = (sen_sym *)arg;
  // todo : target class may not be identical
  sen_ra *raa = (sen_ra *)ra->userdata, *rab = (sen_ra *)rb->userdata;
  sen_set_element_info(ra->records, a, (void **)&pa, NULL);
  sen_set_element_info(rb->records, b, (void **)&pb, NULL);
  va = (oa = sen_ra_at(raa, *pa)) ? _sen_sym_key(key, *oa) : NULL;
  vb = (ob = sen_ra_at(rab, *pb)) ? _sen_sym_key(key, *ob) : NULL;
  // todo : if (key_size)..
  if (va) {
    return vb ? strcmp(va, vb) : 1;
  } else {
    return vb ? -1 : 0;
  }
}

static int
group_obj(sen_records *ra, const sen_recordh *a, void *gkey, void *arg)
{
  sen_id *pa, *oa;
  sen_ra *raa = (sen_ra *)ra->userdata;
  sen_set_element_info(ra->records, a, (void **)&pa, NULL);
  if (!(oa = sen_ra_at(raa, *pa))) { return 1; }
  memcpy(gkey, oa, sizeof(sen_id));
  return 0;
}

inline static sen_obj *
rec_obj_new(sen_ctx *ctx, sen_db_store *cls, sen_rec_unit record_unit,
            sen_rec_unit subrec_unit, unsigned int max_n_subrecs)
{
  sen_records *r;
  sen_obj *res;
  if (!(r = sen_records_open(record_unit, subrec_unit, max_n_subrecs))) {
    QLERR("sen_records_open failed");
  }
  if (cls) {
    r->keys = cls->u.c.keys;
    SEN_OBJ_NEW(ctx, res);
    rec_obj_bind(res, r, cls->id);
  } else {
    r->keys = ctx->db->keys;
    SEN_OBJ_NEW(ctx, res);
    rec_obj_bind(res, r, 0);
  }
  return res;
}

typedef struct {
  sen_ql_native_func *func;
  sen_obj *exprs;
  sen_obj *args;
  sen_sel_operator op;
  sen_obj *objs;
} match_spec;

inline static int
slotexpp(sen_obj *expr)
{
  while (PAIRP(expr)) { expr = CAR(expr); }
  return expr == NIL;
}

inline static sen_obj*
match_prepare(sen_ctx *ctx, match_spec *spec, sen_id base, sen_obj *args)
{
  int ns = 0, ne = 0;
  sen_obj *car, *expr, **ap = &spec->args, **ep = &spec->exprs;
  POP(expr, args);
  ses_check(expr, &ns, &ne);
  if (ne == 1 && PAIRP(expr) && NATIVE_FUNCP(CAR(expr))) {
    POP(car, expr);
    spec->func = car->u.o.func;
    for (*ap = NIL, *ep = NIL; POP(car, expr) != NIL; ap = &CDR(*ap)) {
      sen_obj *v;
      if (slotexpp(car)) {
        v = slotexp_prepare(ctx, base, car, NULL);
        if (ERRP(ctx, SEN_WARN)) { return F; }
        *ep = CONS(v, NIL);
        if (ERRP(ctx, SEN_WARN)) { return F; }
        ep = &CDR(*ep);
        v = sen_obj_new(ctx);
        *ep = CONS(v, NIL);
        if (ERRP(ctx, SEN_WARN)) { return F; }
        ep = &CDR(*ep);
      } else {
        v = car;
      }
      *ap = CONS(v, NIL);
      if (ERRP(ctx, SEN_WARN)) { return F; }
    }
  } else {
    spec->func = NULL;
    spec->exprs = ses_prepare(ctx, base, expr, NULL);
  }
  POP(expr, args);
  if (RECORDSP(expr)) {
    char *ops;
    if (expr->class != base) { QLERR("class unmatch"); }
    POP(car, args);
    spec->op = sen_sel_and;
    if ((ops = str_value(ctx, car))) {
      switch (*ops) {
      case '+': spec->op = sen_sel_or; break;
      case '-': spec->op = sen_sel_but; break;
      case '*': spec->op = sen_sel_and; break;
      case '>': spec->op = sen_sel_adjust; break;
      }
    }
  } else {
    sen_db_store *cls = sen_db_store_by_id(ctx->db, base);
    expr = rec_obj_new(ctx, cls, sen_rec_document, sen_rec_none, 0);
    if (ERRP(ctx, SEN_WARN)) { return F; }
    spec->op = sen_sel_or;
  }
  spec->objs = CONS(expr, spec->exprs);
  return expr;
}

inline static int
match_exec(sen_ctx *ctx, match_spec *spec, sen_id base, sen_id id)
{
  sen_obj *value, *expr, *exprs = spec->exprs, *res;
  if (spec->func) {
    while (POP(expr, exprs) != NIL) {
      POP(value, exprs);
      obj_obj_bind(value, base, id);
      /* todo : slotexp_exec may return F */
      slotexp_exec(ctx, expr, value, NULL);
    }
    res = spec->func(ctx, spec->args, &ctx->co);
  } else {
    obj_obj_bind(&ctx->curobj, base, id);
    res = ses_exec(ctx, exprs, NULL, spec->objs);
    ses_clear(ctx);
  }
  return res != F;
}

static sen_obj *
nf_records(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  char *msg;
  sen_obj *car, *res;
  if (!(res = ctx->code)) { QLERR("invalid receiver"); }
  POP(car, args);
  if (!(msg = str_value(ctx, car))) { QLERR("invalid message"); }
  switch (*msg) {
  case '\0' : /* get instance by key */
    {
      char *name;
      sen_db_store *cls;
      POP(car, args);
      if (!(name = str_value(ctx, car))) { return F; }
      if (ctx->code->class) {
        if (!(cls = sen_db_store_by_id(ctx->db, ctx->code->class))) {
          QLERR("class open failed");
        }
        res = sen_ql_class_at(ctx, cls, name, 0, NULL);
        if (res != F &&
            !sen_set_at(RVALUE(ctx->code)->records, &res->u.o.self, NULL)) {
          res = F;
        }
      } else {
        res = sen_ql_at(ctx, name);
        if (!res || !(res->flags & SEN_OBJ_NATIVE) ||
            !sen_set_at(RVALUE(ctx->code)->records, &res->u.o.self, NULL)) {
          res = F;
        }
      }
    }
    break;
  case ':' :
    switch (msg[1]) {
    case 'd' : /* :difference */
    case 'D' :
      {
        sen_records *r = RVALUE(ctx->code);
        if (PAIRP(args)) {
          POP(car, args);
          if (RECORDSP(car)) {
            sen_records_difference(r, RVALUE(car));
          }
        }
      }
      break;
    case 'g' : /* :group */
    case 'G' :
      {
        char *str;
        int limit = 0;
        sen_db_store *cls, *slot;
        sen_group_optarg arg;
        sen_obj *rec = ctx->code;
        POP(car, args);
        if (!(str = str_value(ctx, car))) { break; }
        if (!(slot = sen_db_class_slot(ctx->db, rec->class, str))) { break; }
        if (!(cls = sen_db_store_by_id(ctx->db, slot->u.o.class))) { break; }
        if (slot->type != sen_db_obj_slot) { break; } // todo : support others
        RVALUE(rec)->userdata = slot->u.o.ra;
        arg.mode = sen_sort_descending;
        arg.func = group_obj;
        arg.func_arg = NULL;
        arg.key_size = sizeof(sen_id);
        POP(car, args);
        if (!sen_obj2int(ctx, car)) { limit = car->u.i.i; }
        POP(car, args);
        if ((str = str_value(ctx, car)) && (*str == 'a')) {
          arg.mode = sen_sort_ascending;
        }
        if (!sen_records_group(RVALUE(rec), limit, &arg)) {
          RVALUE(rec)->subrec_id = rec->class;
          rec->class = slot->u.o.class;
          RVALUE(rec)->keys = cls->u.c.keys;
          res = rec;
        }
      }
      break;
    case 'i' : /* :intersect */
    case 'I' :
      {
        sen_records *r = RVALUE(ctx->code);
        while (PAIRP(args)) {
          POP(car, args);
          if (!RECORDSP(car)) { continue; }
          sen_records_intersect(r, RVALUE(car));
          car->type = sen_ql_void;
          car->u.o.func = nf_void;
          car->flags &= ~SEN_OBJ_ALLOCATED;
        }
      }
      break;
    case 'n' : /* :nrecs */
    case 'N' :
      SEN_OBJ_NEW(ctx, res);
      res->type = sen_ql_int;
      res->u.i.i = sen_records_nhits(RVALUE(ctx->code));
      break;
    case 's' :
    case 'S' :
      {
        switch (msg[2]) {
        case 'c' : /* :scan-select */
        case 'C' :
          {
            recinfo *ri;
            sen_id *rid, base = ctx->code->class;
            match_spec spec;
            res = match_prepare(ctx, &spec, base, args);
            if (ERRP(ctx, SEN_WARN)) { return F; }
            switch (spec.op) {
            case sen_sel_or :
              SEN_SET_EACH(RVALUE(ctx->code)->records, eh, &rid, NULL, {
                if (match_exec(ctx, &spec, base, *rid)) {
                  sen_set_get(RVALUE(res)->records, rid, (void **)&ri);
                }
              });
              break;
            case sen_sel_and :
              SEN_SET_EACH(RVALUE(res)->records, eh, &rid, &ri, {
                if (!sen_set_at(RVALUE(ctx->code)->records, rid, NULL) ||
                    !match_exec(ctx, &spec, base, *rid)) {
                  sen_set_del(RVALUE(res)->records, eh);
                }
              });
              break;
            case sen_sel_but :
              SEN_SET_EACH(RVALUE(res)->records, eh, &rid, &ri, {
                if (sen_set_at(RVALUE(ctx->code)->records, rid, NULL) &&
                    match_exec(ctx, &spec, base, *rid)) {
                  sen_set_del(RVALUE(res)->records, eh);
                }
              });
              break;
            case sen_sel_adjust :
              /* todo : support it */
              break;
            }
          }
          break;
        case 'o' : /* :sort */
        case 'O' :
          {
            int limit = 10;
            const char *str;
            sen_sort_optarg arg;
            sen_obj *rec = ctx->code;
            compar_expr_userdata ceud;
            arg.compar = NULL;
            arg.compar_arg = (void *)(intptr_t)RVALUE(rec)->record_size;
            arg.mode = sen_sort_descending;
            if ((str = str_value(ctx, CAR(args)))) {
              if (*str == ':') {
                switch (str[1]) {
                case 's' : /* :score */
                  break;
                case 'k' : /* :key */
                  if (rec->class) {
                    sen_db_store *cls = sen_db_store_by_id(ctx->db, rec->class);
                    if (cls) {
                      RVALUE(rec)->userdata = cls->u.c.keys;
                      arg.compar = compar_key;
                    }
                  } else {
                    RVALUE(rec)->userdata = ctx->db->keys;
                    arg.compar = compar_key;
                  }
                  break;
                case 'n' :
                  arg.compar_arg =
                    (void *)(intptr_t)(RVALUE(rec)->record_size + sizeof(int));
                  break;
                }
              } else {
                sen_db_store *slot = sen_db_class_slot(ctx->db, rec->class, str);
                if (slot) {
                  switch (slot->type) {
                  case sen_db_ra_slot :
                    RVALUE(rec)->userdata = slot->u.f.ra;
                    switch (slot->u.f.class) {
                    case 1 : /* <int> */
                      arg.compar = compar_int;
                      break;
                    case 2 : /* <uint> */
                      arg.compar = compar_uint;
                      break;
                    case 3 : /* <int64> */
                      arg.compar = compar_int64;
                      break;
                    case 4 : /* <float> */
                      arg.compar = compar_float;
                      break;
                    case 8 : /* <time> */
                      arg.compar = compar_time;
                      break;
                    default :
                      arg.compar = compar_ra;
                      break;
                    }
                    break;
                  case sen_db_ja_slot :
                    RVALUE(rec)->userdata = slot->u.v.ja;
                    arg.compar = compar_ja;
                    break;
                  case sen_db_obj_slot :
                    {
                      sen_db_store *cls = sen_db_store_by_id(ctx->db, slot->u.o.class);
                      if (cls) {
                        RVALUE(rec)->userdata = slot->u.o.ra;
                        arg.compar = compar_obj;
                        arg.compar_arg = cls->u.c.keys;
                      }
                    }
                    break;
                  default :
                    break;
                  }
                }
              }
            } else {
              sen_obj *se;
              se = ses_prepare(ctx, rec->class, CAR(args), RVALUE(rec));
              /* se = slotexp_prepare(ctx, rec->class, CAR(args), RVALUE(rec)); */
              if (ERRP(ctx, SEN_WARN)) { return F; }
              ceud.base = rec->class;
              ceud.se = se;
              RVALUE(rec)->userdata = &ceud;
              arg.compar = compar_expr;
              arg.compar_arg = ctx;
            }
            POP(car, args);
            POP(car, args);
            if (!sen_obj2int(ctx, car)) { limit = car->u.i.i; }
            if (limit <= 0) { limit = RVALUE(rec)->records->n_entries; }
            POP(car, args);
            if ((str = str_value(ctx, car)) && *str == 'a') {
              arg.mode = sen_sort_ascending;
            }
            if (!sen_records_sort(RVALUE(rec), limit, &arg)) { res = rec; }
          }
          break;
        case 'u' : /* :subtract */
        case 'U' :
          {
            sen_records *r = RVALUE(ctx->code);
            while (PAIRP(args)) {
              POP(car, args);
              if (!RECORDSP(car)) { continue; }
              sen_records_subtract(r, RVALUE(car));
              car->type = sen_ql_void;
              car->u.o.func = nf_void;
              car->flags &= ~SEN_OBJ_ALLOCATED;
            }
          }
          break;
        default :
          {
            /* ambiguous message. todo : return error */
            res = F;
          }
        }
      }
      break;
    case 'u' : /* :union */
    case 'U' :
      {
        sen_records *r = RVALUE(ctx->code);
        while (PAIRP(args)) {
          POP(car, args);
          if (!RECORDSP(car)) { continue; }
          sen_records_union(r, RVALUE(car));
          car->type = sen_ql_void;
          car->u.o.func = nf_void;
          car->flags &= ~SEN_OBJ_ALLOCATED;
        }
      }
      break;
    case '+' : /* :+ (iterator next) */
      {
        sen_id *rid;
        sen_records *r = RVALUE(ctx->code);
        if (ctx->code->class) {
          POP(res, args);
          if (res->type == sen_ql_object &&
              res->class == ctx->code->class &&
              sen_records_next(r, NULL, 0, NULL)) {
            sen_set_element_info(r->records, r->curr_rec, (void **)&rid, NULL);
            res->u.o.self = *rid;
          } else {
            res = F;
          }
        } else {
          if (sen_records_next(r, NULL, 0, NULL)) {
            const char *key;
            sen_set_element_info(r->records, r->curr_rec, (void **)&rid, NULL);
            if (!(key = _sen_sym_key(ctx->db->keys, *rid))) { QLERR("invalid key"); }
            res = INTERN(key);
          } else {
            res = F;
          }
        }
      }
      break;
    case '\0' : /* : (iterator begin) */
      {
        sen_id *rid;
        sen_records *r = RVALUE(ctx->code);
        sen_records_rewind(r);
        if (sen_records_next(r, NULL, 0, NULL)) {
          sen_set_element_info(r->records, r->curr_rec, (void **)&rid, NULL);
          if (ctx->code->class) {
            SEN_OBJ_NEW(ctx, res);
            obj_obj_bind(res, ctx->code->class, *rid);
          } else {
            const char *key;
            if (!(key = _sen_sym_key(ctx->db->keys, *rid))) { QLERR("invalid key"); }
            res = INTERN(key);
          }
        } else {
          res = F;
        }
      }
      break;
    }
    break;
  default : /* invalid message */
    res = F;
    break;
  }
  return res;
}

struct _ins_stat {
  sen_obj *slots;
  int nslots;
  int nrecs;
};

inline static void
clear_all_slot_values(sen_ctx *ctx, sen_id base, sen_id self)
{
  sen_set *slots;
  {
    char buf[SEN_SYM_MAX_KEY_SIZE];
    if (sen_db_class_slotpath(ctx->db, base, "", buf)) { return; }
    slots = sen_sym_prefix_search(ctx->db->keys, buf);
  }
  if (slots) {
    sen_id *sid;
    sen_obj o = { sen_ql_list, SEN_OBJ_REFERER };
    o.u.l.car = NIL;
    o.u.l.cdr = NIL;
    if (sen_db_lock(ctx->db, -1)) {
      SEN_LOG(sen_log_crit, "clear_all_slot_values: lock failed");
    } else {
      SEN_SET_EACH(slots, eh, &sid, NULL, {
        sen_db_store *slot = sen_db_store_by_id(ctx->db, *sid);
        /* todo : if (!slot) error handling */
        if (slot && (slot->type != sen_db_idx_slot /* || virtualslot */)) {
          sen_obj dummy;
          slot_value(ctx, slot, self, &o, &dummy);
        }
      });
      sen_db_unlock(ctx->db);
    }
    sen_set_close(slots);
  }
}

// todo : refine
#define MAXSLOTS 0x100

static sen_obj *
nf_class(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  char *msg;
  sen_id base;
  int load = 0;
  sen_sym *sym;
  sen_db_store *cls;
  sen_obj *car, *res;
  if (!(res = ctx->code)) { QLERR("invalid receiver"); }
  base = ctx->code->u.o.self;
  if (!(cls = sen_db_store_by_id(ctx->db, base))) { QLERR("invalid class"); }
  sym = cls->u.c.keys;
  SEN_QL_CO_BEGIN(co);
  POP(car, args);
  if (!(msg = str_value(ctx, car))) { QLERR("invalid message"); }
  switch (*msg) {
  case '\0' : /* get instance by key */
    {
      char *name;
      POP(car, args);
      if (!(name = str_value(ctx, car))) { return F; }
      res = sen_ql_class_at(ctx, cls, name, 0, NULL);
    }
    break;
  case ':' :
    switch (msg[1]) {
    case 'c' :
    case 'C' :
      switch (msg[2]) {
      case 'l' : /* :clearlock */
      case 'L' :
        {
          res = *sym->lock ? T : F;
          sen_sym_clear_lock(sym);
        }
        break;
      case 'o' : /* :common-prefix-search */
      case 'O' :
        {
          sen_id id;
          char *name;
          POP(car, args);
          if (!(name = str_value(ctx, car))) { return F; }
          SYM_DO(sym, name, { id = sen_sym_common_prefix_search(sym, name); });
          if (id) {
            SEN_OBJ_NEW(ctx, res);
            obj_obj_bind(res, base, id);
          } else {
            res = F;
          }
        }
        break;
      }
      break;
    case 'd' :
    case 'D' :
      switch (msg[2]) {
      case 'e' :
      case 'E' :
        switch (msg[3]) {
        case 'f' : /* :def */
        case 'F' :
          {
            char *name;
            sen_id target = 0;
            sen_db_store *slot;
            sen_db_store_spec spec;
            POP(car, args);
            if (!(name = str_value(ctx, car))) { return F; }
            if (sen_db_class_slot(ctx->db, base, name)) { return T; /* already exists */ }
            POP(car, args);
            spec.u.s.class = car->u.o.self;
            spec.u.s.size = 0;
            spec.u.s.collection_type = 0;
            switch (car->type) {
            case sen_db_raw_class :
              {
                sen_db_store *tc = sen_db_store_by_id(ctx->db, spec.u.s.class);
                if (!tc) { return F; }
                /* todo : use tc->id instead of element_size */
                spec.type = (tc->u.bc.element_size > 8) ? sen_db_ja_slot : sen_db_ra_slot;
                spec.u.s.size = tc->u.bc.element_size;
              }
              break;
            case sen_db_rel1 :
            case sen_db_class :
              spec.type = sen_db_obj_slot;
              break;
            case sen_db_obj_slot :
            case sen_db_ra_slot :
            case sen_db_ja_slot :
              spec.type = sen_db_idx_slot;
              break;
            case sen_ql_void :
              /* keyword might be assigned */
              break;
            default :
              return F;
            }
            while (PAIRP(args)) {
              POP(car, args);
              if (PAIRP(car)) { /* view definition */
                char *opt = str_value(ctx, CADR(car));
                if (opt && !strcmp(opt, ":match")) { /* fulltext index */
                  spec.type = sen_db_idx_slot;
                  car = CAR(car);
                  if (PAIRP(car)) {
                    char *slotname;
                    sen_db_store *ts;
                    if (CAR(car)->type != sen_db_class &&
                        CAR(car)->type != sen_db_rel1) {
                      QLERR("class must be assigned as index target");
                    }
                    spec.u.s.class = CAR(car)->u.o.self;
                    if (!(slotname = str_value(ctx, CADR(car))) ||
                        !(ts = sen_db_class_slot(ctx->db, spec.u.s.class, slotname))) {
                      return F;
                    }
                    target = ts->id;
                  } else {
                    sen_db_store *tc = sen_db_slot_class_by_id(ctx->db, car->u.o.self);
                    if (!tc) { return F; }
                    spec.u.s.class = tc->id;
                    target = car->u.o.self;
                  }
                }
              }
            }
            {
              char buf[SEN_SYM_MAX_KEY_SIZE];
              if (sen_db_class_slotpath(ctx->db, base, name, buf)) { return F; }
              if (!(slot = sen_db_store_create(ctx->db, buf, &spec))) { return F; }
              if (spec.type == sen_db_idx_slot && target) {
                sen_db_store_rel_spec rs;
                rs.type = sen_db_index_target;
                rs.target = target;
                sen_db_store_add_trigger(slot, &rs);
                sen_db_idx_slot_build(ctx->db, slot);
              }
              if ((res = INTERN(buf)) != F) {
                sen_ql_bind_symbol(slot, res);
              }
            }
          }
          break;
        case 'l' : /* :delete */
        case 'L' :
          {
            char *name;
            sen_id id;
            POP(car, args);
            if (!(name = str_value(ctx, car))) { return F; }
            SYM_DO(sym, name, { id = sen_sym_at(sym, name); });
            if (!id) { return F; }
            clear_all_slot_values(ctx, base, id);
            /* todo : use sen_sym_del_with_sis if sym->flags & SEN_SYM_WITH_SIS */
            /* todo : check foreign key constraint */
            SYM_DO(sym, name, { sen_sym_del(sym, name); });
          }
          break;
        default :
          res = F;
        }
        break;
      default :
        res = F;
      }
      break;
    case 'l' : /* :load */
    case 'L' :
      load = 1;
      break;
    case 'n' :
    case 'N' :
      {
        switch (msg[2]) {
        case 'e' : /* :new */
        case 'E' :
          {
            char *name;
            POP(car, args);
            if (!(name = str_value(ctx, car))) { return F; }
            if (sen_db_lock(ctx->db, -1)) {
              SEN_LOG(sen_log_crit, "nf_class::new: lock failed");
            } else {
              res = sen_ql_class_at(ctx, cls, name, 1, NULL);
              if (res != F) {
                sen_obj cons, dummy;
                sen_db_store *slot;
                cons.type = sen_ql_list;
                cons.flags = SEN_OBJ_REFERER;
                cons.u.l.cdr = NIL;
                while (PAIRP(args)) {
                  POP(car, args);
                  if (!(msg = str_value(ctx, car))) { break; }
                  POP(car, args);
                  if (VOIDP(car)) { break; }
                  if (!(slot = sen_db_class_slot(ctx->db, base, msg))) { break; }
                  cons.u.l.car = car;
                  slot_value(ctx, slot, res->u.o.self, &cons, &dummy);
                }
              }
              sen_db_unlock(ctx->db);
            }
          }
          break;
        case 'r' : /* :nrecs */
        case 'R' :
          {
            SEN_OBJ_NEW(ctx, res);
            res->type = sen_ql_int;
            res->u.i.i = sen_sym_size(sym);
          }
          break;
        default :
          {
            /* ambiguous message. todo : return error */
            res = F;
          }
        }
      }
      break;
    case 'p' : /* :prefix-search */
    case 'P' :
      {
        char *name;
        POP(car, args);
        if (!(name = str_value(ctx, car))) { return F; }
        res = rec_obj_new(ctx, cls, sen_rec_document, sen_rec_none, 0);
        if (ERRP(ctx, SEN_WARN)) { return F; }
        SYM_DO(sym, name, {
          sen_sym_prefix_search_with_set(sym, name, RVALUE(res)->records);
        });
      }
      break;
    case 's' :
    case 'S' :
      switch (msg[2]) {
      case 'c' :
      case 'C' :
        switch (msg[3]) {
        case 'a' : /* :scan-select */
        case 'A' :
          {
            recinfo *ri;
            sen_id *rid;
            match_spec spec;
            res = match_prepare(ctx, &spec, base, args);
            if (ERRP(ctx, SEN_WARN)) { return F; }
            switch (spec.op) {
            case sen_sel_or :
              {
                sen_id id = SEN_SYM_NIL; /* maxid = sen_sym_curr_id(sym); */
                posinfo *pi = (posinfo *) &id;
                while ((id = sen_sym_next(sym, id))) {
                  if (match_exec(ctx, &spec, base, id)) {
                    sen_set_get(RVALUE(res)->records, pi, (void **)&ri);
                  }
                }
              }
              break;
            case sen_sel_and :
              SEN_SET_EACH(RVALUE(res)->records, eh, &rid, &ri, {
                if (!match_exec(ctx, &spec, base, *rid)) {
                  sen_set_del(RVALUE(res)->records, eh);
                }
              });
              break;
            case sen_sel_but :
              SEN_SET_EACH(RVALUE(res)->records, eh, &rid, &ri, {
                if (match_exec(ctx, &spec, base, *rid)) {
                  sen_set_del(RVALUE(res)->records, eh);
                }
              });
              break;
            case sen_sel_adjust :
              /* todo : support it */
              break;
            }
          }
          break;
        case 'h' : /* :schema */
        case 'H' :
          res = NIL;
          if (sym->flags & SEN_SYM_WITH_SIS) { res = CONS(INTERN(":sis"), res); }
          if (sym->flags & SEN_INDEX_NORMALIZE) { res = CONS(INTERN(":normalize"), res); }
          if (sym->flags & SEN_INDEX_NGRAM) { res = CONS(INTERN(":ngram"), res); }
          if (sym->flags & SEN_INDEX_DELIMITED) { res = CONS(INTERN(":delimited"), res); }
          {
            char encstr[32] = ":";
            strcpy(encstr + 1, sen_enctostr(sym->encoding));
            res = CONS(INTERN(encstr), res);
          }
          res = CONS(INTERN("ptable"),
                     CONS(CONS(INTERN("quote"),
                               CONS(INTERN(_sen_sym_key(ctx->db->keys, base)), NIL)), res));
          break;
        }
        break;
      case 'u' : /* :suffix-search */
      case 'U' :
        {
          char *name;
          POP(car, args);
          if (!(name = str_value(ctx, car))) { return F; }
          res = rec_obj_new(ctx, cls, sen_rec_document, sen_rec_none, 0);
          if (ERRP(ctx, SEN_WARN)) { return F; }
          SYM_DO(sym, name, {
            sen_sym_suffix_search_with_set(sym, name, RVALUE(res)->records);
          });
        }
        break;
      case 'l' : /* :slots */
      case 'L' :
        {
          char *name;
          char buf[SEN_SYM_MAX_KEY_SIZE];
          POP(car, args);
          if (!(name = str_value(ctx, car))) { name = ""; }
          if (sen_db_class_slotpath(ctx->db, base, name, buf)) { return F; }
          {
            sen_records *r;
            if (!(r = sen_records_open(sen_rec_document, sen_rec_none, 0))) {
              return F;
            }
            r->keys = ctx->db->keys;
            SEN_OBJ_NEW(ctx, res);
            rec_obj_bind(res, r, 0);
          }
          sen_sym_prefix_search_with_set(ctx->db->keys, buf, RVALUE(res)->records);
        }
        break;
      }
      break;
    case 'u' : /* :undef */
    case 'U' :
      {
        char *name;
        POP(car, args);
        if (!(name = str_value(ctx, car))) { return F; }
        res = sen_db_class_del_slot(ctx->db, base, name) ? F : T;
      }
      break;
    case '+' : /* :+ (iterator next) */
      {
        sen_id id;
        POP(res, args);
        if (res->type == sen_ql_object &&
            res->class == cls->id &&
            (id = sen_sym_next(sym, res->u.o.self))) {
          res->u.o.self = id;
        } else {
          res = F;
        }
      }
      break;
    case '\0' : /* : (iterator begin) */
      {
        sen_id id;
        id = sen_sym_next(sym, SEN_SYM_NIL);
        if (id == SEN_SYM_NIL) {
          res = F;
        } else {
          SEN_OBJ_NEW(ctx, res);
          obj_obj_bind(res, cls->id, id);
        }
      }
      break;
    }
    break;
  default : /* :slotname */
    {
      int recpslotp;
      res = class_slot(ctx, base, msg, NULL, &recpslotp);
    }
    break;
  }
  if (load) {
    int i, recpslotp;
    sen_obj *s;
    struct _ins_stat *stat;
    for (s = args, i = 0; PAIRP(s); s = CDR(s), i++) {
      car = CAR(s);
      if (!(msg = str_value(ctx, car))) { return F; }
      if ((s->u.l.car = class_slot(ctx, base, msg, NULL, &recpslotp)) == F) { return F; }
    }
    if (!(s = sen_obj_alloc(ctx, sizeof(struct _ins_stat)))) { return F; }
    stat = (struct _ins_stat *)s->u.b.value; // todo : not GC safe
    stat->slots = args;
    stat->nslots = i + 1;
    stat->nrecs = 0;
    do {
      SEN_QL_CO_WAIT(co, stat);
      if (BULKP(args) && args->u.b.size) {
        char *tokbuf[MAXSLOTS];
        sen_db_store *slot;
        sen_obj val, obj, cons, dummy;
        cons.type = sen_ql_list;
        cons.flags = SEN_OBJ_REFERER;
        cons.u.l.car = &val;
        cons.u.l.cdr = NIL;
        val.type = sen_ql_bulk;
        if (sen_str_tok(args->u.b.value, args->u.b.size, '\t', tokbuf, MAXSLOTS, NULL) == stat->nslots) {
          sen_obj *o;
          *tokbuf[0] = '\0';
          if (sen_db_lock(ctx->db, -1)) {
            SEN_LOG(sen_log_crit, "nf_class::load lock failed");
          } else {
            o = sen_ql_class_at(ctx, cls, args->u.b.value, 1, &obj);
            if (o != F) {
              for (s = stat->slots, i = 1; i < stat->nslots; s = CDR(s), i++) {
                val.u.b.value = tokbuf[i - 1] + 1;
                val.u.b.size = tokbuf[i] - val.u.b.value;
                if (!(slot = sen_db_store_by_id(ctx->db, CAR(s)->u.o.self))) { /* todo */ }
                slot_value(ctx, slot, obj.u.o.self, &cons, &dummy); // todo : refine cons
              }
              stat->nrecs++;
            }
            sen_db_unlock(ctx->db);
          }
        }
      } else {
        co->mode |= SEN_CTX_TAIL;
      }
    } while (!(co->mode & (SEN_CTX_HEAD|SEN_CTX_TAIL)));
    if ((res = sen_obj_new(ctx))) {
      res->type = sen_ql_int;
      res->u.i.i = stat->nrecs;
    } else {
      res = F;
    }
  }
  SEN_QL_CO_END(co);
  return res;
}

#define REL1_GET_INSTANCE_BY_KEY(rel,key,id) do {\
  char *name;\
  if (rel->u.f.class) {\
    sen_db_store *tcls = sen_db_store_by_id(ctx->db, rel->u.f.class);\
    if (!tcls || !(name = str_value(ctx, key))) {\
      id = SEN_SYM_NIL;\
    } else {\
      SYM_DO(tcls->u.c.keys, name, {\
        id = sen_sym_at(tcls->u.c.keys, name);\
      });\
    }\
  } else {\
    switch (key->type) {\
    case sen_ql_bulk :\
      name = key->u.b.value;\
      id = sen_atoi(name, name + key->u.b.size, NULL);\
      break;\
    case sen_ql_int :\
      id = key->u.i.i;\
      break;\
    default :\
      id = SEN_SYM_NIL;\
    }\
  }\
} while(0)

static sen_obj *
nf_rel1(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  char *msg;
  sen_id base;
  sen_db_store *cls;
  sen_obj *args0 = args, *car, *res;
  if (!(res = ctx->code)) { QLERR("invalid receiver"); }
  base = ctx->code->u.o.self;
  if (!(cls = sen_db_store_by_id(ctx->db, base))) { QLERR("invalid class"); }
  POP(car, args);
  if (!(msg = str_value(ctx, car))) { QLERR("invalid message"); }
  switch (*msg) {
  case '\0' : /* get instance by key */
    {
      sen_id id;
      uint8_t *v;
      POP(car, args);
      REL1_GET_INSTANCE_BY_KEY(cls, car, id);
      if (!id || !(v = (uint8_t *)sen_ra_at(cls->u.f.ra, id)) || !(*v & 1)) {
        return F;
      }
      res = sen_ql_mk_obj(ctx, base, id);
      return res;
    }
    break;
  case ':' :
    switch (msg[1]) {
    case 'c' :
    case 'C' :
      switch (msg[2]) {
      case 'l' : /* :clearlock */
      case 'L' :
        return res;
        break;
      }
      break;
    case 'd' :
    case 'D' :
      switch (msg[2]) {
      case 'e' :
      case 'E' :
        switch (msg[3]) {
        case 'l' : /* :delete */
        case 'L' :
          {
            sen_id id;
            uint8_t *v;
            POP(car, args);
            REL1_GET_INSTANCE_BY_KEY(cls, car, id);
            if (!id || !(v = (uint8_t *)sen_ra_at(cls->u.f.ra, id)) || !(*v & 1)) {
              return F;
            }
            clear_all_slot_values(ctx, base, id);
            cls->u.f.ra->header->nrecords -= 1;
            *v &= ~1;
            return res;
          }
          break;
        }
      }
      break;
    case 'n' :
    case 'N' :
      {
        switch (msg[2]) {
        case 'e' : /* :new */
        case 'E' :
          {
            sen_id id;
            uint8_t *v;
            if (sen_db_lock(ctx->db, -1)) {
              SEN_LOG(sen_log_crit, "nf_rel1::new lock failed");
            } else {
              if (cls->u.f.class) {
                char *name;
                sen_db_store *tcls = sen_db_store_by_id(ctx->db, cls->u.f.class);
                res = F;
                if (tcls) {
                  POP(car, args);
                  if ((name = str_value(ctx, car))) {
                    res = sen_ql_class_at(ctx, tcls, name, 0, NULL);
                    if (res != F) {
                      id = res->u.o.self;
                      if ((v = (uint8_t *)sen_ra_get(cls->u.f.ra, id))) {
                        if (!*v) {
                          cls->u.f.ra->header->nrecords += 1;
                          *v |= 1;
                        }
                      }
                    }
                  }
                }
              } else {
                id = cls->u.f.ra->header->curr_max + 1;
                if ((v = (uint8_t *)sen_ra_get(cls->u.f.ra, id))) {
                  cls->u.f.ra->header->nrecords += 1;
                  *v |= 1;
                  res = sen_ql_mk_obj(ctx, base, id);
                } else {
                  res = F;
                }
              }
              if (res != F) {
                sen_obj cons, dummy;
                sen_db_store *slot;
                cons.type = sen_ql_list;
                cons.flags = SEN_OBJ_REFERER;
                cons.u.l.cdr = NIL;
                while (PAIRP(args)) {
                  POP(car, args);
                  if (!(msg = str_value(ctx, car))) { continue; }
                  POP(car, args);
                  if (VOIDP(car)) { continue; }
                  if (!(slot = sen_db_class_slot(ctx->db, base, msg))) { break; }
                  cons.u.l.car = car;
                  slot_value(ctx, slot, res->u.o.self, &cons, &dummy);
                }
              }
              sen_db_unlock(ctx->db);
            }
            return res;
          }
          break;
        case 'r' : /* :nrecs */
        case 'R' :
          {
            SEN_OBJ_NEW(ctx, res);
            res->type = sen_ql_int;
            res->u.i.i = cls->u.f.ra->header->nrecords;
            return res;
          }
          break;
        default :
          {
            /* ambiguous message. todo : return error */
            res = F;
          }
        }
      }
      break;
    case 's' :
    case 'S' :
      switch (msg[2]) {
      case 'c' : /* :scan-select */
      case 'C' :
        {
          recinfo *ri;
          sen_id *rid;
          match_spec spec;
          res = match_prepare(ctx, &spec, base, args);
          if (ERRP(ctx, SEN_WARN)) { return F; }
          switch (spec.op) {
          case sen_sel_or :
            {
              sen_id id = SEN_SYM_NIL, maxid = cls->u.f.ra->header->curr_max;
              posinfo *pi = (posinfo *) &id;
              while (++id <= maxid) {
                if (match_exec(ctx, &spec, base, id)) {
                  sen_set_get(RVALUE(res)->records, pi, (void **)&ri);
                }
              }
            }
            break;
          case sen_sel_and :
            SEN_SET_EACH(RVALUE(res)->records, eh, &rid, &ri, {
              if (!match_exec(ctx, &spec, base, *rid)) {
                sen_set_del(RVALUE(res)->records, eh);
              }
            });
            break;
          case sen_sel_but :
            SEN_SET_EACH(RVALUE(res)->records, eh, &rid, &ri, {
              if (match_exec(ctx, &spec, base, *rid)) {
                sen_set_del(RVALUE(res)->records, eh);
              }
            });
            break;
          case sen_sel_adjust :
            /* todo : support it */
            break;
          }
        }
        return res;
        break;
      case 'u' : /* :suffix-search is not available*/
      case 'U' :
        return res;
        break;
      default :
        break;
      }
      break;
    case '+' : /* :+ (iterator next) */
      {
        POP(res, args);
        if (res->type == sen_ql_object && res->class == cls->id) {
          uint8_t *v;
          sen_id id = res->u.o.self, maxid = cls->u.f.ra->header->curr_max;
          for (;;) {
            if (++id > maxid) {
              return F;
            }
            if ((v = (uint8_t *)sen_ra_at(cls->u.f.ra, id)) && (*v & 1)) { break; }
          }
          res->u.o.self = id;
          return res;
        } else { return F; /* cause error ? */ }
      }
      break;
    case '\0' : /* : (iterator begin) */
      {
        uint8_t *v;
        sen_id id = SEN_SYM_NIL + 1, maxid;
        maxid = cls->u.f.ra->header->curr_max;
        while (!(v = (uint8_t *)sen_ra_at(cls->u.f.ra, id)) || !(*v & 1)) {
          if (++id > maxid) { return F; }
        }
        res = sen_ql_mk_obj(ctx, base, id);
        return res;
      }
      break;
    }
    break;
  }
  return nf_class(ctx, args0, co);
}

inline static sen_obj *
sen_obj_query(sen_ctx *ctx, const char *str, unsigned int str_len,
              sen_sel_operator default_op, int max_exprs, sen_encoding encoding)
{
  sen_query *q;
  sen_obj *res = sen_obj_new(ctx);
  if (!res || !(q = sen_query_open(str, str_len, default_op, max_exprs, encoding))) {
    return NULL;
  }
  res->type = sen_ql_query;
  res->flags = SEN_OBJ_ALLOCATED;
  res->u.p.value = q;
  return res;
}

static sen_obj *
nf_toquery(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  sen_obj *o = NULL, *s;
  POP(s, args);
  if (BULKP(s)) {
    /* TODO: operator, exprs, encoding */
    if (!(o = sen_obj_query(ctx, s->u.b.value, s->u.b.size, sen_sel_and, 32, ctx->encoding))) {
      QLERR("query_obj_new failed");
    }
  }
  return o;
}

static sen_obj *
nf_slot(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  char *msg;
  sen_id base;
  sen_obj *car, *res;
  sen_db_store *slot;
  if (!(res = ctx->code)) { QLERR("invalid receiver"); }
  base = ctx->code->u.o.self;
  if (!(slot = sen_db_store_by_id(ctx->db, base))) { QLERR("sen_db_store_by_id failed"); }
  POP(car, args);
  if (!(msg = str_value(ctx, car))) { QLERR("invalid message"); }
  switch (*msg) {
  case '\0' :
    {
      if (IDX_SLOTP(ctx->code)) {
        sen_obj *q;
        sen_sel_operator op;
        POP(q, args);
        if (!QUERYP(q)) {
          if (!BULKP(q)) { return F; }
          if (!(q = sen_obj_query(ctx, q->u.b.value, q->u.b.size, sen_sel_and, 32, ctx->encoding))) {
            QLERR("query_obj_new failed");
          }
        }
        /* TODO: specify record unit */
        /* (idxslot query ((slot1 weight1) (slot2 weight2) ...) records operator+ */
        POP(car, args);
        /* TODO: handle weights */
        POP(res, args);
        if (RECORDSP(res)) {
          char *ops;
          op = sen_sel_and;
          POP(car, args);
          if ((ops = str_value(ctx, car))) {
            switch (*ops) {
            case '+': op = sen_sel_or; break;
            case '-': op = sen_sel_but; break;
            case '*': op = sen_sel_and; break;
            case '>': op = sen_sel_adjust; break;
            }
          }
        } else {
          sen_db_store *cls;
          if (!(cls = sen_db_store_by_id(ctx->db, slot->u.i.class))) { return F; }
          res = rec_obj_new(ctx, cls, sen_rec_document, sen_rec_none, 0);
          if (ERRP(ctx, SEN_WARN)) { return F; }
          op = sen_sel_or;
        }
        sen_query_exec(slot->u.i.index, PVALUE(q, sen_query), RVALUE(res), op);
      } else {
        char *name;
        sen_db_store *cls;
        POP(car, args);
        if (!(name = str_value(ctx, car))) { return F; }
        if (!(cls = sen_db_slot_class_by_id(ctx->db, base))) { return F; }
        res = sen_ql_class_at(ctx, cls, name, 0, NULL);
        if (res != F) {
          if (VOIDP(args)) {
            slot_value(ctx, slot, res->u.o.self, args, res);
          } else {
            if (sen_db_lock(ctx->db, -1)) {
              SEN_LOG(sen_log_crit, "nf_slot: lock failed");
            } else {
              slot_value(ctx, slot, res->u.o.self, args, res);
              sen_db_unlock(ctx->db);
            }
          }
        }
      }
    }
    break;
  case ':' :
    switch (msg[1]) {
    case 'd' : /* :defrag */
    case 'D' :
      if (JA_SLOTP(ctx->code)) {
        int threshold = 1, nsegs;
        POP(car, args);
        if (!sen_obj2int(ctx, car)) { threshold = car->u.i.i; }
        nsegs = sen_ja_defrag(slot->u.v.ja, threshold);
        SEN_OBJ_NEW(ctx, res);
        res->type = sen_ql_int;
        res->u.i.i = nsegs;
      } else {
        QLERR("invalid message");
      }
      break;
    case 's' : /* :schema */
    case 'S' :
      {
        const char *key;
        switch (slot->type) {
        case sen_db_obj_slot :
          if (!(key = _sen_sym_key(ctx->db->keys, slot->u.o.class))) {
            QLERR("invalid target as obj_slot");
          }
          res = CONS(INTERN(key), NIL);
          break;
        case sen_db_ra_slot  :
          if (!(key = _sen_sym_key(ctx->db->keys, slot->u.f.class))) {
            QLERR("invalid target as ra_slot");
          }
          res = CONS(INTERN(key), NIL);
          break;
        case sen_db_ja_slot  :
          if (!(key = _sen_sym_key(ctx->db->keys, slot->u.v.class))) {
            QLERR("invalid target as ja_slot");
          }
          res = CONS(INTERN(key), NIL);
          break;
        case sen_db_idx_slot :
          {
            sen_db_trigger *t;
            res = CONS(INTERN("::match"), CONS(NIL, NIL));
            for (t = slot->triggers; t; t = t->next) {
              if (t->type == sen_db_index_target) {
                if (!(key = _sen_sym_key(ctx->db->keys, t->target))) {
                  QLERR("invalid target as idx_slot");
                }
                res = CONS(INTERN(key), res);
              }
            }
            // todo : support multi column
            res = CONS(INTERN(":as"), CONS(CONS(INTERN("quote"), CONS(res, NIL)), NIL));
          }
          break;
        case sen_db_vslot    :
          QLERR("not supported yet");
          break;
        case sen_db_pslot    :
          QLERR("not supported yet");
          break;
        default :
          QLERR("invalid slot type");
          break;
        }
        {
          char *p, buf[SEN_SYM_MAX_KEY_SIZE];
          strcpy(buf, _sen_sym_key(ctx->db->keys, base));
          if (!(p = strchr(buf, '.'))) { QLERR("invalid slotname %s", buf); }
          *p = ':';
          res = CONS(INTERN("::def"), CONS(INTERN(p), res));
          *p = '\0';
          res = CONS(INTERN(buf), res);
        }
      }
      break;
    }
    break;
  }
  return res;
}

void
sen_ql_bind_symbol(sen_db_store *dbs, sen_obj *symbol)
{
  symbol->type = dbs->type;
  symbol->flags |= SEN_OBJ_NATIVE;
  symbol->u.o.self = dbs->id;
  switch (symbol->type) {
  case sen_db_class :
    symbol->u.o.func = nf_class;
    symbol->class = 0;
    break;
  case sen_db_obj_slot :
    symbol->u.o.func = nf_slot;
    symbol->class = dbs->u.o.class;
    break;
  case sen_db_ra_slot :
    symbol->u.o.func = nf_slot;
    symbol->class = dbs->u.f.class;
    break;
  case sen_db_ja_slot :
    symbol->u.o.func = nf_slot;
    symbol->class = dbs->u.v.class;
    break;
  case sen_db_idx_slot :
    symbol->u.o.func = nf_slot;
    symbol->class = dbs->u.i.class;
    break;
  case sen_db_rel1 :
    symbol->u.o.func = nf_rel1;
    symbol->class = 0;
    break;
  default :
    symbol->u.o.func = nf_void;
    symbol->class = 0;
    break;
  }
}

static sen_obj *
nf_snippet(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  /* args: (width@int max_results@int cond1@list cond2@list ...) */
  /* cond: (keyword@bulk [opentag@bulk closetag@bulk]) */

  /* args: (width@int max_results@int query@query cond1@list cond2@list ...) */
  /* cond: (opentag@bulk closetag@bulk) */

  sen_obj *res, *cur;
  sen_snip *s;
  unsigned int width, max_results;
  POP(cur, args);
  if (sen_obj2int(ctx, cur)) { QLERR("snippet failed (width expected)"); }
  width = IVALUE(cur);
  POP(cur, args);
  if (sen_obj2int(ctx, cur)) { QLERR("snipped failed (max_result expected)"); }
  max_results = IVALUE(cur);
  if (!PAIRP(args)) { QLERR("cond expected"); }
  if (PAIRP(CAR(args)) || BULKP(CAR(args))) {
    /* FIXME: mapping */
    if (!(s = sen_snip_open(ctx->encoding, SEN_SNIP_NORMALIZE, width, max_results,
                            NULL, 0, NULL, 0, (sen_snip_mapping *)-1))) {
      QLERR("sen_snip_open failed");
    }
    SEN_OBJ_NEW(ctx, res);
    snip_obj_bind(res, s);
    while (PAIRP(args)) {
      char *ot = NULL, *ct = NULL;
      uint32_t ot_l = 0, ct_l = 0;
      sen_obj *kw;
      POP(cur, args);
      if (PAIRP(cur)) {
        kw = CAR(cur);
        if (PAIRP(CDR(cur)) && BULKP(CADR(cur))) {
          ot = sen_obj_copy_bulk_value(ctx, CADR(cur));
          ot_l = CADR(cur)->u.b.size;
          if (PAIRP(CDDR(cur)) && BULKP(CADDR(cur))) {
            ct = sen_obj_copy_bulk_value(ctx, CADDR(cur));
            ct_l = CADDR(cur)->u.b.size;
          }
        }
      } else {
        kw = cur;
      }
      if (!BULKP(kw)) { QLERR("snippet failed (invalid kw)"); }
      if ((sen_snip_add_cond(s, kw->u.b.value, kw->u.b.size, ot, ot_l, ct, ct_l))) {
        QLERR("sen_snip_add_cond failed");
      }
    }
    s->flags |= SEN_SNIP_COPY_TAG;
  } else if (QUERYP(CAR(args))) {
    sen_obj *x;
    sen_query *q;
    unsigned int n_tags = 0;
    const char **opentags, **closetags;
    unsigned int *opentag_lens, *closetag_lens;
    SEN_OBJ_NEW(ctx, res);
    POP(cur, args);
    q = cur->u.p.value;
    for (x = args; PAIRP(x); x = CDR(x)) { n_tags++; }
    if (!n_tags) { n_tags++; }
    if (!(opentags = SEN_MALLOC((sizeof(char *) + sizeof(unsigned int)) * 2 * n_tags))) {
      QLERR("malloc failed");
    }
    closetags = &opentags[n_tags];
    opentag_lens = (unsigned int *)&closetags[n_tags];
    closetag_lens = &opentag_lens[n_tags];
    n_tags = 0;
    for (x = args; PAIRP(x); x = CDR(x)) {
      cur = CAR(x);
      if (PAIRP(cur)) {
        if (BULKP(CAR(cur))) {
          opentags[n_tags] = STRVALUE(CAR(cur));
          opentag_lens[n_tags] = CAR(cur)->u.b.size;
          if (PAIRP(CDR(cur)) && BULKP(CADR(cur))) {
            closetags[n_tags] = STRVALUE(CADR(cur));
            closetag_lens[n_tags] = CADR(cur)->u.b.size;
            n_tags++;
          }
        }
      }
    }
    if (!n_tags) {
      n_tags++;
      opentags[0] = NULL;
      closetags[0] = NULL;
      opentag_lens[0] = 0;
      closetag_lens[0] = 0;
    }
    s = sen_query_snip(q, SEN_SNIP_NORMALIZE|SEN_SNIP_COPY_TAG, width, max_results, n_tags,
                       opentags, opentag_lens, closetags, closetag_lens,
                       (sen_snip_mapping *)-1);
    SEN_FREE(opentags);
    snip_obj_bind(res, s);
  } else {
    QLERR("snippet failed. cond or query expected");
  }
  return res;
}

static sen_obj *
nf_snip(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  /* args: (str@bulk) */
  if (!PAIRP(args) || !BULKP(CAR(args))) { QLERR("invalid argument"); }
  {
    sen_rbuf buf;
    unsigned int i, len, max_len, nresults;
    sen_snip *s = PVALUE(ctx->code, sen_snip);
    sen_obj *v, *str = CAR(args), *spc = PAIRP(CDR(args)) ? CADR(args) : NIL;
    if ((sen_snip_exec(s, str->u.b.value, str->u.b.size, &nresults, &max_len))) {
      QLERR("sen_snip_exec failed");
    }
    if (sen_rbuf_init(&buf, max_len)) { QLERR("sen_rbuf_init failed"); }
    if (nresults) {
      for (i = 0; i < nresults; i++) {
        if (i && spc != NIL) { sen_obj_inspect(ctx, spc, &buf, 0); }
        if (sen_rbuf_reserve(&buf, max_len)) {
          sen_rbuf_fin(&buf);
          QLERR("sen_rbuf_space failed");
        }
        if ((sen_snip_get_result(s, i, buf.curr, &len))) {
          sen_rbuf_fin(&buf);
          QLERR("sen_snip_get_result failed");
        }
        buf.curr += len;
      }
    } else {
      char *ss = str->u.b.value, *se = str->u.b.value + str->u.b.size;
      if (sen_substring(&ss, &se, 0, s->width, ctx->encoding)) {
        QLERR("sen_substring failed");
      }
      sen_rbuf_write(&buf, ss, se - ss);
    }
    SEN_RBUF2OBJ(ctx, &buf, v);
    return v;
  }
}

static sen_obj *
nf_db(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  char *msg;
  sen_db_store *cls;
  sen_obj *car, *res = ctx->code;
  POP(car, args);
  if (!(msg = str_value(ctx, car))) { return res; }
  if (*msg == ':') {
    switch (msg[1]) {
    case 'c' : /* :clearlock */
    case 'C' :
      {
        sen_id id;
        sen_db_store *store;
        for (id = sen_sym_curr_id(ctx->db->keys); id; id--) {
          if (strchr(_sen_sym_key(ctx->db->keys, id), '.')) { continue; }
          if ((store = sen_db_store_by_id(ctx->db, id))) {
            if (store->type == sen_db_class) {
              sen_sym_clear_lock(store->u.c.keys);
            }
          }
        }
        res = *ctx->db->keys->lock ? T : F;
        sen_db_clear_lock(ctx->db);
      }
      break;
    case 'd' : /* :drop */
    case 'D' :
      {
        const char *name, *slotname;
        sen_set *slots;
        char buf[SEN_SYM_MAX_KEY_SIZE];
        POP(car, args);
        if (!(name = str_value(ctx, car))) { QLERR("Invalid argument"); }
        if (!(cls = sen_db_store_open(ctx->db, name)) || cls->type != sen_db_class) {
          QLERR("Invalid class %s", name);
        }
        if (sen_db_class_slotpath(ctx->db, cls->id, "", buf)) {
          QLERR("class open failed %s", name);
        }
        if ((slots = sen_sym_prefix_search(ctx->db->keys, buf))) {
          sen_id *sid;
          SEN_SET_EACH(slots, eh, &sid, NULL, {
            if ((slotname = _sen_sym_key(ctx->db->keys, *sid))) {
              sen_db_store_remove(ctx->db, slotname);
            }
          });
          sen_set_close(slots);
        }
        sen_db_store_remove(ctx->db, name);
      }
      break;
    case 'p' : /* :prefix-search */
    case 'P' :
      {
        char *name;
        POP(car, args);
        if (!(name = str_value(ctx, car))) { return F; }
        {
          sen_records *r;
          if (!(r = sen_records_open(sen_rec_document, sen_rec_none, 0))) {
            return F;
          }
          r->keys = ctx->db->keys;
          SEN_OBJ_NEW(ctx, res);
          rec_obj_bind(res, r, 0);
        }
        sen_sym_prefix_search_with_set(ctx->db->keys, name, RVALUE(res)->records);
        {
          sen_id *rid;
          SEN_SET_EACH(RVALUE(res)->records, eh, &rid, NULL, {
            const char *key = _sen_sym_key(ctx->db->keys, *rid);
            if (key && strchr(key, '.')) { sen_set_del(RVALUE(res)->records, eh); }
          });
        }
      }
      break;
    case 't' : /* :typedef */
    case 'T' :
      {
        char *name;
        sen_obj *cdr;
        sen_db_store_spec spec;
        spec.type = sen_db_class;
        spec.u.c.size = 0;
        spec.u.c.flags = SEN_INDEX_NORMALIZE|SEN_INDEX_SHARED_LEXICON;
        spec.u.c.encoding = ctx->encoding;
        spec.type = sen_db_raw_class;
        POP(car, args);
        if (!(name = str_value(ctx, car))) { return F; }
        if (sen_db_store_open(ctx->db, name)) { return T; /* already exists */ }
        for (cdr = args; PAIRP(cdr); cdr = CDR(cdr)) {
          if (!sen_obj2int(ctx, CAR(cdr))) { spec.u.c.size = CAR(cdr)->u.i.i; }
        }
        if (!spec.u.c.size) { return F; } /* size must be assigned */
        if (!(cls = sen_db_store_create(ctx->db, name, &spec))) { return F; }
        if ((res = INTERN(name)) != F) {
          sen_ql_bind_symbol(cls, res);
        }
      }
      break;
    case '+' : /* :+ (iterator next) */
      {
        POP(res, args);
        if (res->type == sen_db_raw_class ||
            res->type == sen_db_class ||
            res->type == sen_db_obj_slot ||
            res->type == sen_db_ra_slot ||
            res->type == sen_db_ja_slot ||
            res->type == sen_db_idx_slot ||
            res->type == sen_db_vslot ||
            res->type == sen_db_pslot ||
            res->type == sen_db_rel1 ||
            res->type == sen_db_rel2) {
          const char *key;
          sen_id id = res->u.o.self;
          while ((id = sen_sym_next(ctx->db->keys, id))) {
            key = _sen_sym_key(ctx->db->keys, id);
            if (key) { break; }
          }
          if (id == SEN_SYM_NIL) {
            res = F;
          } else {
            res = INTERN(key);
          }
        } else {
          res = F;
        }
      }
      break;
    case '\0' : /* : (iterator begin) */
      {
        const char *key;
        sen_id id = SEN_SYM_NIL;
        while ((id = sen_sym_next(ctx->db->keys, id))) {
          key = _sen_sym_key(ctx->db->keys, id);
          if (key) { break; }
        }
        if (id == SEN_SYM_NIL) {
          res = F;
        } else {
          res = INTERN(key);
        }
      }
      break;
    }
  }
  return res;
}

static sen_obj *
nf_table(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  char *opt;
  sen_db_store *cls;
  sen_obj *car, *res = F;
  sen_db_store_spec spec;
  spec.type = sen_db_class;
  spec.u.c.size = 0;
  spec.u.c.flags = SEN_INDEX_SHARED_LEXICON;
  spec.u.c.encoding = ctx->encoding;
  while (PAIRP(args)) {
    POP(car, args);
    switch (car->type) {
    case sen_db_raw_class :
      if (!(cls = sen_db_store_by_id(ctx->db, car->u.o.self))) { return F; }
      if ((spec.u.c.size = cls->u.bc.element_size) == SEN_SYM_MAX_KEY_SIZE) {
        spec.u.c.size = 0;
      }
      if (spec.u.c.size > SEN_SYM_MAX_KEY_SIZE) { return F; }
      break;
    case sen_db_class :
      if (!(cls = sen_db_store_by_id(ctx->db, car->u.o.self))) { return F; }
      /* todo : support subrecs */
      res = rec_obj_new(ctx, cls, sen_rec_document, sen_rec_none, 0);
      if (ERRP(ctx, SEN_WARN)) { return F; }
      break;
    default :
      if ((opt = str_value(ctx, car))) {
        switch (*opt) {
        case 'd' : /* delimited */
        case 'D' :
          spec.u.c.flags |= SEN_INDEX_DELIMITED;
          break;
        case 'e' : /* euc-jp */
        case 'E' :
          spec.u.c.encoding = sen_enc_euc_jp;
          break;
        case 'k' : /* koi8r */
        case 'K' :
          spec.u.c.encoding = sen_enc_koi8r;
          break;
        case 'l' : /* latin1 */
        case 'L' :
          spec.u.c.encoding = sen_enc_latin1;
          break;
        case 'n' :
        case 'N' :
          switch (opt[1]) {
          case 'g' : /* ngram */
          case 'G' :
            spec.u.c.flags |= SEN_INDEX_NGRAM;
            break;
          case 'o' : /* normalize */
          case 'O' :
            spec.u.c.flags |= SEN_INDEX_NORMALIZE;
            break;
          default :
            QLERR("ambiguous option %s", opt);
          }
          break;
        case 's' :
        case 'S' :
          switch (opt[1]) {
          case 'j' : /* shift-jis */
          case 'J' :
            spec.u.c.encoding = sen_enc_sjis;
            break;
          case 'i' : /* with-sis */
          case 'I' :
            spec.u.c.flags |= SEN_SYM_WITH_SIS;
            break;
          case 'u' : /* surrogate-key */
          case 'U' :
            spec.type = sen_db_rel1;
            spec.u.s.class = 0;
            spec.u.s.size = 1;
            break;
          default :
            QLERR("ambiguous option %s", opt);
          }
          break;
        case 'u' : /* utf8 */
        case 'U' :
          spec.u.c.encoding = sen_enc_utf8;
          break;
        case 'v' : /* view */
        case 'V' :
          /* todo */
          break;
        default : /* numeric */
          if (sen_obj2int(ctx, car)) {
            /* todo : illegal option */
          } else {
            spec.u.c.size = car->u.i.i;
          }
          break;
        }
      } else {
        /* todo : invalid arg */
      }
    }
  }
  /* todo : support anonymous class */
  return res;
}

static sen_obj *
nf_ptable(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  sen_obj *car;
  char *name, *opt;
  sen_db_store_spec spec;
  spec.type = sen_db_class;
  spec.u.c.size = 0;
  spec.u.c.flags = SEN_INDEX_SHARED_LEXICON;
  spec.u.c.encoding = ctx->encoding;
  POP(car, args);
  if (!(name = str_value(ctx, car))) { return F; }
  if (sen_db_store_open(ctx->db, name)) { return T; }
  while (PAIRP(args)) {
    POP(car, args);
    switch (car->type) {
    case sen_db_raw_class :
      {
        sen_db_store *cls = sen_db_store_by_id(ctx->db, car->u.o.self);
        if (!cls) { return F; }
        if ((spec.u.c.size = cls->u.bc.element_size) == SEN_SYM_MAX_KEY_SIZE) {
          spec.u.c.size = 0;
        }
        if (spec.u.c.size > SEN_SYM_MAX_KEY_SIZE) { return F; }
      }
      break;
    case sen_db_class :
      spec.type = sen_db_rel1;
      spec.u.s.class = car->u.o.self;
      spec.u.s.size = 1;
      break;
    default :
      if ((opt = str_value(ctx, car))) {
        switch (*opt) {
        case 'd' : /* delimited */
        case 'D' :
          spec.u.c.flags |= SEN_INDEX_DELIMITED;
          break;
        case 'e' : /* euc-jp */
        case 'E' :
          spec.u.c.encoding = sen_enc_euc_jp;
          break;
        case 'k' : /* koi8r */
        case 'K' :
          spec.u.c.encoding = sen_enc_koi8r;
          break;
        case 'l' : /* latin1 */
        case 'L' :
          spec.u.c.encoding = sen_enc_latin1;
          break;
        case 'n' :
        case 'N' :
          switch (opt[1]) {
          case 'g' : /* ngram */
          case 'G' :
            spec.u.c.flags |= SEN_INDEX_NGRAM;
            break;
          case 'o' : /* normalize */
          case 'O' :
            spec.u.c.flags |= SEN_INDEX_NORMALIZE;
            break;
          default :
            QLERR("ambiguous option %s", opt);
          }
          break;
        case 's' :
        case 'S' :
          switch (opt[1]) {
          case 'j' : /* shift-jis */
          case 'J' :
            spec.u.c.encoding = sen_enc_sjis;
            break;
          case 'i' : /* with-sis */
          case 'I' :
            spec.u.c.flags |= SEN_SYM_WITH_SIS;
            break;
          case 'u' : /* surrogate-key */
          case 'U' :
            spec.type = sen_db_rel1;
            spec.u.s.class = 0;
            spec.u.s.size = 1;
            break;
          default :
            QLERR("ambiguous option %s", opt);
          }
          break;
        case 'u' : /* utf8 */
        case 'U' :
          spec.u.c.encoding = sen_enc_utf8;
          break;
        case 'v' : /* view */
        case 'V' :
          /* todo */
          break;
        default : /* numeric */
          if (sen_obj2int(ctx, car)) {
            QLERR("illegal option");
          } else {
            spec.u.c.size = car->u.i.i;
          }
          break;
        }
      } else {
        QLERR("invalid arg");
      }
    }
  }
  {
    sen_obj *res;
    sen_db_store *cls;
    if (!(cls = sen_db_store_create(ctx->db, name, &spec))) { return F; }
    if ((res = INTERN(name)) != F) {
      sen_ql_bind_symbol(cls, res);
    }
    return res;
  }
}

const char *
_sen_obj_key(sen_ctx *ctx, sen_obj *obj)
{
  sen_db_store *cls;
  switch (obj->type) {
  case sen_ql_object :
    if (obj->class) {
      if (!(cls = sen_db_store_by_id(ctx->db, obj->class))) { return NULL; }
      switch (cls->type) {
      case sen_db_class :
        return _sen_sym_key(cls->u.c.keys, obj->u.o.self);
      case sen_db_rel1 :
        {
          /* todo : return key value when cls->u.f.class exists */
          sen_obj *p = int2strobj(ctx, obj->u.o.self);
          return p ? p->u.b.value : NULL;
        }
      default :
        return NULL;
      }
    } else {
      return _sen_sym_key(ctx->db->keys, obj->u.o.self);
    }
  case sen_db_raw_class :
  case sen_db_class :
  case sen_db_obj_slot :
  case sen_db_ra_slot :
  case sen_db_ja_slot :
  case sen_db_idx_slot :
    return _sen_sym_key(ctx->db->keys, obj->u.o.self);
  default :
    return NULL;
  }
}

#define flags(p)         ((p)->flags)
#define issymbol(p)     (flags(p) & SEN_OBJ_SYMBOL)
#define ismacro(p)      (flags(p) & SEN_OBJ_MACRO)

static void disp_j(sen_ctx *ctx, sen_obj *obj, sen_rbuf *buf);

static void
disp_j_with_format(sen_ctx *ctx, sen_obj *args, sen_rbuf *buf)
{
  sen_obj *car;
  POP(car, args);
  switch (car->type) {
  case sen_ql_records :
    {
      sen_id *rp, base;
      recinfo *ri;
      sen_obj *slots, *s, **d, *se, *v;
      const sen_recordh *rh;
      int i, o, hashp = 0, offset = 0, limit = 10;
      sen_records *r = RVALUE(car);
      base = car->class;
      POP(slots, args);
      if (!PAIRP(slots)) {
        disp_j(ctx, car, buf);
        if (ERRP(ctx, SEN_WARN)) { return; }
        return;
      }
      if (CAR(slots) == INTERN("@")) {
        hashp = 1;
        slots = CDR(slots);
      }
      for (s = slots, d = &slots, o = 0; PAIRP(s); s = CDR(s), d = &CDR(*d), o = 1 - o) {
        if (hashp && !o) {
          se = CAR(s);
        } else {
          se = ses_prepare(ctx, base, CAR(s), r);
          /* se = slotexp_prepare(ctx, base, CAR(s), r); */
          if (ERRP(ctx, SEN_WARN)) { return; }
        }
        *d = CONS(se, NIL);
      }
      POP(car, args);
      if (!sen_obj2int(ctx, car)) { offset = car->u.i.i; }
      POP(car, args);
      if (!sen_obj2int(ctx, car)) { limit = car->u.i.i; }
      if (limit <= 0) { limit = r->records->n_entries; }
      sen_records_rewind(r);
      for (i = 0; i < offset; i++) {
        if (!sen_records_next(r, NULL, 0, NULL)) { break; }
      }
      SEN_RBUF_PUTC(buf, '[');
      for (i = 0; i < limit; i++) {
        if (!sen_records_next(r, NULL, 0, NULL) ||
            !(rh = sen_records_curr_rec(r)) ||
            sen_set_element_info(r->records, rh, (void **)&rp, (void **)&ri)) {
          break;
        }
        if (i) { SEN_RBUF_PUTS(buf, ", "); }
        SEN_RBUF_PUTC(buf, hashp ? '{' : '[');
        for (s = slots, o = 0;; o = 1 - o) {
          POP(se, s);
          if (hashp && !o) {
            v = se;
            disp_j(ctx, v, buf);
          } else {
            obj_obj_bind(&ctx->curobj, base, *rp);
            v = ses_exec(ctx, se, ri, slots);
            /* v = slotexp_exec(ctx, se, &obj, ri); */
            disp_j(ctx, v, buf);
            ses_clear(ctx);
          }
          if (ERRP(ctx, SEN_WARN)) { return; }
          if (!PAIRP(s)) { break; }
          SEN_RBUF_PUTS(buf, (hashp && !o) ? ": " : ", ");
        }
        SEN_RBUF_PUTC(buf, hashp ? '}' : ']');
      }
      SEN_RBUF_PUTC(buf, ']');
    }
    break;
  case sen_ql_object :
    {
      sen_id id = car->u.o.self, base = car->class;
      int o, hashp = 0;
      sen_obj *slots, *v;
      POP(slots, args);
      if (!PAIRP(slots)) {
        disp_j(ctx, car, buf);
        return;
      }
      if (CAR(slots) == INTERN("@")) {
        hashp = 1;
        slots = CDR(slots);
        if (!PAIRP(slots)) {
          disp_j(ctx, car, buf);
          return;
        }
      }
      SEN_RBUF_PUTC(buf, hashp ? '{' : '[');
      for (o = 0; ; o = 1 - o) {
        if (hashp && !o) {
          v = CAR(slots);
          disp_j(ctx, v, buf);
        } else {
          sen_obj *se;
          se = ses_prepare(ctx, base, CAR(slots), NULL);
          /* se = slotexp_prepare(ctx, base, CAR(slots), NULL); */
          if (ERRP(ctx, SEN_WARN)) { return; }
          obj_obj_bind(&ctx->curobj, base, id);
          v = ses_exec(ctx, se, NULL, se);
          /* v = slotexp_exec(ctx, se, &obj, NULL); */
          disp_j(ctx, v, buf);
          ses_clear(ctx);
        }
        if (ERRP(ctx, SEN_WARN)) { return; }
        slots = CDR(slots);
        if (!PAIRP(slots)) { break; }
        SEN_RBUF_PUTS(buf, (hashp && !o) ? ": " : ", ");
      }
      SEN_RBUF_PUTC(buf, hashp ? '}' : ']');
    }
    break;
  default :
    disp_j(ctx, car, buf);
    if (ERRP(ctx, SEN_WARN)) { return; }
    break;
  }
}

static void
disp_j(sen_ctx *ctx, sen_obj *obj, sen_rbuf *buf)
{
  if (!obj || obj == NIL) {
    SEN_RBUF_PUTS(buf, "[]");
  } else if (obj == T) {
    SEN_RBUF_PUTS(buf, "true");
  } else if (obj == F) {
    SEN_RBUF_PUTS(buf, "false");
  } else {
    switch (obj->type) {
    case sen_ql_void :
      if (issymbol(obj) && obj != INTERN("null")) {
        const char *r = SEN_SET_STRKEY_BY_VAL(obj);
        sen_rbuf_str_esc(buf, (*r == ':') ? r + 1 : r, -1, ctx->encoding);
      } else {
        SEN_RBUF_PUTS(buf, "null");
      }
      break;
    case sen_ql_records :
      {
        int i;
        sen_id *rp;
        recinfo *ri;
        sen_obj o;
        const sen_recordh *rh;
        sen_records *r = RVALUE(obj);
        sen_records_rewind(r);
        obj_obj_bind(&o, obj->class, 0);
        SEN_RBUF_PUTC(buf, '[');
        for (i = 0;; i++) {
          if (!sen_records_next(r, NULL, 0, NULL) ||
              !(rh = sen_records_curr_rec(r)) ||
              sen_set_element_info(r->records, rh, (void **)&rp, (void **)&ri)) {
            break;
          }
          if (i) { SEN_RBUF_PUTS(buf, ", "); }
          o.u.o.self = *rp;
          disp_j(ctx, &o, buf);
          if (ERRP(ctx, SEN_WARN)) { return; }
        }
        SEN_RBUF_PUTC(buf, ']');
      }
      break;
    case sen_ql_list :
      if (obj->u.l.car == INTERN(":")) {
        disp_j_with_format(ctx, obj->u.l.cdr, buf);
        if (ERRP(ctx, SEN_WARN)) { return; }
      } else if (obj->u.l.car == INTERN("@")) {
        int o;
        SEN_RBUF_PUTC(buf, '{');
        for (obj = obj->u.l.cdr, o = 0;; o = 1 - o) {
          if (PAIRP(obj)) {
            disp_j(ctx, obj->u.l.car, buf);
            if (ERRP(ctx, SEN_WARN)) { return; }
          }
          if ((obj = obj->u.l.cdr) && (obj != NIL)) {
            if (PAIRP(obj)) {
              SEN_RBUF_PUTS(buf, o ? ", " : ": ");
            } else {
              SEN_RBUF_PUTS(buf, " . ");
              disp_j(ctx, obj, buf);
              if (ERRP(ctx, SEN_WARN)) { return; }
              SEN_RBUF_PUTC(buf, '}');
              break;
            }
          } else {
            SEN_RBUF_PUTC(buf, '}');
            break;
          }
        }
      } else {
        SEN_RBUF_PUTC(buf, '[');
        for (;;) {
          disp_j(ctx, obj->u.l.car, buf);
          if (ERRP(ctx, SEN_WARN)) { return; }
          if ((obj = obj->u.l.cdr) && (obj != NIL)) {
            if (PAIRP(obj)) {
              SEN_RBUF_PUTS(buf, ", ");
            } else {
              SEN_RBUF_PUTS(buf, " . ");
              disp_j(ctx, obj, buf);
              if (ERRP(ctx, SEN_WARN)) { return; }
              SEN_RBUF_PUTC(buf, ']');
              break;
            }
          } else {
            SEN_RBUF_PUTC(buf, ']');
            break;
          }
        }
      }
      break;
    case sen_ql_object :
      {
        const char *key = _sen_obj_key(ctx, obj);
        if (key) {
          sen_rbuf_str_esc(buf, key, -1, ctx->encoding);
        } else {
          SEN_RBUF_PUTS(buf, "<LOSTKEY>");
        }
      }
      break;
    default :
      sen_obj_inspect(ctx, obj, buf, SEN_OBJ_INSPECT_ESC|SEN_OBJ_INSPECT_SYM_AS_STR);
      break;
    }
  }
}

static void disp_t(sen_ctx *ctx, sen_obj *obj, sen_rbuf *buf, int *f);

static void
disp_t_with_format(sen_ctx *ctx, sen_obj *args, sen_rbuf *buf, int *f)
{
  sen_obj *car;
  POP(car, args);
  switch (car->type) {
  case sen_ql_records :
    {
      sen_id *rp, base;
      recinfo *ri;
      sen_obj *slots, *s, **d, *se, *v;
      const sen_recordh *rh;
      int i, o, hashp = 0, offset = 0, limit = 10;
      sen_records *r = RVALUE(car);
      base = car->class;
      POP(slots, args);
      if (!PAIRP(slots)) {
        disp_t(ctx, car, buf, f);
        return;
      }
      if (CAR(slots) == INTERN("@")) {
        hashp = 1;
        slots = CDR(slots);
      }
      for (s = slots, d = &slots, o = 0; PAIRP(s); s = CDR(s), o = 1 - o) {
        if (hashp && !o) {
          if (s != slots) { SEN_RBUF_PUTC(buf, '\t'); *f = 1; }
          disp_t(ctx, CAR(s), buf, f);
        } else {
          se = ses_prepare(ctx, base, CAR(s), r);
          /* se = slotexp_prepare(ctx, base, CAR(s), r); */
          if (ERRP(ctx, SEN_WARN)) { return ; }
          *d = CONS(se, NIL);
          d = &CDR(*d);
        }
      }
      POP(car, args);
      if (!sen_obj2int(ctx, car)) { offset = car->u.i.i; }
      POP(car, args);
      if (!sen_obj2int(ctx, car)) { limit = car->u.i.i; }
      if (limit <= 0) { limit = r->records->n_entries; }
      sen_records_rewind(r);
      for (i = 0; i < offset; i++) {
        if (!sen_records_next(r, NULL, 0, NULL)) { break; }
      }
      for (i = 0; i < limit; i++) {
        if (!sen_records_next(r, NULL, 0, NULL) ||
            !(rh = sen_records_curr_rec(r)) ||
            sen_set_element_info(r->records, rh, (void **)&rp, (void **)&ri)) {
          break;
        }
        if (*f) { ctx->output(ctx, SEN_CTX_MORE, ctx->data.ptr); *f = 0; }
        for (s = slots;;) {
          POP(se, s);
          obj_obj_bind(&ctx->curobj, base, *rp);
          v = ses_exec(ctx, se, ri, slots);
          /* v = slotexp_exec(ctx, t, &obj, ri); */
          disp_t(ctx, v, buf, f);
          ses_clear(ctx);
          if (!PAIRP(s)) { break; }
          SEN_RBUF_PUTC(buf, '\t'); *f = 1;
        }
      }
    }
    break;
  case sen_ql_object :
    {
      sen_id id = car->u.o.self, base = car->class;
      int o, hashp = 0;
      sen_obj *slots, *val, *v;
      POP(slots, args);
      if (!PAIRP(slots)) {
        disp_t(ctx, car, buf, f);
        return;
      }
      if (CAR(slots) == INTERN("@")) {
        hashp = 1;
        slots = CDR(slots);
        if (!PAIRP(slots)) {
          disp_t(ctx, car, buf, f);
          return;
        }
        if (*f) { ctx->output(ctx, SEN_CTX_MORE, ctx->data.ptr); *f = 0; }
        for (o = 0, val = slots; ; o = 1 - o) {
          if (!o) {
            if (val != slots) { SEN_RBUF_PUTC(buf, '\t'); *f = 1; }
            disp_t(ctx, CAR(val), buf, f);
          }
          val = CDR(val);
          if (!PAIRP(val)) { break; }
        }
      }
      for (o = 0, val = slots; ; o = 1 - o) {
        if (hashp && !o) {
          val = CDR(val);
          if (!PAIRP(val)) { break; }
        } else {
          sen_obj *se;
          se = ses_prepare(ctx, base, CAR(val), NULL);
          /* se = slotexp_prepare(ctx, base, CAR(val), NULL); */
          if (ERRP(ctx, SEN_WARN)) { return; }
          obj_obj_bind(&ctx->curobj, base, id);
          v = ses_exec(ctx, se, NULL, se);
          /* v = slotexp_exec(ctx, se, &obj, NULL); */
          disp_t(ctx, v, buf, f);
          ses_clear(ctx);
          val = CDR(val);
          if (!PAIRP(val)) { break; }
          if (val != slots) { SEN_RBUF_PUTC(buf, '\t'); *f = 1; }
        }
      }
    }
    break;
  default :
    disp_t(ctx, car, buf, f);
    break;
  }
}

static void
disp_t(sen_ctx *ctx, sen_obj *obj, sen_rbuf *buf, int *f)
{
  if (!obj || obj == NIL) {
    SEN_RBUF_PUTS(buf, "()"); *f = 1;
  } else if (obj == T) {
    SEN_RBUF_PUTS(buf, "#t"); *f = 1;
  } else if (obj == F) {
    SEN_RBUF_PUTS(buf, "#f"); *f = 1;
  } else {
    switch (obj->type) {
    case sen_ql_records :
      {
        int i;
        sen_id *rp;
        recinfo *ri;
        sen_obj o;
        const sen_recordh *rh;
        sen_records *r = RVALUE(obj);
        sen_records_rewind(r);
        obj_obj_bind(&o, obj->class, 0);
        for (i = 0;; i++) {
          if (!sen_records_next(r, NULL, 0, NULL) ||
              !(rh = sen_records_curr_rec(r)) ||
              sen_set_element_info(r->records, rh, (void **)&rp, (void **)&ri)) {
            break;
          }
          o.u.o.self = *rp;
          if (*f) { ctx->output(ctx, SEN_CTX_MORE, ctx->data.ptr); *f = 0; }
          disp_t(ctx, &o, buf, f);
        }
      }
      break;
    case sen_ql_list :
      if (obj->u.l.car == INTERN(":")) {
        disp_t_with_format(ctx, obj->u.l.cdr, buf, f);
      } else if (obj->u.l.car == INTERN("@")) {
        int o0, o;
        sen_obj *val = obj->u.l.cdr;
        for (o0 = 0; o0 <= 1; o0++) {
          if (*f) { ctx->output(ctx, SEN_CTX_MORE, ctx->data.ptr); *f = 0; }
          for (obj = val, o = o0;; o = 1 - o) {
            if (!o) { disp_t(ctx, obj->u.l.car, buf, f); }
            if ((obj = obj->u.l.cdr) && (obj != NIL)) {
              if (PAIRP(obj)) {
                if (!o && PAIRP(CDR(obj))) { SEN_RBUF_PUTC(buf, '\t'); *f = 1; }
              } else {
                if (!o) {
                  SEN_RBUF_PUTC(buf, '\t'); *f = 1; /* dot pair */
                  disp_t(ctx, obj, buf, f);
                }
                break;
              }
            } else {
              break;
            }
          }
        }
      } else {
        for (;;) {
          disp_t(ctx, obj->u.l.car, buf, f);
          if ((obj = obj->u.l.cdr) && (obj != NIL)) {
            if (PAIRP(obj)) {
              SEN_RBUF_PUTC(buf, '\t'); *f = 1;
            } else {
              SEN_RBUF_PUTC(buf, '\t'); *f = 1; /* dot pair */
              disp_t(ctx, obj, buf, f);
              break;
            }
          } else {
            break;
          }
        }
      }
      break;
    default :
      sen_obj_inspect(ctx, obj, buf, 0); *f = 1;
      break;
    }
  }
}

static sen_obj *
nf_disp(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  char *str;
  int f = 0;
  sen_obj *val, *fmt;
  POP(val, args);
  POP(fmt, args);
  if ((str = str_value(ctx, fmt))) {
    switch (str[0]) {
    case 'j' : /* json */
    case 'J' :
      disp_j(ctx, val, &ctx->outbuf);
      f = 1;
      if (ERRP(ctx, SEN_WARN)) { return F; }
      break;
    case 's' : /* sexp */
    case 'S' :
      break;
    case 't' : /* tsv */
    case 'T' :
      disp_t(ctx, val, &ctx->outbuf, &f);
      if (ERRP(ctx, SEN_WARN)) { return F; }
      break;
    case 'x' : /* xml */
    case 'X' :
      break;
    }
  } else {
    QLERR("Few arguments");
  }
  if (f) {
    ctx->output(ctx, SEN_CTX_MORE, ctx->data.ptr);
    if (ERRP(ctx, SEN_WARN)) { return F; }
  }
  return T;
}

typedef struct {
  sen_encoding encoding;
  char *cur;
  char *str_end;
} jctx;

inline static sen_obj *
mk_atom(sen_ctx *ctx, char *str, unsigned int len)
{
  const char *cur, *str_end = str + len;
  int64_t ivalue = sen_atoll(str, str_end, &cur);
  if (cur == str_end) {
    sen_obj *x;
    SEN_OBJ_NEW(ctx, x);
    SETINT(x, ivalue);
    return x;
  }
  switch (*str) {
  case 't' :
    if (len == 4 && !memcmp(str, "true", 4)) { return T; }
    break;
  case 'f' :
    if (len == 5 && !memcmp(str, "false", 5)) { return F; }
    break;
    /*
  case 'n' :
    if (len == 4 && !memcmp(str, "null", 4)) { return NIL; }
    break;
    */
  }
  if (0 < len && len < SEN_SYM_MAX_KEY_SIZE - 1) {
    char buf[SEN_SYM_MAX_KEY_SIZE];
    memcpy(buf, str, len);
    buf[len] = '\0';
    return INTERN(buf);
  } else {
    return F;
  }
}

inline sen_obj *
json_readstr(sen_ctx *ctx, jctx *jc)
{
  char *start, *end;
  for (start = end = jc->cur;;) {
    unsigned int len;
    /* null check and length check */
    if (!(len = sen_str_charlen_nonnull(end, jc->str_end, jc->encoding))) {
      jc->cur = jc->str_end;
      break;
    }
    if (sen_isspace(end, jc->encoding)
        || *end == ':' || *end == ','
        || *end == '[' || *end == '{'
        || *end == ']' || *end == '}') {
      jc->cur = end;
      break;
    }
    end += len;
  }
  if (start < end || jc->cur < jc->str_end) {
    return mk_atom(ctx, start, end - start);
  } else {
    return F;
  }
}

inline sen_obj *
json_readstrexp(sen_ctx *ctx, jctx *jc)
{
  sen_obj *res;
  char *start, *src, *dest;
  for (start = src = dest = jc->cur;;) {
    unsigned int len;
    /* null check and length check */
    if (!(len = sen_str_charlen_nonnull(src, jc->str_end, jc->encoding))) {
      jc->cur = jc->str_end;
      if (start < dest) {
        res = sen_ql_mk_string(ctx, start, dest - start);
        return res ? res : F;
      }
      return F;
    }
    if (src[0] == '"' && len == 1) {
      jc->cur = src + 1;
      res = sen_ql_mk_string(ctx, start, dest - start);
      return res ? res : F;
    } else if (src[0] == '\\' && src + 1 < jc->str_end && len == 1) {
      src++;
      *dest++ = *src++;
    } else {
      while (len--) { *dest++ = *src++; }
    }
  }
}

static sen_obj *
json_read(sen_ctx *ctx, jctx *jc)
{
  for (;;) {
    SKIPSPACE(jc);
    if (jc->cur >= jc->str_end) { return NULL; }
    switch (*jc->cur) {
    case '[':
      jc->cur++;
      {
        sen_obj *o, *r = NIL, **p = &r;
        while ((o = json_read(ctx, jc)) && o != F) {
          *p = CONS(o, NIL);
          if (ERRP(ctx, SEN_WARN)) { return F; }
          p = &CDR(*p);
        }
        return r;
      }
    case '{':
      jc->cur++;
      {
        sen_obj *o, *r = CONS(INTERN("@"), NIL), **p = &(CDR(r));
        while ((o = json_read(ctx, jc)) && o != F) {
          *p = CONS(o, NIL);
          if (ERRP(ctx, SEN_WARN)) { return F; }
          p = &CDR(*p);
        }
        return r;
      }
    case '}':
    case ']':
      jc->cur++;
      return NULL;
    case ',':
      jc->cur++;
      break;
    case ':':
      jc->cur++;
      break;
    case '"':
      jc->cur++;
      return json_readstrexp(ctx, jc);
    default:
      return json_readstr(ctx, jc);
    }
  }
}

static sen_obj *
nf_json_read(sen_ctx *ctx, sen_obj *args, sen_ql_co *co)
{
  sen_obj *car;
  POP(car, args); // todo : delete when called with (())
  if (BULKP(car)) {
    sen_obj *r;
    jctx jc;
    jc.encoding = ctx->encoding;
    jc.cur = car->u.b.value;
    jc.str_end = car->u.b.value + car->u.b.size;
    if ((r = json_read(ctx, &jc))) { return r; }
  }
  return F;
}

void
sen_ql_def_db_funcs(sen_ctx *ctx)
{
  sen_ql_def_native_func(ctx, "<db>", nf_db);
  sen_ql_def_native_func(ctx, "table", nf_table);
  sen_ql_def_native_func(ctx, "ptable", nf_ptable);
  sen_ql_def_native_func(ctx, "snippet", nf_snippet);
  sen_ql_def_native_func(ctx, "disp", nf_disp);
  sen_ql_def_native_func(ctx, "json-read", nf_json_read);
  sen_ql_def_native_func(ctx, "x->query", nf_toquery);
}
