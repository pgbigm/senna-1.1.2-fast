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
/* Changelog:
   2013/01/07
   Add fast string-normalization function into Senna.
   Author: NTT DATA Corporation
 */
#include "senna_in.h"
#include <stdio.h>
#include <string.h>
#include "ctx.h"
#include "str.h"
#include "set.h"

#define __USE_ISOC99
#include <math.h>

static sen_set *prefix = NULL;
static sen_set *suffix = NULL;

#define N_PREFIX 2048
#define N_SUFFIX 0

#define PREFIX_PATH SENNA_HOME PATH_SEPARATOR "prefix"
#define SUFFIX_PATH SENNA_HOME PATH_SEPARATOR "suffix"

inline static void
prefix_init(void)
{
  int i, *ip;
  FILE *fp;
  char buffer[4];
  prefix = sen_set_open(2, sizeof(int), 0);
  if (!prefix) { SEN_LOG(sen_log_alert, "sen_set_open on prefix_init failed !"); return; }
  if ((fp = fopen(PREFIX_PATH, "r"))) {
    for (i = 0; i < N_PREFIX; i++) {
      if (!fgets(buffer, 4, fp)) { break; }
      sen_set_get(prefix, buffer, (void **)&ip);
      *ip = i;
    }
    fclose(fp);
  }
}

inline static void
suffix_init(void)
{
  int i;
  FILE *fp;
  char buffer[4];
  suffix = sen_set_open(2, 0, 0);
  if (!suffix) { SEN_LOG(sen_log_alert, "sen_set_open on suffix_init failed !"); return; }
  if ((fp = fopen(SUFFIX_PATH, "r"))) {
    for (i = N_SUFFIX; i; i--) {
      if (!fgets(buffer, 4, fp)) { break; }
      sen_set_get(suffix, buffer, NULL);
    }
    fclose(fp);
  }
}

static inline size_t
sen_str_charlen_utf8(const unsigned char *str, const unsigned char *end)
{
  /* MEMO: This function allows non-null-terminated string as str. */
  /*       But requires the end of string. */
  const unsigned char *p = str;
  if (!*p || p >= end) { return 0; }
  if (*p & 0x80) {
    int b, w;
    size_t size;
    for (b = 0x40, w = 0; b && (*p & b); b >>= 1, w++);
    if (!w) {
      SEN_LOG(sen_log_warning, "invalid utf8 string(1) on sen_str_charlen_utf8");
      return 0;
    }
    for (size = 1; w--; size++) {
      if (++p >= end || !*p || (*p & 0xc0) != 0x80) {
        SEN_LOG(sen_log_warning, "invalid utf8 string(2) on sen_str_charlen_utf8");
        return 0;
      }
    }
    return size;
  } else {
    return 1;
  }
  return 0;
}

inline static size_t
fast_sen_str_charlen_utf8(const unsigned char *s, const unsigned char *e)
{
	size_t		len;

	if (s >= e)
		return 0;

    if ((*s & 0x80) == 0)
        len = 1;
    else if ((*s & 0xe0) == 0xc0)
        len = 2;
    else if ((*s & 0xf0) == 0xe0)
        len = 3;
    else if ((*s & 0xf8) == 0xf0)
        len = 4;
    else if ((*s & 0xfc) == 0xf8)
        len = 5;
    else if ((*s & 0xfe) == 0xfc)
        len = 6;
    else
        len = 1;
    return len;
}

unsigned int
sen_str_charlen(const char *str, sen_encoding encoding)
{
  /* MEMO: This function requires null-terminated string as str.*/
  unsigned char *p = (unsigned char *) str;
  if (!*p) { return 0; }
  switch (encoding) {
  case sen_enc_euc_jp :
    if (*p & 0x80) {
      if (*(p + 1)) {
        return 2;
      } else {
        /* This is invalid character */
        SEN_LOG(sen_log_warning, "invalid euc-jp string end on sen_str_charlen");
        return 0;
      }
    }
    return 1;
    break;
  case sen_enc_utf8 :
    if (*p & 0x80) {
      int b, w;
      size_t size;
      for (b = 0x40, w = 0; b && (*p & b); b >>= 1, w++);
      if (!w) {
        SEN_LOG(sen_log_warning, "invalid utf8 string(1) on sen_str_charlen");
        return 0;
      }
      for (size = 1; w--; size++) {
        if (!*++p || (*p & 0xc0) != 0x80) {
          SEN_LOG(sen_log_warning, "invalid utf8 string(2) on sen_str_charlen");
          return 0;
        }
      }
      return size;
    } else {
      return 1;
    }
    break;
  case sen_enc_sjis :
    if (*p & 0x80) {
      /* we regard 0xa0 as JIS X 0201 KANA. adjusted to other tools. */
      if (0xa0 <= *p && *p <= 0xdf) {
        /* hankaku-kana */
        return 1;
      } else if (!(*(p + 1))) {
        /* This is invalid character */
        SEN_LOG(sen_log_warning, "invalid sjis string end on sen_str_charlen");
        return 0;
      } else {
        return 2;
      }
    } else {
      return 1;
    }
    break;
  default :
    return 1;
    break;
  }
  return 0;
}

size_t
sen_str_charlen_nonnull(const char *str, const char *end, sen_encoding encoding)
{
  /* MEMO: This function allows non-null-terminated string as str. */
  /*       But requires the end of string. */
  unsigned char *p = (unsigned char *) str;
  if (p >= (unsigned char *)end) { return 0; }
  switch (encoding) {
  case sen_enc_euc_jp :
    if (*p & 0x80) {
      if ((p + 1) < (unsigned char *)end) {
        return 2;
      } else {
        /* This is invalid character */
        SEN_LOG(sen_log_warning, "invalid euc-jp string end on sen_str_charlen_nonnull");
        return 0;
      }
    }
    return 1;
    break;
  case sen_enc_utf8 :
    return sen_str_charlen_utf8(p, (unsigned char *)end);
    break;
  case sen_enc_sjis :
    if (*p & 0x80) {
      /* we regard 0xa0 as JIS X 0201 KANA. adjusted to other tools. */
      if (0xa0 <= *p && *p <= 0xdf) {
        /* hankaku-kana */
        return 1;
      } else if (++p >= (unsigned char *)end) {
        /* This is invalid character */
        SEN_LOG(sen_log_warning, "invalid sjis string end on sen_str_charlen_nonnull");
        return 0;
      } else {
        return 2;
      }
    } else {
      return 1;
    }
    break;
  default :
    return 1;
    break;
  }
  return 0;
}

sen_rc
sen_str_fin(void)
{
  if (prefix) { sen_set_close(prefix); }
  if (suffix) { sen_set_close(suffix); }
  return sen_success;
}

int
sen_str_get_prefix_order(const char *str)
{
  int *ip;
  if (!str) { return -1; }
  if (!prefix) { prefix_init(); }
  if (prefix && sen_set_at(prefix, str, (void **)&ip)) {
    return *ip;
  } else {
    return -1;
  }
}

static unsigned char symbol[] = {
  ',', '.', 0, ':', ';', '?', '!', 0, 0, 0, '`', 0, '^', '~', '_', 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, '-', '-', '/', '\\', 0, 0, '|', 0, 0, 0, '\'', 0,
  '"', '(', ')', 0, 0, '[', ']', '{', '}', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  '+', '-', 0, 0, 0, '=', 0, '<', '>', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  '$', 0, 0, '%', '#', '&', '*', '@', 0, 0, 0, 0, 0, 0, 0, 0
};

inline static sen_rc
normalize_euc(sen_nstr *nstr)
{
  static uint16_t hankana[] = {
    0xa1a1, 0xa1a3, 0xa1d6, 0xa1d7, 0xa1a2, 0xa1a6, 0xa5f2, 0xa5a1, 0xa5a3,
    0xa5a5, 0xa5a7, 0xa5a9, 0xa5e3, 0xa5e5, 0xa5e7, 0xa5c3, 0xa1bc, 0xa5a2,
    0xa5a4, 0xa5a6, 0xa5a8, 0xa5aa, 0xa5ab, 0xa5ad, 0xa5af, 0xa5b1, 0xa5b3,
    0xa5b5, 0xa5b7, 0xa5b9, 0xa5bb, 0xa5bd, 0xa5bf, 0xa5c1, 0xa5c4, 0xa5c6,
    0xa5c8, 0xa5ca, 0xa5cb, 0xa5cc, 0xa5cd, 0xa5ce, 0xa5cf, 0xa5d2, 0xa5d5,
    0xa5d8, 0xa5db, 0xa5de, 0xa5df, 0xa5e0, 0xa5e1, 0xa5e2, 0xa5e4, 0xa5e6,
    0xa5e8, 0xa5e9, 0xa5ea, 0xa5eb, 0xa5ec, 0xa5ed, 0xa5ef, 0xa5f3, 0xa1ab,
    0xa1eb
  };
  static unsigned char dakuten[] = {
    0xf4, 0, 0, 0, 0, 0xac, 0, 0xae, 0, 0xb0, 0, 0xb2, 0, 0xb4, 0, 0xb6, 0,
    0xb8, 0, 0xba, 0, 0xbc, 0, 0xbe, 0, 0xc0, 0, 0xc2, 0, 0, 0xc5, 0, 0xc7,
    0, 0xc9, 0, 0, 0, 0, 0, 0, 0xd0, 0, 0, 0xd3, 0, 0, 0xd6, 0, 0, 0xd9, 0,
    0, 0xdc
  };
  static unsigned char handaku[] = {
    0xd1, 0, 0, 0xd4, 0, 0, 0xd7, 0, 0, 0xda, 0, 0, 0xdd
  };
  int16_t *ch;
  sen_ctx *ctx = nstr->ctx;
  const unsigned char *s, *s_, *e;
  unsigned char *d, *d0, *d_, b;
  uint_least8_t *cp, *ctypes, ctype;
  size_t size = nstr->orig_blen, length = 0;
  int removeblankp = nstr->flags & SEN_STR_REMOVEBLANK;
  if (!(nstr->norm = SEN_MALLOC(size * 2 + 1))) {
    return sen_memory_exhausted;
  }
  d0 = (unsigned char *) nstr->norm;
  if (nstr->flags & SEN_STR_WITH_CHECKS) {
    if (!(nstr->checks = SEN_MALLOC(size * 2 * sizeof(int16_t) + 1))) {
      SEN_FREE(nstr->norm);
      nstr->norm = NULL;
      return sen_memory_exhausted;
    }
  }
  ch = nstr->checks;
  if (nstr->flags & SEN_STR_WITH_CTYPES) {
    if (!(nstr->ctypes = SEN_MALLOC(size + 1))) {
      SEN_FREE(nstr->checks);
      SEN_FREE(nstr->norm);
      nstr->checks = NULL;
      nstr->norm = NULL;
      return sen_memory_exhausted;
    }
  }
  cp = ctypes = nstr->ctypes;
  e = (unsigned char *)nstr->orig + size;
  for (s = s_ = (unsigned char *) nstr->orig, d = d_ = d0; s < e; s++) {
    if ((*s & 0x80)) {
      if (((s + 1) < e) && (*(s + 1) & 0x80)) {
        unsigned char c1 = *s++, c2 = *s, c3 = 0;
        switch (c1 >> 4) {
        case 0x08 :
          if (c1 == 0x8e && 0xa0 <= c2 && c2 <= 0xdf) {
            uint16_t c = hankana[c2 - 0xa0];
            switch (c) {
            case 0xa1ab :
              if (d > d0 + 1 && d[-2] == 0xa5
                  && 0xa6 <= d[-1] && d[-1] <= 0xdb && (b = dakuten[d[-1] - 0xa6])) {
                *(d - 1) = b;
                if (ch) { ch[-1] += 2; s_ += 2; }
                continue;
              } else {
                *d++ = c >> 8; *d = c & 0xff;
              }
              break;
            case 0xa1eb :
              if (d > d0 + 1 && d[-2] == 0xa5
                  && 0xcf <= d[-1] && d[-1] <= 0xdb && (b = handaku[d[-1] - 0xcf])) {
                *(d - 1) = b;
                if (ch) { ch[-1] += 2; s_ += 2; }
                continue;
              } else {
                *d++ = c >> 8; *d = c & 0xff;
              }
              break;
            default :
              *d++ = c >> 8; *d = c & 0xff;
              break;
            }
            ctype = sen_str_katakana;
          } else {
            *d++ = c1; *d = c2;
            ctype = sen_str_others;
          }
          break;
        case 0x09 :
          *d++ = c1; *d = c2;
          ctype = sen_str_others;
          break;
        case 0x0a :
          switch (c1 & 0x0f) {
          case 1 :
            switch (c2) {
            case 0xbc :
              *d++ = c1; *d = c2;
              ctype = sen_str_katakana;
              break;
            case 0xb9 :
              *d++ = c1; *d = c2;
              ctype = sen_str_kanji;
              break;
            case 0xa1 :
              if (removeblankp) {
                if (cp > ctypes) { *(cp - 1) |= SEN_NSTR_BLANK; }
                continue;
              } else {
                *d = ' ';
                ctype = SEN_NSTR_BLANK|sen_str_symbol;
              }
              break;
            default :
              if (c2 >= 0xa4 && (c3 = symbol[c2 - 0xa4])) {
                *d = c3;
                ctype = sen_str_symbol;
              } else {
                *d++ = c1; *d = c2;
                ctype = sen_str_others;
              }
              break;
            }
            break;
          case 2 :
            *d++ = c1; *d = c2;
            ctype = sen_str_symbol;
            break;
          case 3 :
            c3 = c2 - 0x80;
            if ('a' <= c3 && c3 <= 'z') {
              ctype = sen_str_alpha;
              *d = c3;
            } else if ('A' <= c3 && c3 <= 'Z') {
              ctype = sen_str_alpha;
              *d = c3 + 0x20;
            } else if ('0' <= c3 && c3 <= '9') {
              ctype = sen_str_digit;
              *d = c3;
            } else {
              ctype = sen_str_others;
              *d++ = c1; *d = c2;
            }
            break;
          case 4 :
            *d++ = c1; *d = c2;
            ctype = sen_str_hiragana;
            break;
          case 5 :
            *d++ = c1; *d = c2;
            ctype = sen_str_katakana;
            break;
          case 6 :
          case 7 :
          case 8 :
            *d++ = c1; *d = c2;
            ctype = sen_str_symbol;
            break;
          default :
            *d++ = c1; *d = c2;
            ctype = sen_str_others;
            break;
          }
          break;
        default :
          *d++ = c1; *d = c2;
          ctype = sen_str_kanji;
          break;
        }
      } else {
        /* skip invalid character */
        continue;
      }
    } else {
      unsigned char c = *s;
      switch (c >> 4) {
      case 0 :
      case 1 :
        /* skip unprintable ascii */
        if (cp > ctypes) { *(cp - 1) |= SEN_NSTR_BLANK; }
        continue;
      case 2 :
        if (c == 0x20) {
          if (removeblankp) {
            if (cp > ctypes) { *(cp - 1) |= SEN_NSTR_BLANK; }
            continue;
          } else {
            *d = ' ';
            ctype = SEN_NSTR_BLANK|sen_str_symbol;
          }
        } else {
          *d = c;
          ctype = sen_str_symbol;
        }
        break;
      case 3 :
        *d = c;
        ctype = (c <= 0x39) ? sen_str_digit : sen_str_symbol;
        break;
      case 4 :
        *d = ('A' <= c) ? c + 0x20 : c;
        ctype = (c == 0x40) ? sen_str_symbol : sen_str_alpha;
        break;
      case 5 :
        *d = (c <= 'Z') ? c + 0x20 : c;
        ctype = (c <= 0x5a) ? sen_str_alpha : sen_str_symbol;
        break;
      case 6 :
        *d = c;
        ctype = (c == 0x60) ? sen_str_symbol : sen_str_alpha;
        break;
      case 7 :
        *d = c;
        ctype = (c <= 0x7a) ? sen_str_alpha : (c == 0x7f ? sen_str_others : sen_str_symbol);
        break;
      default :
        *d = c;
        ctype = sen_str_others;
        break;
      }
    }
    d++;
    length++;
    if (cp) { *cp++ = ctype; }
    if (ch) {
      *ch++ = (int16_t)(s + 1 - s_);
      s_ = s + 1;
      while (++d_ < d) { *ch++ = 0; }
    }
  }
  if (cp) { *cp = sen_str_null; }
  *d = '\0';
  nstr->length = length;
  nstr->norm_blen = (size_t)(d - (unsigned char *)nstr->norm);
  return sen_success;
}

#ifndef NO_NFKC
uint_least8_t sen_nfkc_ctype(const unsigned char *str);
const char *sen_nfkc_map1(const unsigned char *str);
const char *sen_nfkc_map2(const unsigned char *prefix, const unsigned char *suffix);

/*
 * We backported all changes about normalize_utf8 from Senna 1.1.5.
 * Because normalize_utf8() in Senna 1.1.2 has the bug; the buffer
 * overflow can happen when U+FDFA or U+3316 is given.
 */
inline static sen_rc
normalize_utf8(sen_nstr *nstr)
{
  int16_t *ch;
  sen_ctx *ctx = nstr->ctx;
  const unsigned char *s, *s_, *s__, *p, *p2, *pe, *e;
  unsigned char *d, *d_, *de;
  uint_least8_t *cp;
  size_t length = 0, ls, lp, size = nstr->orig_blen, ds = size * 3;
  int removeblankp = nstr->flags & SEN_STR_REMOVEBLANK;
  if (!(nstr->norm = SEN_MALLOC(ds + 1))) {
    return sen_memory_exhausted;
  }
  if (nstr->flags & SEN_STR_WITH_CHECKS) {
    if (!(nstr->checks = SEN_MALLOC(ds * sizeof(int16_t) + 1))) {
      SEN_FREE(nstr->norm);
      nstr->norm = NULL;
      return sen_memory_exhausted;
    }
  }
  ch = nstr->checks;
  if (nstr->flags & SEN_STR_WITH_CTYPES) {
    if (!(nstr->ctypes = SEN_MALLOC(ds + 1))) {
      if (nstr->checks) {
        SEN_FREE(nstr->checks); nstr->checks = NULL;
      }
      SEN_FREE(nstr->norm); nstr->norm = NULL;
      return sen_memory_exhausted;
    }
  }
  cp = nstr->ctypes;
  d = (unsigned char *)nstr->norm;
  de = d + ds;
  d_ = NULL;
  e = (unsigned char *)nstr->orig + size;
  for (s = s_ = (unsigned char *)nstr->orig; ; s += ls) {
    if (!(ls = sen_str_charlen_utf8(s, e))) {
      break;
    }
    if ((p = (unsigned char *)sen_nfkc_map1(s))) {
      pe = p + strlen((char *)p);
    } else {
      p = s;
      pe = p + ls;
    }
    if (d_ && (p2 = (unsigned char *)sen_nfkc_map2(d_, p))) {
      p = p2;
      pe = p + strlen((char *)p);
      if (cp) { cp--; }
      if (ch) {
        ch -= (d - d_);
        s_ = s__;
      }
      d = d_;
      length--;
    }
    for (; ; p += lp) {
      if (!(lp = sen_str_charlen_utf8(p, pe))) {
        break;
      }
      if ((*p == ' ' && removeblankp) || *p < 0x20  /* skip unprintable ascii */ ) {
        if (cp > nstr->ctypes) { *(cp - 1) |= SEN_NSTR_BLANK; }
      } else {
        if (de <= d + lp) {
          unsigned char *norm;
          ds += (ds >> 1) + lp;
          if (!(norm = SEN_REALLOC(nstr->norm, ds + 1))) {
            if (nstr->ctypes) { SEN_FREE(nstr->ctypes); nstr->ctypes = NULL; }
            if (nstr->checks) { SEN_FREE(nstr->checks); nstr->checks = NULL; }
            SEN_FREE(nstr->norm); nstr->norm = NULL;
            return sen_memory_exhausted;
          }
          de = norm + ds;
          d = norm + (d - (unsigned char *)nstr->norm);
          nstr->norm = norm;
          if (ch) {
            int16_t *checks;
            if (!(checks = SEN_REALLOC(nstr->checks, ds * sizeof(int16_t)+ 1))) {
              if (nstr->ctypes) { SEN_FREE(nstr->ctypes); nstr->ctypes = NULL; }
              SEN_FREE(nstr->checks); nstr->checks = NULL;
              SEN_FREE(nstr->norm); nstr->norm = NULL;
              return sen_memory_exhausted;
            }
            ch = checks + (ch - nstr->checks);
            nstr->checks = checks;
          }
          if (cp) {
            uint_least8_t *ctypes;
            if (!(ctypes = SEN_REALLOC(nstr->ctypes, ds + 1))) {
              SEN_FREE(nstr->ctypes); nstr->ctypes = NULL;
              if (nstr->checks) { SEN_FREE(nstr->checks); nstr->checks = NULL; }
              SEN_FREE(nstr->norm); nstr->norm = NULL;
              return sen_memory_exhausted;
            }
            cp = ctypes + (cp - nstr->ctypes);
            nstr->ctypes = ctypes;
          }
        }

        memcpy(d, p, lp);
        d_ = d;
        d += lp;
        length++;
        if (cp) { *cp++ = sen_nfkc_ctype(p); }
        if (ch) {
          size_t i;
          if (s_ == s + ls) {
            *ch++ = -1;
          } else {
            *ch++ = (int16_t)(s + ls - s_);
            s__ = s_;
            s_ = s + ls;
          }
          for (i = lp; i > 1; i--) { *ch++ = 0; }
        }
      }
    }
  }
  if (cp) { *cp = sen_str_null; }
  *d = '\0';
  nstr->length = length;
  nstr->norm_blen = (size_t)(d - (unsigned char *)nstr->norm);
  return sen_success;
}

/* Assume that nstr->flags is always zero */
inline static sen_rc
fast_normalize_utf8(sen_nstr *nstr)
{
	sen_ctx *ctx = nstr->ctx;
	const unsigned char *s, *s_, *p, *p2, *pe, *e;
	unsigned char *d, *d_, *de;
	size_t ls, lp, size = nstr->orig_blen;
	int		normed = 0;
	size_t	ds = 0;

	ds = size * 3;
	nstr->norm = SEN_MALLOC(ds + 1);
	if (nstr->norm == NULL)
		return sen_memory_exhausted;

	d = (unsigned char *)nstr->norm;
	de = d + ds;
	d_ = NULL;
	e = (unsigned char *)nstr->orig + size;

	for (s = s_ = (unsigned char *)nstr->orig; ; s += ls)
	{
		if (!(ls = fast_sen_str_charlen_utf8(s, e)))
			break;

		if ((p = (unsigned char *)sen_nfkc_map1(s)))
		{
			pe = p + strlen((char *)p);
			normed = 1;
		}
		else
		{
			p = s;
			pe = p + ls;
		}

		if (d_ && (p2 = (unsigned char *)sen_nfkc_map2(d_, p)))
		{
			p = p2;
			pe = p + strlen((char *)p);
			d = d_;
			normed = 1;
		}

		/* Skip unprintable ascii */
		if (*p < 0x20)
		{
			normed = 0;
			continue;
		}

		if (normed)
		{
			for (; ; p += lp)
			{
				if (!(lp = fast_sen_str_charlen_utf8(p, pe)))
					break;

				if (de <= d + lp)
				{
					unsigned char *norm;

					ds += (ds >> 1) + lp;
					if (!(norm = SEN_REALLOC(nstr->norm, ds + 1)))
					{
						SEN_FREE(nstr->norm); nstr->norm = NULL;
						return sen_memory_exhausted;
					}
					de = norm + ds;
					d = norm + (d - (unsigned char *)nstr->norm);
					nstr->norm = norm;
				}

				memcpy(d, p, lp);
				d_ = d;
				d += lp;
			}

			normed = 0;
		}
		else
		{
			if (de <= d + ls)
			{
				unsigned char *norm;

				ds += (ds >> 1) + ls;
				if (!(norm = SEN_REALLOC(nstr->norm, ds + 1)))
				{
					SEN_FREE(nstr->norm); nstr->norm = NULL;
					return sen_memory_exhausted;
				}
				de = norm + ds;
				d = norm + (d - (unsigned char *)nstr->norm);
				nstr->norm = norm;
			}

			memcpy(d, p, ls);
			d_ = d;
			d += ls;
		}
	}
	*d = '\0';
	nstr->norm_blen = (size_t)(d - (unsigned char *)nstr->norm);
	return sen_success;
}
#endif /* NO_NFKC */

inline static sen_rc
normalize_sjis(sen_nstr *nstr)
{
  static uint16_t hankana[] = {
    0x8140, 0x8142, 0x8175, 0x8176, 0x8141, 0x8145, 0x8392, 0x8340, 0x8342,
    0x8344, 0x8346, 0x8348, 0x8383, 0x8385, 0x8387, 0x8362, 0x815b, 0x8341,
    0x8343, 0x8345, 0x8347, 0x8349, 0x834a, 0x834c, 0x834e, 0x8350, 0x8352,
    0x8354, 0x8356, 0x8358, 0x835a, 0x835c, 0x835e, 0x8360, 0x8363, 0x8365,
    0x8367, 0x8369, 0x836a, 0x836b, 0x836c, 0x836d, 0x836e, 0x8371, 0x8374,
    0x8377, 0x837a, 0x837d, 0x837e, 0x8380, 0x8381, 0x8382, 0x8384, 0x8386,
    0x8388, 0x8389, 0x838a, 0x838b, 0x838c, 0x838d, 0x838f, 0x8393, 0x814a,
    0x814b
  };
  static unsigned char dakuten[] = {
    0x94, 0, 0, 0, 0, 0x4b, 0, 0x4d, 0, 0x4f, 0, 0x51, 0, 0x53, 0, 0x55, 0,
    0x57, 0, 0x59, 0, 0x5b, 0, 0x5d, 0, 0x5f, 0, 0x61, 0, 0, 0x64, 0, 0x66,
    0, 0x68, 0, 0, 0, 0, 0, 0, 0x6f, 0, 0, 0x72, 0, 0, 0x75, 0, 0, 0x78, 0,
    0, 0x7b
  };
  static unsigned char handaku[] = {
    0x70, 0, 0, 0x73, 0, 0, 0x76, 0, 0, 0x79, 0, 0, 0x7c
  };
  int16_t *ch;
  sen_ctx *ctx = nstr->ctx;
  const unsigned char *s, *s_;
  unsigned char *d, *d0, *d_, b, *e;
  uint_least8_t *cp, *ctypes, ctype;
  size_t size = nstr->orig_blen, length = 0;
  int removeblankp = nstr->flags & SEN_STR_REMOVEBLANK;
  if (!(nstr->norm = SEN_MALLOC(size * 2 + 1))) {
    return sen_memory_exhausted;
  }
  d0 = (unsigned char *) nstr->norm;
  if (nstr->flags & SEN_STR_WITH_CHECKS) {
    if (!(nstr->checks = SEN_MALLOC(size * 2 * sizeof(int16_t) + 1))) {
      SEN_FREE(nstr->norm);
      nstr->norm = NULL;
      return sen_memory_exhausted;
    }
  }
  ch = nstr->checks;
  if (nstr->flags & SEN_STR_WITH_CTYPES) {
    if (!(nstr->ctypes = SEN_MALLOC(size + 1))) {
      SEN_FREE(nstr->checks);
      SEN_FREE(nstr->norm);
      nstr->checks = NULL;
      nstr->norm = NULL;
      return sen_memory_exhausted;
    }
  }
  cp = ctypes = nstr->ctypes;
  e = (unsigned char *)nstr->orig + size;
  for (s = s_ = (unsigned char *) nstr->orig, d = d_ = d0; s < e; s++) {
    if ((*s & 0x80)) {
      if (0xa0 <= *s && *s <= 0xdf) {
        uint16_t c = hankana[*s - 0xa0];
        switch (c) {
        case 0x814a :
          if (d > d0 + 1 && d[-2] == 0x83
              && 0x45 <= d[-1] && d[-1] <= 0x7a && (b = dakuten[d[-1] - 0x45])) {
            *(d - 1) = b;
            if (ch) { ch[-1]++; s_++; }
            continue;
          } else {
            *d++ = c >> 8; *d = c & 0xff;
          }
          break;
        case 0x814b :
          if (d > d0 + 1 && d[-2] == 0x83
              && 0x6e <= d[-1] && d[-1] <= 0x7a && (b = handaku[d[-1] - 0x6e])) {
            *(d - 1) = b;
            if (ch) { ch[-1]++; s_++; }
            continue;
          } else {
            *d++ = c >> 8; *d = c & 0xff;
          }
          break;
        default :
          *d++ = c >> 8; *d = c & 0xff;
          break;
        }
        ctype = sen_str_katakana;
      } else {
        if ((s + 1) < e && 0x40 <= *(s + 1) && *(s + 1) <= 0xfc) {
          unsigned char c1 = *s++, c2 = *s, c3 = 0;
          if (0x81 <= c1 && c1 <= 0x87) {
            switch (c1 & 0x0f) {
            case 1 :
              switch (c2) {
              case 0x5b :
                *d++ = c1; *d = c2;
                ctype = sen_str_katakana;
                break;
              case 0x58 :
                *d++ = c1; *d = c2;
                ctype = sen_str_kanji;
                break;
              case 0x40 :
                if (removeblankp) {
                  if (cp > ctypes) { *(cp - 1) |= SEN_NSTR_BLANK; }
                  continue;
                } else {
                  *d = ' ';
                  ctype = SEN_NSTR_BLANK|sen_str_symbol;
                }
                break;
              default :
                if (0x43 <= c2 && c2 <= 0x7e && (c3 = symbol[c2 - 0x43])) {
                  *d = c3;
                  ctype = sen_str_symbol;
                } else if (0x7f <= c2 && c2 <= 0x97 && (c3 = symbol[c2 - 0x44])) {
                  *d = c3;
                  ctype = sen_str_symbol;
                } else {
                  *d++ = c1; *d = c2;
                  ctype = sen_str_others;
                }
                break;
              }
              break;
            case 2 :
              c3 = c2 - 0x1f;
              if (0x4f <= c2 && c2 <= 0x58) {
                ctype = sen_str_digit;
                *d = c2 - 0x1f;
              } else if (0x60 <= c2 && c2 <= 0x79) {
                ctype = sen_str_alpha;
                *d = c2 + 0x01;
              } else if (0x81 <= c2 && c2 <= 0x9a) {
                ctype = sen_str_alpha;
                *d = c2 - 0x20;
              } else if (0x9f <= c2 && c2 <= 0xf1) {
                *d++ = c1; *d = c2;
                ctype = sen_str_hiragana;
              } else {
                *d++ = c1; *d = c2;
                ctype = sen_str_others;
              }
              break;
            case 3 :
              if (0x40 <= c2 && c2 <= 0x96) {
                *d++ = c1; *d = c2;
                ctype = sen_str_katakana;
              } else {
                *d++ = c1; *d = c2;
                ctype = sen_str_symbol;
              }
              break;
            case 4 :
            case 7 :
              *d++ = c1; *d = c2;
              ctype = sen_str_symbol;
              break;
            default :
              *d++ = c1; *d = c2;
              ctype = sen_str_others;
              break;
            }
          } else {
            *d++ = c1; *d = c2;
            ctype = sen_str_kanji;
          }
        } else {
          /* skip invalid character */
          continue;
        }
      }
    } else {
      unsigned char c = *s;
      switch (c >> 4) {
      case 0 :
      case 1 :
        /* skip unprintable ascii */
        if (cp > ctypes) { *(cp - 1) |= SEN_NSTR_BLANK; }
        continue;
      case 2 :
        if (c == 0x20) {
          if (removeblankp) {
            if (cp > ctypes) { *(cp - 1) |= SEN_NSTR_BLANK; }
            continue;
          } else {
            *d = ' ';
            ctype = SEN_NSTR_BLANK|sen_str_symbol;
          }
        } else {
          *d = c;
          ctype = sen_str_symbol;
        }
        break;
      case 3 :
        *d = c;
        ctype = (c <= 0x39) ? sen_str_digit : sen_str_symbol;
        break;
      case 4 :
        *d = ('A' <= c) ? c + 0x20 : c;
        ctype = (c == 0x40) ? sen_str_symbol : sen_str_alpha;
        break;
      case 5 :
        *d = (c <= 'Z') ? c + 0x20 : c;
        ctype = (c <= 0x5a) ? sen_str_alpha : sen_str_symbol;
        break;
      case 6 :
        *d = c;
        ctype = (c == 0x60) ? sen_str_symbol : sen_str_alpha;
        break;
      case 7 :
        *d = c;
        ctype = (c <= 0x7a) ? sen_str_alpha : (c == 0x7f ? sen_str_others : sen_str_symbol);
        break;
      default :
        *d = c;
        ctype = sen_str_others;
        break;
      }
    }
    d++;
    length++;
    if (cp) { *cp++ = ctype; }
    if (ch) {
      *ch++ = (int16_t)(s + 1 - s_);
      s_ = s + 1;
      while (++d_ < d) { *ch++ = 0; }
    }
  }
  if (cp) { *cp = sen_str_null; }
  *d = '\0';
  nstr->length = length;
  nstr->norm_blen = (size_t)(d - (unsigned char *)nstr->norm);
  return sen_success;
}

inline static sen_rc
normalize_none(sen_nstr *nstr)
{
  int16_t *ch;
  sen_ctx *ctx = nstr->ctx;
  const unsigned char *s, *s_, *e;
  unsigned char *d, *d0, *d_;
  uint_least8_t *cp, *ctypes, ctype;
  size_t size = nstr->orig_blen, length = 0;
  int removeblankp = nstr->flags & SEN_STR_REMOVEBLANK;
  if (!(nstr->norm = SEN_MALLOC(size + 1))) {
    return sen_memory_exhausted;
  }
  d0 = (unsigned char *) nstr->norm;
  if (nstr->flags & SEN_STR_WITH_CHECKS) {
    if (!(nstr->checks = SEN_MALLOC(size * sizeof(int16_t) + 1))) {
      SEN_FREE(nstr->norm);
      nstr->norm = NULL;
      return sen_memory_exhausted;
    }
  }
  ch = nstr->checks;
  if (nstr->flags & SEN_STR_WITH_CTYPES) {
    if (!(nstr->ctypes = SEN_MALLOC(size + 1))) {
      SEN_FREE(nstr->checks);
      SEN_FREE(nstr->norm);
      nstr->checks = NULL;
      nstr->norm = NULL;
      return sen_memory_exhausted;
    }
  }
  cp = ctypes = nstr->ctypes;
  e = (unsigned char *)nstr->orig + size;
  for (s = s_ = (unsigned char *) nstr->orig, d = d_ = d0; s < e; s++) {
    unsigned char c = *s;
    switch (c >> 4) {
    case 0 :
    case 1 :
      /* skip unprintable ascii */
      if (cp > ctypes) { *(cp - 1) |= SEN_NSTR_BLANK; }
      continue;
    case 2 :
      if (c == 0x20) {
        if (removeblankp) {
          if (cp > ctypes) { *(cp - 1) |= SEN_NSTR_BLANK; }
          continue;
        } else {
          *d = ' ';
          ctype = SEN_NSTR_BLANK|sen_str_symbol;
        }
      } else {
        *d = c;
        ctype = sen_str_symbol;
      }
      break;
    case 3 :
      *d = c;
      ctype = (c <= 0x39) ? sen_str_digit : sen_str_symbol;
      break;
    case 4 :
      *d = ('A' <= c) ? c + 0x20 : c;
      ctype = (c == 0x40) ? sen_str_symbol : sen_str_alpha;
      break;
    case 5 :
      *d = (c <= 'Z') ? c + 0x20 : c;
      ctype = (c <= 0x5a) ? sen_str_alpha : sen_str_symbol;
      break;
    case 6 :
      *d = c;
      ctype = (c == 0x60) ? sen_str_symbol : sen_str_alpha;
      break;
    case 7 :
      *d = c;
      ctype = (c <= 0x7a) ? sen_str_alpha : (c == 0x7f ? sen_str_others : sen_str_symbol);
      break;
    default :
      *d = c;
      ctype = sen_str_others;
      break;
    }
    d++;
    length++;
    if (cp) { *cp++ = ctype; }
    if (ch) {
      *ch++ = (int16_t)(s + 1 - s_);
      s_ = s + 1;
      while (++d_ < d) { *ch++ = 0; }
    }
  }
  if (cp) { *cp = sen_str_null; }
  *d = '\0';
  nstr->length = length;
  nstr->norm_blen = (size_t)(d - (unsigned char *)nstr->norm);
  return sen_success;
}

/* use cp1252 as latin1 */
inline static sen_rc
normalize_latin1(sen_nstr *nstr)
{
  int16_t *ch;
  sen_ctx *ctx = nstr->ctx;
  const unsigned char *s, *s_, *e;
  unsigned char *d, *d0, *d_;
  uint_least8_t *cp, *ctypes, ctype;
  size_t size = strlen(nstr->orig), length = 0;
  int removeblankp = nstr->flags & SEN_STR_REMOVEBLANK;
  if (!(nstr->norm = SEN_MALLOC(size + 1))) {
    return sen_memory_exhausted;
  }
  d0 = (unsigned char *) nstr->norm;
  if (nstr->flags & SEN_STR_WITH_CHECKS) {
    if (!(nstr->checks = SEN_MALLOC(size * sizeof(int16_t) + 1))) {
      SEN_FREE(nstr->norm);
      nstr->norm = NULL;
      return sen_memory_exhausted;
    }
  }
  ch = nstr->checks;
  if (nstr->flags & SEN_STR_WITH_CTYPES) {
    if (!(nstr->ctypes = SEN_MALLOC(size + 1))) {
      SEN_FREE(nstr->checks);
      SEN_FREE(nstr->norm);
      nstr->checks = NULL;
      nstr->norm = NULL;
      return sen_memory_exhausted;
    }
  }
  cp = ctypes = nstr->ctypes;
  e = (unsigned char *)nstr->orig + size;
  for (s = s_ = (unsigned char *) nstr->orig, d = d_ = d0; s < e; s++) {
    unsigned char c = *s;
    switch (c >> 4) {
    case 0 :
    case 1 :
      /* skip unprintable ascii */
      if (cp > ctypes) { *(cp - 1) |= SEN_NSTR_BLANK; }
      continue;
    case 2 :
      if (c == 0x20) {
        if (removeblankp) {
          if (cp > ctypes) { *(cp - 1) |= SEN_NSTR_BLANK; }
          continue;
        } else {
          *d = ' ';
          ctype = SEN_NSTR_BLANK|sen_str_symbol;
        }
      } else {
        *d = c;
        ctype = sen_str_symbol;
      }
      break;
    case 3 :
      *d = c;
      ctype = (c <= 0x39) ? sen_str_digit : sen_str_symbol;
      break;
    case 4 :
      *d = ('A' <= c) ? c + 0x20 : c;
      ctype = (c == 0x40) ? sen_str_symbol : sen_str_alpha;
      break;
    case 5 :
      *d = (c <= 'Z') ? c + 0x20 : c;
      ctype = (c <= 0x5a) ? sen_str_alpha : sen_str_symbol;
      break;
    case 6 :
      *d = c;
      ctype = (c == 0x60) ? sen_str_symbol : sen_str_alpha;
      break;
    case 7 :
      *d = c;
      ctype = (c <= 0x7a) ? sen_str_alpha : (c == 0x7f ? sen_str_others : sen_str_symbol);
      break;
    case 8 :
      if (c == 0x8a || c == 0x8c || c == 0x8e) {
        *d = c + 0x10;
        ctype = sen_str_alpha;
      } else {
        *d = c;
        ctype = sen_str_symbol;
      }
      break;
    case 9 :
      if (c == 0x9a || c == 0x9c || c == 0x9e || c == 0x9f) {
        *d = (c == 0x9f) ? c + 0x60 : c;
        ctype = sen_str_alpha;
      } else {
        *d = c;
        ctype = sen_str_symbol;
      }
      break;
    case 0x0c :
      *d = c + 0x20;
      ctype = sen_str_alpha;
      break;
    case 0x0d :
      *d = (c == 0xd7 || c == 0xdf) ? c : c + 0x20;
      ctype = (c == 0xd7) ? sen_str_symbol : sen_str_alpha;
      break;
    case 0x0e :
      *d = c;
      ctype = sen_str_alpha;
      break;
    case 0x0f :
      *d = c;
      ctype = (c == 0xf7) ? sen_str_symbol : sen_str_alpha;
      break;
    default :
      *d = c;
      ctype = sen_str_others;
      break;
    }
    d++;
    length++;
    if (cp) { *cp++ = ctype; }
    if (ch) {
      *ch++ = (int16_t)(s + 1 - s_);
      s_ = s + 1;
      while (++d_ < d) { *ch++ = 0; }
    }
  }
  if (cp) { *cp = sen_str_null; }
  *d = '\0';
  nstr->length = length;
  nstr->norm_blen = (size_t)(d - (unsigned char *)nstr->norm);
  return sen_success;
}

inline static sen_rc
normalize_koi8r(sen_nstr *nstr)
{
  int16_t *ch;
  sen_ctx *ctx = nstr->ctx;
  const unsigned char *s, *s_, *e;
  unsigned char *d, *d0, *d_;
  uint_least8_t *cp, *ctypes, ctype;
  size_t size = strlen(nstr->orig), length = 0;
  int removeblankp = nstr->flags & SEN_STR_REMOVEBLANK;
  if (!(nstr->norm = SEN_MALLOC(size + 1))) {
    return sen_memory_exhausted;
  }
  d0 = (unsigned char *) nstr->norm;
  if (nstr->flags & SEN_STR_WITH_CHECKS) {
    if (!(nstr->checks = SEN_MALLOC(size * sizeof(int16_t) + 1))) {
      SEN_FREE(nstr->norm);
      nstr->norm = NULL;
      return sen_memory_exhausted;
    }
  }
  ch = nstr->checks;
  if (nstr->flags & SEN_STR_WITH_CTYPES) {
    if (!(nstr->ctypes = SEN_MALLOC(size + 1))) {
      SEN_FREE(nstr->checks);
      SEN_FREE(nstr->norm);
      nstr->checks = NULL;
      nstr->norm = NULL;
      return sen_memory_exhausted;
    }
  }
  cp = ctypes = nstr->ctypes;
  e = (unsigned char *)nstr->orig + size;
  for (s = s_ = (unsigned char *) nstr->orig, d = d_ = d0; s < e; s++) {
    unsigned char c = *s;
    switch (c >> 4) {
    case 0 :
    case 1 :
      /* skip unprintable ascii */
      if (cp > ctypes) { *(cp - 1) |= SEN_NSTR_BLANK; }
      continue;
    case 2 :
      if (c == 0x20) {
        if (removeblankp) {
          if (cp > ctypes) { *(cp - 1) |= SEN_NSTR_BLANK; }
          continue;
        } else {
          *d = ' ';
          ctype = SEN_NSTR_BLANK|sen_str_symbol;
        }
      } else {
        *d = c;
        ctype = sen_str_symbol;
      }
      break;
    case 3 :
      *d = c;
      ctype = (c <= 0x39) ? sen_str_digit : sen_str_symbol;
      break;
    case 4 :
      *d = ('A' <= c) ? c + 0x20 : c;
      ctype = (c == 0x40) ? sen_str_symbol : sen_str_alpha;
      break;
    case 5 :
      *d = (c <= 'Z') ? c + 0x20 : c;
      ctype = (c <= 0x5a) ? sen_str_alpha : sen_str_symbol;
      break;
    case 6 :
      *d = c;
      ctype = (c == 0x60) ? sen_str_symbol : sen_str_alpha;
      break;
    case 7 :
      *d = c;
      ctype = (c <= 0x7a) ? sen_str_alpha : (c == 0x7f ? sen_str_others : sen_str_symbol);
      break;
    case 0x0a :
      *d = c;
      ctype = (c == 0xa3) ? sen_str_alpha : sen_str_others;
      break;
    case 0x0b :
      if (c == 0xb3) {
        *d = c - 0x10;
        ctype = sen_str_alpha;
      } else {
        *d = c;
        ctype = sen_str_others;
      }
      break;
    case 0x0c :
    case 0x0d :
      *d = c;
      ctype = sen_str_alpha;
      break;
    case 0x0e :
    case 0x0f :
      *d = c - 0x20;
      ctype = sen_str_alpha;
      break;
    default :
      *d = c;
      ctype = sen_str_others;
      break;
    }
    d++;
    length++;
    if (cp) { *cp++ = ctype; }
    if (ch) {
      *ch++ = (int16_t)(s + 1 - s_);
      s_ = s + 1;
      while (++d_ < d) { *ch++ = 0; }
    }
  }
  if (cp) { *cp = sen_str_null; }
  *d = '\0';
  nstr->length = length;
  nstr->norm_blen = (size_t)(d - (unsigned char *)nstr->norm);
  return sen_success;
}

sen_nstr *
sen_nstr_open(const char *str, size_t str_len, sen_encoding encoding, int flags)
{
  sen_rc rc;
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  sen_nstr *nstr;
  if (!str) { return NULL; }
  if (!(nstr = SEN_MALLOC(sizeof(sen_nstr)))) {
    SEN_LOG(sen_log_alert, "memory allocation on sen_fakenstr_open failed !");
    return NULL;
  }
  nstr->orig = str;
  nstr->orig_blen = str_len;
  nstr->norm = NULL;
  nstr->norm_blen = 0;
  nstr->checks = NULL;
  nstr->ctypes = NULL;
  nstr->encoding = encoding;
  nstr->flags = flags;
  nstr->ctx = ctx;
  switch (encoding) {
  case sen_enc_euc_jp :
    rc = normalize_euc(nstr);
    break;
  case sen_enc_utf8 :
#ifdef NO_NFKC
    rc = normalize_none(nstr);
#else /* NO_NFKC */
    rc = normalize_utf8(nstr);
#endif /* NO_NFKC */
    break;
  case sen_enc_sjis :
    rc = normalize_sjis(nstr);
    break;
  case sen_enc_latin1 :
    rc = normalize_latin1(nstr);
    break;
  case sen_enc_koi8r :
    rc = normalize_koi8r(nstr);
    break;
  default :
    rc = normalize_none(nstr);
    break;
  }
  if (rc) {
    sen_nstr_close(nstr);
    return NULL;
  }
  return nstr;
}

/* Assume that current encoding is UTF8 */
sen_nstr *
fast_sen_nstr_open(const char *str, size_t str_len)
{
	sen_ctx *ctx = &sen_gctx;
	sen_nstr *nstr = NULL;

	if (!str)
		return NULL;

	nstr = SEN_MALLOC(sizeof(sen_nstr));
	if (nstr == NULL)
	{
		SEN_LOG(sen_log_alert, "memory allocation on sen_fakenstr_open failed !");
		return NULL;
	}

	nstr->orig = str;
	nstr->orig_blen = str_len;
	nstr->norm = NULL;
	nstr->norm_blen = 0;
	nstr->checks = NULL;
	nstr->ctypes = NULL;
	nstr->encoding = sen_enc_utf8;
	nstr->flags = 0;
	nstr->ctx = ctx;

	if (fast_normalize_utf8(nstr))
	{
		sen_nstr_close(nstr);
		nstr = NULL;
	}

	return nstr;
}

sen_nstr *
sen_fakenstr_open(const char *str, size_t str_len, sen_encoding encoding, int flags)
{
  /* TODO: support SEN_STR_REMOVEBLANK flag and ctypes */
  sen_nstr *nstr;
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */

  if (!(nstr = SEN_MALLOC(sizeof(sen_nstr)))) {
    SEN_LOG(sen_log_alert, "memory allocation on sen_fakenstr_open failed !");
    return NULL;
  }
  if (!(nstr->norm = SEN_MALLOC(str_len + 1))) {
    SEN_LOG(sen_log_alert, "memory allocation for keyword on sen_snip_add_cond failed !");
    SEN_FREE(nstr);
    return NULL;
  }
  nstr->orig = str;
  nstr->orig_blen = str_len;
  memcpy(nstr->norm, str, str_len);
  nstr->norm[str_len] = '\0';
  nstr->norm_blen = str_len;
  nstr->ctypes = NULL;
  nstr->flags = flags;
  nstr->ctx = ctx;

  if (flags & SEN_STR_WITH_CHECKS) {
    int16_t f = 0;
    unsigned char c;
    size_t i;
    if (!(nstr->checks = (int16_t *) SEN_MALLOC(sizeof(int16_t) * str_len))) {
      SEN_FREE(nstr->norm);
      SEN_FREE(nstr);
      return NULL;
    }
    switch (encoding) {
    case sen_enc_euc_jp:
      for (i = 0; i < str_len; i++) {
        if (!f) {
          c = (unsigned char) str[i];
          f = ((c >= 0xa1U && c <= 0xfeU) || c == 0x8eU ? 2 : (c == 0x8fU ? 3 : 1)
            );
          nstr->checks[i] = f;
        } else {
          nstr->checks[i] = 0;
        }
        f--;
      }
      break;
    case sen_enc_sjis:
      for (i = 0; i < str_len; i++) {
        if (!f) {
          c = (unsigned char) str[i];
          f = (c >= 0x81U && ((c <= 0x9fU) || (c >= 0xe0U && c <= 0xfcU)) ? 2 : 1);
          nstr->checks[i] = f;
        } else {
          nstr->checks[i] = 0;
        }
        f--;
      }
      break;
    case sen_enc_utf8:
      for (i = 0; i < str_len; i++) {
        if (!f) {
          c = (unsigned char) str[i];
          f = (c & 0x80U ? (c & 0x20U ? (c & 0x10U ? 4 : 3)
                           : 2)
               : 1);
          nstr->checks[i] = f;
        } else {
          nstr->checks[i] = 0;
        }
        f--;
      }
      break;
    default:
      for (i = 0; i < str_len; i++) {
        nstr->checks[i] = 1;
      }
      break;
    }
  }
  else {
    nstr->checks = NULL;
  }
  return nstr;
}

sen_rc
sen_nstr_close(sen_nstr *nstr)
{
  if (nstr) {
    sen_ctx *ctx = nstr->ctx;
    if (nstr->norm) { SEN_FREE(nstr->norm); }
    if (nstr->ctypes) { SEN_FREE(nstr->ctypes); }
    if (nstr->checks) { SEN_FREE(nstr->checks); }
    SEN_FREE(nstr);
    return sen_success;
  } else {
    return sen_invalid_argument;
  }
}

static const char *sen_enc_string[] = {
  "default",
  "none",
  "euc_jp",
  "utf8",
  "sjis",
  "latin1",
  "koi8r"
};

const char *
sen_enctostr(sen_encoding enc)
{
  if (enc < (sizeof(sen_enc_string) / sizeof(char *))) {
    return sen_enc_string[enc];
  } else {
    return "unknown";
  }
}

sen_encoding
sen_strtoenc(const char *str)
{
  sen_encoding e = sen_enc_euc_jp;
  int i = sizeof(sen_enc_string) / sizeof(sen_enc_string[0]);
  while (i--) {
    if (!strcmp(str, sen_enc_string[i])) {
      e = (sen_encoding)i;
    }
  }
  return e;
}

size_t
sen_str_len(const char *str, sen_encoding encoding, const char **last)
{
  size_t len, tlen;
  const char *p = NULL;
  for (len = 0; ; len++) {
    p = str;
    if (!(tlen = sen_str_charlen(str, encoding))) {
      break;
    }
    str += tlen;
  }
  if (last) { *last = p; }
  return len;
}

int
sen_isspace(const char *str, sen_encoding encoding)
{
  const unsigned char *s = (const unsigned char *) str;
  if (!s) { return 0; }
  switch (s[0]) {
  case ' ' :
  case '\f' :
  case '\n' :
  case '\r' :
  case '\t' :
  case '\v' :
    return 1;
  case 0x81 :
    if (encoding == sen_enc_sjis && s[1] == 0x40) { return 2; }
    break;
  case 0xA1 :
    if (encoding == sen_enc_euc_jp && s[1] == 0xA1) { return 2; }
    break;
  case 0xE3 :
    if (encoding == sen_enc_utf8 && s[1] == 0x80 && s[2] == 0x80) { return 3; }
    break;
  default :
    break;
  }
  return 0;
}

int
sen_atoi(const char *nptr, const char *end, const char **rest)
{
  /* FIXME: INT_MIN is not supported */
  const char *p = nptr;
  int v = 0, t, n = 0, o = 0;
  if (p < end && *p == '-') {
    p++;
    n = 1;
    o = 1;
  }
  while (p < end && *p >= '0' && *p <= '9') {
    t = v * 10 + (*p - '0');
    if (t < v) { v =0; break; }
    v = t;
    o = 0;
    p++;
  }
  if (rest) { *rest = o ? nptr : p; }
  return n ? -v : v;
}

unsigned int
sen_atoui(const char *nptr, const char *end, const char **rest)
{
  unsigned int v = 0, t;
  while (nptr < end && *nptr >= '0' && *nptr <= '9') {
    t = v * 10 + (*nptr - '0');
    if (t < v) { v = 0; break; }
    v = t;
    nptr++;
  }
  if (rest) { *rest = nptr; }
  return v;
}

int64_t
sen_atoll(const char *nptr, const char *end, const char **rest)
{
  /* FIXME: INT_MIN is not supported */
  const char *p = nptr;
  int n = 0, o = 0;
  int64_t v = 0, t;
  if (p < end && *p == '-') {
    p++;
    n = 1;
    o = 1;
  }
  while (p < end && *p >= '0' && *p <= '9') {
    t = v * 10 + (*p - '0');
    if (t < v) { v = 0; break; }
    v = t;
    o = 0;
    p++;
  }
  if (rest) { *rest = o ? nptr : p; }
  return n ? -v : v;
}

unsigned int
sen_htoui(const char *nptr, const char *end, const char **rest)
{
  unsigned int v = 0, t;
  while (nptr < end) {
    switch (*nptr) {
    case '0' :
    case '1' :
    case '2' :
    case '3' :
    case '4' :
    case '5' :
    case '6' :
    case '7' :
    case '8' :
    case '9' :
      t = v * 16 + (*nptr++ - '0');
      break;
    case 'a' :
    case 'b' :
    case 'c' :
    case 'd' :
    case 'e' :
    case 'f' :
      t = v * 16 + (*nptr++ - 'a') + 10;
      break;
    case 'A' :
    case 'B' :
    case 'C' :
    case 'D' :
    case 'E' :
    case 'F' :
      t = v * 16 + (*nptr++ - 'A') + 10;
      break;
    default :
      v = 0; goto exit;
    }
    if (t < v) { v = 0; goto exit; }
    v = t;
  }
exit :
  if (rest) { *rest = nptr; }
  return v;
}

void
sen_str_itoh(unsigned int i, char *p, unsigned int len)
{
  static const char *hex = "0123456789ABCDEF";
  p += len;
  *p-- = '\0';
  while (len--) {
    *p-- = hex[i & 0xf];
    i >>= 4;
  }
}

sen_rc
sen_str_itoa(int i, char *p, char *end, char **rest)
{
  /* FIXME: INT_MIN is not supported */
  char *q;
  if (p >= end) { return sen_invalid_argument; }
  if (i < 0) {
    *p++ = '-';
    i = -i;
  }
  q = p;
  do {
    if (p >= end) { return sen_invalid_argument; }
    *p++ = i % 10 + '0';
  } while ((i /= 10) > 0);
  if (rest) { *rest = p; }
  for (p--; q < p; q++, p--) {
    char t = *q;
    *q = *p;
    *p = t;
  }
  return sen_success;
}

sen_rc
sen_str_lltoa(int64_t i, char *p, char *end, char **rest)
{
  /* FIXME: INT_MIN is not supported */
  char *q;
  if (p >= end) { return sen_invalid_argument; }
  if (i < 0) {
    *p++ = '-';
    i = -i;
  }
  q = p;
  do {
    if (p >= end) { return sen_invalid_argument; }
    *p++ = i % 10 + '0';
  } while ((i /= 10) > 0);
  if (rest) { *rest = p; }
  for (p--; q < p; q++, p--) {
    char t = *q;
    *q = *p;
    *p = t;
  }
  return sen_success;
}

#define I2B(i) \
 ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[(i) & 0x3f])

#define B2I(b) \
 (((b) < '+' || 'z' < (b)) ? 0xff : "\x3e\xff\xff\xff\x3f\x34\x35\x36\x37\x38\x39\x3a\x3b\x3c\x3d\xff\xff\xff\xff\xff\xff\xff\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\xff\xff\xff\xff\xff\xff\x1a\x1b\x1c\x1d\x1e\x1f\x20\x21\x22\x23\x24\x25\x26\x27\x28\x29\x2a\x2b\x2c\x2d\x2e\x2f\x30\x31\x32\x33"[(b) - '+'])

#define MASK 0x34d34d34

char *
sen_str_itob(sen_id id, char *p)
{
  id ^= MASK;
  *p++ = I2B(id >> 24);
  *p++ = I2B(id >> 18);
  *p++ = I2B(id >> 12);
  *p++ = I2B(id >> 6);
  *p++ = I2B(id);
  return p;
}

sen_id
sen_str_btoi(char *b)
{
  uint8_t i;
  sen_id id = 0;
  int len = 5;
  while (len--) {
    char c = *b++;
    if ((i = B2I(c)) == 0xff) { return 0; }
    id = (id << 6) + i;
  }
  return id ^ MASK;
}

int
sen_str_tok(char *str, size_t str_len, char delim, char **tokbuf, int buf_size, char **rest)
{
  char **tok = tokbuf, **tok_end = tokbuf + buf_size;
  if (buf_size > 0) {
    char *str_end = str + str_len;
    for (;;str++) {
      if (str == str_end) {
        *tok++ = str;
        break;
      }
      if (delim == *str) {
        *str = '\0';
        *tok++ = str;
        if (tok == tok_end) { break; }
      }
    }
  }
  if (rest) { *rest = str; }
  return tok - tokbuf;
}

inline static void
op_getopt_flag(int *flags, const sen_str_getopt_opt *o,
               int argc, char * const argv[], int *i)
{
  switch (o->op) {
    case getopt_op_none:
      break;
    case getopt_op_on:
      *flags |= o->flag;
      break;
    case getopt_op_off:
      *flags &= ~o->flag;
      break;
    case getopt_op_update:
      *flags = o->flag;
      break;
    default:
      return;
  }
  if (o->arg) {
    if (++(*i) < argc) {
      *o->arg = argv[*i];
    } else {
      /* TODO: error */
    }
  }
}

int
sen_str_getopt(int argc, char * const argv[], const sen_str_getopt_opt *opts,
               int *flags)
{
  int i;
  for (i = 1; i < argc; i++) {
    const char * v = argv[i];
    if (*v == '-') {
      const sen_str_getopt_opt *o;
      int found;
      if (*++v == '-') {
        found = 0;
        for (o = opts; o->opt != '\0' || o->longopt != NULL; o++) {
          if (o->longopt && !strcmp(v, o->longopt)) {
            op_getopt_flag(flags, o, argc, argv, &i);
            found = 1;
            break;
          }
        }
        if (!found) { goto exit; }
      } else {
        const char *p;
        for (p = v; *p; p++) {
          found = 0;
          for (o = opts; o->opt != '\0' || o->longopt != NULL; o++) {
            if (o->opt && *p == o->opt) {
              op_getopt_flag(flags, o, argc, argv, &i);
              found = 1;
              break;
            }
          }
          if (!found) { goto exit; }
        }
      }
    } else {
      break;
    }
  }
  return i;
exit:
  fprintf(stderr, "cannot recognize option '%s'.\n", argv[i]);
  return -1;
}

#define UNIT_SIZE (1 << 12)
#define UNIT_MASK (UNIT_SIZE - 1)

int sen_rbuf_margin_size = 0;

sen_rc
sen_rbuf_init(sen_rbuf *buf, size_t size)
{
  buf->head = NULL;
  buf->curr = NULL;
  buf->tail = NULL;
  return size ? sen_rbuf_resize(buf, size) : sen_success;
}

sen_rc
sen_rbuf_resize(sen_rbuf *buf, size_t newsize)
{
  char *head;
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  newsize += sen_rbuf_margin_size + 1;
  newsize = (newsize + (UNIT_MASK)) & ~UNIT_MASK;
  head = buf->head - (buf->head ? sen_rbuf_margin_size : 0);
  if (!(head = SEN_REALLOC(head, newsize))) { return sen_memory_exhausted; }
  buf->curr = head + sen_rbuf_margin_size + SEN_RBUF_VSIZE(buf);
  buf->head = head + sen_rbuf_margin_size;
  buf->tail = head + newsize;
  return sen_success;
}

sen_rc
sen_rbuf_reinit(sen_rbuf *buf, size_t size)
{
  SEN_RBUF_REWIND(buf);
  return sen_rbuf_resize(buf, size);
}

sen_rc
sen_rbuf_write(sen_rbuf *buf, const char *str, size_t len)
{
  sen_rc rc = sen_success;
  if (SEN_RBUF_REST(buf) < len) {
    if ((rc = sen_rbuf_resize(buf, SEN_RBUF_VSIZE(buf) + len))) { return rc; }
  }
  memcpy(buf->curr, str, len);
  buf->curr += len;
  return rc;
}

sen_rc
sen_rbuf_reserve(sen_rbuf *buf, size_t len)
{
  sen_rc rc = sen_success;
  if (SEN_RBUF_REST(buf) < len) {
    if ((rc = sen_rbuf_resize(buf, SEN_RBUF_VSIZE(buf) + len))) { return rc; }
  }
  return rc;
}

sen_rc
sen_rbuf_space(sen_rbuf *buf, size_t len)
{
  sen_rc rc = sen_rbuf_reserve(buf, len);
  if (!rc) { buf->curr += len; }
  return rc;
}

sen_rc
sen_rbuf_itoa(sen_rbuf *buf, int i)
{
  sen_rc rc = sen_success;
  while (sen_str_itoa(i, buf->curr, buf->tail, &buf->curr)) {
    if ((rc = sen_rbuf_resize(buf, SEN_RBUF_WSIZE(buf) + UNIT_SIZE))) { return rc; }
  }
  return rc;
}

sen_rc
sen_rbuf_lltoa(sen_rbuf *buf, int64_t i)
{
  sen_rc rc = sen_success;
  while (sen_str_lltoa(i, buf->curr, buf->tail, &buf->curr)) {
    if ((rc = sen_rbuf_resize(buf, SEN_RBUF_WSIZE(buf) + UNIT_SIZE))) { return rc; }
  }
  return rc;
}

sen_rc
sen_rbuf_ftoa(sen_rbuf *buf, double d)
{
  size_t len = 32;
  sen_rc rc = sen_success;
  if (SEN_RBUF_REST(buf) < len) {
    if ((rc = sen_rbuf_resize(buf, SEN_RBUF_VSIZE(buf) + len))) { return rc; }
  }
  switch (fpclassify(d)) {
  CASE_FP_NAN
    SEN_RBUF_PUTS(buf, "#<nan>");
    break;
  CASE_FP_INFINITE
    SEN_RBUF_PUTS(buf, d > 0 ? "#i1/0" : "#i-1/0");
    break;
  default :
    len = sprintf(buf->curr, "%#.15g", d);
    if (buf->curr[len - 1] == '.') {
      buf->curr += len;
      SEN_RBUF_PUTC(buf, '0');
    } else {
      char *p, *q;
      buf->curr[len] = '\0';
      if ((p = strchr(buf->curr, 'e'))) {
        for (q = p; *(q - 2) != '.' && *(q - 1) == '0'; q--) { len--; }
        memmove(q, p, buf->curr + len - q);
      } else {
        for (q = buf->curr + len; *(q - 2) != '.' && *(q - 1) == '0'; q--) { len--; }
      }
      buf->curr += len;
    }
    break;
  }
  return rc;
}

sen_rc
sen_rbuf_itoh(sen_rbuf *buf, int i)
{
  size_t len = 8;
  sen_rc rc = sen_success;
  if (SEN_RBUF_REST(buf) < len) {
    if ((rc = sen_rbuf_resize(buf, SEN_RBUF_VSIZE(buf) + len))) { return rc; }
  }
  sen_str_itoh(i, buf->curr, len);
  buf->curr += len;
  return rc;
}

sen_rc
sen_rbuf_itob(sen_rbuf *buf, sen_id id)
{
  size_t len = 5;
  sen_rc rc = sen_success;
  if (SEN_RBUF_REST(buf) < len) {
    if ((rc = sen_rbuf_resize(buf, SEN_RBUF_VSIZE(buf) + len))) { return rc; }
  }
  sen_str_itob(id, buf->curr);
  buf->curr += len;
  return rc;
}

void
sen_rbuf_str_esc(sen_rbuf *buf, const char *s, int len, sen_encoding encoding)
{
  const char *e;
  unsigned int l;
  if (len < 0) { len = strlen(s); }
  SEN_RBUF_PUTC(buf, '"');
  for (e = s + len; s < e; s += l) {
    if (!(l = sen_str_charlen_nonnull(s, e, encoding))) { break; }
    if (l == 1) {
      switch (*s) {
      case '\n' :
        sen_rbuf_write(buf, "\\n", 2);
        break;
      case '"' :
        sen_rbuf_write(buf, "\\\"", 2);
        break;
      case '\\' :
        sen_rbuf_write(buf, "\\\\", 2);
        break;
      default :
        SEN_RBUF_PUTC(buf, *s);
      }
    } else {
      sen_rbuf_write(buf, s, l);
    }
  }
  SEN_RBUF_PUTC(buf, '"');
}

sen_rc
sen_rbuf_fin(sen_rbuf *buf)
{
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  if (buf->head) {
    SEN_REALLOC(buf->head - sen_rbuf_margin_size, 0);
    buf->head = NULL;
  }
  return sen_success;
}

struct _sen_lbuf_node {
  sen_lbuf_node *next;
  size_t size;
  char val[1];
};

sen_rc
sen_lbuf_init(sen_lbuf *buf)
{
  buf->head = NULL;
  buf->tail = &buf->head;
  return sen_success;
}

void *
sen_lbuf_add(sen_lbuf *buf, size_t size)
{
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  sen_lbuf_node *node = SEN_MALLOC(size + (size_t)(&((sen_lbuf_node *)0)->val));
  if (!node) { return NULL;  }
  node->next = NULL;
  node->size = size;
  *buf->tail = node;
  buf->tail = &node->next;
  return node->val;
}

sen_rc
sen_lbuf_fin(sen_lbuf *buf)
{
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  sen_lbuf_node *cur, *next;
  for (cur = buf->head; cur; cur = next) {
    next = cur->next;
    SEN_FREE(cur);
  }
  return sen_success;
}

sen_rc
sen_substring(char **str, char **str_end, int start, int end, sen_encoding encoding)
{
  int i;
  size_t l;
  char *s = *str, *e = *str_end;
  for (i = 0; s < e; i++, s += l) {
    if (i == start) { *str = s; }
    if (!(l = sen_str_charlen_nonnull(s, e, encoding))) {
      return sen_invalid_argument;
    }
    if (i == end) {
      *str_end = s;
      break;
    }
  }
  return sen_success;
}

int
sen_str_normalize(const char *str, unsigned int str_len,
                  sen_encoding encoding, int flags,
                  char *nstrbuf, int buf_size)
{
  int len;
  sen_nstr *nstr;
  if (!(nstr = sen_nstr_open(str, str_len, encoding, flags))) {
    return -1;
  }
  /* if the buffer size is short to store for the normalized string,
     the required size is returned
     (to inform the caller to cast me again). */
  len = (int)nstr->norm_blen;
  if (buf_size > len) {
    memcpy(nstrbuf, nstr->norm, len + 1);
  } else if (buf_size == len) {
    /* NB: non-NULL-terminated */
    memcpy(nstrbuf, nstr->norm, len);
  }
  sen_nstr_close(nstr);
  return len;
}

/* Assume that current encoding is UTF-8 */
int
fast_sen_str_normalize(const char *str, unsigned int str_len,
					   char *nstrbuf, int buf_size)
{
	int len;
	sen_nstr *nstr;

	if (!(nstr = fast_sen_nstr_open(str, str_len)))
		return -1;

	/*
	 * If the buffer size is short to store for the normalized string,
     * the required size is returned (to inform the caller to cast me again).
	 */
	len = (int)nstr->norm_blen;

	if (buf_size > len)
		memcpy(nstrbuf, nstr->norm, len + 1);
	else if (buf_size == len)
		/* NB: non-NULL-terminated */
		memcpy(nstrbuf, nstr->norm, len);
	sen_nstr_close(nstr);
	return len;
}
