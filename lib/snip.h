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
#ifndef SEN_SNIP_H
#define SEN_SNIP_H

#ifndef SENNA_H
#include "senna_in.h"
#endif /* SENNA_H */

#ifndef SEN_STR_H
#include "str.h"
#endif /* SEN_STR_H */

#define ASIZE                   256U
#define MAX_SNIP_TAG_COUNT      512U
#define MAX_SNIP_COND_COUNT     32U
#define MAX_SNIP_RESULT_COUNT   16U

#ifdef  __cplusplus
extern "C"
{
#endif

#define SNIPCOND_NONSTOP 0
#define SNIPCOND_STOP    1
#define SNIPCOND_ACROSS  2

#define SEN_QUERY_SCAN_ALLOCCONDS 0x0002

typedef struct _snip_cond
{
  /* initial parameters */
  const char *opentag;
  const char *closetag;
  size_t opentag_len;
  size_t closetag_len;
  sen_nstr *keyword;

  /* Tuned BM pre */
  size_t bmBc[ASIZE];
  size_t shift;

  /* Tuned BM temporal result */
  size_t found;
  size_t last_found;
  size_t start_offset;
  size_t end_offset;
  size_t found_alpha_head;

  /* search result */
  int count;

  /* stop flag */
  int_least8_t stopflag;
} snip_cond;

typedef struct
{
  size_t start_offset;
  size_t end_offset;
  snip_cond *cond;
} _snip_tag_result;

typedef struct
{
  size_t start_offset;
  size_t end_offset;
  unsigned int first_tag_result_idx;
  unsigned int last_tag_result_idx;
  unsigned int tag_count;
} _snip_result;

struct _sen_snip
{
  sen_encoding encoding;
  int flags;
  size_t width;
  unsigned int max_results;
  const char *defaultopentag;
  const char *defaultclosetag;
  size_t defaultopentag_len;
  size_t defaultclosetag_len;

  sen_snip_mapping *mapping;

  snip_cond cond[MAX_SNIP_COND_COUNT];
  unsigned int cond_len;

  unsigned int tag_count;
  unsigned int snip_count;

  const char *string;
  sen_nstr *nstr;

  _snip_result snip_result[MAX_SNIP_RESULT_COUNT];
  _snip_tag_result tag_result[MAX_SNIP_TAG_COUNT];

  size_t max_tagged_len;
};

sen_rc sen_snip_cond_init(snip_cond *sc, const char *keyword, unsigned int keyword_len,
                       sen_encoding enc, int flags);
void sen_snip_cond_reinit(snip_cond *cond);
sen_rc sen_snip_cond_close(snip_cond *cond);
void sen_bm_tunedbm(snip_cond *cond, sen_nstr *object, int flags);

#ifdef __cplusplus
}
#endif

#endif /* SEN_SNIP_H */
