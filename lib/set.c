/* Copyright(C) 2004 Brazil

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
#include <limits.h>
#include "set.h"

#define INITIAL_N_ENTRIES 256U
#define INITIAL_INDEX_SIZE 256U

#if defined(MAP_ANON) && !defined(MAP_ANONYMOUS)
#define MAP_ANONYMOUS MAP_ANON
#endif

#define GARBAGE ((entry *) 1)

#define STEP(x) (((x) >> 2) | 0x1010101)

typedef struct _sen_set_element entry;
typedef struct _sen_set_element_str entry_str;

#define SEN_SET_STRHASH(e) (((entry_str *)(e))->key)

inline static entry *
entry_new(sen_set *set)
{
  entry *e;
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  if (set->garbages) {
    e = set->garbages;
    set->garbages = *((entry **)e);
    memset(e, 0, set->entry_size);
  } else {
    byte *chunk = set->chunks[set->curr_chunk];
    if (!chunk) {
      chunk = SEN_CALLOC(set->entry_size * (INITIAL_N_ENTRIES << set->curr_chunk));
      if (!chunk) { return NULL; }
      set->chunks[set->curr_chunk] = chunk;
    }
    e = (entry *)(chunk + set->entry_size * set->curr_entry);
    if (++set->curr_entry >= (INITIAL_N_ENTRIES << set->curr_chunk)) {
      set->curr_chunk++;
      set->curr_entry = 0;
    }
  }
  return e;
}

sen_set *
sen_set_open(uint32_t key_size, uint32_t value_size, uint32_t init_size)
{
  sen_set *set;
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  uint32_t entry_size, n, mod;
  for (n = INITIAL_INDEX_SIZE; n < init_size; n *= 2);
  switch (key_size) {
  case 0 :
    entry_size = (intptr_t)(&((entry_str *)0)->dummy) + value_size;
    break;
  case sizeof(uint32_t) :
    entry_size = (intptr_t)(&((entry *)0)->dummy) + value_size;
    break;
  default :
    entry_size = (intptr_t)(&((entry *)0)->dummy) + key_size + value_size;
    break;
  }
  if ((mod = entry_size % sizeof(intptr_t))) {
    entry_size += sizeof(intptr_t) - mod;
  }
  if (!(set = SEN_MALLOC(sizeof(sen_set)))) { return NULL; }
  memset(set, 0, sizeof(sen_set));
  set->key_size = key_size;
  set->value_size = value_size;
  set->entry_size = entry_size;
  set->max_offset = n - 1;
  if (!(set->index = SEN_CALLOC(n * sizeof(entry *)))) {
    SEN_FREE(set);
    return NULL;
  }
  return set;
}

sen_rc
sen_set_reset(sen_set * set, uint32_t ne)
{
  uint32_t i, j, m, n, s;
  entry **index, *e, **sp, **dp;
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  if (!ne) { ne = set->n_entries * 2; }
  if (ne > INT_MAX) { return sen_memory_exhausted; }
  for (n = INITIAL_INDEX_SIZE; n <= ne; n *= 2);
  if (!(index = SEN_CALLOC(n * sizeof(entry *)))) { return sen_memory_exhausted; }
  m = n - 1;
  if (set->key_size) {
    for (j = set->max_offset + 1, sp = set->index; j; j--, sp++) {
      e = *sp;
      if (!e || (e == GARBAGE)) { continue; }
      for (i = e->key, s = STEP(i); ; i += s) {
        dp = index + (i & m);
        if (!*dp) { break; }
      }
      *dp = e;
    }
  } else {
    for (j = set->max_offset + 1, sp = set->index; j; j--, sp++) {
      e = *sp;
      if (!e || (e == GARBAGE)) { continue; }
      for (i = SEN_SET_STRHASH(e), s = STEP(i); ; i += s) {
        dp = index + (i & m);
        if (!*dp) { break; }
      }
      *dp = e;
    }
  }
  {
    entry **i0 = set->index;
    set->index = index;
    set->max_offset = m;
    set->n_garbages = 0;
    SEN_FREE(i0);
  }
  return sen_success;
}

sen_rc
sen_set_close(sen_set * set)
{
  uint32_t i;
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  if (!set) { return sen_invalid_argument; }
  if (!set->key_size) {
    entry *e, **sp;
    for (i = set->max_offset + 1, sp = set->index; i; i--, sp++) {
      e = *sp;
      if (!e || (e == GARBAGE)) { continue; }
      if (SEN_SET_STRKEY(e)) { SEN_FREE(SEN_SET_STRKEY(e)); }
    }
  }
  for (i = 0; i <= SEN_SET_MAX_CHUNK; i++) {
    if (set->chunks[i]) { SEN_FREE(set->chunks[i]); }
  }
  SEN_FREE(set->index);
  SEN_FREE(set);
  return sen_success;
}

sen_rc
sen_set_info(sen_set *set, unsigned *key_size, unsigned *value_size,
             unsigned *n_entries)
{
  if (!set) { return sen_invalid_argument; }
  if (key_size) { *key_size = set->key_size; }
  if (value_size) { *value_size = set->value_size; }
  if (n_entries) { *n_entries = set->n_entries; }
  return sen_success;
}

inline static uint32_t
str_hash(const unsigned char *p)
{
  uint32_t r;
  for (r = 0; *p; p++) { r = (r * 1021) + *p; }
  return r;
}

inline static uint32_t
bin_hash(const uint8_t *p, uint32_t length)
{
  uint32_t r;
  for (r = 0; length--; p++) { r = (r * 1021) + *p; }
  return r;
}

sen_set_eh *
sen_set_int_at(sen_set *set, const uint32_t *key, void **value)
{
  entry *e, **ep, **index = set->index;
  uint32_t h = *key, i, m = set->max_offset, s = STEP(h);
  for (i = h; ep = index + (i & m), (e = *ep); i += s) {
    if (e == GARBAGE) { continue; }
    if (e->key == h) {
      if (value) { *value = e->dummy; }
      return ep;
    }
  }
  return NULL;
}

sen_set_eh *
sen_set_str_at(sen_set *set, const char *key, void **value)
{
  entry *e, **ep, **index = set->index;
  uint32_t h = str_hash((unsigned char *)key), i, m = set->max_offset, s = STEP(h);
  for (i = h; ep = index + (i & m), (e = *ep); i += s) {
    if (e == GARBAGE) { continue; }
    if (SEN_SET_STRHASH(e) == h && !strcmp(key, SEN_SET_STRKEY(e))) {
      if (value) { *value = SEN_SET_STRVAL(e); }
      return (sen_set_eh *) ep;
    }
  }
  return NULL;
}

sen_set_eh *
sen_set_bin_at(sen_set *set, const void *key, void **value)
{
  entry *e, **ep, **index = set->index;
  uint32_t h = bin_hash(key, set->key_size), i, m = set->max_offset, s = STEP(h);
  for (i = h; ep = index + (i & m), (e = *ep); i += s) {
    if (e == GARBAGE) { continue; }
    if (e->key == h && !memcmp(key, e->dummy, set->key_size)) {
      if (value) { *value = SEN_SET_BINVAL(e, set); }
      return ep;
    }
  }
  return NULL;
}

sen_set_eh *
sen_set_at(sen_set *set, const void *key, void **value)
{
  if (set->arrayp) {
    if (sen_set_reset(set, 0)) { return NULL; }
    set->curr_entry = 0;
    set->arrayp = 0;
  }
  switch (set->key_size) {
  case 0 :
    return sen_set_str_at(set, key, value);
  case sizeof(uint32_t) :
    return sen_set_int_at(set, key, value);
  default :
    return sen_set_bin_at(set, key, value);
  }
}

inline static sen_set_eh *
sen_set_int_get(sen_set *set, const uint32_t *key, void **value)
{
  static int _ncalls = 0, _ncolls = 0;
  entry *e, **ep, **np = NULL, **index = set->index;
  uint32_t h = *key, i, m = set->max_offset, s = STEP(h);
  _ncalls++;
  for (i = h; ep = index + (i & m), e = *ep; i += s) {
    if (e == GARBAGE) {
      if (!np) { np = ep; }
    } else {
      if (e->key == h) { goto exit; }
    }
    if (!(++_ncolls % 1000000) && (_ncolls > _ncalls)) {
      if (_ncolls < 0 || _ncalls < 0) {
        _ncolls = 0; _ncalls = 0;
      } else {
        SEN_LOG(sen_log_notice, "total:%d collisions:%d nentries:%d maxoffset:%d",
                _ncalls, _ncolls, set->n_entries, set->max_offset);
      }
    }
  }
  if (np) {
    set->n_garbages--;
    ep = np;
  }
  if (!(e = entry_new(set))) { return NULL; }
  e->key = h;
  *ep = e;
  set->n_entries++;
exit :
  if (value) { *value = e->dummy; }
  return ep;
}

inline static sen_set_eh *
sen_set_str_get(sen_set *set, const char *key, void **value)
{
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  entry *e, **ep, **np = NULL, **index = set->index;
  uint32_t h = str_hash((unsigned char *)key), i, m = set->max_offset, s = STEP(h);
  for (i = h; ep = index + (i & m), e = *ep; i += s) {
    if (e == GARBAGE) {
      if (!np) { np = ep; }
    } else {
      if (SEN_SET_STRHASH(e) == h && !strcmp(key, SEN_SET_STRKEY(e))) { goto exit; }
    }
  }
  {
    char *keybuf = SEN_STRDUP(key);
    if (!keybuf) { return NULL; }
    if (!(e = entry_new(set))) {
      SEN_FREE(keybuf);
      return NULL;
    }
    SEN_SET_STRHASH(e) = h;
    SEN_SET_STRKEY(e) = keybuf;
    if (np) {
      set->n_garbages--;
      ep = np;
    }
    *ep = e;
    set->n_entries++;
  }
exit :
  if (value) { *value = SEN_SET_STRVAL(e); }
  return ep;
}

inline static sen_set_eh *
sen_set_bin_get(sen_set *set, const void *key, void **value)
{
  entry *e, **ep, **np = NULL, **index = set->index;
  uint32_t h = bin_hash(key, set->key_size), i, m = set->max_offset, s = STEP(h);
  for (i = h; ep = index + (i & m), e = *ep; i += s) {
    if (e == GARBAGE) {
      if (!np) { np = ep; }
    } else {
      if (e->key == h && !memcmp(key, e->dummy, set->key_size)) {
        goto exit;
      }
    }
  }
  if (np) {
    set->n_garbages--;
    ep = np;
  }
  if (!(e = entry_new(set))) { return NULL; }
  e->key = h;
  memcpy(e->dummy, key, set->key_size);
  *ep = e;
  set->n_entries++;
exit :
  if (value) { *value = &e->dummy[set->key_size]; }
  return ep;
}

sen_set_eh *
sen_set_get(sen_set *set, const void *key, void **value)
{
  if (!set) { return NULL; }
  if (set->arrayp) {
    if (sen_set_reset(set, 0)) { return NULL; }
    set->curr_entry = 0;
    set->arrayp = 0;
  } else if ((set->n_entries + set->n_garbages) * 2 > set->max_offset) {
    sen_set_reset(set, 0);
  }
  switch (set->key_size) {
  case 0 :
    return sen_set_str_get(set, key, value);
  case sizeof(uint32_t) :
    return sen_set_int_get(set, key, value);
  default :
    return sen_set_bin_get(set, key, value);
  }
}

sen_rc
sen_set_del(sen_set *set, sen_set_eh *ep)
{
  entry *e;
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  if (!set || !ep || !*ep) { return sen_invalid_argument; }
  e = *ep;
  *ep = GARBAGE;
  if (!set->key_size) { SEN_FREE(SEN_SET_STRKEY(e)); }
  *((entry **)e) = set->garbages;
  set->garbages = e;
  set->n_entries--;
  set->n_garbages++;
  return sen_success;
}

sen_set_cursor *
sen_set_cursor_open(sen_set *set)
{
  sen_set_cursor *c;
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  if (!set) { return NULL; }
  if (!(c = SEN_MALLOC(sizeof(sen_set_cursor)))) { return NULL; }
  c->set = set;
  c->index = set->index;
  c->curr = set->index;
  c->rest = set->max_offset + 1;
  return c;
}

sen_set_eh *
sen_set_cursor_next(sen_set_cursor *c, void **key, void **value)
{
  uint32_t i;
  entry *e, **ep;
  if (!c || !c->rest) { return NULL; }
  if (c->index != c->set->index) {
    SEN_LOG(sen_log_alert, "sen_reset invoked during sen_set_cursor is opened!");
    return NULL;
  }
  for (i = c->rest, ep = c->curr;;i--, ep++) {
    if (!i) {
      c->rest = 0;
      return NULL;
    }
    e = *ep;
    if (e && e != GARBAGE) { break; }
  }
  switch (c->set->key_size) {
  case 0 :
    if (key) { *key = SEN_SET_STRKEY(e); }
    if (value) { *value = SEN_SET_STRVAL(e); }
    break;
  case sizeof(uint32_t) :
    if (key) { *key = SEN_SET_INTKEY(e); }
    if (value) { *value = SEN_SET_INTVAL(e); }
    break;
  default :
    if (key) { *key = SEN_SET_BINKEY(e); }
    if (value) { *value = SEN_SET_BINVAL(e, c->set); }
    break;
  }
  c->curr = ep + 1;
  c->rest = i - 1;
  return ep;
}

sen_rc
sen_set_cursor_close(sen_set_cursor *cursor)
{
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  SEN_FREE(cursor);
  return sen_success;
}

inline static void
swap(entry **a, entry **b)
{
  entry *c = *a;
  *a = *b;
  *b = c;
}

#define INT_OFFSET_VAL(x) (((int32_t *)(x))[offset] * dir)

inline static entry **
pack_int(sen_set *set, entry **res, int offset, int dir)
{
  uint32_t i, n, m = set->max_offset;
  int32_t ck;
  entry **head, **tail, *e, *c;
  for (i = m >> 1;; i = (i + 1) & m) {
    if ((c = set->index[i]) && (c != GARBAGE)) { break; }
  }
  head = res;
  n = set->n_entries - 1;
  tail = res + n;
  ck = INT_OFFSET_VAL(c);
  while (n--) {
    for (;;) {
      i = (i + 1) & m;
      if ((e = set->index[i]) && (e != GARBAGE)) { break; }
    }
    if (INT_OFFSET_VAL(e) < ck) {
      *head++ = e;
    } else {
      *tail-- = e;
    }
  }
  *head = c;
  return set->n_entries > 2 ? head : NULL;
}

inline static entry **
part_int(entry **b, entry **e, int offset, int dir)
{
  int32_t ck;
  intptr_t d = e - b;
  entry **c;
  if (INT_OFFSET_VAL(*b) > INT_OFFSET_VAL(*e)) { swap(b, e); }
  if (d < 2) { return NULL; }
  c = b + (d >> 1);
  if (INT_OFFSET_VAL(*b) > INT_OFFSET_VAL(*c)) {
    swap(b, c);
  } else {
    if (INT_OFFSET_VAL(*c) > INT_OFFSET_VAL(*e)) { swap(c, e); }
  }
  if (d < 3) { return NULL; }
  b++;
  ck = INT_OFFSET_VAL(*c);
  swap(b, c);
  c = b;
  for (;;) {
    while (INT_OFFSET_VAL(*++b) < ck) ;
    while (INT_OFFSET_VAL(*--e) > ck) ;
    if (b >= e) { break; }
    swap(b, e);
  }
  swap(c, e);
  return e;
}

static void
_sort_int(entry **head, entry **tail, int limit, int offset, int dir)
{
  intptr_t rest;
  entry **c;
  if (head < tail && (c = part_int(head, tail, offset, dir))) {
    rest = limit - 1 - (c - head);
    _sort_int(head, c - 1, limit, offset, dir);
    if (rest > 0) { _sort_int(c + 1, tail, (int)rest, offset, dir); }
  }
}

inline static void
sort_int(sen_set *set, entry **res, int limit, int offset, int dir)
{
  entry **c = pack_int(set, res, offset, dir);
  if (c) {
    intptr_t rest = limit - 1 - (c - res);
    _sort_int(res, c - 1, limit, offset, dir);
    if (rest > 0 ) { _sort_int(c + 1, res + set->n_entries - 1, (int)rest, offset, dir); }
  }
}

inline static entry **
pack_func(sen_set *set, entry **res,
          int(*func)(sen_set *, entry **, sen_set *, entry **, void *),
          void *arg, void *arg0, int dir)
{
  uint32_t i, n, m = set->max_offset;
  entry **head, **tail, *e, *c;
  for (i = m >> 1;; i = (i + 1) & m) {
    if ((c = set->index[i]) && (c != GARBAGE)) { break; }
  }
  head = res;
  n = set->n_entries - 1;
  tail = res + n;
  while (n--) {
    for (;;) {
      i = (i + 1) & m;
      if ((e = set->index[i]) && (e != GARBAGE)) { break; }
    }
    if (func(arg0, &e, arg0, &c, arg) * dir < 0) {
      *head++ = e;
    } else {
      *tail-- = e;
    }
  }
  *head = c;
  return set->n_entries > 2 ? head : NULL;
}

inline static entry **
part_func(entry **b, entry **e,
          int(*func)(sen_set *, entry **, sen_set *, entry **, void *),
          void *arg, void *arg0, int dir)
{
  intptr_t d = e - b;
  entry **c;
  if (func(arg0, b, arg0, e, arg) * dir > 0) { swap(b, e); }
  if (d < 2) { return NULL; }
  c = b + (d >> 1);
  if (func(arg0, b, arg0, c, arg) * dir > 0) {
    swap(b, c);
  } else {
    if (func(arg0, c, arg0, e, arg) * dir > 0) { swap(c, e); }
  }
  if (d < 3) { return NULL; }
  b++;
  swap(b, c);
  c = b;
  for (;;) {
    while (func(arg0, ++b, arg0, c, arg) * dir < 0) ;
    while (func(arg0, --e, arg0, c, arg) * dir > 0) ;
    if (b >= e) { break; }
    swap(b, e);
  }
  swap(c, e);
  return e;
}

static void
_sort_func(entry **head, entry **tail, int limit,
           int(*func)(sen_set *, entry **, sen_set *, entry **, void *),
           void *arg, void *arg0, int dir)
{
  entry **c;
  if (head < tail && (c = part_func(head, tail, func, arg, arg0, dir))) {
    intptr_t rest = limit - 1 - (c - head);
    _sort_func(head, c - 1, limit, func, arg, arg0, dir);
    if (rest > 0) { _sort_func(c + 1, tail, (int)rest, func, arg, arg0, dir); }
  }
}

inline static void
sort_func(sen_set *set, entry **res, int limit,
          int(*func)(sen_set *, entry **, sen_set *, entry **, void *),
          void *arg, void *arg0, int dir)
{
  entry **c = pack_func(set, res, func, arg, arg0, dir);
  if (c) {
    intptr_t rest = limit - 1 - (c - res);
    _sort_func(res, c - 1, limit, func, arg, arg0, dir);
    if (rest > 0 ) {
      _sort_func(c + 1, res + set->n_entries - 1, (int)rest, func, arg, arg0, dir);
    }
  }
}

inline static int
func_str(sen_set *sa, entry **a, sen_set *sb, entry **b, void *arg)
{
  return strcmp(SEN_SET_STRKEY(*a), SEN_SET_STRKEY(*b));
}

inline static int
func_bin(sen_set *sa, entry **a, sen_set *sb, entry **b, void *arg)
{
  return memcmp(SEN_SET_BINKEY(*a), SEN_SET_BINKEY(*b), (uintptr_t)arg);
}

sen_set_eh *
sen_set_sort(sen_set *set, int limit, sen_set_sort_optarg *optarg)
{
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  entry **res;
  int dir = 1;
  if (!set) {
    SEN_LOG(sen_log_warning, "sen_set_sort: invalid argument !");
    return NULL;
  }
  if (!set->n_entries) {
    SEN_LOG(sen_log_warning, "no entry in the set passed for sen_set_sort");
    return NULL;
  }
  if (!(res = SEN_MALLOC(sizeof(entry *) * set->n_entries))) {
    SEN_LOG(sen_log_alert, "allocation of entries failed on sen_set_sort !");
    return NULL;
  }
  if (!limit || limit > set->n_entries) { limit = set->n_entries; }
  if (optarg) {
    dir = (optarg->mode == sen_sort_ascending) ? 1 : -1;
    if (optarg->compar) {
      sort_func(set, res, limit, optarg->compar, optarg->compar_arg,
                optarg->compar_arg0 ? optarg->compar_arg0 : set, dir);
      goto exit;
    } else if (optarg->compar_arg) {
      sort_int(set, res, limit, ((intptr_t)optarg->compar_arg) / sizeof(int32_t), dir);
      goto exit;
    }
  }
  switch (set->key_size) {
  case 0 :
    sort_func(set, res, limit, func_str, NULL, NULL, dir);
    break;
  case sizeof(uint32_t) :
    sort_int(set, res, limit, 0, dir);
    break;
  default :
    sort_func(set, res, limit, func_bin, (void *)(intptr_t)set->key_size, NULL, dir);
    break;
  }
exit :
  return res;
}

sen_rc
sen_set_element_info(sen_set *set, const sen_set_eh *e, void **key, void **value)
{
  if (!set || !e) { return sen_invalid_argument; }
  switch (set->key_size) {
  case 0 :
    if (key) { *key = SEN_SET_STRKEY(*e); }
    if (value) { *value = SEN_SET_STRVAL(*e); }
    break;
  case sizeof(uint32_t) :
    if (key) { *key = SEN_SET_INTKEY(*e); }
    if (value) { *value = SEN_SET_INTVAL(*e); }
    break;
  default :
    if (key) { *key = SEN_SET_BINKEY(*e); }
    if (value) { *value = SEN_SET_BINVAL(*e, set); }
    break;
  }
  return sen_success;
}

sen_set *
sen_set_union(sen_set *a, sen_set *b)
{
  void *key, *va, *vb;
  entry *e, **ep;
  uint32_t i, key_size = a->key_size, value_size = a->value_size;
  if (key_size != b->key_size || value_size != b->value_size) { return NULL; }
  for (i = b->n_entries, ep = b->index; i; ep++) {
    if ((e = *ep) && e != GARBAGE) {
      switch (key_size) {
      case 0 :
        key = SEN_SET_STRKEY(e);
        vb = SEN_SET_STRVAL(e);
        break;
      case sizeof(uint32_t) :
        key = SEN_SET_INTKEY(e);
        vb = SEN_SET_INTVAL(e);
        break;
      default :
        key = SEN_SET_BINKEY(e);
        vb = &e->dummy[key_size];
        break;
      }
      if (sen_set_at(a, key, &va)) {
        /* do copy of merge? */
      } else {
        if (sen_set_get(a, key, &va)) { memcpy(va, vb, value_size); }
      }
      i--;
    }
  }
  sen_set_close(b);
  return a;
}

sen_set *
sen_set_subtract(sen_set *a, sen_set *b)
{
  void *key;
  entry *e, **ep, **dp;
  uint32_t i, key_size = a->key_size;
  if (key_size != b->key_size) { return NULL; }
  for (i = b->n_entries, ep = b->index; i; ep++) {
    if ((e = *ep) && e != GARBAGE) {
      switch (key_size) {
      case 0 :
        key = SEN_SET_STRKEY(e);
        break;
      case sizeof(uint32_t) :
        key = SEN_SET_INTKEY(e);
        break;
      default :
        key = SEN_SET_BINKEY(e);
        break;
      }
      if ((dp = sen_set_at(a, key, NULL))) { sen_set_del(a, dp); }
      i--;
    }
  }
  sen_set_close(b);
  return a;
}

sen_set *
sen_set_intersect(sen_set *a, sen_set *b)
{
  void *key;
  entry *e, **ep;
  uint32_t i, key_size = a->key_size;
  if (key_size != b->key_size) { return NULL; }
  for (i = a->n_entries, ep = a->index; i; ep++) {
    if ((e = *ep) && e != GARBAGE) {
      switch (key_size) {
      case 0 :
        key = SEN_SET_STRKEY(e);
        break;
      case sizeof(uint32_t) :
        key = SEN_SET_INTKEY(e);
        break;
      default :
        key = SEN_SET_BINKEY(e);
        break;
      }
      if (!sen_set_at(b, key, NULL)) { sen_set_del(a, ep); }
      i--;
    }
  }
  sen_set_close(b);
  return a;
}

int
sen_set_difference(sen_set *a, sen_set *b)
{
  void *key;
  entry *e, **ep, **dp;
  uint32_t count = 0, i, key_size = a->key_size;
  if (key_size != b->key_size) { return -1; }
  for (i = a->n_entries, ep = a->index; i; ep++) {
    if ((e = *ep) && e != GARBAGE) {
      switch (key_size) {
      case 0 :
        key = SEN_SET_STRKEY(e);
        break;
      case sizeof(uint32_t) :
        key = SEN_SET_INTKEY(e);
        break;
      default :
        key = SEN_SET_BINKEY(e);
        break;
      }
      if ((dp = sen_set_at(b, key, NULL))) {
        sen_set_del(b, dp);
        sen_set_del(a, ep);
        count++;
      }
      i--;
    }
  }
  return count;
}

sen_rc
sen_set_array_init(sen_set *set, uint32_t size)
{
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  SEN_ASSERT(!set->n_entries);
  SEN_ASSERT(!set->garbages);
  set->arrayp = 1;
  set->curr_entry = 0;
  if (set->chunks[SEN_SET_MAX_CHUNK]) {
    SEN_FREE(set->chunks[SEN_SET_MAX_CHUNK]);
  }
  if (!(set->chunks[SEN_SET_MAX_CHUNK] = SEN_CALLOC(set->entry_size * size))) {
    return sen_memory_exhausted;
  }
  return sen_set_reset(set, size);
}
