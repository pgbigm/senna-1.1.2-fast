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
#ifndef SEN_INV_H
#define SEN_INV_H

#ifndef SENNA_H
#include "senna_in.h"
#endif /* SENNA_H */

#ifndef SEN_SET_H
#include "set.h"
#endif /* SEN_SET_H */

#ifndef SEN_IO_H
#include "io.h"
#endif /* SEN_IO_H */

#ifndef SEN_STORE_H
#include "store.h"
#endif /* SEN_STORE_H */

#ifdef  __cplusplus
extern "C" {
#endif

#define SEN_INV_MAX_SEGMENT 0x4000

struct sen_inv_header;

struct _sen_inv_pos {
  struct _sen_inv_pos *next;
  uint32_t pos;
};

struct _sen_inv_updspec {
  uint32_t rid;
  uint32_t sid;
  int32_t score;
  int32_t tf;                 /* number of postings successfully stored to index */
  int32_t atf;                /* actual number of postings */
  struct _sen_inv_pos *pos;
  struct _sen_inv_pos *tail;
  sen_vgram_vnode *vnodes;
};

typedef struct _sen_inv_updspec sen_inv_updspec;

sen_inv *sen_inv_create(const char *path, sen_sym *lexicon,
                         uint32_t initial_n_segments);
sen_inv *sen_inv_open(const char *path, sen_sym *lexicon);
sen_rc sen_inv_close(sen_inv *inv);
sen_rc sen_inv_remove(const char *path);
sen_rc sen_inv_info(sen_inv *inv, uint64_t *seg_size, uint64_t *chunk_size);
sen_rc sen_inv_update(sen_inv *inv, uint32_t key, sen_inv_updspec *u, sen_set *h,
                      int hint);
sen_rc sen_inv_delete(sen_inv *inv, uint32_t key, sen_inv_updspec *u, sen_set *h);
uint32_t sen_inv_initial_n_segments(sen_inv *inv);

sen_inv_updspec *sen_inv_updspec_open(uint32_t rid, uint32_t sid);
sen_rc sen_inv_updspec_close(sen_inv_updspec *u);
sen_rc sen_inv_updspec_add(sen_inv_updspec *u, int pos, int32_t weight);
int sen_inv_updspec_cmp(sen_inv_updspec *a, sen_inv_updspec *b);

uint32_t sen_inv_estimate_size(sen_inv *inv, uint32_t key);

void sen_inv_seg_expire(sen_inv *inv, int32_t threshold);

typedef struct {
  sen_id rid;
  uint32_t sid;
  uint32_t pos;
  uint32_t tf;
  uint32_t score;
  uint32_t rest;
} sen_inv_posting;

typedef struct {
  sen_inv *inv;
  sen_inv_posting pc;
  sen_inv_posting pb;
  sen_inv_posting *post;
  uint8_t *cp;
  uint8_t *cpp;
  uint8_t *cpe;
  uint8_t *bp;
  sen_io_win iw;
  struct sen_inv_buffer *buf;
  uint16_t stat;
  uint16_t nextb;
  uint16_t buffer_pseg;
  uint16_t with_pos;
  int flags;
} sen_inv_cursor;

#define SEN_INV_CURSOR_CMP(c1,c2) \
  (((c1)->post->rid > (c2)->post->rid) || \
   (((c1)->post->rid == (c2)->post->rid) && \
    (((c1)->post->sid > (c2)->post->sid) || \
     (((c1)->post->sid == (c2)->post->sid) && \
      ((c1)->post->pos > (c2)->post->pos)))))

sen_inv_cursor *sen_inv_cursor_open(sen_inv *inv, uint32_t key, int with_pos);
sen_inv_cursor *sen_inv_cursor_openv1(sen_inv *inv, uint32_t key);
sen_rc sen_inv_cursor_openv2(sen_inv_cursor **cursors, int ncursors);
sen_rc sen_inv_cursor_next(sen_inv_cursor *c);
sen_rc sen_inv_cursor_next_pos(sen_inv_cursor *c);
sen_rc sen_inv_cursor_close(sen_inv_cursor *c);
uint32_t sen_inv_max_section(sen_inv *inv);

int sen_inv_check(sen_inv *inv);
const char *sen_inv_path(sen_inv *inv);

#ifdef __cplusplus
}
#endif

#endif /* SEN_INV_H */
