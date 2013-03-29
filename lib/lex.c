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
#include <ctype.h>
#include "lex.h"

/* ngram */

inline static sen_lex *
sen_ngram_open(sen_sym *sym, sen_nstr *nstr, uint8_t flags)
{
  sen_lex *lex;
  sen_ctx *ctx = nstr->ctx;
  if (!(lex = SEN_MALLOC(sizeof(sen_lex)))) { return NULL; }
  lex->sym = sym;
#ifndef NO_MECAB
  lex->mecab = NULL;
#endif /* NO_MECAB */
  lex->buf = NULL;
  lex->token = NULL;
  lex->tlen = 0;
  lex->pos = -1;
  lex->skip = 1;
  lex->tail = 0;
  lex->flags = flags;
  lex->status = sen_lex_doing;
  lex->encoding = sym->encoding;
  lex->nstr = nstr;
  lex->orig = (unsigned char *)nstr->norm;
  lex->next = (unsigned char *)nstr->norm;
  lex->uni_alpha = (nstr->ctypes && !(lex->sym->flags & SEN_INDEX_SPLIT_ALPHA));
  lex->uni_digit = (nstr->ctypes && !(lex->sym->flags & SEN_INDEX_SPLIT_DIGIT));
  lex->uni_symbol = (nstr->ctypes && !(lex->sym->flags & SEN_INDEX_SPLIT_SYMBOL));
  lex->force_prefix = 0;
  return lex;
}

#define LEX_TOKEN(lex,str,len) do {\
  if ((lex)->tlen < len) {\
    char *buf = SEN_REALLOC((lex)->token, (len) + 1);\
    if (!(buf)) { (lex)->status = sen_lex_done; return SEN_SYM_NIL; }\
    (lex)->token = buf;\
    (lex)->tlen = len;\
  }\
  memcpy((lex)->token, str, len);\
  (lex)->token[len] = '\0';\
} while (0)

inline static sen_id
sen_ngram_next(sen_lex *lex)
{
  sen_id tid;
  sen_sym *sym = lex->sym;
  sen_ctx *ctx = lex->nstr->ctx;
  uint_least8_t *cp = NULL;
  int32_t len = 0, pos;
  const unsigned char *p, *q, *r;
  if (lex->status == sen_lex_done) { return SEN_SYM_NIL; }
  lex->force_prefix = 0;
  for (p = lex->next, pos = lex->pos + lex->skip; *p; p = r, pos++) {
    if (lex->nstr->ctypes) { cp = lex->nstr->ctypes + pos; }
    if (lex->uni_alpha && SEN_NSTR_CTYPE(*cp) == sen_str_alpha) {
      for (len = 1, r = p;;len++) {
        size_t cl;
        if (!(cl = sen_str_charlen((char *)r, lex->encoding))) { break; }
        r += cl;
        if (SEN_NSTR_ISBLANK(*cp)) { break; }
        if (SEN_NSTR_CTYPE(*++cp) != sen_str_alpha) { break; }
      }
      {
        size_t blen = r - p;
        if (!blen) {
          lex->status = sen_lex_done;
          return SEN_SYM_NIL;
        }
        LEX_TOKEN(lex, p, blen);
        tid = (lex->flags & SEN_LEX_ADD) ? sen_sym_get(sym, lex->token) : sen_sym_at(sym, lex->token);
        lex->skip = len;
      }
    } else if (lex->uni_digit && SEN_NSTR_CTYPE(*cp) == sen_str_digit) {
      for (len = 1, r = p;;len++) {
        size_t cl;
        if (!(cl = sen_str_charlen((char *)r, lex->encoding))) { break; }
        r += cl;
        if (SEN_NSTR_ISBLANK(*cp)) { break; }
        if (SEN_NSTR_CTYPE(*++cp) != sen_str_digit) { break; }
      }
      {
        size_t blen = r - p;
        if (!blen) {
          lex->status = sen_lex_done;
          return SEN_SYM_NIL;
        }
        LEX_TOKEN(lex, p, blen);
        tid = (lex->flags & SEN_LEX_ADD) ? sen_sym_get(sym, lex->token) : sen_sym_at(sym, lex->token);
        lex->skip = len;
      }
    } else if (lex->uni_symbol && SEN_NSTR_CTYPE(*cp) == sen_str_symbol) {
      for (len = 1, r = p;;len++) {
        size_t cl;
        if (!(cl = sen_str_charlen((char *)r, lex->encoding))) { break; }
        r += cl;
        if (SEN_NSTR_ISBLANK(*cp)) { break; }
        if (SEN_NSTR_CTYPE(*++cp) != sen_str_symbol) { break; }
      }
      {
        size_t blen = r - p;
        if (!blen) {
          lex->status = sen_lex_done;
          return SEN_SYM_NIL;
        }
        LEX_TOKEN(lex, p, blen);
        tid = (lex->flags & SEN_LEX_ADD) ? sen_sym_get(sym, lex->token) : sen_sym_at(sym, lex->token);
        lex->skip = len;
      }
    } else {
      size_t cl;
#ifdef PRE_DEFINED_UNSPLIT_WORDS
      {
        const unsigned char *key = NULL;
        if ((tid = sen_sym_common_prefix_search(sym, p))) {
          if (!(key = _sen_sym_key(sym, tid))) {
            lex->status = sen_lex_not_found;
            return SEN_SYM_NIL;
          }
          len = sen_str_len(key, lex->encoding, NULL);
        }
        r = p + sen_str_charlen(p, lex->encoding);
        if (tid && (len > 1 || r == p)) {
          if (r != p && pos + len - 1 <= lex->tail) { continue; }
          p += strlen(key);
          if (!*p && !(lex->flags & SEN_LEX_UPD)) { lex->status = sen_lex_done; }
        }
      }
#endif /* PRE_DEFINED_UNSPLIT_WORDS */
      if (!(cl = sen_str_charlen((char *)p, lex->encoding))) {
        lex->status = sen_lex_done;
        return SEN_SYM_NIL;
      }
      r = p + cl;
      {
        int blankp = 0;
        for (len = 1, q = r; len < SEN_LEX_NGRAM_UNIT_SIZE; len++) {
          if (cp) {
            if (SEN_NSTR_ISBLANK(*cp)) { blankp++; break; }
            cp++;
          }
          if (!(cl = sen_str_charlen((char *)q, lex->encoding)) ||
              (lex->uni_alpha && SEN_NSTR_CTYPE(*cp) == sen_str_alpha) ||
              (lex->uni_digit && SEN_NSTR_CTYPE(*cp) == sen_str_digit) ||
              (lex->uni_symbol && SEN_NSTR_CTYPE(*cp) == sen_str_symbol)) {
            break;
          }
          q += cl;
        }
        if (blankp && !(lex->flags & SEN_LEX_UPD)) { continue; }
      }
      if ((!cl || !*q) && !(lex->flags & SEN_LEX_UPD)) { lex->status = sen_lex_done; }
      if (len < SEN_LEX_NGRAM_UNIT_SIZE) { lex->force_prefix = 1; }
      {
        size_t blen = q - p;
        if (!blen) {
          lex->status = sen_lex_done;
          return SEN_SYM_NIL;
        }
        LEX_TOKEN(lex, p, blen);
        tid = (lex->flags & SEN_LEX_ADD) ? sen_sym_get(sym, lex->token) : sen_sym_at(sym, lex->token);
        lex->skip = 1;
      }
    }
    lex->pos = pos;
    lex->len = len;
    lex->tail = pos + len - 1;
    lex->next = r;
    // printf("tid=%d pos=%d tail=%d (%s) %s\n", tid, lex->pos, lex->tail, _sen_sym_key(sym, tid), r);
    // printf("tid=%d pos=%d tail=%d (%s)\n", tid, lex->pos, lex->tail, _sen_sym_key(sym, tid));
    if (!tid) {
      lex->status = sen_lex_not_found;
    } else {
      if (!*r) { lex->status = sen_lex_done; }
    }
    return tid;
  }
  lex->status = sen_lex_done;
  return SEN_SYM_NIL;
}

/* mecab */

#ifndef NO_MECAB

static mecab_t *sole_mecab;
static sen_mutex sole_mecab_lock;

#define SOLE_MECAB_CONFIRM do {\
  if (!sole_mecab) {\
    char *arg[] = {"", "-Owakati"};\
    MUTEX_LOCK(sole_mecab_lock);\
    if (!sole_mecab) { sole_mecab = mecab_new(2, arg); }\
    MUTEX_UNLOCK(sole_mecab_lock);\
  }\
} while(0)

inline static sen_lex *
sen_mecab_open(sen_sym *sym, sen_nstr *nstr, uint8_t flags)
{
  unsigned int bufsize, maxtrial = 10, len;
  char *buf, *s, *p;
  sen_lex *lex;
  sen_ctx *ctx = nstr->ctx;
  if (!(lex = SEN_MALLOC(sizeof(sen_lex)))) { return NULL; }
  lex->sym = sym;
  // sen_log("(%s)", str);
  SOLE_MECAB_CONFIRM;
  if (!sole_mecab) {
    SEN_LOG(sen_log_alert, "mecab_new failed on sen_mecab_open");
    return NULL;
  }
  lex->mecab = sole_mecab;
  lex->buf = NULL;
  lex->token = NULL;
  lex->tlen = 0;
  // if (!(lex->mecab = mecab_new3())) {
  lex->pos = -1;
  lex->offset = 0;
  lex->len = 0;
  lex->flags = flags;
  lex->status = sen_lex_doing;
  lex->encoding = sym->encoding;
  lex->nstr = nstr;
  len = nstr->norm_blen;
  for (bufsize = len * 2 + 1; maxtrial; bufsize *= 2, maxtrial--) {
    if(!(buf = SEN_MALLOC(bufsize + 1))) {
      SEN_LOG(sen_log_alert, "buffer allocation on sen_mecab_open failed !");
      SEN_FREE(lex);
      return NULL;
    }
    MUTEX_LOCK(sole_mecab_lock);
    s = mecab_sparse_tostr3(lex->mecab, (char *)nstr->norm, len, buf, bufsize);
    MUTEX_UNLOCK(sole_mecab_lock);
    if (s) { break; }
    SEN_FREE(buf);
  }
  if (!maxtrial) {
    SEN_LOG(sen_log_alert, "mecab_sparse_tostr failed len=%d bufsize=%d", len, bufsize);
    sen_lex_close(lex);
    return NULL;
  }
  // certain version of mecab returns trailing lf or spaces.
  for (p = buf + strlen(buf) - 1; buf <= p && (*p == '\n' || isspace(*(unsigned char *)p)); p--) { *p = '\0'; }
  //sen_log("sparsed='%s'", s);
  lex->orig = (unsigned char *)nstr->norm;
  lex->buf = (unsigned char *)buf;
  lex->next = (unsigned char *)buf;
  lex->force_prefix = 0;
  return lex;
}

inline static sen_id
sen_mecab_next(sen_lex *lex)
{
  sen_id tid;
  sen_sym *sym = lex->sym;
  sen_ctx *ctx = lex->nstr->ctx;
  uint32_t size;
  int32_t len, offset = lex->offset + lex->len;
  const unsigned char *p;
  if (lex->status == sen_lex_done) { return SEN_SYM_NIL; }
  for (p = lex->next, len = 0;;) {
    size_t cl;
    if (!(cl = sen_str_charlen((char *)p, lex->encoding)) ||
        sen_isspace(p, lex->encoding)) {
      break;
    }
    p += cl;
    len++;
  }
  if (!len) {
    lex->status = sen_lex_done;
    return SEN_SYM_NIL;
  }
  size = (uint32_t)(p - lex->next);
  LEX_TOKEN(lex, lex->next, size);
  tid = (lex->flags & SEN_LEX_ADD) ? sen_sym_get(sym, lex->token) : sen_sym_at(sym, lex->token);
  {
    int cl;
    while ((cl = sen_isspace(p, lex->encoding))) { p += cl; }
    lex->next = p;
    lex->offset = offset;
    lex->len = len;
  }
  if (tid == SEN_SYM_NIL) {
    lex->status = sen_lex_not_found;
  } else {
    if (!*p) { lex->status = sen_lex_done; }
  }
  lex->pos++;
  return tid;
}

#endif /* NO_MECAB */

/* delimited */

inline static sen_lex *
sen_delimited_open(sen_sym *sym, sen_nstr *nstr, uint8_t flags)
{
  int cl;
  sen_lex *lex;
  sen_ctx *ctx = nstr->ctx;
  const char *p;
  if (!(lex = SEN_MALLOC(sizeof(sen_lex)))) { return NULL; }
  lex->sym = sym;
#ifndef NO_MECAB
  lex->mecab = NULL;
#endif /* NO_MECAB */
  lex->buf = NULL;
  lex->token = NULL;
  lex->tlen = 0;
  lex->pos = -1;
  lex->skip = 1;
  lex->tail = 0;
  lex->flags = flags;
  lex->status = sen_lex_doing;
  lex->encoding = sym->encoding;
  lex->nstr = nstr;
  p = nstr->norm;
  lex->orig = (unsigned char *)p;
  while ((cl = sen_isspace(p, lex->encoding))) { p += cl; }
  lex->next = (unsigned char *)p;
  lex->offset = 0;
  lex->len = 0;
  if (!*p) { lex->status = sen_lex_done; }
  lex->force_prefix = 0;
  return lex;
}

inline static sen_id
sen_delimited_next(sen_lex *lex)
{
  sen_id tid;
  sen_sym *sym = lex->sym;
  sen_ctx *ctx = lex->nstr->ctx;
  uint32_t size;
  int32_t len, offset = lex->offset + lex->len;
  const unsigned char *p;
  if (lex->status == sen_lex_done) { return SEN_SYM_NIL; }
  for (p = lex->next, len = 0;;) {
    size_t cl;
    if (!(cl = sen_str_charlen((char *)p, lex->encoding)) ||
        sen_isspace(p, lex->encoding)) {
      break;
    }
    p += cl;
    len++;
  }
  if (!len) {
    lex->status = sen_lex_done;
    return SEN_SYM_NIL;
  }
  size = (uint32_t)(p - lex->next);
  LEX_TOKEN(lex, lex->next, size);
  tid = (lex->flags & SEN_LEX_ADD) ? sen_sym_get(sym, lex->token) : sen_sym_at(sym, lex->token);
  {
    int cl;
    while ((cl = sen_isspace(p, lex->encoding))) { p += cl; }
    lex->next = p;
    lex->offset = offset;
    lex->len = len;
  }
  if (tid == SEN_SYM_NIL) {
    lex->status = sen_lex_not_found;
  } else {
    if (!*p) { lex->status = sen_lex_done; }
  }
  lex->pos++;
  return tid;
}

/* external */

sen_rc
sen_lex_init(void)
{
#ifndef NO_MECAB
  // char *arg[] = {"", "-Owakati"};
  // return mecab_load_dictionary(2, arg) ? sen_success : sen_external_error;
  sole_mecab = NULL;
  MUTEX_INIT(sole_mecab_lock);
#endif /* NO_MECAB */
  return sen_success;
}

sen_rc
sen_lex_fin(void)
{
#ifndef NO_MECAB
  if (sole_mecab) {
    mecab_destroy(sole_mecab);
    sole_mecab = NULL;
  }
  MUTEX_DESTROY(sole_mecab_lock);
#endif /* NO_MECAB */
  return sen_success;
}

sen_lex *
sen_lex_open(sen_sym *sym, const char *str, size_t str_len, uint8_t flags)
{
  sen_nstr *nstr;
  int nflag, type;
  if (!sym) {
    SEN_LOG(sen_log_warning, "sym is null at sen_lex_open");
    return NULL;
  }
  type = sym->flags & SEN_INDEX_TOKENIZER_MASK;
  nflag = (type == SEN_INDEX_NGRAM ? SEN_STR_REMOVEBLANK|SEN_STR_WITH_CTYPES : 0);
  if (sym->flags & SEN_INDEX_NORMALIZE) {
    if (!(nstr = sen_nstr_open(str, str_len, sym->encoding, nflag))) {
      SEN_LOG(sen_log_alert, "sen_nstr_open failed at sen_lex_open");
      return NULL;
    }
  } else {
    if (!(nstr = sen_fakenstr_open(str, str_len, sym->encoding, nflag))) {
      SEN_LOG(sen_log_alert, "sen_fakenstr_open failed at sen_lex_open");
      return NULL;
    }
  }
  switch (type) {
  case SEN_INDEX_MORPH_ANALYSE :
#ifdef NO_MECAB
    return NULL;
#else /* NO_MECAB */
    return sen_mecab_open(sym, nstr, flags);
#endif /* NO_MECAB */
  case SEN_INDEX_NGRAM :
    return sen_ngram_open(sym, nstr, flags);
  case SEN_INDEX_DELIMITED :
    return sen_delimited_open(sym, nstr, flags);
  default :
    return NULL;
  }
}

sen_rc
sen_lex_next(sen_lex *lex)
{
  /* if (!lex) { return sen_invalid_argument; } */
  switch ((lex->sym->flags & SEN_INDEX_TOKENIZER_MASK)) {
  case SEN_INDEX_MORPH_ANALYSE :
#ifdef NO_MECAB
    return sen_invalid_argument;
#else /* NO_MECAB */
    return sen_mecab_next(lex);
#endif /* NO_MECAB */
  case SEN_INDEX_NGRAM :
    return sen_ngram_next(lex);
  case SEN_INDEX_DELIMITED :
    return sen_delimited_next(lex);
  default :
    return sen_invalid_argument;
  }
}

sen_rc
sen_lex_close(sen_lex *lex)
{
  if (lex) {
    sen_ctx *ctx = lex->nstr->ctx;
    if (lex->nstr) { sen_nstr_close(lex->nstr); }
    // if (lex->mecab) { mecab_destroy(lex->mecab); }
    if (lex->buf) { SEN_FREE(lex->buf); }
    if (lex->token) { SEN_REALLOC(lex->token, 0); }
    SEN_FREE(lex);
    return sen_success;
  } else {
    return sen_invalid_argument;
  }
}

sen_rc
sen_lex_validate(sen_sym *sym)
{
  if (!sym) {
    SEN_LOG(sen_log_warning, "sym is null on sen_lex_validate");
    return sen_invalid_argument;
  }
#ifndef NO_MECAB
#ifdef USE_MECAB_DICINFO
  if ((sym->flags & SEN_INDEX_TOKENIZER_MASK) == SEN_INDEX_MORPH_ANALYSE) {
    sen_encoding enc;
    const mecab_dictionary_info_t *di;

    SOLE_MECAB_CONFIRM;
    if (!sole_mecab) {
      SEN_LOG(sen_log_alert, "mecab_new failed on sen_lex_validate");
      return sen_external_error;
    }
    di = mecab_dictionary_info(sole_mecab);
    if (!di || !di->charset) {
      SEN_LOG(sen_log_alert, "mecab_dictionary_info failed on sen_lex_validate");
      return sen_external_error;
    }
    switch (di->charset[0]) {
      case 'u':
        enc = sen_enc_utf8;
        break;
      case 'e':
        enc = sen_enc_euc_jp;
        break;
      case 'c': /* cp932 */
      case 's':
        enc = sen_enc_sjis;
        break;
      default:
        SEN_LOG(sen_log_alert, "unknown encoding %s on sen_lex_validate", di->charset);
        return sen_external_error;
    }
    if (enc != sym->encoding) {
      SEN_LOG(sen_log_alert,
              "dictionary encoding %s is differ from sym encoding %s",
              di->charset, sen_enctostr(sym->encoding));
      return sen_abnormal_error;
    }
  }
#endif /* USE_MECAB_DICINFO */
#endif /* NO_MECAB */
  return sen_success;
}
