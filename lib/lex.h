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
#ifndef NO_MECAB
#include <mecab.h>
#endif /* NO_MECAB */

#ifndef SEN_LEX_H
#define SEN_LEX_H

#ifndef SENNA_H
#include "senna_in.h"
#endif /* SENNA_H */

#ifndef SEN_SYM_H
#include "sym.h"
#endif /* SEN_SYM_H */

#ifndef SEN_STR_H
#include "str.h"
#endif /* SEN_STR_H */

#ifdef	__cplusplus
extern "C" {
#endif

#define SEN_LEX_ADD 1
#define SEN_LEX_UPD 2

typedef struct {
  sen_sym *sym;
  unsigned char *buf;
  const unsigned char *orig;
  const unsigned char *next;
  unsigned char *token;
  uint32_t tlen;
  sen_nstr *nstr;
#ifndef NO_MECAB
  mecab_t *mecab;
#endif /* NO_MECAB */
  int32_t pos;
  int32_t len;
  uint32_t skip;
  uint32_t tail;
  uint32_t offset;
  uint8_t flags;
  uint8_t status;
  uint8_t uni_alpha;
  uint8_t uni_digit;
  uint8_t uni_symbol;
  uint8_t force_prefix;
  sen_encoding encoding;
} sen_lex;

enum {
  sen_lex_doing = 0,
  sen_lex_done,
  sen_lex_not_found
};

sen_rc sen_lex_init(void);
sen_lex *sen_lex_open(sen_sym *sym, const char *str, size_t str_len, uint8_t flags);
sen_rc sen_lex_next(sen_lex *ng);
sen_rc sen_lex_close(sen_lex *ng);
sen_rc sen_lex_fin(void);
sen_rc sen_lex_validate(sen_sym *sym);

#ifdef __cplusplus
}
#endif

#endif /* SEN_LEX_H */
