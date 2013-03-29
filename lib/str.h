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
#ifndef SEN_STR_H
#define SEN_STR_H

#ifndef SENNA_H
#include "senna_in.h"
#endif /* SENNA_H */

#ifdef	__cplusplus
extern "C" {
#endif

enum {
  sen_str_null = 0,
  sen_str_alpha,
  sen_str_digit,
  sen_str_symbol,
  sen_str_hiragana,
  sen_str_katakana,
  sen_str_kanji,
  sen_str_others
};

typedef struct {
  const char *orig;
  size_t orig_blen;
  char *norm;
  size_t norm_blen;
  uint_least8_t *ctypes;
  int16_t *checks;
  size_t length;
  int flags;
  sen_ctx *ctx;
  sen_encoding encoding;
} sen_nstr;

typedef enum {
  getopt_op_none = 0,
  getopt_op_on,
  getopt_op_off,
  getopt_op_update
} sen_str_getopt_op;

typedef struct {
  const char opt; /* ends opt == 0 && longopt == NULL */
  const char *longopt;
  char **arg; /* if NULL, no arg are required */
  int flag;
  sen_str_getopt_op op;
} sen_str_getopt_opt;

int sen_str_get_prefix_order(const char *str);

sen_nstr *sen_nstr_open(const char *str, size_t str_len, sen_encoding encoding, int flags);
sen_nstr *fast_sen_nstr_open(const char *str, size_t str_len);
sen_nstr *sen_fakenstr_open(const char *str, size_t str_len, sen_encoding encoding, int flags);
sen_rc sen_nstr_close(sen_nstr *nstr);

size_t sen_str_charlen_nonnull(const char *str, const char *end, sen_encoding encoding);
size_t sen_str_len(const char *str, sen_encoding encoding, const char **last);
sen_rc sen_str_fin(void);

#define SEN_NSTR_BLANK 0x80
#define SEN_NSTR_ISBLANK(c) (c & 0x80)
#define SEN_NSTR_CTYPE(c) (c & 0x7f)

int sen_isspace(const char *s, sen_encoding encoding);
int sen_atoi(const char *nptr, const char *end, const char **rest);
unsigned int sen_atoui(const char *nptr, const char *end, const char **rest);
unsigned int sen_htoui(const char *nptr, const char *end, const char **rest);
int64_t sen_atoll(const char *nptr, const char *end, const char **rest);
sen_rc sen_str_itoa(int i, char *p, char *end, char **rest);
sen_rc sen_str_lltoa(int64_t i, char *p, char *end, char **rest);
const char *sen_enctostr(sen_encoding enc);
sen_encoding sen_strtoenc(const char *str);

void sen_str_itoh(unsigned int i, char *p, unsigned int len);
int sen_str_tok(char *str, size_t str_len, char delim, char **tokbuf, int buf_size, char **rest);
int sen_str_getopt(int argc, char * const argv[], const sen_str_getopt_opt *opts, int *flags);

typedef struct _sen_rbuf sen_rbuf;

extern int sen_rbuf_margin_size;

struct _sen_rbuf {
  char *head;
  char *curr;
  char *tail;
};

typedef struct _sen_lbuf_node sen_lbuf_node;

typedef struct {
  sen_lbuf_node *head;
  sen_lbuf_node **tail;
} sen_lbuf;

sen_rc sen_rbuf_init(sen_rbuf *buf, size_t size);
sen_rc sen_rbuf_reinit(sen_rbuf *buf, size_t size);
sen_rc sen_rbuf_resize(sen_rbuf *buf, size_t newsize);
sen_rc sen_rbuf_write(sen_rbuf *buf, const char *str, size_t len);
sen_rc sen_rbuf_reserve(sen_rbuf *buf, size_t len);
sen_rc sen_rbuf_space(sen_rbuf *buf, size_t len);
sen_rc sen_rbuf_itoa(sen_rbuf *buf, int i);
sen_rc sen_rbuf_lltoa(sen_rbuf *buf, int64_t i);
sen_rc sen_rbuf_ftoa(sen_rbuf *buf, double d);
sen_rc sen_rbuf_itoh(sen_rbuf *buf, int i);
sen_rc sen_rbuf_itob(sen_rbuf *buf, sen_id id);
sen_rc sen_rbuf_fin(sen_rbuf *buf);
void sen_rbuf_str_esc(sen_rbuf *buf, const char *s, int len, sen_encoding encoding);

#define SEN_RBUF_PUTS(buf,str) (sen_rbuf_write((buf), (str), strlen(str)))
#define SEN_RBUF_PUTC(buf,c) { char _c = (c); sen_rbuf_write((buf), &_c, 1); }
#define SEN_RBUF_REWIND(buf) ((buf)->curr = (buf)->head)
#define SEN_RBUF_WSIZE(buf) ((buf)->tail - (buf)->head)
#define SEN_RBUF_REST(buf) ((buf)->tail - (buf)->curr)
#define SEN_RBUF_VSIZE(buf) ((buf)->curr - (buf)->head)
#define SEN_RBUF_EMPTYP(buf) ((buf)->curr == (buf)->head)

sen_rc sen_lbuf_init(sen_lbuf *buf);
void *sen_lbuf_add(sen_lbuf *buf, size_t size);
sen_rc sen_lbuf_fin(sen_lbuf *buf);

char *sen_str_itob(sen_id id, char *p);
sen_id sen_str_btoi(char *b);

sen_rc sen_substring(char **str, char **str_end, int start, int end, sen_encoding encoding);

void sen_logger_fin(void);

#ifdef __cplusplus
}
#endif

#endif /* SEN_STR_H */
