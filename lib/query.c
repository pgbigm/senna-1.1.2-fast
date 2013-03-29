/* Copyright(C) 2006-2007 Brazil

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

nnn  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "senna_in.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "snip.h"
#include "sym.h"
#include "ql.h"

/* query string parser and executor */

#define DEFAULT_WEIGHT 5
#define DEFAULT_DECAYSTEP 2
#define DEFAULT_MAX_INTERVAL 10
#define DEFAULT_SIMILARITY_THRESHOLD 10
#define DEFAULT_TERM_EXTRACT_POLICY 0
#define DEFAULT_WEIGHT_VECTOR_SIZE 4096

typedef sen_obj cell;

struct _sen_query {
  char *str;
  char *cur;
  char *str_end;
  sen_sel_operator default_op;
  sen_select_optarg opt;
  sen_sel_mode default_mode;
  int escalation_threshold;
  int escalation_decaystep;
  int weight_offset;
  sen_set *weight_set;
  sen_encoding encoding;
  cell *expr;
  int max_exprs;
  int cur_expr;
  int max_cells;
  int cur_cell;
  snip_cond *snip_conds;
  cell cell_pool[1]; /* dummy */
};

inline static cell *
cell_new(sen_query *q)
{
  if (q->cur_cell <= q->max_cells) {
    cell *c = &q->cell_pool[q->cur_cell++];
    return c;
  }
  return NULL;
}

inline static void
cell_del(sen_query *q)
{
  if (q->cur_cell > 0) { q->cur_cell--; }
}

inline static cell *
cons(sen_query *q, cell *car, cell *cdr)
{
  cell *c;
  if ((c = cell_new(q))) {
    c->type = sen_ql_list;
    c->u.l.car = car;
    c->u.l.cdr = cdr;
    return c;
  } else {
    return NIL;
  }
}

inline static cell *
token_new(sen_query *q, const char *start, const char *end)
{
  cell *c;
  if (start >= end) { return NIL; }
  if ((c = cell_new(q))) {
    unsigned int len = end - start;
    c->type = sen_ql_bulk;
    c->u.b.value = (char *)start;
    c->u.b.size = len;
    q->cur_expr++;
    return c;
  } else {
    return NIL;
  }
}

inline static cell *
op_new(sen_query *q, int8_t op, int16_t weight, int8_t mode, int32_t option)
{
  cell *c;
  if ((c = cell_new(q))) {
    c->type = sen_ql_op;
    c->u.op.op = op;
    c->u.op.weight = weight;
    c->u.op.mode = mode;
    c->u.op.option = option;
    return c;
  } else {
    return NIL;
  }
}

inline static void
skip_space(sen_query *q)
{
  unsigned int len;
  while (q->cur < q->str_end && sen_isspace(q->cur, q->encoding)) {
    /* null check and length check */
    if (!(len = sen_str_charlen_nonnull(q->cur, q->str_end, q->encoding))) {
      q->cur = q->str_end;
      break;
    }
    q->cur += len;
  }
}

inline static cell *
get_phrase(sen_query *q)
{
  char *start, *s, *d;
  start = s = d = q->cur;
  while (1) {
    unsigned int len;
    if (s >= q->str_end) {
      q->cur = s;
      break;
    }
    len = sen_str_charlen_nonnull(s, q->str_end, q->encoding);
    if (len == 1) {
      if (*s == SEN_QUERY_QUOTER) {
        q->cur = s + 1;
        break;
      } else if (*s == SEN_QUERY_ESCAPE && s + 1 < q->str_end) {
        s++;
        len = sen_str_charlen_nonnull(s, q->str_end, q->encoding);
      }
    }
    while (len--) { *d++ = *s++; }
  }
  return token_new(q, start, d);
}

inline static cell *
get_word(sen_query *q, int *prefixp)
{
  char *start = q->cur, *end;
  unsigned int len;
  for (end = q->cur;; ) {
    /* null check and length check */
    if (!(len = sen_str_charlen_nonnull(end, q->str_end, q->encoding))) {
      q->cur = q->str_end;
      break;
    }
    if (sen_isspace(end, q->encoding) ||
        *end == SEN_QUERY_PARENR) {
      q->cur = end;
      break;
    }
    if (*end == SEN_QUERY_PREFIX) {
      *prefixp = 1;
      q->cur = end + 1;
      break;
    }
    end += len;
  }
  return token_new(q, start, end);
}

inline static cell *
get_op(sen_query *q, sen_sel_operator op, int weight)
{
  char *start, *end = q->cur;
  int mode, option;
  switch (*end) {
  case 'S' :
    mode = sen_sel_similar;
    start = ++end;
    option = sen_atoi(start, q->str_end, (const char **)&end);
    if (start == end) { option = DEFAULT_SIMILARITY_THRESHOLD; }
    q->cur = end;
    break;
  case 'N' :
    mode = sen_sel_near;
    start = ++end;
    option = sen_atoi(start, q->str_end, (const char **)&end);
    if (start == end) { option = DEFAULT_MAX_INTERVAL; }
    q->cur = end;
    break;
  case 'n' :
    mode = sen_sel_near2;
    start = ++end;
    option = sen_atoi(start, q->str_end, (const char **)&end);
    if (start == end) { option = DEFAULT_MAX_INTERVAL; }
    q->cur = end;
    break;
  case 'T' :
    mode = sen_sel_term_extract;
    start = ++end;
    option = sen_atoi(start, q->str_end, (const char **)&end);
    if (start == end) { option = DEFAULT_TERM_EXTRACT_POLICY; }
    q->cur = end;
    break;
  default :
    return NIL;
  }
  return op_new(q, op, weight, mode, option);
}

static cell *get_expr(sen_query *q);

inline static cell *
get_token(sen_query *q)
{
  cell *token = NIL;
  sen_sel_operator op = q->default_op;
  {
    int weight = DEFAULT_WEIGHT, prefixp = 0, mode = -1, option = 0;
    skip_space(q);
    if (q->cur_expr >= q->max_exprs ||
        q->cur_cell >= q->max_cells ||
        q->cur >= q->str_end) { return NIL; }
    switch (*q->cur) {
    case '\0' :
      return NIL;
    case SEN_QUERY_PARENR :
      q->cur++;
      return NIL;
    case SEN_QUERY_QUOTEL :
      q->cur++;
      token = get_phrase(q);
      break;
    case SEN_QUERY_PREFIX :
      q->cur++;
      token = get_op(q, op, weight);
      break;
    case SEN_QUERY_AND :
      q->cur++;
      token = op_new(q, sen_sel_and, weight, mode, option);
      break;
    case SEN_QUERY_BUT :
      q->cur++;
      token = op_new(q, sen_sel_but, weight, mode, option);
      break;
    case SEN_QUERY_ADJ_INC :
      q->cur++;
      if (weight < 127) { weight++; }
      token = op_new(q, sen_sel_adjust, weight, mode, option);
      break;
    case SEN_QUERY_ADJ_DEC :
      q->cur++;
      if (weight > -128) { weight--; }
      token = op_new(q, sen_sel_adjust, weight, mode, option);
      break;
    case SEN_QUERY_ADJ_NEG :
      q->cur++;
      token = op_new(q, sen_sel_adjust, -1, mode, option);
      break;
    case SEN_QUERY_PARENL :
      q->cur++;
      token = get_expr(q);
      break;
    default :
      if ((token = get_word(q, &prefixp)) &&
          token->u.b.value[0] == 'O' &&
          token->u.b.value[1] == 'R' &&
          token->u.b.size == 2) {
        cell_del(q);
        q->cur_expr--;
        token = op_new(q, sen_sel_or, weight, mode, option);
      }
      break;
    }
  }
  return cons(q, token, NIL);
}

static cell *
get_expr(sen_query *q)
{
  cell *r, *c, *c_;
  for (c = r = get_token(q); c != NIL; c = c_) {
    c_ = c->u.l.cdr = get_token(q);
  }
  return r;
}

static const char *
get_weight_vector(sen_query *query, const char *source)
{
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  const char *p;

  if (!query->opt.weight_vector &&
      !query->weight_set &&
      !(query->opt.weight_vector = SEN_CALLOC(sizeof(int) * DEFAULT_WEIGHT_VECTOR_SIZE))) {
    SEN_LOG(sen_log_alert, "get_weight_vector malloc fail");
    return source;
  }
  for (p = source; p < query->str_end; ) {
    unsigned int key;
    int value;

    /* key, key is not zero */
    key = sen_atoui(p, query->str_end, &p);
    if (!key || key > SEN_ID_MAX) { break; }

    /* value */
    if (*p == ':') {
      p++;
      value = sen_atoi(p, query->str_end, &p);
    } else {
      value = 1;
    }

    if (query->weight_set) {
      int *pval;
      if (sen_set_get(query->weight_set, &key, (void **)&pval)) {
        *pval = value;
      }
    } else if (key < DEFAULT_WEIGHT_VECTOR_SIZE) {
      query->opt.weight_vector[key - 1] = value;
    } else {
      SEN_FREE(query->opt.weight_vector);
      query->opt.weight_vector = NULL;
      if (!(query->weight_set = sen_set_open(sizeof(unsigned int), sizeof(int), 0))) {
        return source;
      }
      p = source;           /* reparse */
      continue;
    }
    if (*p != ',') { break; }
    p++;
  }
  return p;
}

inline static void
get_pragma(sen_query *q)
{
  char *start, *end = q->cur;
  while (end < q->str_end && *end == SEN_QUERY_PREFIX) {
    if (++end >= q->str_end) { break; }
    switch (*end) {
    case 'E' :
      start = ++end;
      q->escalation_threshold = sen_atoi(start, q->str_end, (const char **)&end);
      while (end < q->str_end && (isdigit(*end) || *end == '-')) { end++; }
      if (*end == ',') {
        start = ++end;
        q->escalation_decaystep = sen_atoi(start, q->str_end, (const char **)&end);
      }
      q->cur = end;
      break;
    case 'D' :
      start = ++end;
      while (end < q->str_end && *end != SEN_QUERY_PREFIX && !sen_isspace(end, q->encoding)) {
        end++;
      }
      if (end > start) {
        switch (*start) {
        case 'O' :
          q->default_op = sen_sel_or;
          break;
        case SEN_QUERY_AND :
          q->default_op = sen_sel_and;
          break;
        case SEN_QUERY_BUT :
          q->default_op = sen_sel_but;
          break;
        case SEN_QUERY_ADJ_INC :
          q->default_op = sen_sel_adjust;
          break;
        }
      }
      q->cur = end;
      break;
    case 'W' :
      start = ++end;
      end = (char *)get_weight_vector(q, start);
      q->cur = end;
      break;
    }
  }
}

static int
section_weight_cb(sen_records *r, const void *rid, int sid, void *arg)
{
  int *w;
  sen_set *s = (sen_set *)arg;
  if (s && sen_set_at(s, &sid, (void **)&w)) {
    return *w;
  } else {
    return 0;
  }
}

sen_query *
sen_query_open(const char *str, unsigned int str_len,
               sen_sel_operator default_op, int max_exprs, sen_encoding encoding)
{
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  sen_query *q;
  int max_cells = max_exprs * 4;
  if (!(q = SEN_MALLOC(sizeof(sen_query) + max_cells * sizeof(cell) + str_len + 1))) {
    SEN_LOG(sen_log_alert, "sen_query_open malloc fail");
    return NULL;
  }
  q->str = (char *)&q->cell_pool[max_cells];
  memcpy(q->str, str, str_len);
  q->str[str_len] = '\0';
  q->cur = q->str;
  q->str_end = q->str + str_len;
  q->default_op = default_op;
  q->encoding = encoding;
  q->max_exprs = max_exprs;
  q->max_cells = max_cells;
  q->cur_cell = 0;
  q->cur_expr = 0;
  q->escalation_threshold = SENNA_DEFAULT_QUERY_ESCALATION_THRESHOLD;
  q->escalation_decaystep = DEFAULT_DECAYSTEP;
  q->weight_offset = 0;
  q->opt.weight_vector = NULL;
  q->weight_set = NULL;
  get_pragma(q);
  q->expr = get_expr(q);
  q->opt.vector_size = DEFAULT_WEIGHT_VECTOR_SIZE;
  q->opt.func = q->weight_set ? section_weight_cb : NULL;
  q->opt.func_arg = q->weight_set;
  q->snip_conds = NULL;
  return q;
}

unsigned int
sen_query_rest(sen_query *q, const char ** const rest)
{
  if (!q) { return 0; }
  if (rest) {
    *rest = q->cur;
  }
  return (unsigned int)(q->str_end - q->cur);
}

sen_rc
sen_query_close(sen_query *q)
{
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  if (!q) { return sen_invalid_argument; }
  if (q->opt.weight_vector) {
    SEN_FREE(q->opt.weight_vector);
  }
  if (q->weight_set) {
    sen_set_close(q->weight_set);
  }
  if (q->snip_conds) {
    snip_cond *sc;
    for (sc = q->snip_conds; sc < q->snip_conds + q->cur_expr; sc++) {
      sen_snip_cond_close(sc);
    }
    SEN_FREE(q->snip_conds);
  }
  SEN_FREE(q);
  return sen_success;
}

static void
exec_query(sen_index *i, sen_query *q, cell *c, sen_records *r, sen_sel_operator op)
{
  sen_records *s;
  cell *e, *ope = NIL;
  int n = sen_records_nhits(r);
  sen_sel_operator op0 = sen_sel_or, *opp = &op0, op1 = q->default_op;
  if (!n && op != sen_sel_or) { return; }
  s = n ? sen_records_open(r->record_unit, r->subrec_unit, 0) : r;
  while (c != NIL) {
    POP(e, c);
    switch (e->type) {
    case sen_ql_op :
      if (opp == &op0 && e->u.op.op == sen_sel_but) {
        POP(e, c);
      } else {
        ope = e;
        op1 = ope->u.op.op;
      }
      continue;
    case sen_ql_bulk :
      if (ope != NIL) {
        q->opt.mode = ope->u.op.mode == -1 ? q->default_mode : ope->u.op.mode;
        q->opt.max_interval = q->opt.similarity_threshold = ope->u.op.option;
        if (!q->opt.weight_vector) {
          q->opt.vector_size = ope->u.op.weight + q->weight_offset;
        }
        if (ope->u.op.mode == sen_sel_similar) {
          q->opt.max_interval = q->default_mode;
        }
      } else {
        q->opt.mode = q->default_mode;
        q->opt.max_interval = DEFAULT_MAX_INTERVAL;
        q->opt.similarity_threshold = DEFAULT_SIMILARITY_THRESHOLD;
        if (!q->opt.weight_vector) {
          q->opt.vector_size = DEFAULT_WEIGHT + q->weight_offset;
        }
      }
      if (sen_index_select(i, e->u.b.value, e->u.b.size, s, *opp, &q->opt)) {
        SEN_LOG(sen_log_error, "sen_index_select on exec_query failed !");
        return;
      }
      break;
    case sen_ql_list :
      exec_query(i, q, e, s, *opp);
      break;
    default :
      SEN_LOG(sen_log_notice, "invalid object assigned in query (%d)", e->type);
      break;
    }
    opp = &op1;
    ope = NIL;
    op1 = q->default_op;
  }
  if (n) {
    switch (op) {
    case sen_sel_or :
      if (!sen_records_union(r, s)) { sen_records_close(s); }
      break;
    case sen_sel_and :
      if (!sen_records_intersect(r, s)) { sen_records_close(s); }
      break;
    case sen_sel_but :
      if (!sen_records_subtract(r, s)) { sen_records_close(s); }
      break;
      /* todo: adjust
    case sen_sel_adjust :
      break;
      */
    default :
      sen_records_close(s);
      break;
    }
  }
}

sen_rc
sen_query_exec(sen_index *i, sen_query *q, sen_records *r, sen_sel_operator op)
{
  int p;
  if (!i || !q || !r || !PAIRP(q->expr)) { return sen_invalid_argument; }
  p = q->escalation_threshold;
  // dump_query(q, q->expr, 0);
  // sen_log("escalation_threshold=%d", p);
  if (p >= 0 || (-p & 1)) {
    q->default_mode = sen_sel_exact;
    exec_query(i, q, q->expr, r, op);
    SEN_LOG(sen_log_info, "hits(exact)=%d", sen_records_nhits(r));
  }
  if ((p >= 0) ? (p >= sen_records_nhits(r)) : (-p & 2)) {
    q->weight_offset -= q->escalation_decaystep;
    q->default_mode = sen_sel_unsplit;
    exec_query(i, q, q->expr, r, op);
    SEN_LOG(sen_log_info, "hits(unsplit)=%d", sen_records_nhits(r));
  }
  if ((p >= 0) ? (p >= sen_records_nhits(r)) : (-p & 4)) {
    q->weight_offset -= q->escalation_decaystep;
    q->default_mode = sen_sel_partial;
    exec_query(i, q, q->expr, r, op);
    SEN_LOG(sen_log_info, "hits(partial)=%d", sen_records_nhits(r));
  }
  return sen_success;
}

static int
query_term_rec(sen_query* q, cell* c, query_term_callback func, void *func_arg)
{
  cell *token;
  if (BULKP(c)) {
    return func(c->u.b.value, c->u.b.size, func_arg);
  }
  for (token = c; PAIRP(token); token = CDR(token)) {
    if (!query_term_rec(q, CAR(token), func, func_arg)) {
      return 0; /* abort */
    }
  }
  return 1; /* continue */
}

void
sen_query_term(sen_query *q, query_term_callback func, void *func_arg)
{
  query_term_rec(q, q->expr, func, func_arg);
}

/* FIXME: for test */
sen_rc
sen_query_str(sen_query *q, const char **str, unsigned int *len)
{
  if (str) { *str = q->str; }
  if (len) { *len = q->str_end - q->str; }
  return sen_success;
}

static void
scan_keyword(snip_cond *sc, sen_nstr *str, sen_id section,
             sen_sel_operator op, sen_select_optarg *optarg,
             int *found, int *score)
{
  int tf;
  int w = 1;
  for (tf = 0; ; tf++) {
    sen_bm_tunedbm(sc, str, 0);
    if (sc->stopflag == SNIPCOND_STOP) { break; }
  }
  if (optarg->vector_size) {
    if (!optarg->weight_vector) {
      w = optarg->vector_size;
    } else if (section) {
      w = (section <= optarg->vector_size ?
                      optarg->weight_vector[section - 1] : 0);
    }
  }
  switch (op) {
  case sen_sel_or :
    if (tf) {
      *found = 1;
      *score += w * tf;
    }
    break;
  case sen_sel_and :
    if (tf) {
      *score += w * tf;
    } else {
      *found = 0;
    }
    break;
  case sen_sel_but :
    if (tf) {
      *found = 0;
    }
    break;
  case sen_sel_adjust :
    *score += w * tf;
  }
}

/* TODO: delete overlapping logic with exec_query */
static sen_rc
scan_query(sen_query *q, sen_nstr *nstr, sen_id section, cell *c, snip_cond **sc,
           sen_sel_operator op, int flags, int *found, int *score)
{
  int _found = 0, _score = 0;
  cell *e, *ope = NIL;
  sen_sel_operator op0 = sen_sel_or, *opp = &op0, op1 = q->default_op;
  while (c != NIL) {
    POP(e, c);
    switch (e->type) {
    case sen_ql_op :
      if (opp == &op0 && e->u.op.op == sen_sel_but) {
        POP(e, c);
      } else {
        ope = e;
        op1 = ope->u.op.op;
      }
      continue;
    case sen_ql_bulk :
      if (ope != NIL) {
        q->opt.mode = ope->u.op.mode == -1 ? q->default_mode : ope->u.op.mode;
        q->opt.max_interval = q->opt.similarity_threshold = ope->u.op.option;
        if (!q->opt.weight_vector) {
          q->opt.vector_size = ope->u.op.weight + q->weight_offset;
        }
      } else {
        q->opt.mode = q->default_mode;
        q->opt.max_interval = DEFAULT_MAX_INTERVAL;
        q->opt.similarity_threshold = DEFAULT_SIMILARITY_THRESHOLD;
        if (!q->opt.weight_vector) {
          q->opt.vector_size = DEFAULT_WEIGHT + q->weight_offset;
        }
      }
      if ((flags & SEN_QUERY_SCAN_ALLOCCONDS)) {
        sen_rc rc;
        /* NOTE: SEN_SNIP_NORMALIZE = SEN_QUERY_SCAN_NORMALIZE */
        if ((rc = sen_snip_cond_init(*sc, e->u.b.value, e->u.b.size,
                                     q->encoding, flags & SEN_SNIP_NORMALIZE))) {
          return rc;
        }
      } else {
        sen_snip_cond_reinit(*sc);
      }
      scan_keyword(*sc, nstr, section, *opp, &q->opt, &_found, &_score);
      (*sc)++;
      break;
    case sen_ql_list :
      scan_query(q, nstr, section, e, sc, *opp, flags, &_found, &_score);
      break;
    default :
      SEN_LOG(sen_log_notice, "invalid object assigned in query! (%d)", e->type);
      break;
    }
    opp = &op1;
    ope = NIL;
    op1 = q->default_op;
  }
  switch (op) {
  case sen_sel_or :
    *found |= _found;
    *score += _score;
    break;
  case sen_sel_and :
    *found &= _found;
    *score += _score;
    break;
  case sen_sel_but :
    *found &= !_found;
    break;
  case sen_sel_adjust :
    *score += _score;
    break;
  default :
    break;
  }
  return sen_success;
}

static sen_rc
alloc_snip_conds(sen_query *q)
{
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  if (!(q->snip_conds = SEN_CALLOC(sizeof(snip_cond) * q->cur_expr))) {
    SEN_LOG(sen_log_alert, "snip_cond allocation failed");
    return sen_memory_exhausted;
  }
  return sen_success;
}

sen_rc
sen_query_scan(sen_query *q, const char **strs, unsigned int *str_lens, unsigned int nstrs,
               int flags, int *found, int *score)
{
  unsigned int i;
  sen_rc rc;
  if (!q || !strs || !nstrs) { return sen_invalid_argument; }
  *found = *score = 0;
  if (!q->snip_conds) {
    if ((rc = alloc_snip_conds(q))) { return rc; }
    flags |= SEN_QUERY_SCAN_ALLOCCONDS;
  } else if (flags & SEN_QUERY_SCAN_ALLOCCONDS) {
    SEN_LOG(sen_log_warning, "invalid flags specified on sen_query_scan")
    return sen_invalid_argument;
  }
  for (i = 0; i < nstrs; i++) {
    sen_nstr *n;
    snip_cond *sc = q->snip_conds;
    if (flags & SEN_QUERY_SCAN_NORMALIZE) {
      n = sen_nstr_open(*(strs + i), *(str_lens + i), q->encoding,
                        SEN_STR_WITH_CHECKS | SEN_STR_REMOVEBLANK);
    } else {
      n = sen_fakenstr_open(*(strs + i), *(str_lens + i), q->encoding,
                        SEN_STR_WITH_CHECKS | SEN_STR_REMOVEBLANK);
    }
    if (!n) { return sen_memory_exhausted; }
    if ((rc = scan_query(q, n, i + 1, q->expr, &sc, sen_sel_or, flags, found, score))) {
      sen_nstr_close(n);
      return rc;
    }
    flags &= ~SEN_QUERY_SCAN_ALLOCCONDS;
    sen_nstr_close(n);
  }
  return sen_success;
}

/* TODO: delete overlapping logic with exec_query */
sen_rc
snip_query(sen_query *q, sen_snip *snip, cell *c, sen_sel_operator op,
           unsigned int n_tags, int c_but,
           const char **opentags, unsigned int *opentag_lens,
           const char **closetags, unsigned int *closetag_lens)
{
  cell *e, *ope = NIL;
  sen_sel_operator op0 = sen_sel_or, *opp = &op0, op1 = q->default_op;
  while (c != NIL) {
	  POP(e, c);
    switch (e->type) {
    case sen_ql_op :
      ope = e;
      op1 = ope->u.op.op;
      continue;
    case sen_ql_bulk :
      if (ope != NIL) {
        q->opt.mode = ope->u.op.mode == -1 ? q->default_mode : ope->u.op.mode;
      } else {
        q->opt.mode = q->default_mode;
      }
      if (!(c_but ^ (*opp == sen_sel_but))) {
        sen_rc rc;
        unsigned int i = snip->cond_len % n_tags;
        if ((rc = sen_snip_add_cond(snip, e->u.b.value, e->u.b.size,
                                    opentags[i], opentag_lens[i],
                                    closetags[i], closetag_lens[i]))) {
          return rc;
        }
      }
      break;
    case sen_ql_list :
      snip_query(q, snip, e, *opp, n_tags, (*opp == sen_sel_but) ? c_but ^ 1 : c_but,
                 opentags, opentag_lens, closetags, closetag_lens);
      break;
    default :
      SEN_LOG(sen_log_notice, "invalid object assigned in query!! (%d)", e->type);
      break;
    }
    opp = &op1;
    ope = NIL;
    op1 = q->default_op;
  }
  return sen_success;
}

sen_snip *
sen_query_snip(sen_query *query, int flags,
               unsigned int width, unsigned int max_results,
               unsigned int n_tags,
               const char **opentags, unsigned int *opentag_lens,
               const char **closetags, unsigned int *closetag_lens,
               sen_snip_mapping *mapping)
{
  sen_snip *res;
  if (!(res = sen_snip_open(query->encoding, flags, width, max_results,
                            NULL, 0, NULL, 0, mapping))) {
    return NULL;
  }
  if (snip_query(query, res, query->expr, sen_sel_or, n_tags, 0,
                 opentags, opentag_lens, closetags, closetag_lens)) {
    sen_snip_close(res);
    return NULL;
  }
  return res;
}

#ifdef DEBUG

static void
dump_query(sen_query *q, cell *c, int level)
{
  { int i; for (i = level; i; i--) { putchar(' '); }}
  printf("%d:%d ", c->weight, c->op);
  if (c->type == cell_token) {
    { int i; for (i = level; i; i--) { putchar(' '); }}
    fwrite(c->u.b.value, 1, c->u.b.size, stdout);
  } else {
    cell *token;
    putchar('\n');
    for (token = c->u.l.car; token; token = token->u.l.cdr) {
      dump_query(q, token, level + 1);
    }
  }
}

#endif /* DEBUG */
