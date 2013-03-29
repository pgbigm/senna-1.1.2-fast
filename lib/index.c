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
/* Changelog:
   2013/01/07
   Add fast string-normalization function into Senna.
   Author: NTT DATA Corporation
 */
#include "senna_in.h"
#include <stdio.h>
#include <string.h>
#include "sym.h"
#include "inv.h"
#include "str.h"
#include "set.h"
#include "lex.h"
#include "cache.h"
#include "store.h"
#include "ctx.h"
#include "com.h"
#include "ql.h"

#define FOREIGN_KEY     1
#define FOREIGN_LEXICON 2

/* private classes */

/* b-heap */

typedef struct {
  int n_entries;
  int n_bins;
  sen_inv_cursor **bins;
} cursor_heap;

static inline cursor_heap *
cursor_heap_open(int max)
{
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  cursor_heap *h = SEN_MALLOC(sizeof(cursor_heap));
  if (!h) { return NULL; }
  h->bins = SEN_MALLOC(sizeof(sen_inv_cursor *) * max);
  if (!h->bins) {
    SEN_FREE(h);
    return NULL;
  }
  h->n_entries = 0;
  h->n_bins = max;
  return h;
}

static inline sen_rc
cursor_heap_push(cursor_heap *h, sen_inv *inv, sen_id tid, uint32_t offset2)
{
  int n, n2;
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  sen_inv_cursor *c, *c2;
  if (h->n_entries >= h->n_bins) {
    int max = h->n_bins * 2;
    sen_inv_cursor **bins = SEN_REALLOC(h->bins, sizeof(sen_inv_cursor *) * max);
    SEN_LOG(sen_log_debug, "expanded cursor_heap to %d,%p", max, bins);
    if (!bins) { return sen_memory_exhausted; }
    h->n_bins = max;
    h->bins = bins;
  }
#ifdef USE_AIO
  if (sen_aio_enabled) {
    if (!(c = sen_inv_cursor_openv1(inv, tid))) {
      SEN_LOG(sen_log_error, "cursor open failed");
      return sen_internal_error;
    }
    h->bins[h->n_entries++] = c;
  } else
#endif /* USE_AIO */
  {
    if (!(c = sen_inv_cursor_open(inv, tid, 1))) {
      SEN_LOG(sen_log_error, "cursor open failed");
      return sen_internal_error;
    }
    if (sen_inv_cursor_next(c)) {
      sen_inv_cursor_close(c);
      return sen_internal_error;
    }
    if (sen_inv_cursor_next_pos(c)) {
      SEN_LOG(sen_log_error, "invalid inv_cursor b");
      sen_inv_cursor_close(c);
      return sen_internal_error;
    }
    // c->offset = offset2;
    n = h->n_entries++;
    while (n) {
      n2 = (n - 1) >> 1;
      c2 = h->bins[n2];
      if (SEN_INV_CURSOR_CMP(c, c2)) { break; }
      h->bins[n] = c2;
      n = n2;
    }
    h->bins[n] = c;
  }
  return sen_success;
}

static inline sen_rc
cursor_heap_push2(cursor_heap *h)
{
  sen_rc rc = sen_success;
#ifdef USE_AIO
  if (sen_aio_enabled) {
    int i, j, n, n2;
    sen_inv_cursor *c, *c2;
    if (h && h->n_entries) {
      rc = sen_inv_cursor_openv2(h->bins, h->n_entries);
      SEN_ASSERT(rc);
      for (i = 0, j = 0; i < h->n_entries; i++) {
        c = h->bins[i];
        if (sen_inv_cursor_next(c)) {
          sen_inv_cursor_close(c);
          continue;
        }
        if (sen_inv_cursor_next_pos(c)) {
          SEN_LOG(sen_log_error, "invalid inv_cursor b");
          sen_inv_cursor_close(c);
          continue;
        }
        n = j++;
        while (n) {
          n2 = (n - 1) >> 1;
          c2 = h->bins[n2];
          if (SEN_INV_CURSOR_CMP(c, c2)) { break; }
          h->bins[n] = c2;
          n = n2;
        }
        h->bins[n] = c;
      }
      h->n_entries = j;
    }
  }
#endif /* USE_AIO */
  return rc;
}

static inline sen_inv_cursor *
cursor_heap_min(cursor_heap *h)
{
  return h->n_entries ? h->bins[0] : NULL;
}

static inline void
cursor_heap_recalc_min(cursor_heap *h)
{
  int n = 0, n1, n2, m;
  if ((m = h->n_entries) > 1) {
    sen_inv_cursor *c = h->bins[0], *c1, *c2;
    for (;;) {
      n1 = n * 2 + 1;
      n2 = n1 + 1;
      c1 = n1 < m ? h->bins[n1] : NULL;
      c2 = n2 < m ? h->bins[n2] : NULL;
      if (c1 && SEN_INV_CURSOR_CMP(c, c1)) {
        if (c2 && SEN_INV_CURSOR_CMP(c, c2) && SEN_INV_CURSOR_CMP(c1, c2)) {
          h->bins[n] = c2;
          n = n2;
        } else {
          h->bins[n] = c1;
          n = n1;
        }
      } else {
        if (c2 && SEN_INV_CURSOR_CMP(c, c2)) {
          h->bins[n] = c2;
          n = n2;
        } else {
          h->bins[n] = c;
          break;
        }
      }
    }
  }
}

static inline void
cursor_heap_pop(cursor_heap *h)
{
  if (h->n_entries) {
    sen_inv_cursor *c = h->bins[0];
    if (sen_inv_cursor_next(c)) {
      sen_inv_cursor_close(c);
      h->bins[0] = h->bins[--h->n_entries];
    } else if (sen_inv_cursor_next_pos(c)) {
      SEN_LOG(sen_log_error, "invalid inv_cursor c");
      sen_inv_cursor_close(c);
      h->bins[0] = h->bins[--h->n_entries];
    }
    if (h->n_entries > 1) { cursor_heap_recalc_min(h); }
  }
}

static inline void
cursor_heap_pop_pos(cursor_heap *h)
{
  if (h->n_entries) {
    sen_inv_cursor *c = h->bins[0];
    if (sen_inv_cursor_next_pos(c)) {
      if (sen_inv_cursor_next(c)) {
        sen_inv_cursor_close(c);
        h->bins[0] = h->bins[--h->n_entries];
      } else if (sen_inv_cursor_next_pos(c)) {
        SEN_LOG(sen_log_error, "invalid inv_cursor d");
        sen_inv_cursor_close(c);
        h->bins[0] = h->bins[--h->n_entries];
      }
    }
    if (h->n_entries > 1) { cursor_heap_recalc_min(h); }
  }
}

static inline void
cursor_heap_close(cursor_heap *h)
{
  int i;
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  if (!h) { return; }
  for (i = h->n_entries; i--;) { sen_inv_cursor_close(h->bins[i]); }
  SEN_FREE(h->bins);
  SEN_FREE(h);
}

/* token_info */

typedef struct {
  cursor_heap *cursors;
  int offset;
  int pos;
  int size;
  int ntoken;
  sen_inv_posting *p;
} token_info;

#define EX_NONE   0
#define EX_PREFIX 1
#define EX_SUFFIX 2
#define EX_BOTH   3

inline static void
token_info_expand_both(sen_index *i, const char *key, token_info *ti)
{
  int s = 0;
  sen_set *h, *g;
  uint32_t *offset2;
  sen_set_cursor *c;
  sen_id *tp, *tq;
  if ((h = sen_sym_prefix_search(i->lexicon, key))) {
    // sen_log("key=%s h->n=%d", key, h->n_entries);
    if ((ti->cursors = cursor_heap_open(h->n_entries + 256))) {
      if ((c = sen_set_cursor_open(h))) {
        while (sen_set_cursor_next(c, (void **) &tp, NULL)) {
          const char *key2 = _sen_sym_key(i->lexicon, *tp);
          if (!key2) { break; }
          // sen_log("key2=%s", key2);
          if (sen_str_len(key2, i->lexicon->encoding, NULL) == 1) { // todo:
            if ((s = sen_inv_estimate_size(i->inv, *tp))) {
              cursor_heap_push(ti->cursors, i->inv, *tp, 0);
              ti->ntoken++;
              ti->size += s;
            }
          } else {
            g = sen_sym_suffix_search(i->lexicon, key2);
            // sen_log("g=%d", g ? g->n_entries : 0);
            if (g) {
              SEN_SET_EACH(g, eh, &tq, &offset2, {
                if ((s = sen_inv_estimate_size(i->inv, *tq))) {
                  // sen_log("est=%d key=%s", s, _sen_sym_key(i->lexicon, *tq));
                  cursor_heap_push(ti->cursors, i->inv, *tq, /* *offset2 */ 0);
                  ti->ntoken++;
                  ti->size += s;
                }
              });
              sen_set_close(g);
            }
          }
          // if (ti->size > 1000000) { sen_log("huge! %d", ti->size); break; } // todo!!!
        }
        sen_set_cursor_close(c);
      }
    }
    sen_set_close(h);
  }
  // sen_log("key=%s done..", key);
}

inline static sen_rc
token_info_close(token_info *ti)
{
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  cursor_heap_close(ti->cursors);
  SEN_FREE(ti);
  return sen_success;
}

inline static token_info *
token_info_open(sen_index *i, const char *key, uint32_t offset, int mode)
{
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  int s = 0;
  sen_set *h;
  token_info *ti;
  sen_id tid;
  sen_id *tp;
  if (!key) { return NULL; }
  if (!(ti = SEN_MALLOC(sizeof(token_info)))) { return NULL; }
  ti->cursors = NULL;
  ti->size = 0;
  ti->ntoken = 0;
  ti->offset = offset;
  switch (mode) {
  case EX_BOTH :
    token_info_expand_both(i, key, ti);
    break;
  case EX_NONE :
    if ((tid = sen_sym_at(i->lexicon, key)) &&
        (s = sen_inv_estimate_size(i->inv, tid)) &&
        (ti->cursors = cursor_heap_open(1))) {
      cursor_heap_push(ti->cursors, i->inv, tid, 0);
      ti->ntoken++;
      ti->size = s;
    }
    break;
  case EX_PREFIX :
    if ((h = sen_sym_prefix_search(i->lexicon, key))) {
      // sen_log("key=%s h->n=%d", key, h->n_entries);
      if ((ti->cursors = cursor_heap_open(h->n_entries))) {
        SEN_SET_EACH(h, eh, &tp, NULL, {
          if ((s = sen_inv_estimate_size(i->inv, *tp))) {
            // sen_log("%8d %s", s, _sen_sym_key(i->lexicon, *tp));
            cursor_heap_push(ti->cursors, i->inv, *tp, 0);
            ti->ntoken++;
            ti->size += s;
          }
          //if (ti->size > 1000000) { sen_log("huge! %d", ti->size); break; } // todo!!!
        });
      // sen_log("key=%s done.", key);
      }
      sen_set_close(h);
    }
    break;
  case EX_SUFFIX :
    if ((h = sen_sym_suffix_search(i->lexicon, key))) {
      // sen_log("key=%s h->n=%d", key, h->n_entries);
      if ((ti->cursors = cursor_heap_open(h->n_entries))) {
        uint32_t *offset2;
        SEN_SET_EACH(h, eh, &tp, &offset2, {
          if ((s = sen_inv_estimate_size(i->inv, *tp))) {
            cursor_heap_push(ti->cursors, i->inv, *tp, /* *offset2 */ 0);
            ti->ntoken++;
            ti->size += s;
          }
        });
      }
      sen_set_close(h);
    }
    break;
  }
  if (cursor_heap_push2(ti->cursors)) {
    token_info_close(ti);
    return NULL;
  }
  {
    sen_inv_cursor *ic;
    if (ti->cursors && (ic = cursor_heap_min(ti->cursors))) {
      sen_inv_posting *p = ic->post;
      ti->pos = p->pos - ti->offset;
      ti->p = p;
    } else {
      token_info_close(ti);
      ti = NULL;
    }
  }
  return ti;
}

static inline sen_rc
token_info_skip(token_info *ti, uint32_t rid, uint32_t sid)
{
  sen_inv_cursor *c;
  sen_inv_posting *p;
  for (;;) {
    if (!(c = cursor_heap_min(ti->cursors))) { return sen_internal_error; }
    p = c->post;
    if (p->rid > rid || (p->rid == rid && p->sid >= sid)) { break; }
    cursor_heap_pop(ti->cursors);
  }
  // sen_log("r=%d s=%d pr=%d ps=%d", rid, sid, p->rid, p->sid);
  ti->pos = p->pos - ti->offset;
  ti->p = p;
  return sen_success;
}

static inline sen_rc
token_info_skip_pos(token_info *ti, uint32_t rid, uint32_t sid, uint32_t pos)
{
  sen_inv_cursor *c;
  sen_inv_posting *p;
  // sen_log("r=%d s=%d p=%d o=%d", rid, sid, pos, ti->offset);
  pos += ti->offset;
  for (;;) {
    if (!(c = cursor_heap_min(ti->cursors))) { return sen_internal_error; }
    p = c->post;
    if (p->rid != rid || p->sid != sid || p->pos >= pos) { break; }
    cursor_heap_pop_pos(ti->cursors);
  }
  ti->pos = p->pos - ti->offset;
  ti->p = p;
  return sen_success;
}

inline static int
token_compare(const void *a, const void *b)
{
  const token_info *t1 = *((token_info **)a), *t2 = *((token_info **)b);
  return t1->size - t2->size;
}

inline static sen_rc
token_info_build(sen_index *i, const char *string, size_t string_len, token_info **tis, uint32_t *n,
                 sen_sel_mode mode)
{
  token_info *ti;
  sen_rc rc = sen_internal_error;
  sen_lex *lex = sen_lex_open(i->lexicon, string, string_len, 0);
  if (!lex) { return sen_memory_exhausted; }
  if (mode == sen_sel_unsplit) {
    if ((ti = token_info_open(i, (char *)lex->orig, 0, EX_BOTH))) {
      tis[(*n)++] = ti;
      rc = sen_success;
    }
  } else {
    sen_id tid;
    int ef;
    switch (mode) {
    case sen_sel_prefix :
      ef = EX_PREFIX;
      break;
    case sen_sel_suffix :
      ef = EX_SUFFIX;
      break;
    case sen_sel_partial :
      ef = EX_BOTH;
      break;
    default :
      ef = EX_NONE;
      break;
    }
    tid = sen_lex_next(lex);
    if (lex->force_prefix) { ef |= EX_PREFIX; }
    switch (lex->status) {
    case sen_lex_doing :
      ti = token_info_open(i, _sen_sym_key(i->lexicon, tid), lex->pos, ef & EX_SUFFIX);
      break;
    case sen_lex_done :
      ti = token_info_open(i, _sen_sym_key(i->lexicon, tid), lex->pos, ef);
      break;
    case sen_lex_not_found :
      ti = token_info_open(i, (char *)lex->orig, 0, ef);
      break;
    default :
      goto exit;
    }
    if (!ti) { goto exit ; }
    tis[(*n)++] = ti;
    // sen_log("%d:%s(%d)", lex->pos, (tid == SEN_SYM_NIL) ? lex->orig : _sen_sym_key(i->lexicon, tid), tid);
    while (lex->status == sen_lex_doing) {
      tid = sen_lex_next(lex);
      switch (lex->status) {
      case sen_lex_doing :
        ti = token_info_open(i, _sen_sym_key(i->lexicon, tid), lex->pos, EX_NONE);
        break;
      case sen_lex_done :
        ti = token_info_open(i, _sen_sym_key(i->lexicon, tid), lex->pos, ef & EX_PREFIX);
        break;
      default :
        ti = token_info_open(i, (char *)lex->token, lex->pos, ef & EX_PREFIX);
        break;
      }
      if (!ti) { goto exit; }
      tis[(*n)++] = ti;
      // sen_log("%d:%s(%d)", lex->pos, (tid == SEN_SYM_NIL) ? lex->token : _sen_sym_key(i->lexicon, tid), tid);
    }
    rc = sen_success;
  }
exit :
  sen_lex_close(lex);
  return rc;
}

static void
token_info_clear_offset(token_info **tis, uint32_t n)
{
  token_info **tie;
  for (tie = tis + n; tis < tie; tis++) { (*tis)->offset = 0; }
}

/* sen_index */

inline static int
build_flags(int flags)
{
  if (flags & SEN_INDEX_ENABLE_SUFFIX_SEARCH) {
    return flags | SEN_SYM_WITH_SIS;
  } else if (flags & SEN_INDEX_DISABLE_SUFFIX_SEARCH) {
    return flags & ~SEN_SYM_WITH_SIS;
  } else {   /* default */
    switch (flags & SEN_INDEX_TOKENIZER_MASK) {
    case SEN_INDEX_MORPH_ANALYSE :
      return flags | SEN_SYM_WITH_SIS;
    case SEN_INDEX_NGRAM :
      return flags & ~SEN_SYM_WITH_SIS;
    case SEN_INDEX_DELIMITED :
      return flags & ~SEN_SYM_WITH_SIS;
    default :
      return flags & ~SEN_SYM_WITH_SIS;
    }
  }
}

inline static void
index_open(const char *path, sen_index *i)
{
  sen_obj *obj = sen_get(path);
  if (obj == F) {
    SEN_LOG(sen_log_warning, "sen_get(%s) failed", path);
  } else {
    obj->type = sen_ql_index;
    obj->u.b.value = (char *) i;
  }
}

inline static void
index_close(sen_index *i)
{
  const char *path = sen_inv_path(i->inv);
  sen_rc rc = sen_del(path);
  if (rc) { SEN_LOG(sen_log_warning, "sen_del(%s) failed", path); }
}

void
sen_index_expire(void)
{
  const char *key;
  sen_obj *obj;
  if (sen_gctx.symbols) {
    SEN_SET_EACH(sen_gctx.symbols, eh, &key, &obj, {
      if (obj->type == sen_ql_index) {
        sen_index *i = (sen_index *)obj->u.b.value;
        sen_inv_seg_expire(i->inv, 0);
      }
    });
  }
}

sen_index *
sen_index_create(const char *path, int key_size,
                 int flags, int initial_n_segments, sen_encoding encoding)
{
  sen_index *i;
  char buffer[PATH_MAX];
  if (!path) { SEN_LOG(sen_log_warning, "sen_index_create: invalid argument"); return NULL; }
  if (initial_n_segments == 0) { initial_n_segments = SENNA_DEFAULT_INITIAL_N_SEGMENTS; }
  if (encoding == sen_enc_default) { encoding = sen_gctx.encoding; }
  if (strlen(path) > PATH_MAX - 4) {
    SEN_LOG(sen_log_warning, "sen_index_create: too long index path (%s)", path);
    return NULL;
  }
  if (!(i = SEN_GMALLOC(sizeof(sen_index)))) { return NULL; }
  SEN_LOG(sen_log_notice, "creating '%s' encoding=%s initial_n_segments=%d",
          path, sen_enctostr(encoding), initial_n_segments);
  strcpy(buffer, path);
  strcat(buffer, ".SEN");
  i->foreign_flags = 0;
  if ((i->keys = sen_sym_create(buffer, key_size, (flags & 0x70000), sen_enc_none))) {
    strcpy(buffer, path);
    strcat(buffer, ".SEN.l");
    if ((i->lexicon = sen_sym_create(buffer, 0, build_flags(flags), encoding))) {
      strcpy(buffer, path);
      strcat(buffer, ".SEN.i");
      if ((i->inv = sen_inv_create(buffer, i->lexicon, initial_n_segments))) {
        index_open(buffer, i);
        if ((flags & SEN_INDEX_WITH_VGRAM)) {
          strcpy(buffer, path);
          strcat(buffer, ".SEN.v");
          i->vgram= sen_vgram_create(buffer);
        } else {
          i->vgram = NULL;
        }
        if (!(flags & SEN_INDEX_WITH_VGRAM) || i->vgram) {
          SEN_LOG(sen_log_notice, "index created (%s) flags=%x", path, i->lexicon->flags);
          return i;
        }
        sen_inv_close(i->inv);
      }
      sen_sym_close(i->lexicon);
    }
    sen_sym_close(i->keys);
  }
  SEN_GFREE(i);
  return NULL;
}

sen_index *
sen_index_open(const char *path)
{
  sen_index *i;
  char buffer[PATH_MAX];
  if (!path) { SEN_LOG(sen_log_warning, "sen_index_open: invalid argument"); return NULL; }
  if (strlen(path) > PATH_MAX - 4) {
    SEN_LOG(sen_log_warning, "sen_index_open: too long index path (%s)", path);
    return NULL;
  }
  if (!(i = SEN_GMALLOC(sizeof(sen_index)))) { return NULL; }
  strcpy(buffer, path);
  strcat(buffer, ".SEN");
  i->foreign_flags = 0;
  if ((i->keys = sen_sym_open(buffer))) {
    strcpy(buffer, path);
    strcat(buffer, ".SEN.l");
    if ((i->lexicon = sen_sym_open(buffer))) {
      strcpy(buffer, path);
      strcat(buffer, ".SEN.i");
      if ((i->inv = sen_inv_open(buffer, i->lexicon))) {
        index_open(buffer, i);
        if ((i->lexicon->flags & SEN_INDEX_WITH_VGRAM)) {
          strcpy(buffer, path);
          strcat(buffer, ".SEN.v");
          i->vgram = sen_vgram_open(buffer);
        } else {
          i->vgram = NULL;
        }
        if (!(i->lexicon->flags & SEN_INDEX_WITH_VGRAM) || i->vgram) {
          SEN_LOG(sen_log_notice, "index opened (%p:%s) flags=%x", i, path, i->lexicon->flags);
          return i;
        }
        sen_inv_close(i->inv);
      }
      sen_sym_close(i->lexicon);
    }
    sen_sym_close(i->keys);
  }
  SEN_GFREE(i);
  return NULL;
}

sen_index *
sen_index_create_with_keys(const char *path, sen_sym *keys,
                           int flags, int initial_n_segments, sen_encoding encoding)
{
  sen_index *i;
  char buffer[PATH_MAX];
  if (!path || !keys) {
    SEN_LOG(sen_log_warning, "sen_index_create_with_keys: invalid argument");
    return NULL;
  }
  if (initial_n_segments == 0) { initial_n_segments = SENNA_DEFAULT_INITIAL_N_SEGMENTS; }
  if (encoding == sen_enc_default) { encoding = sen_gctx.encoding; }
  if (strlen(path) > PATH_MAX - 4) {
    SEN_LOG(sen_log_warning, "too long index path (%s)", path);
    return NULL;
  }
  if (!(i = SEN_GMALLOC(sizeof(sen_index)))) { return NULL; }
  SEN_LOG(sen_log_notice, "creating '%s' encoding=%s initial_n_segments=%d",
          path, sen_enctostr(encoding), initial_n_segments);
  i->keys = keys;
  i->foreign_flags = FOREIGN_KEY;
  strcpy(buffer, path);
  strcat(buffer, ".SEN.l");
  if ((i->lexicon = sen_sym_create(buffer, 0, build_flags(flags), encoding))) {
    strcpy(buffer, path);
    strcat(buffer, ".SEN.i");
    if ((i->inv = sen_inv_create(buffer, i->lexicon, initial_n_segments))) {
      index_open(buffer, i);
      if ((flags & SEN_INDEX_WITH_VGRAM)) {
        strcpy(buffer, path);
        strcat(buffer, ".SEN.v");
        i->vgram= sen_vgram_create(buffer);
      } else {
        i->vgram = NULL;
      }
      if (!(flags & SEN_INDEX_WITH_VGRAM) || i->vgram) {
        SEN_LOG(sen_log_notice, "index created (%s) flags=%x", path, i->lexicon->flags);
        return i;
      }
      sen_inv_close(i->inv);
    }
    sen_sym_close(i->lexicon);
  }
  SEN_GFREE(i);
  return NULL;
}

sen_index *
sen_index_open_with_keys(const char *path, sen_sym *keys)
{
  sen_index *i;
  char buffer[PATH_MAX];
  if (!path || !keys) {
    SEN_LOG(sen_log_warning, "sen_index_open_with_keys: invalid argument");
    return NULL;
  }
  if (strlen(path) > PATH_MAX - 4) {
    SEN_LOG(sen_log_warning, "too long index path (%s)", path);
    return NULL;
  }
  if (!(i = SEN_GMALLOC(sizeof(sen_index)))) { return NULL; }
  i->keys = keys;
  i->foreign_flags = FOREIGN_KEY;
  strcpy(buffer, path);
  strcat(buffer, ".SEN.l");
  if ((i->lexicon = sen_sym_open(buffer))) {
    strcpy(buffer, path);
    strcat(buffer, ".SEN.i");
    if ((i->inv = sen_inv_open(buffer, i->lexicon))) {
      index_open(buffer, i);
      if ((i->lexicon->flags & SEN_INDEX_WITH_VGRAM)) {
        strcpy(buffer, path);
        strcat(buffer, ".SEN.v");
        i->vgram = sen_vgram_open(buffer);
      } else {
        i->vgram = NULL;
      }
      if(!(i->lexicon->flags & SEN_INDEX_WITH_VGRAM) || i->vgram) {
        SEN_LOG(sen_log_notice, "index opened (%p:%s) flags=%x", i, path, i->lexicon->flags);
        return i;
      }
      sen_inv_close(i->inv);
    }
    sen_sym_close(i->lexicon);
  }
  SEN_GFREE(i);
  return NULL;
}

sen_index *
sen_index_create_with_keys_lexicon(const char *path, sen_sym *keys, sen_sym *lexicon,
                                   int initial_n_segments)
{
  sen_index *i;
  if (!keys || !path || !lexicon) {
    SEN_LOG(sen_log_warning, "sen_index_create_with_keys_lexicon: invalid argument");
    return NULL;
  }
  if (initial_n_segments == 0) { initial_n_segments = SENNA_DEFAULT_INITIAL_N_SEGMENTS; }
  if (!(i = SEN_GMALLOC(sizeof(sen_index)))) { return NULL; }
  SEN_LOG(sen_log_notice, "creating '%s' encoding=%s initial_n_segments=%d",
          path, sen_enctostr(lexicon->encoding), initial_n_segments);
  i->keys = keys;
  i->lexicon = lexicon;
  i->foreign_flags = FOREIGN_KEY|FOREIGN_LEXICON;
  i->vgram = NULL;
  if ((i->inv = sen_inv_create(path, i->lexicon, initial_n_segments))) {
    index_open(path, i);
    SEN_LOG(sen_log_notice, "index created (%s) flags=%x", path, i->lexicon->flags);
    return i;
  }
  SEN_GFREE(i);
  return NULL;
}

sen_index *
sen_index_open_with_keys_lexicon(const char *path, sen_sym *keys, sen_sym *lexicon)
{
  sen_index *i;
  if (!keys || !path || !lexicon) {
    SEN_LOG(sen_log_warning, "sen_index_open_with_keys_lexicon: invalid argument");
    return NULL;
  }
  if (!(i = SEN_GMALLOC(sizeof(sen_index)))) { return NULL; }
  i->keys = keys;
  i->lexicon = lexicon;
  i->foreign_flags = FOREIGN_KEY|FOREIGN_LEXICON;
  i->vgram = NULL;
  if ((i->inv = sen_inv_open(path, i->lexicon))) {
    index_open(path, i);
    SEN_LOG(sen_log_notice, "index opened (%p:%s) flags=%x", i, path, i->lexicon->flags);
    return i;
  }
  SEN_GFREE(i);
  return NULL;
}

sen_rc
sen_index_close(sen_index *i)
{
  if (!i) { return sen_invalid_argument; }
  if (!(i->foreign_flags & FOREIGN_KEY)) { sen_sym_close(i->keys); }
  if (!(i->foreign_flags & FOREIGN_LEXICON)) { sen_sym_close(i->lexicon); }
  index_close(i);
  sen_inv_close(i->inv);
  if (i->vgram) { sen_vgram_close(i->vgram); }
  SEN_GFREE(i);
  return sen_success;
}

sen_rc
sen_index_remove(const char *path)
{
  sen_rc rc;
  char buffer[PATH_MAX];
  if (!path || strlen(path) > PATH_MAX - 8) { return sen_invalid_argument; }
  snprintf(buffer, PATH_MAX, "%s.SEN", path);
  if ((rc = sen_sym_remove(buffer))) { goto exit; }
  snprintf(buffer, PATH_MAX, "%s.SEN.i", path);
  if ((rc = sen_inv_remove(buffer))) { goto exit; }
  snprintf(buffer, PATH_MAX, "%s.SEN.l", path);
  if ((rc = sen_sym_remove(buffer))) { goto exit; }
  snprintf(buffer, PATH_MAX, "%s.SEN.v", path);
  sen_io_remove(buffer); // sen_vgram_remove
exit :
  return rc;
}

sen_rc
sen_index_rename(const char *old_name, const char *new_name)
{
  char old_buffer[PATH_MAX];
  char new_buffer[PATH_MAX];
  if (!old_name || strlen(old_name) > PATH_MAX - 8) { return sen_invalid_argument; }
  if (!new_name || strlen(new_name) > PATH_MAX - 8) { return sen_invalid_argument; }
  snprintf(old_buffer, PATH_MAX, "%s.SEN", old_name);
  snprintf(new_buffer, PATH_MAX, "%s.SEN", new_name);
  sen_io_rename(old_buffer, new_buffer);
  snprintf(old_buffer, PATH_MAX, "%s.SEN.i", old_name);
  snprintf(new_buffer, PATH_MAX, "%s.SEN.i", new_name);
  sen_io_rename(old_buffer, new_buffer);
  snprintf(old_buffer, PATH_MAX, "%s.SEN.i.c", old_name);
  snprintf(new_buffer, PATH_MAX, "%s.SEN.i.c", new_name);
  sen_io_rename(old_buffer, new_buffer);
  snprintf(old_buffer, PATH_MAX, "%s.SEN.l", old_name);
  snprintf(new_buffer, PATH_MAX, "%s.SEN.l", new_name);
  sen_io_rename(old_buffer, new_buffer);
  snprintf(old_buffer, PATH_MAX, "%s.SEN.v", old_name);
  snprintf(new_buffer, PATH_MAX, "%s.SEN.v", new_name);
  sen_io_rename(old_buffer, new_buffer);
  return sen_success;
}

sen_rc
sen_index_info(sen_index *i, int *key_size, int *flags,
               int *initial_n_segments, sen_encoding *encoding,
               unsigned *nrecords_keys, unsigned *file_size_keys,
               unsigned *nrecords_lexicon, unsigned *file_size_lexicon,
               unsigned long long *inv_seg_size, unsigned long long *inv_chunk_size)
{
  sen_rc rc = sen_success;

  if (!i) { return sen_invalid_argument; }
  if (key_size) { *key_size = i->keys->key_size; }
  if (flags) { *flags = i->lexicon->flags & ~SEN_SYM_WITH_SIS; }
  if (initial_n_segments) { *initial_n_segments = sen_inv_initial_n_segments(i->inv); }
  if (encoding) { *encoding = i->lexicon->encoding; }
  if (nrecords_keys || file_size_keys) {
    if ((rc = sen_sym_info(i->keys, NULL, NULL, NULL, nrecords_keys, file_size_keys))) { return rc; }
  }
  if (nrecords_lexicon || file_size_lexicon) {
    if ((rc = sen_sym_info(i->lexicon, NULL, NULL, NULL, nrecords_lexicon, file_size_lexicon))) { return rc; }
  }
  if (inv_seg_size || inv_chunk_size) {
    uint64_t seg_size, chunk_size;

    rc = sen_inv_info(i->inv, &seg_size, &chunk_size);

    if (inv_seg_size) {
      *inv_seg_size = seg_size;
    }

    if (inv_chunk_size) {
      *inv_chunk_size = chunk_size;
    }

    if (rc != sen_success) {
      return rc;
    }
  }
  return sen_success;
}

sen_rc
sen_index_lock(sen_index *i, int timeout)
{
  if (!i) { return sen_invalid_argument; }
  return sen_sym_lock(i->keys, timeout);
}

sen_rc
sen_index_unlock(sen_index *i)
{
  if (!i) { return sen_invalid_argument; }
  return sen_sym_unlock(i->keys);
}

sen_rc
sen_index_clear_lock(sen_index *i)
{
  if (!i) { return sen_invalid_argument; }
  return sen_sym_clear_lock(i->keys);
}

int
sen_index_path(sen_index *i, char *pathbuf, int bufsize)
{
  const char *invpath;
  int pathsize;
  if (!i) {
    SEN_LOG(sen_log_warning, "sen_index_path: invalid argument");
    return sen_invalid_argument;
  }
  invpath = sen_io_path(i->lexicon->io);
  pathsize = strlen(invpath) - 5;
  if (bufsize >= pathsize && pathbuf) {
    memcpy(pathbuf, invpath, pathsize - 1);
    pathbuf[pathsize - 1] = '\0';
  }
  return pathsize;
}

/* update */

inline static sen_rc
index_add(sen_index *i, const void *key, const char *value, size_t value_len)
{
  int hint;
  sen_set *h;
  sen_lex *lex;
  sen_inv_updspec **u;
  sen_id rid, tid, *tp;
  sen_rc r, rc = sen_success;
  sen_vgram_buf *sbuf = NULL;
  //  sen_log("add > (%x:%x)", i, key);
  if (!(rid = sen_sym_get(i->keys, key))) { return sen_invalid_argument; }
  if (!(lex = sen_lex_open(i->lexicon, value, value_len, SEN_LEX_ADD|SEN_LEX_UPD))) {
    return sen_memory_exhausted;
  }
  if (i->vgram) { sbuf = sen_vgram_buf_open(value_len); }
  h = sen_set_open(sizeof(sen_id), sizeof(sen_inv_updspec *), 0);
  if (!h) {
    SEN_LOG(sen_log_alert, "sen_set_open on index_add failed !");
    sen_lex_close(lex);
    if (sbuf) { sen_vgram_buf_close(sbuf); }
    return sen_memory_exhausted;
  }
  while (!lex->status) {
    (tid = sen_lex_next(lex));
    if (tid) {
      if (!sen_set_get(h, &tid, (void **) &u)) { break; }
      if (!*u) {
        if (!(*u = sen_inv_updspec_open(rid, 1))) {
          SEN_LOG(sen_log_error, "sen_inv_updspec_open on index_add failed!");
          goto exit;
        }
      }
      if (sen_inv_updspec_add(*u, lex->pos, 0)) {
        SEN_LOG(sen_log_error, "sen_inv_updspec_add on index_add failed!");
        goto exit;
      }
      if (sbuf) { sen_vgram_buf_add(sbuf, tid); }
    }
  }
  sen_lex_close(lex);
  if (sbuf) { sen_vgram_update(i->vgram, rid, sbuf, h); }
  SEN_SET_EACH(h, eh, &tp, &u, {
    hint = sen_str_get_prefix_order(_sen_sym_key(i->lexicon, *tp));
    if (hint == -1) { hint = *tp; }
    // sen_log("inv_update > %d '%s'", *tp, _sen_sym_key(i->lexicon, *tp));
    if ((r = sen_inv_update(i->inv, *tp, *u, h, hint))) { rc = r; }
    // sen_log("inv_update < %d '%s'", *tp, _sen_sym_key(i->lexicon, *tp));
    sen_inv_updspec_close(*u);
  });
  sen_set_close(h);
  //  sen_log("add < (%x:%d:%d) %d", key, rid, *((int *)key), value_len);
  if (sbuf) { sen_vgram_buf_close(sbuf); }
  return rc;
exit:
  sen_set_close(h);
  sen_lex_close(lex);
  if (sbuf) { sen_vgram_buf_close(sbuf); }
  return sen_memory_exhausted;
}

inline static sen_rc
index_del(sen_index *i, const void *key, const char *value, size_t value_len)
{
  sen_set *h;
  sen_lex *lex;
  sen_inv_updspec **u;
  sen_id rid, tid, *tp;
  // sen_log("deleting(%s)", value);
  if (!(rid = sen_sym_at(i->keys, key))) {
    SEN_LOG(sen_log_error, "del : (%x) (invalid key)", key);
    return sen_invalid_argument;
  }
  // sen_log("del > (%x:%d:%d) %d", key, rid, *((int *)key), strlen(value));
  if (!(lex = sen_lex_open(i->lexicon, value, value_len, SEN_LEX_UPD))) {
    return sen_memory_exhausted;
  }
  h = sen_set_open(sizeof(sen_id), sizeof(sen_inv_updspec *), 0);
  if (!h) {
    SEN_LOG(sen_log_alert, "sen_set_open on index_del failed !");
    sen_lex_close(lex);
    return sen_memory_exhausted;
  }
  while (!lex->status) {
    if ((tid = sen_lex_next(lex))) {
      if (!sen_set_get(h, &tid, (void **) &u)) { break; }
      if (!*u) {
        if (!(*u = sen_inv_updspec_open(rid, 0))) {
          SEN_LOG(sen_log_alert, "sen_inv_updspec_open on index_del failed !");
          sen_set_close(h);
          sen_lex_close(lex);
          return sen_memory_exhausted;
        }
      }
    }
    // sen_log("index_del: tid=%d(%s)", tid, _sen_sym_key(i->lexicon, tid));
  }
  sen_lex_close(lex);
  SEN_SET_EACH(h, eh, &tp, &u, {
    if (*tp) {
      // sen_log("inv_delete > %d '%s'", *tp, _sen_sym_key(i->lexicon, *tp));
      sen_inv_delete(i->inv, *tp, *u, NULL);
      // sen_log("inv_delete < %d '%s'", *tp, _sen_sym_key(i->lexicon, *tp));
    }
    sen_inv_updspec_close(*u);
  });
  sen_set_close(h);
  // sen_log("del < (%x)", key);
  return sen_success;
}

sen_rc
sen_index_upd(sen_index *i, const void *key,
              const char *oldvalue, unsigned int oldvalue_len,
              const char *newvalue, unsigned int newvalue_len)
{
  // if (!strcmp(oldvalue, newvalue)) { return sen_success; }
  sen_rc rc;
  if (!i || !key) { SEN_LOG(sen_log_warning, "sen_index_upd: invalid argument"); return sen_invalid_argument; }
  if ((rc = sen_index_lock(i, -1))) {
    SEN_LOG(sen_log_crit, "sen_index_upd: index lock failed");
    return rc;
  }
  // sen_log("index_upd %p %x > '%s'->'%s'", i, key, oldvalue, newvalue);
  if (oldvalue && *oldvalue) {
    if ((rc = index_del(i, key, oldvalue, oldvalue_len))) {
      SEN_LOG(sen_log_error, "index_del on sen_index_upd failed !");
      goto exit;
    }
  }
  if (newvalue && *newvalue) {
    rc = index_add(i, key, newvalue, newvalue_len);
  } else {
    if (!(i->foreign_flags & FOREIGN_KEY)) {
      rc = sen_sym_del(i->keys, key);
    }
  }
  //  sen_log("index_upd %x < %d", key, rc);
exit :
  sen_index_unlock(i);
  sen_inv_seg_expire(i->inv, -1);
  return rc;
}

#define DELETE_FLAG 1

sen_rc
sen_index_del(sen_index *i, const void *key)
{
  sen_id rid;
  if (!i || !key) { SEN_LOG(sen_log_warning, "sen_index_del: invalid argument"); return sen_invalid_argument; }
  rid = sen_sym_at(i->keys, key);
  if (!rid) { return sen_invalid_argument; }
  return sen_sym_pocket_set(i->keys, rid, DELETE_FLAG);
}

#define INITIAL_VALUE_SIZE 1024

sen_values *
sen_values_open(void)
{
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  sen_values *v = SEN_MALLOC(sizeof(sen_values));
  if (v) {
    v->n_values = 0;
    v->values = NULL;
  }
  return v;
}

sen_rc
sen_values_close(sen_values *v)
{
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  if (!v) { return sen_invalid_argument; }
  if (v->values) { SEN_FREE(v->values); }
  SEN_FREE(v);
  return sen_success;
}

sen_rc
sen_values_add(sen_values *v, const char *str, unsigned int str_len, unsigned int weight)
{
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  sen_value *vp;
  if (!v || !str) { SEN_LOG(sen_log_warning, "sen_values_add: invalid argument"); return sen_invalid_argument; }
  if (!(v->n_values & (INITIAL_VALUE_SIZE - 1))) {
    vp = SEN_REALLOC(v->values, sizeof(sen_value) * (v->n_values + INITIAL_VALUE_SIZE));
    SEN_LOG(sen_log_debug, "expanded values to %d,%p", v->n_values + INITIAL_VALUE_SIZE, vp);
    if (!vp) { return sen_memory_exhausted; }
    v->values = vp;
  }
  vp = &v->values[v->n_values];
  vp->str = str;
  vp->str_len = str_len;
  vp->weight = weight;
  v->n_values++;
  return sen_success;
}

sen_rc
sen_index_update(sen_index *i, const void *key, unsigned int section,
                 sen_values *oldvalues, sen_values *newvalues)
{
  int j, hint;
  sen_value *v;
  sen_lex *lex;
  sen_rc r, rc;
  sen_set *old, *new;
  sen_id rid, tid, *tp;
  sen_inv_updspec **u, **un;
  if (!i || !key) {
    SEN_LOG(sen_log_warning, "sen_index_update: invalid argument");
    return sen_invalid_argument;
  }
  if ((rc = sen_index_lock(i, -1))) {
    SEN_LOG(sen_log_crit, "sen_index_update: index lock failed");
    return rc;
  }
  if (newvalues) {
    if (!(rid = sen_sym_get(i->keys, key))) {
      rc = sen_invalid_argument;
      goto exit;
    }
    new = sen_set_open(sizeof(sen_id), sizeof(sen_inv_updspec *), 0);
    if (!new) {
      SEN_LOG(sen_log_alert, "sen_set_open on sen_index_update failed !");
      rc = sen_memory_exhausted;
      goto exit;
    }
    for (j = newvalues->n_values, v = newvalues->values; j; j--, v++) {
      if ((lex = sen_lex_open(i->lexicon, v->str, v->str_len, SEN_LEX_ADD|SEN_LEX_UPD))) {
        while (!lex->status) {
          if ((tid = sen_lex_next(lex))) {
            if (!sen_set_get(new, &tid, (void **) &u)) { break; }
            if (!*u) {
              if (!(*u = sen_inv_updspec_open(rid, section))) {
                SEN_LOG(sen_log_alert, "sen_inv_updspec_open on sen_index_update failed!");
                sen_lex_close(lex);
                sen_set_close(new);
                rc = sen_memory_exhausted;
                goto exit;
              }
            }
            if (sen_inv_updspec_add(*u, lex->pos, v->weight)) {
              SEN_LOG(sen_log_alert, "sen_inv_updspec_add on sen_index_update failed!");
              sen_lex_close(lex);
              sen_set_close(new);
              rc = sen_memory_exhausted;
              goto exit;
            }
          }
        }
        sen_lex_close(lex);
      }
    }
    if (!new->n_entries) {
      sen_set_close(new);
      new = NULL;
    }
  } else {
    if (!(rid = sen_sym_at(i->keys, key))) {
      rc = sen_invalid_argument;
      goto exit;
    }
    new = NULL;
  }
  if (oldvalues) {
    old = sen_set_open(sizeof(sen_id), sizeof(sen_inv_updspec *), 0);
    if (!old) {
      SEN_LOG(sen_log_alert, "sen_set_open(old) on sen_index_update failed!");
      if (new) { sen_set_close(new); }
      rc = sen_memory_exhausted;
      goto exit;
    }
    for (j = oldvalues->n_values, v = oldvalues->values; j; j--, v++) {
      if ((lex = sen_lex_open(i->lexicon, v->str, v->str_len, SEN_LEX_UPD))) {
        while (!lex->status) {
          if ((tid = sen_lex_next(lex))) {
            if (!sen_set_get(old, &tid, (void **) &u)) { break; }
            if (!*u) {
              if (!(*u = sen_inv_updspec_open(rid, section))) {
                SEN_LOG(sen_log_alert, "sen_inv_updspec_open on sen_index_update failed!");
                sen_lex_close(lex);
                if (new) { sen_set_close(new); };
                sen_set_close(old);
                rc = sen_memory_exhausted;
                goto exit;
              }
            }
            if (sen_inv_updspec_add(*u, lex->pos, v->weight)) {
              SEN_LOG(sen_log_alert, "sen_inv_updspec_add on sen_index_update failed!");
              sen_lex_close(lex);
              if (new) { sen_set_close(new); };
              sen_set_close(old);
              rc = sen_memory_exhausted;
              goto exit;
            }
          }
        }
        sen_lex_close(lex);
      }
    }
  } else {
    old = NULL;
  }
  if (old) {
    sen_set_eh *ehn;
    SEN_SET_EACH(old, eh, &tp, &u, {
      if (new && (ehn = sen_set_at(new, tp, (void **) &un))) {
        if (!sen_inv_updspec_cmp(*u, *un)) {
          sen_inv_updspec_close(*un);
          sen_set_del(new, ehn);
        }
      } else {
        sen_inv_delete(i->inv, *tp, *u, new);
      }
      sen_inv_updspec_close(*u);
    });
    sen_set_close(old);
  }
  if (new) {
    SEN_SET_EACH(new, eh, &tp, &u, {
      hint = sen_str_get_prefix_order(_sen_sym_key(i->lexicon, *tp));
      if (hint == -1) { hint = *tp; }
      if ((r = sen_inv_update(i->inv, *tp, *u, new, hint))) { rc = r; }
      sen_inv_updspec_close(*u);
    });
    sen_set_close(new);
  } else {
    if (!section) {
      /* todo: delete key when all sections deleted */
      if (!(i->foreign_flags & FOREIGN_KEY)) {
        rc = sen_sym_del(i->keys, key);
      }
    }
  }
exit :
  sen_index_unlock(i);
  return rc;
}

/* sen_records */

#define SCORE_SIZE (sizeof(int))

typedef struct {
  int score;
  int n_subrecs;
  byte subrecs[1];
} recinfo;

inline static int
rec_unit_size(sen_rec_unit unit)
{
  switch (unit) {
  case sen_rec_document :
    return sizeof(sen_id);
  case sen_rec_section :
    return sizeof(sen_id) + sizeof(int);
  case sen_rec_position :
    return sizeof(sen_id) + sizeof(int) + sizeof(int);
  default :
    return -1;
  }
}

sen_records *
sen_records_open(sen_rec_unit record_unit,
                 sen_rec_unit subrec_unit, unsigned int max_n_subrecs)
{
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  sen_records *r;
  int record_size = rec_unit_size(record_unit);
  int subrec_size = rec_unit_size(subrec_unit);
  if (record_size < 0) { return NULL; }
  if (max_n_subrecs && subrec_size <= record_size) { return NULL; }
  if (!(r = SEN_MALLOC(sizeof(sen_records)))) { return NULL; }
  r->record_unit = record_unit;
  r->subrec_unit = subrec_unit;
  r->record_size = record_size;
  r->subrec_size = subrec_size - record_size;
  r->max_n_subrecs = max_n_subrecs;
  r->keys = NULL;
  r->cursor = NULL;
  r->sorted = NULL;
  r->curr_rec = NULL;
  r->ignore_deleted_records = 0;
  if (!(r->records = sen_set_open(r->record_size,
                                  SCORE_SIZE + sizeof(int) +
                                  max_n_subrecs * (SCORE_SIZE + r->subrec_size), 0))) {
    SEN_FREE(r);
    return NULL;
  }
  return r;
}

inline static void
sen_records_cursor_clear(sen_records *r)
{
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  if (r->sorted) {
    SEN_FREE(r->sorted);
    r->sorted = NULL;
  }
  if (r->cursor) {
    sen_set_cursor_close(r->cursor);
    r->cursor = NULL;
  }
  r->curr_rec = NULL;
}

sen_rc
sen_records_close(sen_records *r)
{
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  if (!r) { return sen_invalid_argument; }
  if (r->curr_rec) {
    sen_id *rid;
    recinfo *ri;
    if (!sen_set_element_info(r->records, r->curr_rec, (void **)&rid, (void **)&ri)) {
      SEN_LOG(sen_log_debug, "curr_rec: %d:%d", *rid, ri->score);
      // sen_log("sen_record_next=%d,%d", *rid, *(_sen_sym_key(r->keys, *rid)));
    }
  }
  sen_records_cursor_clear(r);
  sen_set_close(r->records);
  SEN_FREE(r);
  return sen_success;
}

sen_rc
sen_records_reopen(sen_records *r, sen_rec_unit record_unit,
                   sen_rec_unit subrec_unit, unsigned int max_n_subrecs)
{
  int record_size = rec_unit_size(record_unit);
  int subrec_size = rec_unit_size(subrec_unit);
  if (!r || record_size < 0 || (max_n_subrecs && subrec_size <= record_size)) {
    return sen_invalid_argument;
  }
  sen_records_cursor_clear(r);
  sen_set_close(r->records);
  r->record_unit = record_unit;
  r->subrec_unit = subrec_unit;
  r->record_size = record_size;
  r->subrec_size = subrec_size - record_size;
  r->max_n_subrecs = max_n_subrecs;
  r->keys = NULL;
  r->cursor = NULL;
  r->sorted = NULL;
  r->curr_rec = NULL;
  r->ignore_deleted_records = 0;
  if (!(r->records = sen_set_open(r->record_size,
                                  SCORE_SIZE + sizeof(int) +
                                  max_n_subrecs * (SCORE_SIZE + r->subrec_size), 0))) {
    return sen_internal_error;
  }
  return sen_success;
}

sen_rc
sen_records_sort(sen_records *r, int limit, sen_sort_optarg *optarg)
{
  sen_set_sort_optarg arg;
  if (!r || !r->records) { return sen_invalid_argument; }
  sen_records_cursor_clear(r);
  if (!r->records->n_entries) { return sen_invalid_argument; }
  if (limit > r->records->n_entries) { limit = r->records->n_entries; }
  if (optarg) {
    arg.mode = optarg->mode;
    arg.compar =
      (int (*)(sen_set *, sen_set_eh *, sen_set *, sen_set_eh *, void *))optarg->compar;
    arg.compar_arg = optarg->compar_arg;
    arg.compar_arg0 = (sen_set *) r;
    r->sorted = sen_set_sort(r->records, limit, &arg);
  } else {
    arg.mode = sen_sort_descending;
    arg.compar = NULL;
    arg.compar_arg = (void *)(intptr_t)r->records->key_size;
    arg.compar_arg0 = NULL;
    r->sorted = sen_set_sort(r->records, limit, &arg);
  }
  r->limit = limit;
  return r->sorted ? sen_success : sen_internal_error;
}

sen_rc
sen_records_rewind(sen_records *r)
{
  if (!r) { return sen_invalid_argument; }
  if (r->sorted) {
    r->curr_rec = NULL;
    return sen_success;
  } else {
    sen_records_cursor_clear(r);
    r->cursor = sen_set_cursor_open(r->records);
    return r->cursor ? sen_success : sen_internal_error;
  }
}

int
sen_records_next(sen_records *r, void *keybuf, int bufsize, int *score)
{
  if (!r) { return 0; }
  if (r->sorted) {
    if (r->curr_rec) {
      if (r->curr_rec - r->sorted >= r->limit - 1) { return 0; }
      r->curr_rec++;
    } else {
      if (r->limit > 0) { r->curr_rec = r->sorted; }
    }
  } else {
    if (!r->cursor && sen_records_rewind(r)) { return 0; }
    r->curr_rec = sen_set_cursor_next(r->cursor, NULL, NULL);
  }
  if (r->curr_rec) {
    sen_id *rid;
    recinfo *ri;
    if (!sen_set_element_info(r->records, r->curr_rec,  (void **)&rid, (void **)&ri)) {
      if (score) { *score = ri->score; }
      if (r->record_unit != sen_rec_userdef) {
        return r->keys ? sen_sym_key(r->keys, *rid, keybuf, bufsize) : r->record_size;
      } else {
        if (r->record_size <= bufsize) { memcpy(keybuf, rid, r->record_size); }
        return r->record_size;
      }
    }
  }
  return 0;
}

int
sen_records_find(sen_records *r, const void *key)
{
  sen_id rid;
  recinfo *ri;
  if (!r || !r->keys || r->record_unit != sen_rec_document) { return 0; }
  if (!(rid = sen_sym_at(r->keys, key))) { return 0; }
  if (!sen_set_at(r->records, &rid, (void **) &ri)) { return 0; }
  return ri->score;
}

int
sen_records_nhits(sen_records *r)
{
  if (!r || !r->records) { return 0; }
  return r->records->n_entries;
}

int
sen_records_curr_score(sen_records *r)
{
  recinfo *ri;
  if (!r || !r->curr_rec) { return 0; }
  if (sen_set_element_info(r->records, r->curr_rec, NULL, (void **)&ri)) { return 0; }
  return ri->score;
}

int
sen_records_curr_key(sen_records *r, void *keybuf, int bufsize)
{
  sen_id *rid;
  if (!r || !r->curr_rec) { return 0; }
  if (sen_set_element_info(r->records, r->curr_rec, (void **)&rid, NULL)) {
    return 0;
  }
  if (r->record_unit != sen_rec_userdef) {
    return sen_sym_key(r->keys, *rid, keybuf, bufsize);
  } else {
    if (r->record_size <= bufsize) { memcpy(keybuf, rid, r->record_size); }
    return r->record_size;
  }
}

const sen_recordh *
sen_records_curr_rec(sen_records *r)
{
  if (!r) { return NULL; }
  return r->curr_rec;
}

sen_records *
sen_records_union(sen_records *a, sen_records *b)
{
  if (!a || !b) { return NULL; }
  if (a->keys != b->keys) { return NULL; }
  if (!sen_set_union(a->records, b->records)) { return NULL; }
  b->records = NULL;
  sen_records_close(b);
  sen_records_cursor_clear(a);
  return a;
}

sen_records *
sen_records_subtract(sen_records *a, sen_records *b)
{
  if (!a || !b) { return NULL; }
  if (a->keys != b->keys) { return NULL; }
  if (!sen_set_subtract(a->records, b->records)) { return NULL; }
  b->records = NULL;
  sen_records_close(b);
  sen_records_cursor_clear(a);
  return a;
}

sen_records *
sen_records_intersect(sen_records *a, sen_records *b)
{
  sen_set *c;
  if (!a || !b) { return NULL; }
  if (a->keys != b->keys) { return NULL; }
  if (a->records->n_entries > b->records->n_entries) {
    c = a->records;
    a->records = b->records;
    b->records = c;
  }
  if (!sen_set_intersect(a->records, b->records)) { return NULL; }
  b->records = NULL;
  sen_records_close(b);
  sen_records_cursor_clear(a);
  return a;
}

int
sen_records_difference(sen_records *a, sen_records *b)
{
  int count;
  if (!a || !b) { return -1; }
  if (a->keys != b->keys) { return -1; }
  if ((count = sen_set_difference(a->records, b->records)) >= 0) {
    sen_records_cursor_clear(a);
    sen_records_cursor_clear(b);
  }
  return count;
}

#define SUBRECS_CMP(a,b,dir) (((a) - (b))*(dir) > 0)
#define SUBRECS_NTH(subrecs,size,n) ((int *)(subrecs + n * (size + SCORE_SIZE)))
#define SUBRECS_COPY(subrecs,size,n,src) \
  (memcpy(subrecs + n * (size + SCORE_SIZE), src, size + SCORE_SIZE))

inline static void
subrecs_push(byte *subrecs, int size, int n_subrecs, int score, void *body, int dir)
{
  byte *v;
  int *c2;
  int n = n_subrecs - 1, n2;
  while (n) {
    n2 = (n - 1) >> 1;
    c2 = SUBRECS_NTH(subrecs,size,n2);
    if (SUBRECS_CMP(score, *c2, dir)) { break; }
    SUBRECS_COPY(subrecs,size,n,c2);
    n = n2;
  }
  v = subrecs + n * (size + SCORE_SIZE);
  *((int *)v) = score;
  memcpy(v + SCORE_SIZE, body, size);
}

inline static void
subrecs_replace_min(byte *subrecs, int size, int n_subrecs, int score, void *body, int dir)
{
  byte *v;
  int n = 0, n1, n2, *c1, *c2;
  for (;;) {
    n1 = n * 2 + 1;
    n2 = n1 + 1;
    c1 = n1 < n_subrecs ? SUBRECS_NTH(subrecs,size,n1) : NULL;
    c2 = n2 < n_subrecs ? SUBRECS_NTH(subrecs,size,n2) : NULL;
    if (c1 && SUBRECS_CMP(score, *c1, dir)) {
      if (c2 && SUBRECS_CMP(score, *c2, dir) && SUBRECS_CMP(*c1, *c2, dir)) {
        SUBRECS_COPY(subrecs,size,n,c2);
        n = n2;
      } else {
        SUBRECS_COPY(subrecs,size,n,c1);
        n = n1;
      }
    } else {
      if (c2 && SUBRECS_CMP(score, *c2, dir)) {
        SUBRECS_COPY(subrecs,size,n,c2);
        n = n2;
      } else {
        break;
      }
    }
  }
  v = subrecs + n * (size + SCORE_SIZE);
  memcpy(v, &score, SCORE_SIZE);
  memcpy(v + SCORE_SIZE, body, size);
}

sen_rc
sen_records_group(sen_records *r, int limit, sen_group_optarg *optarg)
{
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  sen_set *g;
  recinfo *ri;
  sen_rec_unit unit;
  sen_set_cursor *c;
  const sen_recordh *rh;
  byte *key, *ekey, *gkey = NULL;
  int funcp, ssize, unit_size, dir;
  unsigned int rsize;
  if (!r || !r->records) { return sen_invalid_argument; }
  if (optarg) {
    unit = sen_rec_userdef;
    rsize = optarg->key_size;
    funcp = optarg->func ? 1 : 0;
    dir = (optarg->mode == sen_sort_ascending) ? -1 : 1;
  } else {
    unit = sen_rec_document;
    rsize = rec_unit_size(unit);
    funcp = 0;
    dir = 1;
  }
  if (funcp) {
    ssize = r->record_size;
    gkey = SEN_MALLOC(rsize ? rsize : 8192);
    if (!gkey) {
      SEN_LOG(sen_log_alert, "allocation for gkey failed !");
      return sen_memory_exhausted;
    }
  } else {
    if (r->record_size <= rsize) { return sen_invalid_argument; }
    ssize = r->record_size - rsize;
  }
  unit_size = SCORE_SIZE + ssize;
  if (!(c = sen_set_cursor_open(r->records))) {
    SEN_LOG(sen_log_alert, "sen_set_cursor_open on sen_records_group failed !");
    if (gkey) { SEN_FREE(gkey); }
    return sen_memory_exhausted;
  }
  if (!(g = sen_set_open(rsize, SCORE_SIZE + sizeof(int) + unit_size * limit, 0))) {
    SEN_LOG(sen_log_alert, "sen_set_open on sen_records_group failed !");
    sen_set_cursor_close(c);
    if (gkey) { SEN_FREE(gkey); }
    return sen_memory_exhausted;
  }
  while ((rh = sen_set_cursor_next(c, (void **)&key, (void **)&ri))) {
    if (funcp) {
      if (optarg->func(r, rh, gkey, optarg->func_arg)) { continue; }
      ekey = key;
    } else {
      gkey = key;
      ekey = key + rsize;
    }
    {
      recinfo *gri;
      if (sen_set_get(g, gkey, (void **)&gri)) {
        gri->score += ri->score;
        gri->n_subrecs += 1;
        if (limit) {
          if (limit < gri->n_subrecs) {
            if (SUBRECS_CMP(ri->score, *gri->subrecs, dir)) {
              subrecs_replace_min(gri->subrecs, ssize, limit, ri->score, ekey, dir);
            }
          } else {
            subrecs_push(gri->subrecs, ssize, gri->n_subrecs, ri->score, ekey, dir);
          }
        }
      }
    }
  }
  sen_set_cursor_close(c);
  sen_set_close(r->records);
  if (funcp) { SEN_FREE(gkey); }
  r->records = g;
  r->subrec_unit = r->record_unit;
  r->record_unit = unit; //sen_rec_document;??
  r->subrec_size = ssize;
  r->record_size = rsize;
  r->max_n_subrecs = limit;
  sen_records_cursor_clear(r);
  return sen_success;
}

typedef struct {
  sen_id rid;
  uint32_t sid;
  uint32_t pos;
} posinfo;

const sen_recordh *
sen_records_at(sen_records *r, const void *key, unsigned section, unsigned pos,
               int *score, int *n_subrecs)
{
  recinfo *ri;
  const sen_recordh *res;
  if (!r || !r->keys) { return NULL; }
  if (r->record_unit == sen_rec_userdef) {
    res = r->curr_rec = sen_set_at(r->records, key, (void **) &ri);
  } else {
    posinfo pi;
    if (!(pi.rid = sen_sym_at(r->keys, key))) { return NULL; }
    pi.sid = section;
    pi.pos = pos;
    res = r->curr_rec = sen_set_at(r->records, &pi, (void **) &ri);
  }
  if (res) {
    if (score) { *score = ri->score; }
    if (n_subrecs) { *n_subrecs = ri->n_subrecs; }
  }
  return res;
}

sen_rc
sen_record_info(sen_records *r, const sen_recordh *rh,
                void *keybuf, int bufsize, int *keysize,
                int *section, int *pos, int *score, int *n_subrecs)
{
  sen_rc rc;
  posinfo *pi;
  recinfo *ri;
  if (!r || !rh) { return sen_invalid_argument; }
  rc = sen_set_element_info(r->records, rh, (void **)&pi, (void **)&ri);
  if (rc) { return rc; }
  switch (r->record_unit) {
  case sen_rec_document :
    if ((keybuf && bufsize) || keysize) {
      int len = sen_sym_key(r->keys, pi->rid, keybuf, bufsize);
      if (keysize) { *keysize = len; }
    }
    if (section) { *section = 0; }
    if (pos) { *pos = 0; }
    break;
  case sen_rec_section :
    if ((keybuf && bufsize) || keysize) {
      int len = sen_sym_key(r->keys, pi->rid, keybuf, bufsize);
      if (keysize) { *keysize = len; }
    }
    if (section) { *section = pi->sid; }
    if (pos) { *pos = 0; }
    break;
  case sen_rec_position :
    if ((keybuf && bufsize) || keysize) {
      int len = sen_sym_key(r->keys, pi->rid, keybuf, bufsize);
      if (keysize) { *keysize = len; }
    }
    if (section) { *section = pi->sid; }
    if (pos) { *pos = pi->pos; }
    break;
  case sen_rec_userdef :
    if ((keybuf && bufsize) || keysize) {
      unsigned int len = r->record_size;
      if (!len) { len = (unsigned int)strlen((const char *)pi) + 1; }
      if ((unsigned int)bufsize >= len) { memcpy(keybuf, pi, len); }
      if (keysize) { *keysize = len; }
    }
    if (section) { *section = 0; }
    if (pos) { *pos = 0; }
    break;
  default :
    return sen_invalid_format;
    break;
  }
  if (score) { *score = ri->score; }
  if (n_subrecs) { *n_subrecs = ri->n_subrecs; }
  return sen_success;
}

sen_rc
sen_record_subrec_info(sen_records *r, const sen_recordh *rh, int index,
                       void *keybuf, int bufsize, int *keysize,
                       int *section, int *pos, int *score)
{
  sen_rc rc;
  posinfo *pi;
  recinfo *ri;
  int *p, unit_size = SCORE_SIZE + r->subrec_size;
  if (!r || !rh || index < 0) { return sen_invalid_argument; }
  if ((unsigned int)index >= r->max_n_subrecs) { return sen_invalid_argument; }
  rc = sen_set_element_info(r->records, rh, (void **)&pi, (void **)&ri);
  if (rc) { return rc; }
  if (index >= ri->n_subrecs) { return sen_invalid_argument; }
  p = (int *)(ri->subrecs + index * unit_size);
  if (score) { *score = p[0]; }
  switch (r->record_unit) {
  case sen_rec_document :
    if ((keybuf && bufsize) || keysize) {
      int len = sen_sym_key(r->keys, pi->rid, keybuf, bufsize);
      if (keysize) { *keysize = len; }
    }
    if (section) { *section = (r->subrec_unit != sen_rec_userdef) ? p[1] : 0; }
    if (pos) { *pos = (r->subrec_unit == sen_rec_position) ? p[2] : 0; }
    break;
  case sen_rec_section :
    if ((keybuf && bufsize) || keysize) {
      int len = sen_sym_key(r->keys, pi->rid, keybuf, bufsize);
      if (keysize) { *keysize = len; }
    }
    if (section) { *section = pi->sid; }
    if (pos) { *pos = (r->subrec_unit == sen_rec_position) ? p[1] : 0; }
    break;
  default :
    {
      posinfo *spi = (posinfo *)&p[1];
      switch (r->subrec_unit) {
      case sen_rec_document :
        if ((keybuf && bufsize) || keysize) {
          int len = sen_sym_key(r->keys, spi->rid, keybuf, bufsize);
          if (keysize) { *keysize = len; }
        }
        if (section) { *section = 0; }
        if (pos) { *pos = 0; }
        break;
      case sen_rec_section :
        if ((keybuf && bufsize) || keysize) {
          int len = sen_sym_key(r->keys, spi->rid, keybuf, bufsize);
          if (keysize) { *keysize = len; }
        }
        if (section) { *section = spi->sid; }
        if (pos) { *pos = 0; }
        break;
      case sen_rec_position :
        if ((keybuf && bufsize) || keysize) {
          int len = sen_sym_key(r->keys, spi->rid, keybuf, bufsize);
          if (keysize) { *keysize = len; }
        }
        if (section) { *section = spi->sid; }
        if (pos) { *pos = spi->pos; }
        break;
      default :
        if ((keybuf && bufsize) || keysize) {
          unsigned int len = r->subrec_size;
          if (!len) { len = (unsigned int)strlen((char *)&p[1]) + 1; }
          if ((unsigned int)bufsize >= len) { memcpy(keybuf, &p[1], len); }
          if (keysize) { *keysize = len; }
        }
        if (section) { *section = 0; }
        if (pos) { *pos = 0; }
        break;
      }
    }
    break;
  }
  return sen_success;
}

/* select */

#define B31    0x80000000
#define B30_00 0x7fffffff
#define BIT30_00(x) (x & B30_00)

inline static void
res_add(sen_records *r, posinfo *pi, uint32_t score, sen_sel_operator op)
{
  recinfo *ri;
  sen_set_eh *eh = NULL;
  if (r->ignore_deleted_records &&
      sen_sym_pocket_get(r->keys, pi->rid) == DELETE_FLAG) { return; }
  switch (op) {
  case sen_sel_or :
    eh = sen_set_get(r->records, pi, (void **)&ri);
    break;
  case sen_sel_and :
    if ((eh = sen_set_at(r->records, pi, (void **)&ri))) {
      ri->n_subrecs |= B31;
    }
    break;
  case sen_sel_but :
    if ((eh = sen_set_at(r->records, pi, (void **)&ri))) {
      sen_set_del(r->records, eh);
      eh = NULL;
    }
    break;
  case sen_sel_adjust :
    if ((eh = sen_set_at(r->records, pi, (void **)&ri))) {
      ri->score += score;
      eh = NULL;
    }
    break;
  }

  if (eh) {
    int limit = r->max_n_subrecs;
    ri->score += score;
    ri->n_subrecs += 1;
    if (limit) {
      int dir = 1;
      int ssize = r->subrec_size;
      int n_subrecs = BIT30_00(ri->n_subrecs);
      byte *ekey = ((byte *)pi) + r->record_size;
      if (limit < n_subrecs) {
        if (SUBRECS_CMP(score, *ri->subrecs, dir)) {
          subrecs_replace_min(ri->subrecs, ssize, limit, score, ekey, dir);
        }
      } else {
        subrecs_push(ri->subrecs, ssize, n_subrecs, score, ekey, dir);
      }
    }
  }
}

struct _btr_node {
  struct _btr_node *car;
  struct _btr_node *cdr;
  token_info *ti;
};

typedef struct _btr_node btr_node;

typedef struct {
  int n;
  token_info *min;
  token_info *max;
  btr_node *root;
  btr_node *nodes;
} btr;

inline static void
bt_zap(btr *bt)
{
  bt->n = 0;
  bt->min = NULL;
  bt->max = NULL;
  bt->root = NULL;
}

inline static btr *
bt_open(int size)
{
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  btr *bt = SEN_MALLOC(sizeof(btr));
  if (bt) {
    bt_zap(bt);
    if (!(bt->nodes = SEN_MALLOC(sizeof(btr_node) * size))) {
      SEN_FREE(bt);
      bt = NULL;
    }
  }
  return bt;
}

inline static void
bt_close(btr *bt)
{
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  if (!bt) { return; }
  SEN_FREE(bt->nodes);
  SEN_FREE(bt);
}

inline static void
bt_push(btr *bt, token_info *ti)
{
  int pos = ti->pos, minp = 1, maxp = 1;
  btr_node *node, *new, **last;
  new = bt->nodes + bt->n++;
  new->ti = ti;
  new->car = NULL;
  new->cdr = NULL;
  for (last = &bt->root; (node = *last);) {
    if (pos < node->ti->pos) {
      last = &node->car;
      maxp = 0;
    } else {
      last = &node->cdr;
      minp = 0;
    }
  }
  *last = new;
  if (minp) { bt->min = ti; }
  if (maxp) { bt->max = ti; }
}

inline static void
bt_pop(btr *bt)
{
  btr_node *node, *min, *newmin, **last;
  for (last = &bt->root; (min = *last) && min->car; last = &min->car) ;
  if (min) {
    int pos = min->ti->pos, minp = 1, maxp = 1;
    *last = min->cdr;
    min->cdr = NULL;
    for (last = &bt->root; (node = *last);) {
      if (pos < node->ti->pos) {
        last = &node->car;
        maxp = 0;
      } else {
        last = &node->cdr;
        minp = 0;
      }
    }
    *last = min;
    if (maxp) { bt->max = min->ti; }
    if (!minp) {
      for (newmin = bt->root; newmin->car; newmin = newmin->car) ;
      bt->min = newmin->ti;
    }
  }
}

typedef enum {
  sen_wv_none = 0,
  sen_wv_static,
  sen_wv_dynamic,
  sen_wv_constant
} sen_wv_mode;

inline static int
get_weight(sen_records *r, sen_id rid, int sid,
           sen_wv_mode wvm, sen_select_optarg *optarg)
{
  switch (wvm) {
  case sen_wv_none :
    return 1;
  case sen_wv_static :
    return sid <= optarg->vector_size ? optarg->weight_vector[sid - 1] : 0;
  case sen_wv_dynamic :
    {
      const char *key = _sen_sym_key(r->keys, rid);
      return key ? optarg->func(r, key, sid, optarg->func_arg) : 0;
    }
  case sen_wv_constant :
    return optarg->vector_size;
  default :
    return 1;
  }
}

sen_rc
sen_index_similar_search(sen_index *i, const char *string,
                         unsigned int string_len, sen_records *r,
                         sen_sel_operator op, sen_select_optarg *optarg)
{
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  int *w1, limit;
  sen_id tid, *tp;
  sen_rc rc = sen_success;
  sen_set *h;
  sen_lex *lex;
  if (!i || !string || !r || !optarg) { return sen_invalid_argument; }
  if (!(h = sen_set_open(sizeof(sen_id), sizeof(int), 0))) {
    return sen_memory_exhausted;
  }
  if (!(lex = sen_lex_open(i->lexicon, string, string_len, 0))) {
    sen_set_close(h);
    return sen_memory_exhausted;
  }
  r->keys = i->keys;
  while (lex->status != sen_lex_done) {
    if ((tid = sen_lex_next(lex))) {
      if (sen_set_get(h, &tid, (void **)&w1)) { (*w1)++; }
    }
    if (lex->token && *lex->token) {
      if (optarg->max_interval == sen_sel_unsplit) {
        sen_sym_prefix_search_with_set(i->lexicon, lex->token, h);
      }
      if (optarg->max_interval == sen_sel_partial) {
        sen_sym_suffix_search_with_set(i->lexicon, lex->token, h);
      }
    }
  }
  sen_lex_close(lex);
  {
    sen_set_eh *eh;
    sen_set_cursor *c = sen_set_cursor_open(h);
    int maxsize = sen_sym_size(i->keys) * sizeof(int);
    if (!c) {
      SEN_LOG(sen_log_alert, "sen_set_cursor_open on sen_index_similar_search failed !");
      sen_set_close(h);
      return sen_memory_exhausted;
    }
    while ((eh = sen_set_cursor_next(c, (void **) &tp, (void **) &w1))) {
      uint32_t es = sen_inv_estimate_size(i->inv, *tp);
      if (es) {
        *w1 += maxsize / es;
      } else {
        sen_set_del(h, eh);
      }
    }
    sen_set_cursor_close(c);
  }
  limit = optarg->similarity_threshold
    ? (optarg->similarity_threshold > h->n_entries
       ? h->n_entries
       : optarg->similarity_threshold)
    : (h->n_entries >> 3) + 1;
  if (h->n_entries) {
    int j, w2, rep;
    sen_inv_cursor *c;
    sen_inv_posting *pos;
    sen_wv_mode wvm = sen_wv_none;
    sen_set_sort_optarg arg = {sen_sort_descending, NULL, (void *)sizeof(sen_id), NULL};
    sen_set_eh *eh, *sorted = sen_set_sort(h, limit, &arg);
    if (!sorted) {
      SEN_LOG(sen_log_alert, "sen_set_sort on sen_index_similar_search failed !");
      sen_set_close(h);
      return sen_memory_exhausted;
    }
    rep = (r->record_unit == sen_rec_position || r->subrec_unit == sen_rec_position);
    if (optarg->func) {
      wvm = sen_wv_dynamic;
    } else if (optarg->vector_size) {
      wvm = optarg->weight_vector ? sen_wv_static : sen_wv_constant;
    }
    for (j = 0, eh = sorted; j < limit; j++, eh++) {
      sen_set_element_info(h, eh, (void **) &tp, (void **) &w1);
      if (!*tp || !(c = sen_inv_cursor_open(i->inv, *tp, rep))) {
        SEN_LOG(sen_log_error, "cursor open failed (%d)", *tp);
        continue;
      }
      if (rep) {
        while (!sen_inv_cursor_next(c)) {
          pos = c->post;
          if ((w2 = get_weight(r, pos->rid, pos->sid, wvm, optarg))) {
            while (!sen_inv_cursor_next_pos(c)) {
              res_add(r, (posinfo *) pos, *w1 * w2 * (1 + pos->score), op);
            }
          }
        }
      } else {
        while (!sen_inv_cursor_next(c)) {
          pos = c->post;
          if ((w2 = get_weight(r, pos->rid, pos->sid, wvm, optarg))) {
            res_add(r, (posinfo *) pos, *w1 * w2 * (pos->tf + pos->score), op);
          }
        }
      }
      sen_inv_cursor_close(c);
    }
    SEN_FREE(sorted);
  }
  sen_set_close(h);
  if (op == sen_sel_and) {
    recinfo *ri;
    sen_set_eh *eh;
    sen_set_cursor *c = sen_set_cursor_open(r->records);
    if (!c) {
      SEN_LOG(sen_log_alert, "sen_set_cursor_open on sen_index_similar_search failed!");
      return sen_memory_exhausted;
    }
    while ((eh = sen_set_cursor_next(c, NULL, (void **) &ri))) {
      if ((ri->n_subrecs & B31)) {
        ri->n_subrecs &= B30_00;
      } else {
        sen_set_del(r->records, eh);
      }
    }
    sen_set_cursor_close(c);
  }
  sen_records_cursor_clear(r);
  return rc;
}

#define TERM_EXTRACT_EACH_POST 0
#define TERM_EXTRACT_EACH_TERM 1

sen_rc
sen_index_term_extract(sen_index *i, const char *string,
                       unsigned int string_len, sen_records *r,
                       sen_sel_operator op, sen_select_optarg *optarg)
{
  posinfo pi;
  sen_id tid;
  const char *p;
  sen_nstr *nstr;
  sen_inv_cursor *c;
  sen_inv_posting *pos;
  sen_rc rc = sen_success;
  sen_sym *sym = i->lexicon;
  sen_wv_mode wvm = sen_wv_none;
  int skip, position, rep, policy;
  if (!i || !string || !string_len || !r || !optarg ||
      !(nstr = sen_nstr_open(string, string_len, sym->encoding, 0)) ) {
    return sen_invalid_argument;
  }
  policy = optarg->max_interval;
  if (optarg->func) {
    wvm = sen_wv_dynamic;
  } else if (optarg->vector_size) {
    wvm = optarg->weight_vector ? sen_wv_static : sen_wv_constant;
  }
  if (policy == TERM_EXTRACT_EACH_POST) {
    if ((rc = sen_records_reopen(r, sen_rec_section, sen_rec_none, 0))) { goto exit; }
  }
  r->keys = i->keys;
  rep = (r->record_unit == sen_rec_position || r->subrec_unit == sen_rec_position);
  for (p = nstr->norm, position = 0; *p; p += skip, position += skip) {
    if ((tid = sen_sym_common_prefix_search(sym, p))) {
      if (policy == TERM_EXTRACT_EACH_POST) {
        if (!(skip = sen_sym_key(sym, tid, NULL, 0))) { break; }
        skip -= 1;
      } else {
        if (!(skip = (int)sen_str_charlen(p, sym->encoding))) { break; }
      }
      if (!(c = sen_inv_cursor_open(i->inv, tid, rep))) {
        SEN_LOG(sen_log_error, "cursor open failed (%d)", tid);
        continue;
      }
      if (rep) {
        while (!sen_inv_cursor_next(c)) {
          pos = c->post;
          while (!sen_inv_cursor_next_pos(c)) {
            res_add(r, (posinfo *) pos,
                    get_weight(r, pos->rid, pos->sid, wvm, optarg), op);
          }
        }
      } else {
        while (!sen_inv_cursor_next(c)) {
          if (policy == TERM_EXTRACT_EACH_POST) {
            pi.rid = c->post->rid;
            pi.sid = position;
            res_add(r, &pi, position + 1, op);
          } else {
            pos = c->post;
            res_add(r, (posinfo *) pos,
                    get_weight(r, pos->rid, pos->sid, wvm, optarg), op);
          }
        }
      }
      sen_inv_cursor_close(c);
    } else {
      if (!(skip = (int)sen_str_charlen(p, sym->encoding))) {
        break;
      }
    }
  }
  sen_records_cursor_clear(r);
  if (policy == TERM_EXTRACT_EACH_POST) {
    sen_sort_optarg opt;
    opt.mode = sen_sort_ascending;
    opt.compar = NULL;
    opt.compar_arg = (void *)(intptr_t)r->records->key_size;
    sen_records_sort(r, 10000, &opt); /* todo : why 10000? */
  }
exit :
  sen_nstr_close(nstr);
  return rc;
}

sen_rc
sen_index_select(sen_index *i, const char *string, unsigned int string_len,
                 sen_records *r, sen_sel_operator op, sen_select_optarg *optarg)
{
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  btr *bt = NULL;
  sen_rc rc = sen_success;
  int rep, orp, weight, max_interval = 0;
  token_info *ti, **tis, **tip, **tie;
  uint32_t n = 0, rid, sid, nrid, nsid;
  sen_sel_mode mode = sen_sel_exact;
  sen_wv_mode wvm = sen_wv_none;
  if (!i || !r) { return sen_invalid_argument; }
  if (optarg) {
    mode = optarg->mode;
    if (optarg->func) {
      wvm = sen_wv_dynamic;
    } else if (optarg->vector_size) {
      wvm = optarg->weight_vector ? sen_wv_static : sen_wv_constant;
    }
  }
  if (mode == sen_sel_similar) {
    return sen_index_similar_search(i, string, string_len, r, op, optarg);
  }
  if (mode == sen_sel_term_extract) {
    return sen_index_term_extract(i, string, string_len, r, op, optarg);
  }
  rep = (r->record_unit == sen_rec_position || r->subrec_unit == sen_rec_position);
  orp = (r->record_unit == sen_rec_position || op == sen_sel_or);
  if (!(tis = SEN_MALLOC(sizeof(token_info *) * string_len * 2))) {
    return sen_memory_exhausted;
  }
  r->keys = i->keys;
  if (token_info_build(i, string, string_len, tis, &n, mode) || !n) { goto exit; }
  switch (mode) {
  case sen_sel_near2 :
    token_info_clear_offset(tis, n);
    mode = sen_sel_near;
    /* fallthru */
  case sen_sel_near :
    if (!(bt = bt_open(n))) { rc = sen_memory_exhausted; goto exit; }
    max_interval = optarg->max_interval;
    break;
  default :
    break;
  }
  qsort(tis, n, sizeof(token_info *), token_compare);
  tie = tis + n;
  /*
  for (tip = tis; tip < tie; tip++) {
    ti = *tip;
    sen_log("o=%d n=%d s=%d r=%d", ti->offset, ti->ntoken, ti->size, ti->rid);
  }
  */
  SEN_LOG(sen_log_info, "n=%d (%s)", n, string);
  if (n == 1 && (*tis)->cursors->n_entries == 1 && op == sen_sel_or
      && !r->records->n_entries && !r->records->garbages
      && r->record_unit == sen_rec_document && !r->max_n_subrecs
      && sen_inv_max_section(i->inv) == 1) {
    sen_inv_cursor *c = (*tis)->cursors->bins[0];
    if ((rc = sen_set_array_init(r->records, (*tis)->size + 32768))) { goto exit; }
    do {
      recinfo *ri;
      sen_inv_posting *p = c->post;
      if ((weight = get_weight(r, p->rid, p->sid, wvm, optarg))) {
        SEN_SET_INT_ADD(r->records, p, ri);
        ri->score = (p->tf + p->score) * weight;
        ri->n_subrecs = 1;
      }
    } while (!sen_inv_cursor_next(c));
    goto exit;
  }
  for (;;) {
    rid = (*tis)->p->rid;
    sid = (*tis)->p->sid;
    for (tip = tis + 1, nrid = rid, nsid = sid + 1; tip < tie; tip++) {
      ti = *tip;
      if (token_info_skip(ti, rid, sid)) { goto exit; }
      if (ti->p->rid != rid || ti->p->sid != sid) {
        nrid = ti->p->rid;
        nsid = ti->p->sid;
        break;
      }
    }
    weight = get_weight(r, rid, sid, wvm, optarg);
    if (tip == tie && weight) {
      posinfo pi = {rid, sid, 0};
      if (orp || sen_set_at(r->records, &pi, NULL)) {
        int count = 0, noccur = 0, pos = 0, score = 0, tscore = 0, min, max;

#define SKIP_OR_BREAK(pos) {\
  if (token_info_skip_pos(ti, rid, sid, pos)) { break; } \
  if (ti->p->rid != rid || ti->p->sid != sid) { \
    nrid = ti->p->rid; \
    nsid = ti->p->sid; \
    break; \
  } \
}
        if (n == 1 && !rep) {
          noccur = (*tis)->p->tf;
          tscore = (*tis)->p->score;
        } else if (mode == sen_sel_near) {
          bt_zap(bt);
          for (tip = tis; tip < tie; tip++) {
            ti = *tip;
            SKIP_OR_BREAK(pos);
            bt_push(bt, ti);
          }
          if (tip == tie) {
            for (;;) {
              ti = bt->min; min = ti->pos; max = bt->max->pos;
              if (min > max) { exit(0); }
              if (max - min <= max_interval) {
                if (rep) { pi.pos = min; res_add(r, &pi, weight, op); }
                noccur++;
                if (ti->pos == max + 1) {
                  break;
                }
                SKIP_OR_BREAK(max + 1);
              } else {
                if (ti->pos == max - max_interval) {
                  break;
                }
                SKIP_OR_BREAK(max - max_interval);
              }
              bt_pop(bt);
            }
          }
        } else {
          for (tip = tis; ; tip++) {
            if (tip == tie) { tip = tis; }
            ti = *tip;
            SKIP_OR_BREAK(pos);
            if (ti->pos == pos) {
              score += ti->p->score; count++;
            } else {
              score = ti->p->score; count = 1; pos = ti->pos;
            }
            if (count == n) {
              if (rep) { pi.pos = pos; res_add(r, &pi, (score + 1) * weight, op); }
              tscore += score;
              score = 0; count = 0; pos++;
              noccur++;
            }
          }
        }
        if (noccur && !rep) { res_add(r, &pi, (noccur + tscore) * weight, op); }
      }
    }
    if (token_info_skip(*tis, nrid, nsid)) { goto exit; }
  }
exit :
  for (tip = tis; tip < tis + n; tip++) {
    if (*tip) { token_info_close(*tip); }
  }
  SEN_FREE(tis);
  if (op == sen_sel_and) {
    recinfo *ri;
    sen_set_eh *eh;
    sen_set_cursor *c = sen_set_cursor_open(r->records);
    if (c) {
      while ((eh = sen_set_cursor_next(c, NULL, (void **) &ri))) {
        if ((ri->n_subrecs & B31)) {
          ri->n_subrecs &= B30_00;
        } else {
          sen_set_del(r->records, eh);
        }
      }
      sen_set_cursor_close(c);
    }
    else {
      SEN_LOG(sen_log_alert, "sen_set_cursor_open on sen_index_select failed !");
    }
  }
  sen_records_cursor_clear(r);
  bt_close(bt);
#ifdef DEBUG
  {
    uint32_t segno = SEN_INV_MAX_SEGMENT, nnref = 0;
    sen_io_mapinfo *info = i->inv->seg->maps;
    for (; segno; segno--, info++) { if (info->nref) { nnref++; } }
    SEN_LOG(sen_log_info, "nnref=%d", nnref);
  }
#endif /* DEBUG */
  sen_inv_seg_expire(i->inv, -1);
  return rc;
}

sen_records *
sen_index_sel(sen_index *i, const char *string, unsigned int string_len)
{
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  ERRCLR(ctx);
  SEN_LOG(sen_log_info, "sen_index_sel > (%s)", string);
  {
  sen_select_optarg arg = {sen_sel_exact, 0, 0, NULL, 0, NULL, NULL};
  sen_records *r = sen_records_open(sen_rec_document, sen_rec_none, 0);
  if (!r) { return NULL; }
  if (sen_index_select(i, string, string_len, r, sen_sel_or, &arg)) {
    SEN_LOG(sen_log_error, "sen_index_select on sen_index_sel(1) failed !");
    sen_records_close(r);
    return NULL;
  }
  SEN_LOG(sen_log_info, "exact: %d", r->records->n_entries);
  if (r->records->n_entries <= SENNA_DEFAULT_QUERY_ESCALATION_THRESHOLD) {
    arg.mode = sen_sel_unsplit;
    if (sen_index_select(i, string, string_len, r, sen_sel_or, &arg)) {
      SEN_LOG(sen_log_error, "sen_index_select on sen_index_sel(2) failed !");
      sen_records_close(r);
      return NULL;
    }
    SEN_LOG(sen_log_info, "unsplit: %d", r->records->n_entries);
  }
  if (r->records->n_entries <= SENNA_DEFAULT_QUERY_ESCALATION_THRESHOLD) {
    arg.mode = sen_sel_partial;
    if (sen_index_select(i, string, string_len, r, sen_sel_or, &arg)) {
      SEN_LOG(sen_log_error, "sen_index_select on sen_index_sel(3) failed !");
      sen_records_close(r);
      return NULL;
    }
    SEN_LOG(sen_log_info, "partial: %d", r->records->n_entries);
  }
  SEN_LOG(sen_log_info, "hits=%d", r->records->n_entries);
  if (!r->records->n_entries) {
    sen_records_close(r);
    r = NULL;
  }
  return r;
  }
}

/* sen_records_heap class */

struct _sen_records_heap {
  int n_entries;
  int n_bins;
  sen_records **bins;
  int limit;
  int curr;
  int dir;
  int (*compar)(sen_records *, const sen_recordh *, sen_records *, const sen_recordh *, void *);
  void *compar_arg;
};

inline static int
records_heap_cmp(sen_records_heap *h, sen_records *r1, sen_records *r2)
{
  const sen_recordh *rh1 = sen_records_curr_rec(r1);
  const sen_recordh *rh2 = sen_records_curr_rec(r2);
  if (!h->compar) {
    int off1, off2;
    if (h->compar_arg == (void *)-1) {
      off1 = (r1->records->key_size) / sizeof(int32_t);
      off2 = (r2->records->key_size) / sizeof(int32_t);
    } else {
      off1 = off2 = (int)(intptr_t)h->compar_arg;
    }
    return (((int32_t *)(rh2))[off2] - ((int32_t *)(rh1))[off1]) * h->dir > 0;
  }
  return h->compar(r1, rh1, r2, rh2, h->compar_arg) * h->dir > 0;
}

sen_records_heap *
sen_records_heap_open(int size, int limit, sen_sort_optarg *optarg)
{
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  sen_records_heap *h = SEN_MALLOC(sizeof(sen_records_heap));
  if (!h) { return NULL; }
  h->bins = SEN_MALLOC(sizeof(sen_records *) * size);
  if (!h->bins) {
    SEN_FREE(h);
    return NULL;
  }
  h->n_entries = 0;
  h->n_bins = size;
  h->limit = limit;
  h->curr = 0;
  if (optarg) {
    h->dir = (optarg->mode == sen_sort_ascending) ? 1 : -1;
    h->compar = optarg->compar;
    h->compar_arg = optarg->compar_arg;
  } else {
    h->dir = -1;
    h->compar = NULL;
    h->compar_arg = (void *) -1;
  }
  return h;
}

sen_rc
sen_records_heap_add(sen_records_heap *h, sen_records *r)
{
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  if (h->n_entries >= h->n_bins) {
    int size = h->n_bins * 2;
    sen_records **bins = SEN_REALLOC(h->bins, sizeof(sen_records *) * size);
    // sen_log("expanded sen_records_heap to %d,%p", size, bins);
    if (!bins) { return sen_memory_exhausted; }
    h->n_bins = size;
    h->bins = bins;
  }
  if (!sen_records_next(r, NULL, 0, NULL)) {
    sen_records_close(r);
    return sen_internal_error;
  }
  {
    int n, n2;
    sen_records *r2;
    n = h->n_entries++;
    while (n) {
      n2 = (n - 1) >> 1;
      r2 = h->bins[n2];
      if (records_heap_cmp(h, r, r2)) { break; }
      h->bins[n] = r2;
      n = n2;
    }
    h->bins[n] = r;
  }
  return sen_success;
}

int
sen_records_heap_next(sen_records_heap *h)
{
  if (!h || !h->n_entries) { return 0; }
  {
    sen_records *r = h->bins[0];
    if (!sen_records_next(r, NULL, 0, NULL)) {
      sen_records_close(r);
      r = h->bins[0] = h->bins[--h->n_entries];
    }
    {
      int n = 0, m = h->n_entries;
      if (m > 1) {
        for (;;) {
          int n1 = n * 2 + 1;
          int n2 = n1 + 1;
          sen_records *r1 = n1 < m ? h->bins[n1] : NULL;
          sen_records *r2 = n2 < m ? h->bins[n2] : NULL;
          if (r1 && records_heap_cmp(h, r, r1)) {
            if (r2 && records_heap_cmp(h, r, r2) && records_heap_cmp(h, r1, r2)) {
              h->bins[n] = r2;
              n = n2;
            } else {
              h->bins[n] = r1;
              n = n1;
            }
          } else {
            if (r2 && records_heap_cmp(h, r, r2)) {
              h->bins[n] = r2;
              n = n2;
            } else {
              h->bins[n] = r;
              break;
            }
          }
        }
      }
      h->curr++;
      return m;
    }
  }
}

sen_records *
sen_records_heap_head(sen_records_heap *h)
{
  return h->n_entries ? h->bins[0] : NULL;
}

sen_rc
sen_records_heap_close(sen_records_heap *h)
{
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  int i;
  if (!h) { return sen_invalid_argument; }
  for (i = h->n_entries; i--;) { sen_records_close(h->bins[i]); }
  SEN_FREE(h->bins);
  SEN_FREE(h);
  return sen_success;
}

/* todo : config_path will be disappeared */
sen_rc
sen_info(char **version,
         char **configure_options,
         char **config_path,
         sen_encoding *default_encoding,
         unsigned int *initial_n_segments,
         unsigned int *partial_match_threshold)
{
  if (version) {
    *version = PACKAGE_VERSION " (last update: 2013.02.25)";
  }
  if (configure_options) {
    *configure_options = CONFIGURE_OPTIONS;
  }
  if (default_encoding) {
    *default_encoding = sen_gctx.encoding;
  }
  if (initial_n_segments) {
    *initial_n_segments = SENNA_DEFAULT_INITIAL_N_SEGMENTS;
  }
  if (partial_match_threshold) {
    *partial_match_threshold = SENNA_DEFAULT_QUERY_ESCALATION_THRESHOLD;
  }
  return sen_success;
}
