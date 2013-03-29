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
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>

#include "str.h"
#include "sym.h"
#include "inv.h"

struct _sen_inv {
  uint8_t v08p;
  sen_io *seg;
  sen_io *chunk;
  sen_sym *lexicon;
  struct sen_inv_header *header;
  uint32_t total_chunk_size;
  uint16_t ainfo[SEN_INV_MAX_SEGMENT];
  uint16_t binfo[SEN_INV_MAX_SEGMENT];
  uint16_t amax;
  uint16_t bmax;
};

typedef struct {
  sen_inv *inv;
  sen_inv_posting pc;
  sen_inv_posting pb;
  sen_inv_posting *post;
  uint8_t *cp;
  uint8_t *cpe;
  uint8_t *bp;
  sen_io_win iw;
  struct sen_inv_buffer *buf;
  uint16_t stat;
  uint16_t nextb;
  uint32_t buffer_pos;
} sen_inv_cursor08;

struct sen_inv_header {
  char idstr[16];
  uint32_t initial_n_segments;
  // todo: initial_n_segments should be uint16_t
  // uint32_t total_chunk_size; todo: should be added when index format changed
  uint16_t segments[SEN_INV_MAX_SEGMENT];
  // todo: exchange segments and ainfo,binfo.
  uint8_t chunks[1]; /* dummy */
};

#define SEN_INV_IDSTR "SENNA:INV:00.00"
#define SEN_INV_SEGMENT_SIZE 0x40000
/* SEN_INV_MAX_SEGMENT == 0x10000 >> 2 */
#define SEN_INV_CHUNK_SIZE   0x40000
#define N_CHUNKS_PER_FILE (SEN_IO_FILE_SIZE / SEN_INV_CHUNK_SIZE)
#define W_OF_SEGMENT 18
#define W_OF_ARRAY (W_OF_SEGMENT - 2)
#define ARRAY_MASK_IN_A_SEGMENT ((SEN_INV_SEGMENT_SIZE >> 2) - 1)
#define BUFFER_MASK_IN_A_SEGMENT (SEN_INV_SEGMENT_SIZE - 1)
#define CHUNK_NOT_ASSIGNED 0xffffffff
#define SEG_NOT_ASSIGNED 0xffff

#define SEGMENT_ARRAY 0x8000
#define SEGMENT_BUFFER 0x4000
#define SEGMENT_MASK (SEN_INV_MAX_SEGMENT - 1)

#define BIT11_01(x) ((x >> 1) & 0x7ff)
#define BIT31_12(x) (x >> 12)

#define SEN_INV_INITIAL_N_SEGMENTS 512
#define MAX_CHUNK_RATIO 64

#define NEXT_ADDR(p) (((byte *)(p)) + sizeof *(p))

/* segment */

inline static sen_rc
segment_get(sen_inv *inv, uint16_t type, uint16_t segno, uint16_t *pseg)
{
  uint16_t s, i, empty = SEN_INV_MAX_SEGMENT;
  for (i = 0; i < SEN_INV_MAX_SEGMENT; i++) {
    if ((s = inv->header->segments[i])) {
      if (s == (type | segno)) { break; }
    } else {
      if (empty == SEN_INV_MAX_SEGMENT) { empty = i; }
    }
  }
  if (i == SEN_INV_MAX_SEGMENT) {
    void *p = NULL;
    if (empty == SEN_INV_MAX_SEGMENT) { return sen_memory_exhausted; }
    inv->header->segments[empty] = type | segno;
    SEN_IO_SEG_REF(inv->seg, empty, p);
    if (!p) { return sen_memory_exhausted; }
    memset(p, 0, SEN_INV_SEGMENT_SIZE);
    SEN_IO_SEG_UNREF(inv->seg, empty);
    *pseg = empty;
  } else {
    *pseg = i;
  }
  return sen_success;
}

inline static sen_rc
segment_new(sen_inv *inv, uint16_t type, uint16_t *segno)
{
  sen_rc rc = sen_success;
  uint16_t s, i, seg, empty = SEN_INV_MAX_SEGMENT;
  char used[SEN_INV_MAX_SEGMENT];
  memset(used, 0, SEN_INV_MAX_SEGMENT);
  for (i = 0; i < SEN_INV_MAX_SEGMENT; i++) {
    if ((s = inv->header->segments[i])) {
      if (s & type) { used[s & SEGMENT_MASK]++; }
    } else {
      if (empty == SEN_INV_MAX_SEGMENT) { empty = i; }
    }
  }
  if (empty == SEN_INV_MAX_SEGMENT) { return sen_memory_exhausted; }
  if (segno && *segno < SEN_INV_MAX_SEGMENT) {
    if (used[*segno]) { return sen_invalid_argument; }
    seg = *segno;
  } else {
    for (seg = 0; used[seg]; seg++) ;
  }
  inv->header->segments[empty] = type | seg;
  switch (type) {
  case SEGMENT_ARRAY :
    inv->ainfo[seg] = empty;
    if (seg > inv->amax) { inv->amax = seg; }
    break;
  case SEGMENT_BUFFER :
    inv->binfo[seg] = empty;
    if (seg > inv->bmax) { inv->bmax = seg; }
    break;
  }
  if (segno) { *segno = seg; }
  return rc;
}

inline static sen_rc
load_all_segments(sen_inv *inv)
{
  sen_rc rc = sen_success;
  uint16_t s, seg, amax = 0, bmax = 0;
  char used[SEN_INV_MAX_SEGMENT];
  memset(used, 0, SEN_INV_MAX_SEGMENT);
  for (seg = 0; seg < SEN_INV_MAX_SEGMENT; seg++) {
    if (!(s = inv->header->segments[seg])) { continue; }
    if (s & SEGMENT_ARRAY) {
      used[s & SEGMENT_MASK] |= 2;
      inv->ainfo[s & SEGMENT_MASK] = seg;
    }
    if (s & SEGMENT_BUFFER) {
      used[s & SEGMENT_MASK] |= 1;
      inv->binfo[s & SEGMENT_MASK] = seg;
    }
  }
  for (seg = 0; seg < SEN_INV_MAX_SEGMENT; seg++) {
    if ((used[seg] & 2)) { amax = seg; } else { inv->ainfo[seg] = SEG_NOT_ASSIGNED; }
    if ((used[seg] & 1)) { bmax = seg; } else { inv->binfo[seg] = SEG_NOT_ASSIGNED; }
  }
  inv->amax = amax;
  inv->bmax = bmax;
  return rc;
}

void
sen_inv_seg_expire08(sen_inv *inv)
{
  uint32_t expire_threshold = inv->header->initial_n_segments * 2;
  if (inv->seg->nmaps > expire_threshold) {
    uint16_t seg;
    for (seg = inv->bmax; seg; seg--) {
      uint16_t pseg = inv->binfo[seg];
      if (pseg != SEG_NOT_ASSIGNED) {
        sen_io_mapinfo *info = &inv->seg->maps[pseg];
        uint32_t *pnref = &inv->seg->nrefs[pseg];
        if (info->map && !*pnref) {
          sen_io_seg_expire(inv->seg, pseg, 100);
          if (inv->seg->nmaps <= expire_threshold) { return; }
        }
      }
    }
    for (seg = inv->amax; seg; seg--) {
      uint16_t pseg = inv->ainfo[seg];
      if (pseg != SEG_NOT_ASSIGNED) {
        sen_io_mapinfo *info = &inv->seg->maps[pseg];
        uint32_t *pnref = &inv->seg->nrefs[pseg];
        if (info->map && !*pnref) {
          sen_io_seg_expire(inv->seg, pseg, 100);
          if (inv->seg->nmaps <= expire_threshold) { return; }
        }
      }
    }
  }
}

/* chunk */

inline static sen_rc
chunk_new(sen_inv *inv, uint32_t *res, uint32_t size)
{
  int i, j;
  uint32_t n = size / SEN_INV_CHUNK_SIZE;
  int max_chunk = inv->header->initial_n_segments * MAX_CHUNK_RATIO;
  uint32_t base_seg = sen_io_base_seg(inv->chunk);
  if (n * SEN_INV_CHUNK_SIZE < size) { n++; }
  for (i = 0, j = -1; i < max_chunk; i++) {
    if (inv->header->chunks[i]) {
      j = i;
    } else {
      if (i - j == n) {
        if (res) { *res = j + 1; }
        while (j < i) {
          inv->header->chunks[++j] = 1;
        }
        return sen_success;
      }
      if ((i + base_seg)/ N_CHUNKS_PER_FILE !=
          (i + base_seg + 1) / N_CHUNKS_PER_FILE) { j = i; }
    }
  }
  SEN_LOG(sen_log_crit, "index full. set bigger value to initial_n_segments. current value = %d",
          inv->header->initial_n_segments);
  return sen_memory_exhausted;
}

inline static sen_rc
chunk_free(sen_inv *inv, int start, uint32_t size)
{
  uint32_t i, n = size / SEN_INV_CHUNK_SIZE;
  if (n * SEN_INV_CHUNK_SIZE < size) { n++; }
  // sen_log("chunk_free start=%d size=%d(%d)", start, size, n);
  for (i = 0; i < n; i++) {
    inv->header->chunks[start + i] = 0;
  }
  return sen_success;
}

/* buffer */

typedef struct {
  uint32_t tid;
  uint32_t size_in_chunk;
  uint32_t pos_in_chunk;
  uint16_t size_in_buffer;
  uint16_t pos_in_buffer;
} buffer_term;

typedef struct {
  uint16_t step;
  uint16_t jump;
} buffer_rec;

typedef struct {
  uint32_t chunk;
  uint32_t chunk_size;
  uint32_t buffer_free;
  uint16_t nterms;
  uint16_t nterms_void;
} buffer_header;

struct sen_inv_buffer {
  buffer_header header;
  buffer_term terms[(SEN_INV_SEGMENT_SIZE - sizeof(buffer_header))/sizeof(buffer_term)];
};

typedef struct sen_inv_buffer buffer;

inline static sen_rc
buffer_open(sen_inv *inv, uint32_t pos, buffer_term **bt, buffer **b)
{
  byte *p = NULL;
  uint16_t lseg = (uint16_t) (pos >> W_OF_SEGMENT);
  uint16_t pseg = inv->binfo[lseg];
  if (pseg == SEG_NOT_ASSIGNED ||
      inv->header->segments[pseg] != (SEGMENT_BUFFER|lseg)) {
    load_all_segments(inv);
    pseg = inv->binfo[lseg];
    if (pseg == SEG_NOT_ASSIGNED ||
        inv->header->segments[pseg] != (SEGMENT_BUFFER|lseg)) {
      return sen_invalid_argument;
    }
  }
  SEN_IO_SEG_REF(inv->seg, pseg, p);
  if (!p) { return sen_memory_exhausted; }
  if (b) { *b = (buffer *)p; }
  if (bt) { *bt = (buffer_term *)(p + (pos & BUFFER_MASK_IN_A_SEGMENT)); }
  return sen_success;
}

inline static sen_rc
buffer_close(sen_inv *inv, uint32_t pos)
{
  uint16_t pseg = inv->binfo[pos >> W_OF_SEGMENT];
  if (pseg >= SEN_INV_MAX_SEGMENT) { return sen_invalid_argument; }
  SEN_IO_SEG_UNREF(inv->seg, pseg);
  return sen_success;
}

inline static int
buffer_open_if_capable(sen_inv *inv, int32_t seg, int size, buffer **b)
{
  int res, nterms;
  uint32_t pos = ((uint32_t) seg) * SEN_INV_SEGMENT_SIZE;
  if (buffer_open(inv, pos, NULL, b)) { return 0; }
  nterms = (*b)->header.nterms - (*b)->header.nterms_void;
  res = ((nterms < 4096 || 
          (inv->total_chunk_size >> ((nterms >> 8) - 6)) > (*b)->header.chunk_size) &&
         ((*b)->header.buffer_free >= size + sizeof(buffer_term)));
  if (!res) { buffer_close(inv, pos); }
  return res;
}

inline static sen_rc
buffer_new(sen_inv *inv, int size, uint32_t *pos,
           buffer_term **bt, buffer_rec **br, buffer **bp, int hint)
{
  buffer *b;
  uint16_t nseg0 = inv->header->initial_n_segments;
  uint16_t seg, offset, seg0 = hint % nseg0;
  uint16_t segmax = (uint16_t) (inv->total_chunk_size >> 7) + nseg0;
  if (size + sizeof(buffer_header) + sizeof(buffer_term) > SEN_INV_SEGMENT_SIZE) {
    return sen_invalid_argument;
  }
  // load_all_segments(inv); todo: ainfo and binfo should be inside the header
  for (seg = seg0; seg < segmax; seg += nseg0) {
    if (inv->binfo[seg] == SEG_NOT_ASSIGNED) { break; }
    if (buffer_open_if_capable(inv, seg, size, &b)) { goto exit; }    
  }
  if (seg >= segmax) {
    for (seg = (seg0 + 1) % nseg0; seg != seg0; seg = (seg + 1) % nseg0) {
      if (inv->binfo[seg] == SEG_NOT_ASSIGNED) { break; }
      if (buffer_open_if_capable(inv, seg, size, &b)) { goto exit; }    
    }
    if (seg == seg0) {
      for (seg = nseg0; seg < SEN_INV_MAX_SEGMENT; seg++) {
        if (inv->binfo[seg] == SEG_NOT_ASSIGNED) { break; }
        if (buffer_open_if_capable(inv, seg, size, &b)) { goto exit; }    
      }
    }
  }
  SEN_LOG(sen_log_debug, "inv=%p new seg=%d", inv, seg);
  if (segment_new(inv, SEGMENT_BUFFER, &seg) || 
      buffer_open(inv, seg * SEN_INV_SEGMENT_SIZE, NULL, &b)) {
    return sen_memory_exhausted;
  }
  memset(b, 0, SEN_INV_SEGMENT_SIZE);
  b->header.buffer_free = SEN_INV_SEGMENT_SIZE - sizeof(buffer_header);
  b->header.chunk = CHUNK_NOT_ASSIGNED;
  b->header.chunk_size = 0;
exit :
  if (b->header.nterms_void) {
    for (offset = 0; offset < b->header.nterms; offset++) {
      if (!b->terms[offset].tid) { break; }
    }
    if (offset == b->header.nterms) {
      SEN_LOG(sen_log_notice, "inconsistent buffer(%d)", seg);
      b->header.nterms_void = 0;
      b->header.nterms++;
      b->header.buffer_free -= size + sizeof(buffer_term);
    } else {
      b->header.nterms_void--;
      b->header.buffer_free -= size;
    }
  } else {
    offset = b->header.nterms++;
    b->header.buffer_free -= size + sizeof(buffer_term);
  }
  *pos = seg * SEN_INV_SEGMENT_SIZE
    + sizeof(buffer_header) + sizeof(buffer_term) * offset;
  *bt = &b->terms[offset];
  *br = (buffer_rec *)(((byte *)&b->terms[b->header.nterms]) + b->header.buffer_free);
  *bp = b;
  return sen_success;
}

typedef struct {
  uint32_t rid;
  uint32_t sid;
} docid;

#define BUFFER_REC_DEL(r)  ((r)->jump = 1)
#define BUFFER_REC_DELETED(r) ((r)->jump == 1)

#define BUFFER_REC_AT(b,pos) ((buffer_rec *)(b) + (pos))
#define BUFFER_REC_POS(b,rec) ((uint16_t)((rec) - (buffer_rec *)(b)))

inline static void
buffer_term_dump(buffer *b, buffer_term *bt)
{
  int pos, rid, sid;
  uint8_t *p;
  buffer_rec *r;
  SEN_LOG(sen_log_debug,
          "b=(%x %u %u %u)", b->header.chunk, b->header.chunk_size, b->header.buffer_free, b->header.nterms);
  SEN_LOG(sen_log_debug,
          "bt=(%u %u %u %u %u)", bt->tid, bt->size_in_chunk, bt->pos_in_chunk, bt->size_in_buffer, bt->pos_in_buffer);
  for (pos = bt->pos_in_buffer; pos; pos = r->step) {
    r = BUFFER_REC_AT(b, pos);
    p = NEXT_ADDR(r);
    SEN_B_DEC(rid, p);
    SEN_B_DEC(sid, p);
    SEN_LOG(sen_log_debug, "%d=(%d:%d),(%d:%d)", pos, r->jump, r->step, rid, sid);
  }
}

static buffer_term *tmp_bt;

inline static sen_rc
check_jump(buffer *b, buffer_rec *r, int j)
{
  uint16_t i = BUFFER_REC_POS(b, r);
  uint8_t *p;
  buffer_rec *r2;
  docid id, id2;
  if (!j) { return sen_success; }
  p = NEXT_ADDR(r);
  SEN_B_DEC(id.rid, p);
  SEN_B_DEC(id.sid, p);
  if (j == 1) {
    SEN_LOG(sen_log_debug, "deleting! %d(%d:%d)", i, id.rid, id.sid);
    return sen_success;
  }
  r2 = BUFFER_REC_AT(b, j);
  p = NEXT_ADDR(r2);
  SEN_B_DEC(id2.rid, p);
  SEN_B_DEC(id2.sid, p);
  if (r2->step == i) {
    SEN_LOG(sen_log_emerg, "cycle! %d(%d:%d)<->%d(%d:%d)", i, id.rid, id.sid, j, id2.rid, id2.sid);
    buffer_term_dump(b, tmp_bt);
    return sen_other_error;
  }
  if (id2.rid < id.rid || (id2.rid == id.rid && id2.sid <= id.sid)) {
    SEN_LOG(sen_log_crit, "invalid jump! %d(%d:%d)(%d:%d)->%d(%d:%d)(%d:%d)", i, r->jump, r->step, id.rid, id.sid, j, r2->jump, r2->step, id2.rid, id2.sid);
    return sen_other_error;
  }
  return sen_success;
}

inline static sen_rc
set_jump_r(buffer *b, buffer_rec *from, int to)
{
  int i, j, max_jump = 100;
  buffer_rec *r, *r2;
  for (r = from, j = to; j > 1 && max_jump--; r = BUFFER_REC_AT(b, r->step)) {
    r2 = BUFFER_REC_AT(b, j);
    if (r == r2) { break; }
    if (BUFFER_REC_DELETED(r2)) { break; }
    if (j == (i = r->jump)) { break; }
    if (j == r->step) { break; }
    if (check_jump(b, r, j)) { return sen_other_error; }
    r->jump = j;
    j = i;
    if (!r->step) { return sen_other_error; }
  }
  return sen_success;
}

#define GET_NUM_BITS(x,n) { \
  n = x; \
  n = (n & 0x55555555) + ((n >> 1) & 0x55555555); \
  n = (n & 0x33333333) + ((n >> 2) & 0x33333333); \
  n = (n & 0x0F0F0F0F) + ((n >> 4) & 0x0F0F0F0F); \
  n = (n & 0x00FF00FF) + ((n >> 8) & 0x00FF00FF); \
  n = (n & 0x0000FFFF) + ((n >>16) & 0x0000FFFF); \
}

inline static sen_rc
buffer_put(buffer *b, buffer_term *bt, buffer_rec *rnew, uint8_t *bs,
           sen_inv_updspec *u, int size)
{
  uint8_t *p;
  sen_rc rc = sen_success;
  docid id_curr = {0, 0}, id_start = {0, 0}, id_post = {0, 0};
  buffer_rec *r_curr, *r_start = NULL;
  uint16_t last = 0, *lastp = &bt->pos_in_buffer, pos = BUFFER_REC_POS(b, rnew);
  int vdelta = 0, delta, delta0 = 0, vhops = 0, nhops = 0, reset = 1;

  tmp_bt = bt; // test

  memcpy(NEXT_ADDR(rnew), bs, size - sizeof(buffer_rec));
  //  sen_log("tid=%d u->rid=%d u->sid=%d", bt->tid, u->rid, u->sid);
  for (;;) {
    //    sen_log("*lastp=%d", *lastp);
    if (!*lastp) {
      rnew->step = 0;
      rnew->jump = 0;
      *lastp = pos;
      if (bt->size_in_buffer++ > 1) {
        buffer_rec *rhead = BUFFER_REC_AT(b, bt->pos_in_buffer);
        rhead->jump = pos;
        if (!(bt->size_in_buffer & 1)) {
          int n;
          buffer_rec *r = BUFFER_REC_AT(b, rhead->step), *r2;
          GET_NUM_BITS(bt->size_in_buffer, n);
          while (n-- && (r->jump > 1)) {
            r2 = BUFFER_REC_AT(b, r->jump);
            if (BUFFER_REC_DELETED(r2)) { break; }
            r = r2;
          }
          if (r != rnew) { set_jump_r(b, r, last); }
        }
      }
      break;
    }
    r_curr = BUFFER_REC_AT(b, *lastp);
    p = NEXT_ADDR(r_curr);
    SEN_B_DEC(id_curr.rid, p);
    SEN_B_DEC(id_curr.sid, p);
    if (id_curr.rid < id_post.rid ||
        (id_curr.rid == id_post.rid && id_curr.sid < id_post.sid)) {
      SEN_LOG(sen_log_emerg, "loop found!!! (%d:%d)->(%d:%d)",
              id_post.rid, id_post.sid, id_curr.rid, id_curr.sid);
      buffer_term_dump(b, bt);
      /* abandon corrupt list */
      bt->pos_in_buffer = 0;
      bt->size_in_buffer = 0;
      lastp = &bt->pos_in_buffer;
      rc = sen_invalid_format;
      continue;
    }
    id_post.rid = id_curr.rid;
    id_post.sid = id_curr.sid;
    if (u->rid < id_curr.rid || (u->rid == id_curr.rid && u->sid <= id_curr.sid)) {
      uint16_t step = *lastp, jump = r_curr->jump;
      if (u->rid == id_curr.rid) {
        if (u->sid == 0) {
          while (id_curr.rid == u->rid) {
            BUFFER_REC_DEL(r_curr);
            if (!(step = r_curr->step)) { break; }
            r_curr = BUFFER_REC_AT(b, step);
            p = NEXT_ADDR(r_curr);
            SEN_B_DEC(id_curr.rid, p);
            SEN_B_DEC(id_curr.sid, p);
          }
        } else if (u->sid == id_curr.sid) {
          BUFFER_REC_DEL(r_curr);
          step = r_curr->step;
        }
      }
      rnew->step = step;
      rnew->jump = check_jump(b, rnew, jump) ? 0 : jump;
      *lastp = pos;
      break;
    }

    if (reset) {
      r_start = r_curr;
      id_start.rid = id_curr.rid;
      id_start.sid = id_curr.sid;
      if (!(delta0 = u->rid - id_start.rid)) { delta0 = u->sid - id_start.sid; }
      nhops = 0;
      vhops = 1;
      vdelta = delta0 >> 1;
    } else {
      if (!(delta = id_curr.rid - id_start.rid)) { delta = id_curr.sid - id_start.sid; }
      if (vdelta < delta) {
        vdelta += (delta0 >> ++vhops);
        r_start = r_curr;
      }
      if (nhops > vhops) {
        set_jump_r(b, r_start, *lastp);
      } else {
        nhops++;
      }
    }

    last = *lastp;
    lastp = &r_curr->step;
    reset = 0;
    {
      uint16_t posj = r_curr->jump;
      if (posj > 1) {
        buffer_rec *rj = BUFFER_REC_AT(b, posj);
        if (!BUFFER_REC_DELETED(rj)) {
          docid idj;
          p = NEXT_ADDR(rj);
          SEN_B_DEC(idj.rid, p);
          SEN_B_DEC(idj.sid, p);
          if (idj.rid < u->rid || (idj.rid == u->rid && idj.sid < u->sid)) {
            last = posj;
            lastp = &rj->step;
          } else {
            reset = 1;
          }
        }
      }
    }
  }
  return rc;
}

/* array */

inline static uint32_t *
array_at(sen_inv *inv, uint32_t id)
{
  byte *p = NULL;
  uint16_t seg, pseg;
  if (id > SEN_SYM_MAX_ID) { return NULL; }
  seg = id >> W_OF_ARRAY;
  if ((pseg = inv->ainfo[seg]) == SEG_NOT_ASSIGNED) {
    load_all_segments(inv);
    if ((pseg = inv->ainfo[seg]) == SEG_NOT_ASSIGNED) {
      return NULL;
    }
  }
  SEN_IO_SEG_REF(inv->seg, pseg, p);
  if (!p) { return NULL; }
  return (uint32_t *)(p + (id & ARRAY_MASK_IN_A_SEGMENT) * sizeof(uint32_t));
}

inline static uint32_t *
array_get(sen_inv *inv, uint32_t id)
{
  byte *p = NULL;
  uint16_t seg, pseg;
  if (id > SEN_SYM_MAX_ID) { return NULL; }
  seg = id >> W_OF_ARRAY;
  if ((pseg = inv->ainfo[seg]) == SEG_NOT_ASSIGNED) {
    if (segment_get(inv, SEGMENT_ARRAY, seg, &pseg)) { return NULL; }
    inv->ainfo[seg] = pseg;
    if (seg > inv->amax) { inv->amax = seg; }
  }
  SEN_IO_SEG_REF(inv->seg, pseg, p)
  if (!p) { return NULL; }
  return (uint32_t *)(p + (id & ARRAY_MASK_IN_A_SEGMENT) * sizeof(uint32_t));
}

inline static void
array_unref(sen_inv *inv, uint32_t id)
{
  SEN_IO_SEG_UNREF(inv->seg, inv->ainfo[id >> W_OF_ARRAY]);
}

inline static uint8_t *
encode_rec(sen_inv_updspec *u, unsigned int *size, int deletep)
{
  intptr_t s;
  uint8_t *br, *p;
  struct _sen_inv_pos *pp;
  uint32_t lpos, tf = deletep ? 0 : u->tf;
  if (!(br = SEN_GMALLOC((u->tf + 4) * 5))) {
    return NULL;
  }
  p = br;
  SEN_B_ENC(u->rid, p);
  SEN_B_ENC(u->sid, p);
  if (!u->score) {
    SEN_B_ENC(tf * 2, p);
  } else {
    SEN_B_ENC(tf * 2 + 1, p);
    SEN_B_ENC(u->score, p);
  }
  for (lpos = 0, pp = u->pos; pp && tf--; lpos = pp->pos, pp = pp->next) {
    SEN_B_ENC(pp->pos - lpos, p);
  }
  s = (p - br) + sizeof(buffer_rec);
  *size = (unsigned int) ((s + 0x03) & ~0x03);
  return br;
}

inline static int
sym_deletable(uint32_t tid, sen_set *h)
{
  sen_inv_updspec **u;
  if (!h) { return 1; }
  if (!sen_set_at(h, &tid, (void **) &u)) { return 1; }
  if (!(*u)->tf || !(*u)->sid) { return 1; }
  return 0;
}

typedef struct {
  sen_inv *inv;
  sen_set *h;
} sis_deletable_arg;

static int
sis_deletable(sen_id tid, void *arg)
{
  uint32_t *a;
  sen_set *h = ((sis_deletable_arg *)arg)->h;
  sen_inv *inv = ((sis_deletable_arg *)arg)->inv;
  if ((a = array_at(inv, tid))) {
    if (*a) {
      array_unref(inv, tid);
      return 0;
    }
    array_unref(inv, tid);
  } 
  return sym_deletable(tid, h);
}

inline static void
sym_delete(sen_inv *inv, uint32_t tid, sen_set *h)
{
  sis_deletable_arg arg = {inv, h};
  if (inv->lexicon->flags & SEN_SYM_WITH_SIS) {
    sen_sym_del_with_sis(inv->lexicon, tid, sis_deletable, &arg);
    /*
    uint32_t *a;
    while ((tid = sen_sym_del_with_sis(inv->lexicon, tid))) {
      if ((a = array_at(inv, tid))) {
        if (*a) {
          array_unref(inv, tid);
          break;
        }
        array_unref(inv, tid);
      } 
      if (!sym_deletable(tid, h)) { break; }
    }
    */
  } else {
    if (sym_deletable(tid, h)) {
      sen_sym_del(inv->lexicon, _sen_sym_key(inv->lexicon, tid));
    }
  }
}

inline static sen_rc
buffer_flush(sen_inv *inv, uint32_t seg, sen_set *h)
{
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  buffer *sb, *db = NULL;
  sen_rc rc = sen_success;
  sen_io_win sw, dw;
  uint8_t *dc, *sc = NULL;
  uint16_t ss, ds;
  uint32_t scn, dcn, max_dest_chunk_size;
  ss = inv->binfo[seg];
  if (ss == SEG_NOT_ASSIGNED) { return sen_invalid_format; }
  if (buffer_open(inv, seg * SEN_INV_SEGMENT_SIZE, NULL, &sb)) {
    return sen_memory_exhausted;
  }
  for (ds = 0; inv->header->segments[ds];) {
    if (++ds == SEN_INV_MAX_SEGMENT) {
      buffer_close(inv, seg * SEN_INV_SEGMENT_SIZE);
      return sen_memory_exhausted;
    }
  }
  SEN_IO_SEG_REF(inv->seg, ds, db);
  if (!db) {
    buffer_close(inv, seg * SEN_INV_SEGMENT_SIZE);
    return sen_memory_exhausted;
  }
  memset(db, 0, SEN_INV_SEGMENT_SIZE);

  max_dest_chunk_size = sb->header.chunk_size + SEN_INV_SEGMENT_SIZE;
  if (chunk_new(inv, &dcn, max_dest_chunk_size)) {
    buffer_close(inv, seg * SEN_INV_SEGMENT_SIZE);
    SEN_IO_SEG_UNREF(inv->seg, ds);
    return sen_memory_exhausted;
  }
  //  sen_log("db=%p ds=%d sb=%p seg=%d", db, ds, sb, seg);
  if ((scn = sb->header.chunk) != CHUNK_NOT_ASSIGNED) {
    sc = sen_io_win_map(inv->chunk, ctx, &sw, scn, 0, sb->header.chunk_size, sen_io_rdwr);
    if (!sc) {
      SEN_LOG(sen_log_alert, "io_win_map(%d, %d) failed!!", scn, sb->header.chunk_size);
      buffer_close(inv, seg * SEN_INV_SEGMENT_SIZE);
      SEN_IO_SEG_UNREF(inv->seg, ds);
      chunk_free(inv, dcn, max_dest_chunk_size);
      return sen_memory_exhausted;
    }
  }
  // dc = sen_io_win_map(inv->chunk, &dw, dcn, 0, max_dest_chunk_size, sen_io_wronly);
  dc = sen_io_win_map(inv->chunk, ctx, &dw, dcn, 0, max_dest_chunk_size, sen_io_rdwr);
  if (!dc) { 
    SEN_LOG(sen_log_alert, "io_win_map(%d, %d) failed!!", dcn, max_dest_chunk_size);
    buffer_close(inv, seg * SEN_INV_SEGMENT_SIZE);
    SEN_IO_SEG_UNREF(inv->seg, ds);
    chunk_free(inv, dcn, max_dest_chunk_size);
    if (scn != CHUNK_NOT_ASSIGNED) { sen_io_win_unmap(&sw); }
    return sen_memory_exhausted;
  }
  {
    uint8_t *bp = NULL, *cp = NULL, *cpe = NULL, *dp = dc;
    uint16_t nextb;
    buffer_rec *br;
    buffer_term *bt;
    int n = sb->header.nterms;
    int nterms_void = 0;
    memcpy(db->terms, sb->terms, n * sizeof(buffer_term));
    // sen_log(" scn=%d, dcn=%d, nterms=%d", sb->header.chunk, dcn, n);
    for (bt = db->terms; n; n--, bt++) {
      docid cid = {0, 0}, lid = {0, 0}, bid = {0, 0};
      uint32_t ndf = 0, tf2, ltf2 = 0, gap;
      if (!bt->tid) {
        nterms_void++;
        continue;
      }
      if (sc) { 
        cp = sc + bt->pos_in_chunk;
        cpe = cp + bt->size_in_chunk;
      }
      nextb = bt->pos_in_buffer;
      bt->pos_in_chunk = (uint32_t)(dp - dc);
      bt->size_in_buffer = 0;
      bt->pos_in_buffer = 0;

      // sen_log("db=%p n=%d, bt=%p tid=%d bdf=%d", db, n, bt, bt->tid, bdf);

#define GETNEXTC() { \
  if (cp < cpe && cid.rid) { \
    SEN_B_DEC(tf2, cp); \
    if (tf2 & 1) { SEN_B_SKIP(cp); } \
    tf2 >>= 1; \
    while (cp < cpe && tf2--) { SEN_B_SKIP(cp); } \
  } \
  if (cp < cpe) { \
    SEN_B_DEC(gap, cp); \
    cid.rid += gap; \
    if (gap) { cid.sid = 0; } \
    SEN_B_DEC(gap, cp); \
    cid.sid += gap; \
  } else { \
    cid.rid = 0; \
  } \
}
#define PUTNEXTC() { \
  if (cid.rid) { \
    /* sen_log("srid=%d", srid); */ \
    SEN_B_DEC(tf2, cp); \
    if (tf2) { \
      if (lid.rid > cid.rid || (lid.rid == cid.rid && lid.sid >= cid.sid)) { \
        SEN_LOG(sen_log_crit, "brokenc!! (%d:%d) -> (%d:%d)", lid.rid, lid.sid, bid.rid, bid.sid); \
        rc = sen_invalid_format;\
        break; \
      } \
      ndf++; \
      gap = cid.rid - lid.rid; \
      SEN_B_ENC(gap, dp); \
      if (gap) { SEN_B_ENC(cid.sid, dp); } else { SEN_B_ENC(cid.sid - lid.sid, dp); } \
      SEN_B_ENC(tf2, dp); \
      if (tf2 & 1) { SEN_B_COPY(dp, cp); } \
      ltf2 = tf2; \
      tf2 >>= 1; \
      while (tf2--) { SEN_B_COPY(dp, cp); } \
      lid.rid = cid.rid; \
      lid.sid = cid.sid; \
    } else { \
      SEN_LOG(sen_log_crit, "invalid chunk(%d,%d)", bt->tid, cid.rid);\
      rc = sen_invalid_format;\
      break; \
    } \
  } \
  if (cp < cpe) { \
    SEN_B_DEC(gap, cp); \
    cid.rid += gap; \
    if (gap) { cid.sid = 0; } \
    SEN_B_DEC(gap, cp); \
    cid.sid += gap; \
  } else { \
    cid.rid = 0; \
  } \
  /* sen_log("gap=%d srid=%d", gap, srid); */ \
}
#define GETNEXTB() { \
  if (nextb) { \
    uint32_t lrid = bid.rid, lsid = bid.sid; \
    br = BUFFER_REC_AT(sb, nextb); \
    bp = NEXT_ADDR(br); \
    SEN_B_DEC(bid.rid, bp); \
    SEN_B_DEC(bid.sid, bp); \
    if (lrid > bid.rid || (lrid == bid.rid && lsid >= bid.sid)) { \
      SEN_LOG(sen_log_crit, "brokeng!! (%d:%d) -> (%d:%d)", lrid, lsid, bid.rid, bid.sid); \
      rc = sen_invalid_format;\
      break; \
    } \
    nextb = br->step; \
  } else { \
    bid.rid = 0; \
  } \
}
#define PUTNEXTB() { \
  if (bid.rid && bid.sid) { \
    SEN_B_DEC(tf2, bp); \
    if (tf2) { \
      /* sen_log("brid=%d", bid.rid); */ \
      if (lid.rid > bid.rid || (lid.rid == bid.rid && lid.sid >= bid.sid)) { \
        SEN_LOG(sen_log_crit, "brokenb!! (%d:%d) -> (%d:%d)", lid.rid, lid.sid, bid.rid, bid.sid); \
        rc = sen_invalid_format;\
        break; \
      } \
      ndf++; \
      gap = bid.rid - lid.rid; \
      SEN_B_ENC(gap, dp); \
      if (gap) { SEN_B_ENC(bid.sid, dp); } else { SEN_B_ENC(bid.sid - lid.sid, dp); } \
      SEN_B_ENC(tf2, dp); \
      if (tf2 & 1) { SEN_B_COPY(dp, bp); } \
      ltf2 = tf2; \
      tf2 >>= 1; \
      while (tf2--) { SEN_B_COPY(dp, bp); } \
      lid.rid = bid.rid; \
      lid.sid = bid.sid; \
    } \
  } \
  GETNEXTB(); \
}

      GETNEXTC();
      GETNEXTB();
      for (;;) {
        if (bid.rid) {
          if (cid.rid) {
            if (cid.rid < bid.rid) {
              PUTNEXTC();
            } else {
              if (bid.rid < cid.rid) {
                PUTNEXTB();
              } else {
                if (bid.sid) {
                  if (cid.sid < bid.sid) {
                    PUTNEXTC();
                  } else {
                    if (bid.sid == cid.sid) { GETNEXTC(); }
                    PUTNEXTB();
                  }
                } else {
                  GETNEXTC();
                }
              }
            }
          } else {
            PUTNEXTB();
          }
        } else {
          if (cid.rid) {
            PUTNEXTC();
          } else {
            break;
          }
        }
      }
      // sen_log("break: dp=%p cp=%p", dp, cp);

      bt->size_in_chunk = (uint32_t)((dp - dc) - bt->pos_in_chunk);

      if (!ndf) {
        uint32_t *a;
        if ((a = array_at(inv, bt->tid))) {
          sen_sym_pocket_set(inv->lexicon, bt->tid, 0);
          *a = 0;
          sym_delete(inv, bt->tid, h);
          array_unref(inv, bt->tid);
        }
        bt->tid = 0;
        bt->pos_in_chunk = 0;
        bt->size_in_chunk = 0;
        nterms_void++;
      } else if (ndf == 1 && lid.rid < 0x100000 && lid.sid < 0x800 && ltf2 == 2) {
        uint32_t rid_, sid_, tf_, pos_;
        uint8_t *dp_ = dc + bt->pos_in_chunk;
        SEN_B_DEC(rid_, dp_);
        if (rid_ < 0x100000) {
          SEN_B_DEC(sid_, dp_);
          if (sid_ < 0x800) {
            SEN_B_DEC(tf_, dp_);
            if (tf_ == 2) {
              SEN_B_DEC(pos_, dp_);
              if (pos_ < 0x4000) {
                uint32_t *a;
                if ((a = array_at(inv, bt->tid))) {
                  sen_sym_pocket_set(inv->lexicon, bt->tid, pos_);
                  *a = (rid_ << 12) + (sid_ << 1) + 1;
                  array_unref(inv, bt->tid);
                }
                dp = dc + bt->pos_in_chunk;
                bt->tid = 0;
                bt->pos_in_chunk = 0;
                bt->size_in_chunk = 0;
                nterms_void++;
              }
            }
          }
        }
      }
      // sen_log("db=%p df=%d size=%d", db, ndf, (dp - dc) - bt->pos_in_chunk);
    }
    db->header.chunk_size = (uint32_t)(dp - dc);
    db->header.nterms_void = nterms_void;
    inv->total_chunk_size += db->header.chunk_size >> 10;
  }
  db->header.chunk = dcn;
  db->header.buffer_free = SEN_INV_SEGMENT_SIZE
    - sizeof(buffer_header) - sb->header.nterms * sizeof(buffer_term);
  db->header.nterms = sb->header.nterms;

  {
    uint32_t mc, ec;
    mc = max_dest_chunk_size / SEN_INV_CHUNK_SIZE;
    if (mc * SEN_INV_CHUNK_SIZE < max_dest_chunk_size) { mc++; }
    ec = db->header.chunk_size / SEN_INV_CHUNK_SIZE;
    if (ec * SEN_INV_CHUNK_SIZE < db->header.chunk_size) { ec++; }
    // sen_log(" ss=%d ds=%d inv->binfo[%d]=%p max_size=%d(%d) chunk_size=%d(%d)", ss, ds, seg, db, max_dest_chunk_size, mc, db->header.chunk_size, ec);
    while (ec < mc) {
      // sen_log("chunk[%d]=0(%d)", ec, mc);
      inv->header->chunks[db->header.chunk + ec++] = 0;
    }
  }
  buffer_close(inv, seg * SEN_INV_SEGMENT_SIZE);
  SEN_IO_SEG_UNREF(inv->seg, ds);
  inv->binfo[seg] = ds;
  inv->header->segments[ss] = 0;
  inv->header->segments[ds] = SEGMENT_BUFFER | seg;
  if (scn != CHUNK_NOT_ASSIGNED) {
    sen_io_win_unmap(&sw);
    chunk_free(inv, scn, sb->header.chunk_size);
    inv->total_chunk_size -= sb->header.chunk_size >> 10;
  }
  sen_io_win_unmap(&dw);
  return rc;
}

/* inv */

sen_inv *
sen_inv_create08(const char *path, sen_sym *lexicon, uint32_t initial_n_segments)
{
  int i, max_chunk;
  sen_io *seg, *chunk;
  sen_inv *inv;
  char path2[PATH_MAX];
  struct sen_inv_header *header;
  if (strlen(path) + 6 >= PATH_MAX) { return NULL; }
  strcpy(path2, path);
  strcat(path2, ".c");
  if (!initial_n_segments) { initial_n_segments = SEN_INV_INITIAL_N_SEGMENTS; }
  if (initial_n_segments > SEN_INV_MAX_SEGMENT) {
    initial_n_segments = SEN_INV_MAX_SEGMENT;
  }
  max_chunk = initial_n_segments * MAX_CHUNK_RATIO;
  seg = sen_io_create(path, sizeof(struct sen_inv_header) + max_chunk,
                      SEN_INV_SEGMENT_SIZE, SEN_INV_MAX_SEGMENT,
                      sen_io_auto, SEN_INV_MAX_SEGMENT);
  if (!seg) { return NULL; }
  chunk = sen_io_create(path2, 0, SEN_INV_CHUNK_SIZE,
                        max_chunk, sen_io_auto, max_chunk);
  if (!chunk) {
    sen_io_close(seg);
    return NULL;
  }
  header = sen_io_header(seg);
  memcpy(header->idstr, SEN_INV_IDSTR, 16);
  for (i = 0; i < SEN_INV_MAX_SEGMENT; i++) { header->segments[i] = 0; }
  header->initial_n_segments = initial_n_segments;
  if (!(inv = SEN_GMALLOC(sizeof(sen_inv)))) {
    sen_io_close(seg);
    sen_io_close(chunk);
    return NULL;
  }
  inv->v08p = 1;
  inv->seg = seg;
  inv->chunk = chunk;
  inv->header = header;
  inv->lexicon = lexicon;
  inv->total_chunk_size = 0;
  load_all_segments(inv);
  return inv;
}

sen_inv *
sen_inv_open08(const char *path, sen_sym *lexicon)
{
  sen_io *seg, *chunk;
  sen_inv *inv;
  char path2[PATH_MAX];
  struct sen_inv_header *header;
  if (strlen(path) + 6 >= PATH_MAX) { return NULL; }
  strcpy(path2, path);
  strcat(path2, ".c");
  seg = sen_io_open(path, sen_io_auto, SEN_INV_MAX_SEGMENT);
  if (!seg) { return NULL; }
  chunk = sen_io_open(path2, sen_io_auto, SEN_INV_MAX_SEGMENT);
  if (!chunk) {
    sen_io_close(seg);
    return NULL;
  }
  header = sen_io_header(seg);
  if (!(inv = SEN_GMALLOC(sizeof(sen_inv)))) {
    sen_io_close(seg);
    sen_io_close(chunk);
    return NULL;
  }
  inv->v08p = 1;
  inv->seg = seg;
  inv->chunk = chunk;
  inv->header = header;
  inv->lexicon = lexicon;
  {
    off_t size = 0;
    sen_io_size(inv->chunk, &size);
    inv->total_chunk_size = (uint32_t) (size >> 10);
  }
  load_all_segments(inv);
  return inv;
}

sen_rc
sen_inv_update08(sen_inv *inv, uint32_t key, sen_inv_updspec *u, sen_set *h, int hint)
{
  sen_rc r = sen_success;
  buffer *b;
  uint8_t *bs;
  buffer_rec *br = NULL;
  buffer_term *bt;
  uint32_t pos = 0, size, *a;
  if (!u->tf || !u->sid) { return sen_inv_delete(inv, key, u, h); }
  if (!(a = array_get(inv, key))) { return sen_memory_exhausted; }
  if (!(bs = encode_rec(u, &size, 0))) { r = sen_memory_exhausted; goto exit; }
  for (;;) {
    if (*a) {
      if (!(*a & 1)) {
        pos = *a;
        if ((r = buffer_open(inv, pos, &bt, &b))) { goto exit; }
        if (b->header.buffer_free < size) {
          int bfb = b->header.buffer_free;
          SEN_LOG(sen_log_debug, "flushing *a=%d seg=%d(%p) free=%d",
                  *a, *a >> W_OF_SEGMENT, b, b->header.buffer_free);
          buffer_close(inv, pos);
          if ((r = buffer_flush(inv, pos >> W_OF_SEGMENT, h))) { goto exit; }
          if (*a != pos) {
            SEN_LOG(sen_log_debug, "sen_inv_update: *a changed %d->%d", *a, pos);
            continue;
          }
          if ((r = buffer_open(inv, pos, &bt, &b))) {
            SEN_LOG(sen_log_crit, "buffer not found *a=%d", *a);
            goto exit; 
          }
          SEN_LOG(sen_log_debug, "flushed  *a=%d seg=%d(%p) free=%d->%d nterms=%d v=%d",
                  *a, *a >> W_OF_SEGMENT, b, bfb, b->header.buffer_free,
                  b->header.nterms, b->header.nterms_void);
          if (b->header.buffer_free < size) {
            buffer_close(inv, pos);
            SEN_LOG(sen_log_crit, "buffer(%d) is full (%d < %d) in sen_inv_update",
                    *a, b->header.buffer_free, size);
            /* todo: must be splitted */
            r = sen_memory_exhausted;
            goto exit;
          }
        }
        b->header.buffer_free -= size;
        br = (buffer_rec *)(((byte *)&b->terms[b->header.nterms])
                            + b->header.buffer_free);
      } else {
        sen_inv_updspec u2;
        uint32_t size2 = 0, v = *a;
        struct _sen_inv_pos pos2;
        pos2.pos = sen_sym_pocket_get(inv->lexicon, key);
        pos2.next = NULL;
        u2.pos = &pos2;
        u2.rid = BIT31_12(v);
        u2.sid = BIT11_01(v);
        u2.tf = 1;
        u2.score = 0;
        if (u2.rid != u->rid || u2.sid != u->sid) {
          uint8_t *bs2 = encode_rec(&u2, &size2, 0);
    if (!bs2) {
      SEN_LOG(sen_log_alert, "encode_rec on sen_inv_update failed !");
      r = sen_memory_exhausted; 
      goto exit;
    }
          if ((r = buffer_new(inv, size + size2, &pos, &bt, &br, &b, hint))) {
            SEN_GFREE(bs2);
            goto exit;
          }
          bt->tid = key;
          bt->size_in_chunk = 0;
          bt->pos_in_chunk = 0;
          bt->size_in_buffer = 0;
          bt->pos_in_buffer = 0;
          if ((r = buffer_put(b, bt, br, bs2, &u2, size2))) {
            SEN_GFREE(bs2);
            buffer_close(inv, pos);
            goto exit;
          }
          br = (buffer_rec *)(((byte *)br) + size2);
          SEN_GFREE(bs2);
        }
      }
    }
    break;
  }
  if (!br) {
    if (u->rid < 0x100000 && u->sid < 0x800 &&
        u->tf == 1 && u->score == 0 && u->pos->pos < 0x4000) {
      sen_sym_pocket_set(inv->lexicon, key, u->pos->pos);
      *a = (u->rid << 12) + (u->sid << 1) + 1;
      goto exit;
    } else {
      if ((r = buffer_new(inv, size, &pos, &bt, &br, &b, hint))) { goto exit; }
      bt->tid = key;
      bt->size_in_chunk = 0;
      bt->pos_in_chunk = 0;
      bt->size_in_buffer = 0;
      bt->pos_in_buffer = 0;
    }
  }
  r = buffer_put(b, bt, br, bs, u, size);
  buffer_close(inv, pos);
  if (!*a || (*a & 1)) {
    *a = pos;
    sen_sym_pocket_set(inv->lexicon, key, 0);
  }
exit :
  array_unref(inv, key);
  if (bs) { SEN_GFREE(bs); }
  return r;
}

sen_rc
sen_inv_delete08(sen_inv *inv, uint32_t key, sen_inv_updspec *u, sen_set *h)
{
  sen_rc r = sen_success;
  buffer *b;
  uint8_t *bs = NULL;
  buffer_rec *br;
  buffer_term *bt;
  uint32_t size, *a = array_at(inv, key);
  if (!a) { return sen_invalid_argument; }
  for (;;) {
    if (!*a) { goto exit; }
    if (*a & 1) {
      uint32_t rid = BIT31_12(*a);
      uint32_t sid = BIT11_01(*a);
      if (u->rid == rid && (!u->sid || u->sid == sid)) {
        *a = 0;
        sym_delete(inv, key, h);
      }
      goto exit;
    } 
    if (!(bs = encode_rec(u, &size, 1))) { 
      r = sen_memory_exhausted;
      goto exit;
    }
    if ((r = buffer_open(inv, *a, &bt, &b))) { goto exit; }
    //  sen_log("b->header.buffer_free=%d size=%d", b->header.buffer_free, size);
    if (b->header.buffer_free < size) {
      uint32_t _a = *a;
      SEN_LOG(sen_log_debug, "flushing! b=%p free=%d, seg(%d)", b, b->header.buffer_free, *a >> W_OF_SEGMENT);
      buffer_close(inv, *a);
      if ((r = buffer_flush(inv, *a >> W_OF_SEGMENT, h))) { goto exit; }
      if (*a != _a) {
        SEN_LOG(sen_log_debug, "sen_inv_delete: *a changed %d->%d)", *a, _a);
        continue;
      }
      if ((r = buffer_open(inv, *a, &bt, &b))) { goto exit; }
      SEN_LOG(sen_log_debug, "flushed!  b=%p free=%d, seg(%d)", b, b->header.buffer_free, *a >> W_OF_SEGMENT);
      if (b->header.buffer_free < size) {
        /* todo: must be splitted ? */
        SEN_LOG(sen_log_crit, "buffer(%d) is full (%d < %d) in sen_inv_delete",
                *a, b->header.buffer_free, size);
        r = sen_memory_exhausted;
        buffer_close(inv, *a);
        goto exit;
      }
    }

    b->header.buffer_free -= size;
    br = (buffer_rec *)(((byte *)&b->terms[b->header.nterms]) + b->header.buffer_free);
    r = buffer_put(b, bt, br, bs, u, size);
    buffer_close(inv, *a);
    break;
  }
exit :
  array_unref(inv, key);
  if (bs) { SEN_GFREE(bs); }
  return r;
}

#define CHUNK_USED    1
#define BUFFER_USED   2
#define SOLE_DOC_USED 4
#define SOLE_POS_USED 8

sen_inv_cursor *
sen_inv_cursor_open08(sen_inv *inv, uint32_t key)
{
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  sen_inv_cursor08 *c  = NULL;
  uint32_t pos, *a = array_at(inv, key);
  if (!a) { return NULL; }
  if (!(pos = *a)) { goto exit; }
  if (!(c = SEN_GMALLOC(sizeof(sen_inv_cursor08)))) { goto exit; }
  memset(c, 0, sizeof(sen_inv_cursor08));
  c->inv = inv;
  if (pos & 1) {
    c->stat = 0;
    c->pb.rid = BIT31_12(pos);
    c->pb.sid = BIT11_01(pos);
    c->pb.tf = 1;
    c->pb.score = 0;
    c->pb.pos = sen_sym_pocket_get(inv->lexicon, key);
  } else {
    uint32_t chunk;
    buffer_term *bt;
    c->pb.rid = 0; c->pb.sid = 0; /* for check */
    c->buffer_pos = pos;
    if (buffer_open(inv, pos, &bt, &c->buf)) {
      SEN_GFREE(c);
      c = NULL;
      goto exit;
    }
    if (bt->size_in_chunk && (chunk = c->buf->header.chunk) != CHUNK_NOT_ASSIGNED) {
      c->cp = sen_io_win_map(inv->chunk, ctx, &c->iw,
                             chunk, bt->pos_in_chunk, bt->size_in_chunk, sen_io_rdonly);
      if (!c->cp) {
        buffer_close(inv, pos);
        SEN_GFREE(c);
        c = NULL;
        goto exit;
      }
      c->cpe = c->cp + bt->size_in_chunk;
      c->pc.rid = 0;
      c->pc.sid = 0;
    }
    c->nextb = bt->pos_in_buffer;
    c->stat = CHUNK_USED|BUFFER_USED;
  }
exit :
  array_unref(inv, key);
  return (sen_inv_cursor *) c;
}

sen_rc
sen_inv_cursor_next08(sen_inv_cursor *c1)
{
  sen_inv_cursor08 *c = (sen_inv_cursor08 *)c1;
  if (c->buf) {
    for (;;) {
      if (c->stat & CHUNK_USED) {
        while (c->cp < c->cpe && c->pc.rest--) { SEN_B_SKIP(c->cp); }
        if (c->cp < c->cpe) {
          uint32_t gap;
          SEN_B_DEC(gap, c->cp);
          c->pc.rid += gap;
          if (gap) { c->pc.sid = 0; }
          SEN_B_DEC(gap, c->cp);
          c->pc.sid += gap;
          SEN_B_DEC(c->pc.tf, c->cp);
          if (c->pc.tf & 1) { SEN_B_DEC(c->pc.score, c->cp); } else { c->pc.score = 0; }
          c->pc.rest = c->pc.tf >>= 1;
          c->pc.pos = 0;
        } else {
          c->pc.rid = 0;
        }
      }
      if (c->stat & BUFFER_USED) {
        if (c->nextb) {
          uint32_t lrid = c->pb.rid, lsid = c->pb.sid; /* for check */
          buffer_rec *br = BUFFER_REC_AT(c->buf, c->nextb);
          c->bp = NEXT_ADDR(br);
          SEN_B_DEC(c->pb.rid, c->bp);
          SEN_B_DEC(c->pb.sid, c->bp);
          if (lrid > c->pb.rid || (lrid == c->pb.rid && lsid >= c->pb.sid)) {
            SEN_LOG(sen_log_crit, "brokend!! (%d:%d) -> (%d:%d)", lrid, lsid, c->pb.rid, c->pb.sid);
            return sen_other_error;
          }
          c->nextb = br->step;
          SEN_B_DEC(c->pb.tf, c->bp);
          if (c->pb.tf & 1) { SEN_B_DEC(c->pb.score, c->bp); } else { c->pb.score = 0; }
          c->pb.rest = c->pb.tf >>= 1;
          c->pb.pos = 0;
        } else {
          c->pb.rid = 0;
        }
      }
      if (c->pb.rid) {
        if (c->pc.rid) {
          if (c->pc.rid < c->pb.rid) {
            c->stat = CHUNK_USED;
            if (c->pc.tf && c->pc.sid) { c->post = &c->pc; break; }
          } else {
            if (c->pb.rid < c->pc.rid) {
              c->stat = BUFFER_USED;
              if (c->pb.tf && c->pb.sid) { c->post = &c->pb; break; }
            } else {
              if (c->pb.sid) {
                if (c->pc.sid < c->pb.sid) {
                  c->stat = CHUNK_USED;
                  if (c->pc.tf && c->pc.sid) { c->post = &c->pc; break; }
                } else {
                  c->stat = BUFFER_USED;
                  if (c->pb.sid == c->pc.sid) { c->stat |= CHUNK_USED; }
                  if (c->pb.tf) { c->post = &c->pb; break; }
                }
              } else {
                c->stat = CHUNK_USED;
              }
            }
          }
        } else {
          c->stat = BUFFER_USED;
          if (c->pb.tf && c->pb.sid) { c->post = &c->pb; break; }
        }
      } else {
        if (c->pc.rid) {
          c->stat = CHUNK_USED;
          if (c->pc.tf && c->pc.sid) { c->post = &c->pc; break; }
        } else {
          c->post = NULL;
          return sen_other_error;
        }
      }
    }
  } else {
    if (c->stat & SOLE_DOC_USED) {
      c->post = NULL;
      return sen_other_error;
    } else {
      c->post = &c->pb;
      c->stat |= SOLE_DOC_USED;
    }
  }
  return sen_success;
}

sen_rc
sen_inv_cursor_next_pos08(sen_inv_cursor *c1)
{
  sen_inv_cursor08 *c = (sen_inv_cursor08 *)c1;
  uint32_t gap;
  sen_rc rc = sen_success;
  if (c->buf) {
    if (c->post == &c->pc) {
      if (c->pc.rest) {
        c->pc.rest--;
        SEN_B_DEC(gap, c->cp);
        c->pc.pos += gap;
      } else {
        rc = sen_other_error;
      }
    } else if (c->post == &c->pb) {
      if (c->pb.rest) {
        c->pb.rest--;
        SEN_B_DEC(gap, c->bp);
        c->pb.pos += gap;
      } else {
        rc = sen_other_error;
      }
    } else {
      rc = sen_other_error;
    }
  } else {
    if (c->stat & SOLE_POS_USED) {
      rc = sen_other_error;
    } else {
      c->stat |= SOLE_POS_USED;
    }
  }
  return rc;
}

sen_rc
sen_inv_cursor_close08(sen_inv_cursor *c1)
{
  sen_inv_cursor08 *c = (sen_inv_cursor08 *) c1;
  if (!c) { return sen_invalid_argument; }
  if (c->cp) { sen_io_win_unmap(&c->iw); }
  if (c->buf) { buffer_close(c->inv, c->buffer_pos); }
  SEN_GFREE(c);
  return sen_success;
}

uint32_t
sen_inv_estimate_size08(sen_inv *inv, uint32_t key)
{
  uint32_t res, pos, *a = array_at(inv, key);
  if (!a) { return 0; }
  if ((pos = *a)) {
    if (pos & 1) {
      res = 1;
    } else {
      buffer *buf;
      buffer_term *bt;
      if (buffer_open(inv, pos, &bt, &buf)) {
        res = 0;
      } else {
        res = (bt->size_in_chunk >> 2) + bt->size_in_buffer + 2;
        buffer_close(inv, pos);
      }
    }
  } else {
    res = 0;
  }
  array_unref(inv, key);
  return res;
}

int
sen_inv_entry_info08(sen_inv *inv, unsigned key, unsigned *a, unsigned *pocket,
                   unsigned *chunk, unsigned *chunk_size, unsigned *buffer_free,
                   unsigned *nterms, unsigned *nterms_void, unsigned *tid,
                   unsigned *size_in_chunk, unsigned *pos_in_chunk,
                   unsigned *size_in_buffer, unsigned *pos_in_buffer)
{
  buffer *b;
  buffer_term *bt;
  uint32_t *ap = array_at(inv, key);
  *pocket = sen_sym_pocket_get(inv->lexicon, key);
  if (!ap) { return 0; }
  *a = *ap;
  array_unref(inv, key);
  if (!*a) { return 1; }
  if (*a & 1) { return 2; }
  if (buffer_open(inv, *a, &bt, &b)) { return 3; }
  *chunk = b->header.chunk;
  *chunk_size = b->header.chunk_size;
  *buffer_free = b->header.buffer_free;
  *nterms = b->header.nterms;
  *tid = bt->tid;
  *size_in_chunk = bt->size_in_chunk;
  *pos_in_chunk = bt->pos_in_chunk;
  *size_in_buffer = bt->size_in_buffer;
  *pos_in_buffer = bt->pos_in_buffer;
  buffer_close(inv, *a);
  return 4;
}
