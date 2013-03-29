/* Copyright(C) 2004-2005 Brazil

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
#include <stddef.h>
#include <assert.h>
#include "snip.h"
#include "ctx.h"

#if !defined MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#if !defined MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

static int
sen_bm_check_euc(const unsigned char *x, const size_t y)
{
  const unsigned char *p;
  for (p = x + y - 1; p >= x && *p >= 0x80U; p--);
  return (int) ((x + y - p) & 1);
}

static int
sen_bm_check_sjis(const unsigned char *x, const size_t y)
{
  const unsigned char *p;
  for (p = x + y - 1; p >= x; p--)
    if ((*p < 0x81U) || (*p > 0x9fU && *p < 0xe0U) || (*p > 0xfcU))
      break;
  return (int) ((x + y - p) & 1);
}

/*
static void
sen_bm_suffixes(const unsigned char *x, size_t m, size_t *suff)
{
  size_t f, g;
  intptr_t i;
  f = 0;
  suff[m - 1] = m;
  g = m - 1;
  for (i = m - 2; i >= 0; --i) {
    if (i > (intptr_t) g && suff[i + m - 1 - f] < i - g)
      suff[i] = suff[i + m - 1 - f];
    else {
      if (i < (intptr_t) g)
        g = i;
      f = i;
      while (g > 0 && x[g] == x[g + m - 1 - f])
        --g;
      suff[i] = f - g;
    }
  }
}
*/

static void
sen_bm_preBmBc(const unsigned char *x, size_t m, size_t *bmBc)
{
  size_t i;
  for (i = 0; i < ASIZE; ++i) {
    bmBc[i] = m;
  }
  for (i = 0; i < m - 1; ++i) {
    bmBc[(unsigned int) x[i]] = m - (i + 1);
  }
}

#define SEN_BM_COMPARE \
  if (object->checks[found]) { \
    size_t offset = cond->start_offset, found_alpha_head = cond->found_alpha_head; \
    /* calc real offset */\
    for (i = cond->last_found; i < found; i++) { \
      if (object->checks[i] > 0) { \
        found_alpha_head = i; \
        offset += object->checks[i]; \
      } \
    } \
    /* if real offset is in a character, move it the head of the character */ \
    if (object->checks[found] < 0) { \
      offset -= object->checks[found_alpha_head]; \
      cond->last_found = found_alpha_head; \
    } else { \
      cond->last_found = found; \
    } \
    if (flags & SEN_SNIP_SKIP_LEADING_SPACES) { \
      while (offset < object->orig_blen && \
             (i = sen_isspace(object->orig + offset, object->encoding))) { offset += i; } \
    } \
    cond->start_offset = offset; \
    for (i = cond->last_found; i < found + m; i++) { \
      if (object->checks[i] > 0) { \
        offset += object->checks[i]; \
      } \
    } \
    cond->end_offset = offset; \
    cond->found = found + shift; \
    cond->found_alpha_head = found_alpha_head; \
    /* printf("bm: cond:%p found:%zd last_found:%zd st_off:%zd ed_off:%zd\n", cond, cond->found,cond->last_found,cond->start_offset,cond->end_offset); */ \
    return; \
  }

#define SEN_BM_BM_COMPARE \
{ \
  if (p[-2] == ck) { \
    for (i = 3; i <= m && p[-(intptr_t)i] == cp[-(intptr_t)i]; ++i) { \
    } \
    if (i > m) { \
      found = p - y - m; \
      SEN_BM_COMPARE; \
    } \
  } \
}

void
sen_bm_tunedbm(snip_cond *cond, sen_nstr *object, int flags)
{
  register unsigned char *limit, ck;
  register const unsigned char *p, *cp;
  register size_t *bmBc, delta1, i;

  const unsigned char *x;
  unsigned char *y;
  size_t shift, found;

  const size_t n = object->norm_blen, m = cond->keyword->norm_blen;

  y = (unsigned char *) object->norm;
  if (m == 1) {
    if (n > cond->found) {
      shift = 1;
      p = memchr(y + cond->found, cond->keyword->norm[0], n - cond->found);
      if (p != NULL) {
        found = p - y;
        SEN_BM_COMPARE;
      }
    }
    cond->stopflag = SNIPCOND_STOP;
    return;
  }

  x = (unsigned char *) cond->keyword->norm;
  bmBc = cond->bmBc;
  shift = cond->shift;

  /* Restart */
  p = y + m + cond->found;
  cp = x + m;
  ck = cp[-2];

  /* 12 means 1(initial offset) + 10 (in loop) + 1 (shift) */
  if (n - cond->found > 12 * m) {
    limit = y + n - 11 * m;
    while (p <= limit) {
      p += bmBc[p[-1]];
      if(!(delta1 = bmBc[p[-1]])) {
        goto check;
      }
      p += delta1;
      p += bmBc[p[-1]];
      p += bmBc[p[-1]];
      if(!(delta1 = bmBc[p[-1]])) {
        goto check;
      }
      p += delta1;
      p += bmBc[p[-1]];
      p += bmBc[p[-1]];
      if(!(delta1 = bmBc[p[-1]])) {
        goto check;
      }
      p += delta1;
      p += bmBc[p[-1]];
      p += bmBc[p[-1]];
      continue;
    check:
      SEN_BM_BM_COMPARE;
      p += shift;
    }
  }
  /* limit check + search */
  limit = y + n;
  while(p <= limit) {
    if (!(delta1 = bmBc[p[-1]])) {
      SEN_BM_BM_COMPARE;
      p += shift;
    }
    p += delta1;
  }
  cond->stopflag = SNIPCOND_STOP;
}

static size_t
count_mapped_chars(const char *str, const char *end)
{
  const char *p;
  size_t dl;

  dl = 0;
  for (p = str; p != end; p++) {
    switch (*p) {
    case '<':
    case '>':
      dl += 4;                  /* &lt; or &gt; */
      break;
    case '&':
      dl += 5;                  /* &amp; */
      break;
    case '"':
      dl += 6;                  /* &quot; */
      break;
    default:
      dl++;
      break;
    }
  }
  return dl;
}

sen_rc
sen_snip_cond_close(snip_cond *cond)
{
  if (!cond) {
    return sen_invalid_argument;
  }
  if (cond->keyword) {
    sen_nstr_close(cond->keyword);
  }
  return sen_success;
}

sen_rc
sen_snip_cond_init(snip_cond *sc, const char *keyword, unsigned int keyword_len,
                sen_encoding enc, int flags)
{
  size_t norm_blen;
  memset(sc, 0, sizeof(snip_cond));
  if (flags & SEN_SNIP_NORMALIZE) {
    if (!(sc->keyword = sen_nstr_open(keyword, keyword_len,
                                      enc, SEN_STR_REMOVEBLANK))) {
      SEN_LOG(sen_log_alert, "sen_nstr_open on snip_cond_init failed !");
      return sen_memory_exhausted;
    }
  } else {
    if (!(sc->keyword = sen_fakenstr_open(keyword, keyword_len,
                                          enc, SEN_STR_REMOVEBLANK))) {
      SEN_LOG(sen_log_alert, "sen_fakenstr_open on snip_cond_init failed !");
      return sen_memory_exhausted;
    }
  }
  norm_blen = sc->keyword->norm_blen; /* byte length, not cond->keyword->length */
  if (!norm_blen) {
    sen_snip_cond_close(sc);
    return sen_invalid_argument;
  }
  if (norm_blen != 1) {
    sen_bm_preBmBc((unsigned char *)sc->keyword->norm, norm_blen, sc->bmBc);
    sc->shift = sc->bmBc[(unsigned char)sc->keyword->norm[norm_blen - 1]];
    sc->bmBc[(unsigned char)sc->keyword->norm[norm_blen - 1]] = 0;
  }
  return sen_success;
}

void
sen_snip_cond_reinit(snip_cond *cond)
{
  cond->found = 0;
  cond->last_found = 0;
  cond->start_offset = 0;
  cond->end_offset = 0;

  cond->count = 0;
  cond->stopflag = SNIPCOND_NONSTOP;
}

sen_rc
sen_snip_add_cond(sen_snip *snip,
                  const char *keyword, unsigned int keyword_len,
                  const char *opentag, unsigned int opentag_len,
                  const char *closetag, unsigned int closetag_len)
{
  sen_rc rc;
  snip_cond *cond;
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */

  if (!snip || !keyword || !keyword_len || snip->cond_len >= MAX_SNIP_COND_COUNT) {
    return sen_invalid_argument;
  }
  cond = snip->cond + snip->cond_len;
  if ((rc = sen_snip_cond_init(cond, keyword, keyword_len,
                               snip->encoding, snip->flags))) {
    return rc;
  }
  if (cond->keyword->norm_blen > snip->width) {
    sen_snip_cond_close(cond);
    return sen_invalid_argument;
  }
  if (opentag) {
    if (snip->flags & SEN_SNIP_COPY_TAG) {
      char *t = SEN_MALLOC(opentag_len + 1);
      if (!t) {
        sen_snip_cond_close(cond);
        return sen_memory_exhausted;
      }
      memcpy(t, opentag, opentag_len);
      t[opentag_len]= '\0'; /* not required, but for ql use */
      cond->opentag = t;
    } else {
      cond->opentag = opentag;
    }
    cond->opentag_len = opentag_len;
  } else {
    cond->opentag = snip->defaultopentag;
    cond->opentag_len = snip->defaultopentag_len;
  }
  if (closetag) {
    if (snip->flags & SEN_SNIP_COPY_TAG) {
      char *t = SEN_MALLOC(closetag_len + 1);
      if (!t) { return sen_memory_exhausted; }
      memcpy(t, closetag, closetag_len);
      t[closetag_len]= '\0'; /* not required, but for ql use */
      cond->closetag = t;
    } else {
      cond->closetag = closetag;
    }
    cond->closetag_len = closetag_len;
  } else {
    cond->closetag = snip->defaultclosetag;
    cond->closetag_len = snip->defaultclosetag_len;
  }
  snip->cond_len++;
  return sen_success;
}

static size_t
sen_snip_find_firstbyte(const char *string, sen_encoding encoding, size_t offset,
                        size_t doffset)
{
  switch (encoding) {
  case sen_enc_euc_jp:
    while (!(sen_bm_check_euc((unsigned char *) string, offset)))
      offset += doffset;
    break;
  case sen_enc_sjis:
    if (!(sen_bm_check_sjis((unsigned char *) string, offset)))
      offset += doffset;
    break;
  case sen_enc_utf8:
    while (string[offset] <= (char)0xc0)
      offset += doffset;
    break;
  default:
    break;
  }
  return offset;
}

sen_snip *
sen_snip_open(sen_encoding encoding, int flags, unsigned int width,
              unsigned int max_results,
              const char *defaultopentag, unsigned int defaultopentag_len,
              const char *defaultclosetag, unsigned int defaultclosetag_len,
              sen_snip_mapping *mapping)
{
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  sen_snip *ret = NULL;
  if (!(ret = SEN_MALLOC(sizeof(sen_snip)))) {
    SEN_LOG(sen_log_alert, "sen_snip allocation failed on sen_snip_open");
    return NULL;
  }
  if (max_results > MAX_SNIP_RESULT_COUNT || max_results == 0) {
    SEN_LOG(sen_log_warning, "max_results is invalid on sen_snip_open");
    return NULL;
  }
  ret->encoding = encoding;
  ret->flags = flags;
  ret->width = width;
  ret->max_results = max_results;
  if (flags & SEN_SNIP_COPY_TAG) {
    char *t;
    t = SEN_MALLOC(defaultopentag_len + 1);
    if (!t) {
      SEN_FREE(ret);
      return NULL;
    }
    memcpy(t, defaultopentag, defaultopentag_len);
    t[defaultopentag_len]= '\0'; /* not required, but for ql use */
    ret->defaultopentag = t;

    t = SEN_MALLOC(defaultclosetag_len + 1);
    if (!t) {
      SEN_FREE((void *)ret->defaultopentag);
      SEN_FREE(ret);
      return NULL;
    }
    memcpy(t, defaultclosetag, defaultclosetag_len);
    t[defaultclosetag_len]= '\0'; /* not required, but for ql use */
    ret->defaultclosetag = t;
  } else {
    ret->defaultopentag = defaultopentag;
    ret->defaultclosetag = defaultclosetag;
  }
  ret->defaultopentag_len = defaultopentag_len;
  ret->defaultclosetag_len = defaultclosetag_len;
  ret->cond_len = 0;
  ret->mapping = mapping;
  ret->nstr = NULL;
  ret->tag_count = 0;
  ret->snip_count = 0;

  return ret;
}

static sen_rc
exec_clean(sen_snip *snip)
{
  snip_cond *cond, *cond_end;
  if (snip->nstr) {
    sen_nstr_close(snip->nstr);
    snip->nstr = NULL;
  }
  snip->tag_count = 0;
  snip->snip_count = 0;
  for (cond = snip->cond, cond_end = cond + snip->cond_len;
       cond < cond_end; cond++) {
    sen_snip_cond_reinit(cond);
  }
  return sen_success;
}

sen_rc
sen_snip_close(sen_snip *snip)
{
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  snip_cond *cond, *cond_end;
  if (!snip) { return sen_invalid_argument; }
  if (snip->flags & SEN_SNIP_COPY_TAG) {
    int i;
    snip_cond *sc;
    const char *dot = snip->defaultopentag, *dct = snip->defaultclosetag;
    for (i = snip->cond_len, sc = snip->cond; i; i--, sc++) {
      if (sc->opentag != dot) { SEN_FREE((void *)sc->opentag); }
      if (sc->closetag != dct) { SEN_FREE((void *)sc->closetag); }
    }
    if (dot) { SEN_FREE((void *)dot); }
    if (dct) { SEN_FREE((void *)dct); }
  }
  if (snip->nstr) {
    sen_nstr_close(snip->nstr);
  }
  for (cond = snip->cond, cond_end = cond + snip->cond_len;
       cond < cond_end; cond++) {
    sen_snip_cond_close(cond);
  }
  SEN_FREE(snip);
  return sen_success;
}

sen_rc
sen_snip_exec(sen_snip *snip, const char *string, unsigned int string_len,
              unsigned int *nresults, unsigned int *max_tagged_len)
{
  size_t i;
  if (!snip || !string) {
    return sen_invalid_argument;
  }
  exec_clean(snip);
  *nresults = 0;
  if (snip->flags & SEN_SNIP_NORMALIZE) {
    snip->nstr =
      sen_nstr_open(string, string_len, snip->encoding,
                    SEN_STR_WITH_CHECKS | SEN_STR_REMOVEBLANK);
  } else {
    snip->nstr =
      sen_fakenstr_open(string, string_len, snip->encoding,
                        SEN_STR_WITH_CHECKS | SEN_STR_REMOVEBLANK);
  }
  if (!snip->nstr) {
    exec_clean(snip);
    SEN_LOG(sen_log_alert, "sen_nstr_open on sen_snip_exec failed !");
    return sen_memory_exhausted;
  }
  for (i = 0; i < snip->cond_len; i++) {
    sen_bm_tunedbm(snip->cond + i, snip->nstr, snip->flags);
  }

  {
    _snip_tag_result *tag_result = snip->tag_result;
    _snip_result *snip_result = snip->snip_result;
    size_t last_end_offset = 0, last_last_end_offset = 0;
    unsigned int unfound_cond_count = snip->cond_len;

    *max_tagged_len = 0;
    while (1) {
      size_t tagged_len = 0, last_tag_end = 0;
      int_least8_t all_stop = 1, found_cond = 0;
      snip_result->tag_count = 0;

      while (1) {
        size_t min_start_offset = (size_t) -1;
        size_t max_end_offset = 0;
        snip_cond *cond = NULL;

        /* get condition which have minimum offset and is not stopped */
        for (i = 0; i < snip->cond_len; i++) {
          if (snip->cond[i].stopflag == SNIPCOND_NONSTOP &&
              (min_start_offset > snip->cond[i].start_offset ||
               (min_start_offset == snip->cond[i].start_offset &&
                max_end_offset < snip->cond[i].end_offset))) {
            min_start_offset = snip->cond[i].start_offset;
            max_end_offset = snip->cond[i].end_offset;
            cond = &snip->cond[i];
          }
        }
        if (!cond) {
          break;
        }
        /* check whether condtion is the first condition in snippet */
        if (snip_result->tag_count == 0) {
          /* skip condition if the number of rest snippet field is smaller than */
          /* the number of unfound keywords. */
          if (snip->max_results - *nresults <= unfound_cond_count && cond->count > 0) {
            int_least8_t exclude_other_cond = 1;
            for (i = 0; i < snip->cond_len; i++) {
              if ((snip->cond + i) != cond
                  && snip->cond[i].end_offset <= cond->start_offset + snip->width
                  && snip->cond[i].count == 0) {
                exclude_other_cond = 0;
              }
            }
            if (exclude_other_cond) {
              sen_bm_tunedbm(cond, snip->nstr, snip->flags);
              continue;
            }
          }
          snip_result->start_offset = cond->start_offset;
          snip_result->first_tag_result_idx = snip->tag_count;
        } else {
          if (cond->start_offset >= snip_result->start_offset + snip->width) {
            break;
          }
          /* check nesting to make valid HTML */
          /* ToDo: allow <test><te>te</te><st>st</st></test> */
          if (cond->start_offset < last_tag_end) {
            sen_bm_tunedbm(cond, snip->nstr, snip->flags);
            continue;
          }
        }
        if (cond->end_offset > snip_result->start_offset + snip->width) {
          /* If a keyword gets across a snippet, */
          /* it was skipped and never to be tagged. */
          cond->stopflag = SNIPCOND_ACROSS;
          sen_bm_tunedbm(cond, snip->nstr, snip->flags);
        } else {
          found_cond = 1;
          if (cond->count == 0) {
            unfound_cond_count--;
          }
          cond->count++;
          last_end_offset = cond->end_offset;

          tag_result->cond = cond;
          tag_result->start_offset = cond->start_offset;
          tag_result->end_offset = last_tag_end = cond->end_offset;

          snip_result->tag_count++;
          tag_result++;
          tagged_len += cond->opentag_len + cond->closetag_len;
          if (++snip->tag_count >= MAX_SNIP_TAG_COUNT) {
            break;
          }
          sen_bm_tunedbm(cond, snip->nstr, snip->flags);
        }
      }
      if (!found_cond) {
        break;
      }
      if (snip_result->start_offset + last_end_offset < snip->width) {
        snip_result->start_offset = 0;
      } else {
        snip_result->start_offset =
          MAX(MIN
              ((snip_result->start_offset + last_end_offset - snip->width) / 2,
               string_len - snip->width), last_last_end_offset);
      }
      snip_result->start_offset =
        sen_snip_find_firstbyte(string, snip->encoding, snip_result->start_offset, 1);

      snip_result->end_offset = snip_result->start_offset + snip->width;
      if (snip_result->end_offset < string_len) {
        snip_result->end_offset =
          sen_snip_find_firstbyte(string, snip->encoding, snip_result->end_offset, -1);
      } else {
        snip_result->end_offset = string_len;
      }
      last_last_end_offset = snip_result->end_offset;

      tagged_len +=
        count_mapped_chars(&string[snip_result->start_offset],
                           &string[snip_result->end_offset]) + 1;

      *max_tagged_len = MAX(*max_tagged_len, tagged_len);

      snip_result->last_tag_result_idx = snip->tag_count - 1;
      (*nresults)++;
      snip_result++;

      if (*nresults == snip->max_results || snip->tag_count == MAX_SNIP_TAG_COUNT) {
        break;
      }
      for (i = 0; i < snip->cond_len; i++) {
        if (snip->cond[i].stopflag != SNIPCOND_STOP) {
          all_stop = 0;
          snip->cond[i].stopflag = SNIPCOND_NONSTOP;
        }
      }
      if (all_stop) {
        break;
      }
    }
  }
  snip->snip_count = *nresults;
  snip->string = string;

  snip->max_tagged_len = *max_tagged_len;

  return sen_success;
}

sen_rc
sen_snip_get_result(sen_snip *snip, const unsigned int index, char *result, unsigned int *result_len)
{
  char *p;
  size_t i, j, k;
  _snip_result *sres = &snip->snip_result[index];

  if (snip->snip_count <= index || !snip->nstr) {
    return sen_invalid_argument;
  }

  assert(snip->snip_count != 0 && snip->tag_count != 0);

  j = sres->first_tag_result_idx;
  for (p = result, i = sres->start_offset; i < sres->end_offset; i++) {
    for (; j <= sres->last_tag_result_idx && snip->tag_result[j].start_offset == i; j++) {
      if (snip->tag_result[j].end_offset > sres->end_offset) {
        continue;
      }
      memcpy(p, snip->tag_result[j].cond->opentag, snip->tag_result[j].cond->opentag_len);
      p += snip->tag_result[j].cond->opentag_len;
    }

    if (snip->mapping == (sen_snip_mapping *) -1) {
      switch (snip->string[i]) {
      case '<':
        *p++ = '&';
        *p++ = 'l';
        *p++ = 't';
        *p++ = ';';
        break;
      case '>':
        *p++ = '&';
        *p++ = 'g';
        *p++ = 't';
        *p++ = ';';
        break;
      case '&':
        *p++ = '&';
        *p++ = 'a';
        *p++ = 'm';
        *p++ = 'p';
        *p++ = ';';
        break;
      case '"':
        *p++ = '&';
        *p++ = 'q';
        *p++ = 'u';
        *p++ = 'o';
        *p++ = 't';
        *p++ = ';';
        break;
      default:
        *p++ = snip->string[i];
        break;
      }
    } else {
      *p++ = snip->string[i];
    }

    for (k = sres->last_tag_result_idx;
         snip->tag_result[k].end_offset <= sres->end_offset; k--) {
      /* TODO: avoid all loop */
      if (snip->tag_result[k].end_offset == i + 1) {
        memcpy(p, snip->tag_result[k].cond->closetag,
               snip->tag_result[k].cond->closetag_len);
        p += snip->tag_result[k].cond->closetag_len;
      }
      if (k <= sres->first_tag_result_idx) {
        break;
      }
    };
  }
  *p = '\0';

  if(result_len) { *result_len = (unsigned int)(p - result); }
  assert((unsigned int)(p - result) <= snip->max_tagged_len);

  return sen_success;
}
