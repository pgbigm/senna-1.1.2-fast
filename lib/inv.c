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
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>

#include "ctx.h"
#include "sym.h"
#include "inv.h"

/* v08 functions */

void sen_inv_seg_expire08(sen_inv *inv);
sen_inv * sen_inv_create08(const char *path,  sen_sym *lexicon,
                           uint32_t initial_n_segments);
sen_inv * sen_inv_open08(const char *path, sen_sym *lexicon);
sen_rc sen_inv_update08(sen_inv *inv, uint32_t key, sen_inv_updspec *u, sen_set *h,
                        int hint);
sen_rc sen_inv_delete08(sen_inv *inv, uint32_t key, sen_inv_updspec *u, sen_set *h);
sen_inv_cursor *sen_inv_cursor_open08(sen_inv *inv, uint32_t key);
sen_rc sen_inv_cursor_next08(sen_inv_cursor *c);
sen_rc sen_inv_cursor_next_pos08(sen_inv_cursor *c);
sen_rc sen_inv_cursor_close08(sen_inv_cursor *c);
uint32_t sen_inv_estimate_size08(sen_inv *inv, uint32_t key);
int sen_inv_entry_info08(sen_inv *inv, unsigned key, unsigned *a, unsigned *pocket,
                         unsigned *chunk, unsigned *chunk_size, unsigned *buffer_free,
                         unsigned *nterms, unsigned *nterms_void, unsigned *tid,
                         unsigned *size_in_chunk, unsigned *pos_in_chunk,
                         unsigned *size_in_buffer, unsigned *pos_in_buffer);

struct _sen_inv {
  uint8_t v08p;
  sen_io *seg;
  sen_io *chunk;
  sen_sym *lexicon;
  struct sen_inv_header *header;
};

struct sen_inv_header {
  char idstr[16];
  uint32_t initial_n_segments;
  uint32_t total_chunk_size;
  uint32_t amax;
  uint32_t bmax;
  uint32_t smax;
  uint32_t reserved[7];
  uint16_t ainfo[SEN_INV_MAX_SEGMENT];
  uint16_t binfo[SEN_INV_MAX_SEGMENT];
  uint8_t chunks[1]; /* dummy */
};

#define SEN_INV_IDSTR "SENNA:INV:01.00"
#define SEN_INV_SEGMENT_SIZE 0x40000
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

inline static uint16_t
segment_get(sen_inv *inv)
{
  int i;
  uint16_t seg;
  char used[SEN_INV_MAX_SEGMENT];
  memset(used, 0, SEN_INV_MAX_SEGMENT);
  for (i = 0; i < SEN_INV_MAX_SEGMENT; i++) {
    if ((seg = inv->header->ainfo[i]) != SEG_NOT_ASSIGNED) { used[seg] = 1; }
    if ((seg = inv->header->binfo[i]) != SEG_NOT_ASSIGNED) { used[seg] = 1; }
  }
  for (seg = 0; used[seg] && seg < SEN_INV_MAX_SEGMENT; seg++) ;
  return seg;
}

inline static sen_rc
segment_get_clear(sen_inv *inv, uint16_t *pseg)
{
  uint16_t seg = segment_get(inv);
  if (seg < SEN_INV_MAX_SEGMENT) {
    void *p = NULL;
    SEN_IO_SEG_REF(inv->seg, seg, p);
    if (!p) { return sen_memory_exhausted; }
    memset(p, 0, SEN_INV_SEGMENT_SIZE);
    SEN_IO_SEG_UNREF(inv->seg, seg);
    *pseg = seg;
    return sen_success;
  } else {
    return sen_memory_exhausted;
  }
}

inline static sen_rc
buffer_segment_new(sen_inv *inv, uint16_t *segno)
{
  uint16_t lseg, pseg;
  if (*segno < SEN_INV_MAX_SEGMENT) {
    if (inv->header->binfo[*segno] != SEG_NOT_ASSIGNED) {
      return sen_invalid_argument;
    }
    lseg = *segno;
  } else {
    for (lseg = 0; lseg < SEN_INV_MAX_SEGMENT; lseg++) {
      if (inv->header->binfo[lseg] == SEG_NOT_ASSIGNED) { break; }
    }
    if (lseg == SEN_INV_MAX_SEGMENT) { return sen_memory_exhausted; }
    *segno = lseg;
  }
  pseg = segment_get(inv);
  if (pseg < SEN_INV_MAX_SEGMENT) {
    inv->header->binfo[lseg] = pseg;
    if (lseg >= inv->header->bmax) { inv->header->bmax = lseg + 1; }
    return sen_success;
  } else {
    return sen_memory_exhausted;
  }
}

void
sen_inv_seg_expire(sen_inv *inv, int32_t threshold)
{
  uint16_t seg;
  uint32_t th, nmaps;
  if (inv->v08p) { sen_inv_seg_expire08(inv); return; }
  th = (threshold < 0) ? (inv->header->initial_n_segments * 2) : (uint32_t) threshold;
  if ((nmaps = inv->seg->nmaps) <= th) { return; }
  for (seg = inv->header->bmax; seg && (inv->seg->nmaps > th); seg--) {
    uint16_t pseg = inv->header->binfo[seg - 1];
    if (pseg != SEG_NOT_ASSIGNED) {
      sen_io_mapinfo *info = &inv->seg->maps[pseg];
      uint32_t *pnref = &inv->seg->nrefs[pseg];
      if (info->map && !*pnref) { sen_io_seg_expire(inv->seg, pseg, 0); }
    }
  }
  for (seg = inv->header->amax; seg && (inv->seg->nmaps > th); seg--) {
    uint16_t pseg = inv->header->ainfo[seg - 1];
    if (pseg != SEG_NOT_ASSIGNED) {
      sen_io_mapinfo *info = &inv->seg->maps[pseg];
      uint32_t *pnref = &inv->seg->nrefs[pseg];
      if (info->map && !*pnref) { sen_io_seg_expire(inv->seg, pseg, 0); }
    }
  }
  SEN_LOG(sen_log_notice, "expired(%d) (%u -> %u)", threshold, nmaps, inv->seg->nmaps);
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

inline static uint16_t
buffer_open(sen_inv *inv, uint32_t pos, buffer_term **bt, buffer **b)
{
  byte *p = NULL;
  uint16_t lseg = (uint16_t) (pos >> W_OF_SEGMENT);
  uint16_t pseg = inv->header->binfo[lseg];
  if (pseg != SEG_NOT_ASSIGNED) {
    SEN_IO_SEG_REF(inv->seg, pseg, p);
    if (!p) { return SEG_NOT_ASSIGNED; }
    if (b) { *b = (buffer *)p; }
    if (bt) { *bt = (buffer_term *)(p + (pos & BUFFER_MASK_IN_A_SEGMENT)); }
  }
  return pseg;
}

inline static sen_rc
buffer_close(sen_inv *inv, uint16_t pseg)
{
  if (pseg >= SEN_INV_MAX_SEGMENT) {
    SEN_LOG(sen_log_notice, "invalid pseg buffer_close(%d)", pseg);
    return sen_invalid_argument;
  }
  SEN_IO_SEG_UNREF(inv->seg, pseg);
  return sen_success;
}

inline static uint16_t
buffer_open_if_capable(sen_inv *inv, int32_t seg, int size, buffer **b)
{
  uint16_t pseg;
  uint32_t pos = ((uint32_t) seg) * SEN_INV_SEGMENT_SIZE;
  if ((pseg = buffer_open(inv, pos, NULL, b)) != SEG_NOT_ASSIGNED) {
    uint16_t nterms = (*b)->header.nterms - (*b)->header.nterms_void;
    if (!((nterms < 4096 ||
           (inv->header->total_chunk_size >> ((nterms >> 8) - 6))
           > (*b)->header.chunk_size) &&
          ((*b)->header.buffer_free >= size + sizeof(buffer_term)))) {
      buffer_close(inv, pseg);
      return SEG_NOT_ASSIGNED;
    }
  }
  return pseg;
}

inline static uint16_t
buffer_new(sen_inv *inv, int size, uint32_t *pos,
           buffer_term **bt, buffer_rec **br, buffer **bp, int hint)
{
  buffer *b;
  uint16_t nseg0 = inv->header->initial_n_segments;
  uint16_t pseg, seg, offset, seg0 = hint % nseg0;
  uint16_t segmax = (uint16_t) (inv->header->total_chunk_size >> 7) + nseg0;
  if (size + sizeof(buffer_header) + sizeof(buffer_term) > SEN_INV_SEGMENT_SIZE) {
    return SEG_NOT_ASSIGNED;
  }
  for (seg = seg0; seg < segmax; seg += nseg0) {
    if (inv->header->binfo[seg] == SEG_NOT_ASSIGNED) { break; }
    if ((pseg = buffer_open_if_capable(inv, seg, size, &b)) != SEG_NOT_ASSIGNED) {
      goto exit;
    }
  }
  if (seg >= segmax) {
    for (seg = (seg0 + 1) % nseg0; seg != seg0; seg = (seg + 1) % nseg0) {
      if (inv->header->binfo[seg] == SEG_NOT_ASSIGNED) { break; }
      if ((pseg = buffer_open_if_capable(inv, seg, size, &b)) != SEG_NOT_ASSIGNED) {
        goto exit;
      }
    }
    if (seg == seg0) {
      for (seg = nseg0; seg < SEN_INV_MAX_SEGMENT; seg++) {
        if (inv->header->binfo[seg] == SEG_NOT_ASSIGNED) { break; }
        if ((pseg = buffer_open_if_capable(inv, seg, size, &b)) != SEG_NOT_ASSIGNED) {
          goto exit;
        }
      }
    }
  }
  SEN_LOG(sen_log_debug, "inv=%p new seg=%d", inv, seg);
  if (buffer_segment_new(inv, &seg) ||
      (pseg = buffer_open(inv, seg * SEN_INV_SEGMENT_SIZE, NULL, &b)) == SEG_NOT_ASSIGNED) {
    return SEG_NOT_ASSIGNED;
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
  return pseg;
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
    return sen_abnormal_error;
  }
  if (id2.rid < id.rid || (id2.rid == id.rid && id2.sid <= id.sid)) {
    SEN_LOG(sen_log_crit, "invalid jump! %d(%d:%d)(%d:%d)->%d(%d:%d)(%d:%d)", i, r->jump, r->step, id.rid, id.sid, j, r2->jump, r2->step, id2.rid, id2.sid);
    return sen_abnormal_error;
  }
  return sen_success;
}

inline static int
buffer_check(buffer *b, uint32_t *nerrors)
{
  uint8_t *sbp = NULL;
  uint16_t nextb;
  buffer_rec *br;
  buffer_term *bt;
  int n = b->header.nterms;
  int nterms = 0;
  for (bt = b->terms; n; n--, bt++) {
    uint32_t rid = 0, sid = 0;
    if (!bt->tid) { continue; }
    nterms++;
    for (nextb = bt->pos_in_buffer; nextb; nextb = br->step) {
      uint32_t lrid = rid, lsid = sid;
      br = BUFFER_REC_AT(b, nextb);
      if (check_jump(b, br, br->jump)) { (*nerrors)++; }
      sbp = NEXT_ADDR(br);
      SEN_B_DEC(rid, sbp);
      SEN_B_DEC(sid, sbp);
      if (lrid > rid || (lrid == rid && lsid >= sid)) {
        SEN_LOG(sen_log_crit, "brokeng!! tid=%d (%d:%d) -> (%d:%d)", bt->tid, lrid, lsid, rid, sid);
        (*nerrors)++;
        break;
      }
    }
  }
  return nterms;
}

int
sen_inv_check(sen_inv *inv)
{
  buffer *b;
  uint32_t pos, total_nterms = 0, nerrors = 0;
  uint16_t nseg0 = inv->header->initial_n_segments;
  uint16_t pseg, seg, nsegs = 0;
  uint16_t segmax = (uint16_t) (inv->header->total_chunk_size >> 7) + nseg0;
  for (seg = 0; seg < segmax; seg++) {
    if (inv->header->binfo[seg] == SEG_NOT_ASSIGNED) { continue; }
    pos = ((uint32_t) seg) * SEN_INV_SEGMENT_SIZE;
    if ((pseg = buffer_open(inv, pos, NULL, &b)) == SEG_NOT_ASSIGNED) { continue; }
    nsegs++;
    total_nterms += buffer_check(b, &nerrors);
    buffer_close(inv, pseg);
  }
  sen_log("sen_inv_check done nsegs=%d total_nterms=%d", nsegs, total_nterms);
  return nerrors;
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
    if (check_jump(b, r, j)) { return sen_internal_error; }
    r->jump = j;
    j = i;
    if (!r->step) { return sen_abnormal_error; }
  }
  return sen_success;
}

#define GET_NUM_BITS(x,n) {\
  n = x;\
  n = (n & 0x55555555) + ((n >> 1) & 0x55555555);\
  n = (n & 0x33333333) + ((n >> 2) & 0x33333333);\
  n = (n & 0x0F0F0F0F) + ((n >> 4) & 0x0F0F0F0F);\
  n = (n & 0x00FF00FF) + ((n >> 8) & 0x00FF00FF);\
  n = (n & 0x0000FFFF) + ((n >>16) & 0x0000FFFF);\
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
  // buffer_check(b);
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
  if ((pseg = inv->header->ainfo[seg]) == SEG_NOT_ASSIGNED) { return NULL; }
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
  if ((pseg = inv->header->ainfo[seg]) == SEG_NOT_ASSIGNED) {
    if (segment_get_clear(inv, &pseg)) { return NULL; }
    inv->header->ainfo[seg] = pseg;
    if (seg >= inv->header->amax) { inv->header->amax = seg + 1; }
  }
  SEN_IO_SEG_REF(inv->seg, pseg, p)
  if (!p) { return NULL; }
  return (uint32_t *)(p + (id & ARRAY_MASK_IN_A_SEGMENT) * sizeof(uint32_t));
}

inline static void
array_unref(sen_inv *inv, uint32_t id)
{
  SEN_IO_SEG_UNREF(inv->seg, inv->header->ainfo[id >> W_OF_ARRAY]);
}

/* updspec */

sen_inv_updspec *
sen_inv_updspec_open(uint32_t rid, uint32_t sid)
{
  sen_inv_updspec *u;
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  if (!(u = SEN_MALLOC(sizeof(sen_inv_updspec)))) { return NULL; }
  u->rid = rid;
  u->sid = sid;
  u->score = 0;
  u->tf = 0;
  u->atf = 0;
  u->pos = NULL;
  u->tail = NULL;
  u->vnodes = NULL;
  return u;
}

#define SEN_INV_MAX_TF 0x1ffff

sen_rc
sen_inv_updspec_add(sen_inv_updspec *u, int pos, int32_t weight)
{
  struct _sen_inv_pos *p;
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  u->atf++;
  if (u->tf >= SEN_INV_MAX_TF) { return sen_success; }
  if (!(p = SEN_MALLOC(sizeof(struct _sen_inv_pos)))) {
    return sen_memory_exhausted;
  }
  u->score += weight;
  p->pos = pos;
  p->next = NULL;
  if (u->tail) {
    u->tail->next = p;
  } else {
    u->pos = p;
  }
  u->tail = p;
  u->tf++;
  return sen_success;
}

int
sen_inv_updspec_cmp(sen_inv_updspec *a, sen_inv_updspec *b)
{
  struct _sen_inv_pos *pa, *pb;
  if (a->rid != b->rid) { return a->rid - b->rid; }
  if (a->sid != b->sid) { return a->sid - b->sid; }
  if (a->score != b->score) { return a->score - b->score; }
  if (a->tf != b->tf) { return a->tf - b->tf; }
  for (pa = a->pos, pb = b->pos; pa && pb; pa = pa->next, pb = pb->next) {
    if (pa->pos != pb->pos) { return pa->pos - pb->pos; }
  }
  if (pa) { return 1; }
  if (pb) { return -1; }
  return 0;
}

sen_rc
sen_inv_updspec_close(sen_inv_updspec *u)
{
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  struct _sen_inv_pos *p = u->pos, *q;
  while (p) {
    q = p->next;
    SEN_FREE(p);
    p = q;
  }
  SEN_FREE(u);
  return sen_success;
}

inline static uint8_t *
encode_rec(sen_inv_updspec *u, unsigned int *size, int deletep)
{
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  uint8_t *br, *p;
  struct _sen_inv_pos *pp;
  uint32_t lpos, tf, score;
  if (deletep) {
    tf = 0;
    score = 0;
  } else {
    tf = u->tf;
    score = u->score;
  }
  if (!(br = SEN_MALLOC((tf + 4) * 5))) {
    return NULL;
  }
  p = br;
  SEN_B_ENC(u->rid, p);
  SEN_B_ENC(u->sid, p);
  if (!score) {
    SEN_B_ENC(tf * 2, p);
  } else {
    SEN_B_ENC(tf * 2 + 1, p);
    SEN_B_ENC(score, p);
  }
  for (lpos = 0, pp = u->pos; pp && tf--; lpos = pp->pos, pp = pp->next) {
    SEN_B_ENC(pp->pos - lpos, p);
  }
  while (((intptr_t)p & 0x03)) { *p++ = 0; }
  *size = (unsigned int) ((p - br) + sizeof(buffer_rec));
  return br;
}

inline static int
sym_deletable(uint32_t tid, sen_set *h)
{
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  sen_inv_updspec **u;
  if (!h) { return 1; }
  if (!sen_set_at(h, &tid, (void **) &u)) {
    return (ERRP(ctx, SEN_ERROR)) ? 0 : 1;
  }
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
  if ((inv->lexicon->flags & SEN_INDEX_SHARED_LEXICON)) {
    sen_sym_pocket_decr(inv->lexicon, tid);
  }
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

typedef struct {
  sen_id rid;
  uint32_t sid;
  uint32_t tf;
  uint32_t score;
  uint32_t flags;
} docinfo;

#define SEN_C_ENC(dgap,tf,p)\
{\
  uint8_t *p2 = (uint8_t *)p;\
  uint32_t _d = dgap;\
  uint32_t _t = tf;\
  if (_d < 0x46 && _t <= 2) {\
    *p2++ = (_d << 1) + (_t - 1);\
  } else if (_d < 0x46 && _t <= 6) {\
    *p2++ = 0xc0 + (_d >> 6);\
    *p2++ = (_d << 2) + (_t - 3);\
  } else if (_d < 0x1000 && _t <= 4) {\
    *p2++ = 0xc0 + (_d >> 6);\
    *p2++ = (_d << 2) + (_t - 1);\
  } else if (_d < 0x4000 && _t <= 0x40) {\
    *p2++ = 0x90 + (_d >> 10);\
    *p2++ = (_d >> 2);\
    *p2++ = (_d << 6) + (_t - 1);\
  } else if (_d < 0x80000 && _t <= 4) {\
    *p2++ = 0xa0 + (_d >> 14);\
    *p2++ = (_d >> 6);\
    *p2++ = (_d << 2) + (_t - 1);\
  } else {\
    *p2++ = 0x8f;\
    SEN_B_ENC(_d, p2);\
    SEN_B_ENC(_t, p2);\
  }\
  p = p2;\
}

#define SEN_C_DEC(dgap,tf,p)\
{\
  uint8_t *p2 = (uint8_t *)p;\
  uint32_t _v = *p2++;\
  switch (_v >> 4) {\
  case 0x08 :\
    if (_v == 0x8f) {\
      SEN_B_DEC(dgap, p2);\
      SEN_B_DEC(tf, p2);\
    } else {\
      dgap = _v >> 1;\
      tf = (_v & 1) + 1;\
      break;\
    }\
    break;\
  case 0x09 :\
    dgap = ((_v - 0x90) << 10) + ((*p2++) << 2);\
    _v = *p2++;\
    dgap += _v >> 6;\
    tf = (_v & 0x3f) + 1;\
    break;\
  case 0x0a :\
  case 0x0b :\
    dgap = ((_v - 0xa0) << 14) + ((*p2++) << 6);\
    _v = *p2++;\
    dgap += _v >> 2;\
    tf = (_v & 3) + 1;\
    break;\
  case 0x0c :\
  case 0x0d :\
  case 0x0e :\
  case 0x0f :\
    dgap = (_v - 0xc0) << 6;\
    _v = *p2++;\
    dgap += _v >> 2;\
    tf = (_v & 3) + 1;\
    if (dgap < 0x46) { tf += 2; }\
    break;\
  default :\
    dgap = _v >> 1;\
    tf = (_v & 1) + 1;\
    break;\
  }\
  p = p2;\
}

inline static sen_rc
buffer_flush(sen_inv *inv, sen_ctx *ctx, uint32_t seg, sen_set *h)
{
  buffer *sb, *db = NULL;
  sen_rc rc = sen_success;
  sen_io_win sw, dw;
  uint8_t *tc, *tp, *dc, *sc = NULL;
  uint16_t ss, ds, pseg;
  uint32_t scn, dcn, max_dest_chunk_size;
  ss = inv->header->binfo[seg];
  if (ss == SEG_NOT_ASSIGNED) { return sen_invalid_format; }
  pseg = buffer_open(inv, seg * SEN_INV_SEGMENT_SIZE, NULL, &sb);
  if (pseg == SEG_NOT_ASSIGNED) { return sen_memory_exhausted; }
  if ((ds = segment_get(inv)) == SEN_INV_MAX_SEGMENT) {
    buffer_close(inv, pseg);
    return sen_memory_exhausted;
  }
  SEN_IO_SEG_REF(inv->seg, ds, db);
  if (!db) {
    buffer_close(inv, pseg);
    return sen_memory_exhausted;
  }
  memset(db, 0, SEN_INV_SEGMENT_SIZE);
  max_dest_chunk_size = sb->header.chunk_size + SEN_INV_SEGMENT_SIZE;
  if (!(tc = SEN_MALLOC(max_dest_chunk_size * 2))) {
    buffer_close(inv, pseg);
    SEN_IO_SEG_UNREF(inv->seg, ds);
    return sen_memory_exhausted;
  }
  tp = tc + max_dest_chunk_size;
  if (chunk_new(inv, &dcn, max_dest_chunk_size)) {
    buffer_close(inv, pseg);
    SEN_IO_SEG_UNREF(inv->seg, ds);
    SEN_FREE(tc);
    return sen_memory_exhausted;
  }
  //  sen_log("db=%p ds=%d sb=%p seg=%d", db, ds, sb, seg);
  if ((scn = sb->header.chunk) != CHUNK_NOT_ASSIGNED) {
    sc = sen_io_win_map(inv->chunk, ctx, &sw, scn, 0, sb->header.chunk_size, SEN_IO_COPY);
    if (!sc) {
      SEN_LOG(sen_log_alert, "io_win_map(%d, %d) failed!", scn, sb->header.chunk_size);
      buffer_close(inv, pseg);
      SEN_IO_SEG_UNREF(inv->seg, ds);
      SEN_FREE(tc);
      chunk_free(inv, dcn, max_dest_chunk_size);
      return sen_memory_exhausted;
    }
  }
  dc = sen_io_win_map(inv->chunk, ctx, &dw, dcn, 0, max_dest_chunk_size, SEN_IO_UPDATE);
  if (!dc) {
    SEN_LOG(sen_log_alert, "io_win_map(%d, %d) failed!!", dcn, max_dest_chunk_size);
    buffer_close(inv, pseg);
    SEN_IO_SEG_UNREF(inv->seg, ds);
    SEN_FREE(tc);
    chunk_free(inv, dcn, max_dest_chunk_size);
    if (scn != CHUNK_NOT_ASSIGNED) { sen_io_win_unmap(&sw); }
    return sen_memory_exhausted;
  }
  {
    uint8_t *sbp = NULL, *scp = NULL, *sce = NULL, *dcp = dc;
    uint16_t nextb;
    buffer_rec *br;
    buffer_term *bt;
    int n = sb->header.nterms;
    int nterms_void = 0;
    uint8_t *tpp, *tcp, *spp = NULL;
    memcpy(db->terms, sb->terms, n * sizeof(buffer_term));
    // sen_log(" scn=%d, dcn=%d, nterms=%d", sb->header.chunk, dcn, n);
    for (bt = db->terms; n; n--, bt++) {
      uint32_t ndf = 0, dgap_ = 0, sgap_ = 0;
      docinfo cid = {0, 0, 0, 0, 0}, lid = {0, 0, 0, 0, 0}, bid = {0, 0};
      tpp = tp; tcp = tc;
      if (!bt->tid) {
        nterms_void++;
        continue;
      }
      if (sc) {
        uint32_t o;
        scp = sc + bt->pos_in_chunk;
        sce = scp + bt->size_in_chunk;
        if (bt->size_in_chunk) {
          SEN_B_DEC(o, scp);
          sce = spp = scp + o;
        }
      }
      nextb = bt->pos_in_buffer;
      bt->pos_in_chunk = (uint32_t)(dcp - dc);
      bt->size_in_buffer = 0;
      bt->pos_in_buffer = 0;

#define GETNEXTC_() {\
  if (scp < sce) {\
    uint32_t dgap;\
    if (*scp == 0x8c) { cid.flags |= 1; scp++; } else { cid.flags &= ~1; }\
    if (*scp == 0x8d) { cid.flags ^= 2; scp++; }\
    if (*scp == 0x8e) { cid.flags ^= 4; scp++; }\
    SEN_C_DEC(dgap, cid.tf, scp);\
    cid.rid += dgap;\
    if (dgap) { cid.sid = 0; }\
    if (cid.flags & 4) { SEN_B_DEC(dgap, scp); } else { dgap = 0; }\
    if (cid.flags & 3) { SEN_B_DEC(cid.score, scp); } else { cid.score = 0; }\
    cid.sid += dgap + 1;\
  } else {\
    cid.rid = 0;\
  }\
}
#define GETNEXTC() {\
  if (scp < sce && cid.rid) { while (cid.tf--) { SEN_B_SKIP(spp); } }\
  GETNEXTC_();\
}
#define PUTNEXT_(id,pp) {\
  uint32_t dgap = id.rid - lid.rid;\
  uint32_t sgap = (dgap ? id.sid : id.sid - lid.sid);\
  if (sgap_) {\
    lid.flags &= ~1;\
    if (lid.score) {\
      if (!(lid.flags & 2)) {\
        if (id.score) {\
          lid.flags |= 2; *tcp++ = 0x8d;\
        } else {\
          lid.flags |= 1; *tcp++ = 0x8c;\
        }\
      }\
    } else {\
      if (!id.score && (lid.flags & 2)) { lid.flags &= ~2; *tcp++ = 0x8d; }\
    }\
    sgap_--;\
    if (sgap_) {\
      if (!(lid.flags & 4)) { lid.flags |= 4; *tcp++ = 0x8e; }\
    } else {\
      if (sgap == 1 && (lid.flags & 4)) { lid.flags &= ~4; *tcp++ = 0x8e; }\
    }\
    SEN_C_ENC(dgap_, lid.tf, tcp);\
    if (lid.flags & 4) { SEN_B_ENC(sgap_, tcp); }\
    if (lid.flags & 3) { SEN_B_ENC(lid.score, tcp); }\
  }\
  dgap_ = dgap;\
  lid.tf = id.tf;\
  sgap_ = sgap;\
  lid.score = id.score;\
  while (id.tf--) { SEN_B_COPY(tpp, pp); }\
  lid.rid = id.rid;\
  lid.sid = id.sid;\
}
#define PUTNEXTC() {\
  if (cid.rid) {\
    if (cid.tf) {\
      if (lid.rid > cid.rid || (lid.rid == cid.rid && lid.sid >= cid.sid)) {\
        SEN_LOG(sen_log_crit, "brokenc!! (%d:%d) -> (%d:%d)", lid.rid, lid.sid, bid.rid, bid.sid);\
        rc = sen_invalid_format;\
        break;\
      }\
      ndf++;\
      PUTNEXT_(cid, spp);\
    } else {\
      SEN_LOG(sen_log_crit, "invalid chunk(%d,%d)", bt->tid, cid.rid);\
      rc = sen_invalid_format;\
      break;\
    }\
  }\
  GETNEXTC_();\
}
#define GETNEXTB() {\
  if (nextb) {\
    uint32_t lrid = bid.rid, lsid = bid.sid;\
    br = BUFFER_REC_AT(sb, nextb);\
    sbp = NEXT_ADDR(br);\
    SEN_B_DEC(bid.rid, sbp);\
    SEN_B_DEC(bid.sid, sbp);\
    if (lrid > bid.rid || (lrid == bid.rid && lsid >= bid.sid)) {\
      SEN_LOG(sen_log_crit, "brokeng!! (%d:%d) -> (%d:%d)", lrid, lsid, bid.rid, bid.sid);\
      rc = sen_invalid_format;\
      break;\
    }\
    nextb = br->step;\
  } else {\
    bid.rid = 0;\
  }\
}
#define PUTNEXTB() {\
  if (bid.rid && bid.sid) {\
    SEN_B_DEC(bid.tf, sbp);\
    if (bid.tf > 1) {\
      if (lid.rid > bid.rid || (lid.rid == bid.rid && lid.sid >= bid.sid)) {\
        SEN_LOG(sen_log_crit, "brokenb!! (%d:%d) -> (%d:%d)", lid.rid, lid.sid, bid.rid, bid.sid);\
        rc = sen_invalid_format;\
        break;\
      }\
      if (bid.tf & 1) { SEN_B_DEC(bid.score, sbp); } else { bid.score = 0; }\
      bid.tf >>= 1;\
      ndf++;\
      PUTNEXT_(bid, sbp);\
    }\
  }\
  GETNEXTB();\
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

      if (sgap_) {
        if (lid.score && !(lid.flags & 2)) { lid.flags |= 2; *tcp++ = 0x8d; }
        sgap_--;
        if (sgap_ && !(lid.flags & 4)) { lid.flags |= 4; *tcp++ = 0x8e; }
        SEN_C_ENC(dgap_, lid.tf, tcp);
        if (lid.flags & 4) { SEN_B_ENC(sgap_, tcp); }
        if (lid.flags & 2) { SEN_B_ENC(lid.score, tcp); }
      }

#define BTCLR {\
  bt->tid = 0;\
  bt->pos_in_chunk = 0;\
  bt->size_in_chunk = 0;\
  nterms_void++;\
}
#define BTSET {\
  uint32_t o = tcp - tc;\
  SEN_B_ENC(o, dcp);\
  memcpy(dcp, tc, o);\
  dcp += o;\
  o = tpp - tp;\
  memcpy(dcp, tp, o);\
  dcp += o;\
  bt->size_in_chunk = (uint32_t)((dcp - dc) - bt->pos_in_chunk);\
}

      {
        uint32_t *a;
        if (!ndf) {
          if ((a = array_at(inv, bt->tid))) {
            if (!(inv->lexicon->flags & SEN_INDEX_SHARED_LEXICON)) {
              sen_sym_pocket_set(inv->lexicon, bt->tid, 0);
            }
            *a = 0;
            sym_delete(inv, bt->tid, h);
            array_unref(inv, bt->tid);
            BTCLR;
          } else {
            BTSET;
          }
        } else if (ndf == 1 && lid.rid < 0x100000 && lid.sid < 0x800 && lid.tf == 1 && lid.score == 0) {
          uint32_t pos_;
          spp = tp;
          SEN_B_DEC(pos_, spp);
          if (inv->lexicon->flags & SEN_INDEX_SHARED_LEXICON) {
            if (lid.sid == 1 && pos_ < 0x800 && (a = array_at(inv, bt->tid))) {
              *a = (lid.rid << 12) + (pos_ << 1) + 1;
              array_unref(inv, bt->tid);
              BTCLR;
            } else {
              BTSET;
            }
          } else {
            if (pos_ < 0x4000 && (a = array_at(inv, bt->tid))) {
              sen_sym_pocket_set(inv->lexicon, bt->tid, pos_);
              *a = (lid.rid << 12) + (lid.sid << 1) + 1;
              array_unref(inv, bt->tid);
              BTCLR;
            } else {
              BTSET;
            }
          }
        } else {
          BTSET;
        }
      }
    }
    db->header.chunk_size = (uint32_t)(dcp - dc);
    db->header.nterms_void = nterms_void;
    inv->header->total_chunk_size += db->header.chunk_size >> 10;
  }
  db->header.chunk = db->header.chunk_size ? dcn : CHUNK_NOT_ASSIGNED;
  db->header.buffer_free = SEN_INV_SEGMENT_SIZE
    - sizeof(buffer_header) - sb->header.nterms * sizeof(buffer_term);
  db->header.nterms = sb->header.nterms;

  {
    uint32_t mc, ec;
    mc = (max_dest_chunk_size + SEN_INV_CHUNK_SIZE - 1) / SEN_INV_CHUNK_SIZE;
    ec = (db->header.chunk_size + SEN_INV_CHUNK_SIZE - 1) / SEN_INV_CHUNK_SIZE;
    while (ec < mc) {
      inv->header->chunks[dcn + ec++] = 0;
    }
  }
  buffer_close(inv, pseg);
  SEN_FREE(tc);
  SEN_IO_SEG_UNREF(inv->seg, ds);
  inv->header->binfo[seg] = ds;
  if (scn != CHUNK_NOT_ASSIGNED) {
    sen_io_win_unmap(&sw);
    chunk_free(inv, scn, sb->header.chunk_size);
    inv->header->total_chunk_size -= sb->header.chunk_size >> 10;
  }
  sen_io_win_unmap(&dw);
  return rc;
}

/* inv */

sen_inv *
sen_inv_create(const char *path, sen_sym *lexicon, uint32_t initial_n_segments)
{
  int i, max_chunk;
  sen_io *seg, *chunk;
  sen_inv *inv;
  char path2[PATH_MAX];
  struct sen_inv_header *header;
  if ((lexicon->flags & 0x70000)) {
    return sen_inv_create08(path, lexicon, initial_n_segments);
  }
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
  for (i = 0; i < SEN_INV_MAX_SEGMENT; i++) {
    header->ainfo[i] = SEG_NOT_ASSIGNED;
    header->binfo[i] = SEG_NOT_ASSIGNED;
  }
  header->initial_n_segments = initial_n_segments;
  if (!(inv = SEN_GMALLOC(sizeof(sen_inv)))) {
    sen_io_close(seg);
    sen_io_close(chunk);
    return NULL;
  }
  inv->v08p = 0;
  inv->seg = seg;
  inv->chunk = chunk;
  inv->header = header;
  inv->lexicon = lexicon;
  inv->header->total_chunk_size = 0;
  return inv;
}

sen_rc
sen_inv_remove(const char *path)
{
  sen_rc rc;
  char buffer[PATH_MAX];
  if (!path || strlen(path) > PATH_MAX - 4) { return sen_invalid_argument; }
  if ((rc = sen_sym_remove(path))) { goto exit; }
  snprintf(buffer, PATH_MAX, "%s.c", path);
  rc = sen_io_remove(buffer);
exit :
  return rc;
}

sen_inv *
sen_inv_open(const char *path, sen_sym *lexicon)
{
  sen_io *seg, *chunk;
  sen_inv *inv;
  char path2[PATH_MAX];
  struct sen_inv_header *header;
  if ((lexicon->flags & 0x70000)) {
    return sen_inv_open08(path, lexicon);
  }
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
  if (memcmp(header->idstr, SEN_INV_IDSTR, 16)) {
    SEN_LOG(sen_log_notice, "inv_idstr (%s)", header->idstr);
    sen_io_close(seg);
    sen_io_close(chunk);
    return sen_inv_open08(path, lexicon);
  }
  if (!(inv = SEN_GMALLOC(sizeof(sen_inv)))) {
    sen_io_close(seg);
    sen_io_close(chunk);
    return NULL;
  }
  inv->v08p = 0;
  inv->seg = seg;
  inv->chunk = chunk;
  inv->header = header;
  inv->lexicon = lexicon;
  return inv;
}

sen_rc
sen_inv_close(sen_inv *inv)
{
  sen_rc rc;
  if (!inv) { return sen_invalid_argument; }
  if ((rc = sen_io_close(inv->seg))) { return rc; }
  if ((rc = sen_io_close(inv->chunk))) { return rc; }
  SEN_GFREE(inv);
  return rc;
}

sen_rc
sen_inv_info(sen_inv *inv, uint64_t *seg_size, uint64_t *chunk_size)
{
  sen_rc rc;

  if (seg_size) {
    if ((rc = sen_io_size(inv->seg, seg_size))) {
      return rc;
    }
  }

  if (chunk_size) {
    if ((rc = sen_io_size(inv->chunk, chunk_size))) {
      return rc;
    }
  }

  return sen_success;
}

sen_rc
sen_inv_update(sen_inv *inv, uint32_t key, sen_inv_updspec *u, sen_set *h, int hint)
{
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  sen_rc rc = sen_success;
  buffer *b;
  uint8_t *bs;
  uint16_t pseg = 0;
  buffer_rec *br = NULL;
  buffer_term *bt;
  uint32_t pos = 0, size, *a;
  if (inv->v08p) {
    return sen_inv_update08(inv, key, u, h, hint);
  }
  // sen_log("key=%d tf=%d pos0=%d rid=%d", key, u->tf, u->pos->pos, u->rid);
  if (!u->tf || !u->sid) { return sen_inv_delete(inv, key, u, h); }
  if (u->sid > inv->header->smax) { inv->header->smax = u->sid; }
  if (!(a = array_get(inv, key))) { return sen_memory_exhausted; }
  if (!(bs = encode_rec(u, &size, 0))) { rc = sen_memory_exhausted; goto exit; }
  for (;;) {
    if (*a) {
      if (!(*a & 1)) {
        pos = *a;
        if ((pseg = buffer_open(inv, pos, &bt, &b)) == SEG_NOT_ASSIGNED) {
          rc = sen_memory_exhausted;
          goto exit;
        }
        if (b->header.buffer_free < size) {
          int bfb = b->header.buffer_free;
          SEN_LOG(sen_log_debug, "flushing *a=%d seg=%d(%p) free=%d",
                  *a, *a >> W_OF_SEGMENT, b, b->header.buffer_free);
          buffer_close(inv, pseg);
          if ((rc = buffer_flush(inv, ctx, pos >> W_OF_SEGMENT, h))) { goto exit; }
          if (*a != pos) {
            SEN_LOG(sen_log_debug, "sen_inv_update: *a changed %d->%d", *a, pos);
            continue;
          }
          if ((pseg = buffer_open(inv, pos, &bt, &b)) == SEG_NOT_ASSIGNED) {
            SEN_LOG(sen_log_crit, "buffer not found *a=%d", *a);
            rc = sen_memory_exhausted;
            goto exit;
          }
          SEN_LOG(sen_log_debug, "flushed  *a=%d seg=%d(%p) free=%d->%d nterms=%d v=%d",
                  *a, *a >> W_OF_SEGMENT, b, bfb, b->header.buffer_free,
                  b->header.nterms, b->header.nterms_void);
          if (b->header.buffer_free < size) {
            buffer_close(inv, pseg);
            SEN_LOG(sen_log_crit, "buffer(%d) is full (%d < %d) in sen_inv_update",
                    *a, b->header.buffer_free, size);
            /* todo: must be splitted */
            rc = sen_memory_exhausted;
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
        if (inv->lexicon->flags & SEN_INDEX_SHARED_LEXICON) {
          pos2.pos = BIT11_01(v);
          pos2.next = NULL;
          u2.pos = &pos2;
          u2.rid = BIT31_12(v);
          u2.sid = 1;
          u2.tf = 1;
          u2.score = 0;
        } else {
          pos2.pos = sen_sym_pocket_get(inv->lexicon, key);
          pos2.next = NULL;
          u2.pos = &pos2;
          u2.rid = BIT31_12(v);
          u2.sid = BIT11_01(v);
          u2.tf = 1;
          u2.score = 0;
        }
        if (u2.rid != u->rid || u2.sid != u->sid) {
          uint8_t *bs2 = encode_rec(&u2, &size2, 0);
          if (!bs2) {
            SEN_LOG(sen_log_alert, "encode_rec on sen_inv_update failed !");
            rc = sen_memory_exhausted;
            goto exit;
          }
          pseg = buffer_new(inv, size + size2, &pos, &bt, &br, &b, hint);
          if (pseg == SEG_NOT_ASSIGNED) {
            SEN_FREE(bs2);
            goto exit;
          }
          bt->tid = key;
          bt->size_in_chunk = 0;
          bt->pos_in_chunk = 0;
          bt->size_in_buffer = 0;
          bt->pos_in_buffer = 0;
          if ((rc = buffer_put(b, bt, br, bs2, &u2, size2))) {
            SEN_FREE(bs2);
            buffer_close(inv, pseg);
            goto exit;
          }
          br = (buffer_rec *)(((byte *)br) + size2);
          SEN_FREE(bs2);
        }
      }
    }
    break;
  }
  if (!*a && (inv->lexicon->flags & SEN_INDEX_SHARED_LEXICON)) {
    sen_sym_pocket_incr(inv->lexicon, key);
  }
  if (!br) {
    if (inv->lexicon->flags & SEN_INDEX_SHARED_LEXICON) {
      if (u->rid < 0x100000 && u->sid == 1 &&
          u->tf == 1 && u->score == 0 && u->pos->pos < 0x800) {
        *a = (u->rid << 12) + (u->pos->pos << 1) + 1;
        goto exit;
      }
    } else {
      if (u->rid < 0x100000 && u->sid < 0x800 &&
          u->tf == 1 && u->score == 0 && u->pos->pos < 0x4000) {
        sen_sym_pocket_set(inv->lexicon, key, u->pos->pos);
        *a = (u->rid << 12) + (u->sid << 1) + 1;
        goto exit;
      }
    }
    pseg = buffer_new(inv, size, &pos, &bt, &br, &b, hint);
    if (pseg == SEG_NOT_ASSIGNED) { goto exit; }
    bt->tid = key;
    bt->size_in_chunk = 0;
    bt->pos_in_chunk = 0;
    bt->size_in_buffer = 0;
    bt->pos_in_buffer = 0;
  }
  rc = buffer_put(b, bt, br, bs, u, size);
  buffer_close(inv, pseg);
  if (!*a || (*a & 1)) {
    *a = pos;
    if (!(inv->lexicon->flags & SEN_INDEX_SHARED_LEXICON)) {
      sen_sym_pocket_set(inv->lexicon, key, 0);
    }
  }
exit :
  array_unref(inv, key);
  if (bs) { SEN_FREE(bs); }
  if (u->tf != u->atf) {
    SEN_LOG(sen_log_warning, "too many postings(%d) on '%s'. discarded %d.", u->atf, _sen_sym_key(inv->lexicon, key), u->atf - u->tf);
  }
  return rc;
}

sen_rc
sen_inv_delete(sen_inv *inv, uint32_t key, sen_inv_updspec *u, sen_set *h)
{
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  sen_rc rc = sen_success;
  buffer *b;
  uint16_t pseg;
  uint8_t *bs = NULL;
  buffer_rec *br;
  buffer_term *bt;
  uint32_t size, *a;
  if (inv->v08p) {
    return sen_inv_delete08(inv, key, u, h);
  }
  if (!(a = array_at(inv, key))) { return sen_invalid_argument; }
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
      rc = sen_memory_exhausted;
      goto exit;
    }
    if ((pseg = buffer_open(inv, *a, &bt, &b)) == SEG_NOT_ASSIGNED) {
      rc = sen_memory_exhausted;
      goto exit;
    }
    //  sen_log("b->header.buffer_free=%d size=%d", b->header.buffer_free, size);
    if (b->header.buffer_free < size) {
      uint32_t _a = *a;
      SEN_LOG(sen_log_debug, "flushing! b=%p free=%d, seg(%d)", b, b->header.buffer_free, *a >> W_OF_SEGMENT);
      buffer_close(inv, pseg);
      if ((rc = buffer_flush(inv, ctx, *a >> W_OF_SEGMENT, h))) { goto exit; }
      if (*a != _a) {
        SEN_LOG(sen_log_debug, "sen_inv_delete: *a changed %d->%d)", *a, _a);
        continue;
      }
      if ((pseg = buffer_open(inv, *a, &bt, &b)) == SEG_NOT_ASSIGNED) {
        rc = sen_memory_exhausted;
        goto exit;
      }
      SEN_LOG(sen_log_debug, "flushed!  b=%p free=%d, seg(%d)", b, b->header.buffer_free, *a >> W_OF_SEGMENT);
      if (b->header.buffer_free < size) {
        /* todo: must be splitted ? */
        SEN_LOG(sen_log_crit, "buffer(%d) is full (%d < %d) in sen_inv_delete",
                *a, b->header.buffer_free, size);
        rc = sen_memory_exhausted;
        buffer_close(inv, pseg);
        goto exit;
      }
    }

    b->header.buffer_free -= size;
    br = (buffer_rec *)(((byte *)&b->terms[b->header.nterms]) + b->header.buffer_free);
    rc = buffer_put(b, bt, br, bs, u, size);
    buffer_close(inv, pseg);
    break;
  }
exit :
  array_unref(inv, key);
  if (bs) { SEN_FREE(bs); }
  return rc;
}

uint32_t
sen_inv_initial_n_segments(sen_inv *inv)
{
  return inv->header->initial_n_segments;
}

#define CHUNK_USED    1
#define BUFFER_USED   2
#define SOLE_DOC_USED 4
#define SOLE_POS_USED 8

sen_inv_cursor *
sen_inv_cursor_open(sen_inv *inv, uint32_t key, int with_pos)
{
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  sen_inv_cursor *c  = NULL;
  uint32_t pos, *a;
  if (inv->v08p) {
    return sen_inv_cursor_open08(inv, key);
  }
  if (!(a = array_at(inv, key))) { return NULL; }
  if (!(pos = *a)) { goto exit; }
  if (!(c = SEN_MALLOC(sizeof(sen_inv_cursor)))) { goto exit; }
  memset(c, 0, sizeof(sen_inv_cursor));
  c->inv = inv;
  c->iw.ctx = ctx;
  c->with_pos = (uint16_t) with_pos;
  if (pos & 1) {
    c->stat = 0;
    c->pb.rid = BIT31_12(pos);
    c->pb.tf = 1;
    c->pb.score = 0;
    if (inv->lexicon->flags & SEN_INDEX_SHARED_LEXICON) {
      c->pb.sid = 1;
      c->pb.pos = BIT11_01(pos);
    } else {
      c->pb.sid = BIT11_01(pos);
      c->pb.pos = sen_sym_pocket_get(inv->lexicon, key);
    }
  } else {
    uint32_t chunk;
    buffer_term *bt;
    c->pb.rid = 0; c->pb.sid = 0; /* for check */
    if ((c->buffer_pseg = buffer_open(inv, pos, &bt, &c->buf)) == SEG_NOT_ASSIGNED) {
      SEN_FREE(c);
      c = NULL;
      goto exit;
    }
    if (bt->size_in_chunk && (chunk = c->buf->header.chunk) != CHUNK_NOT_ASSIGNED) {
      c->cp = sen_io_win_map(inv->chunk, ctx, &c->iw,
                             chunk, bt->pos_in_chunk, bt->size_in_chunk, sen_io_rdonly);
      if (!c->cp) {
        buffer_close(inv, c->buffer_pseg);
        SEN_FREE(c);
        c = NULL;
        goto exit;
      }
      c->cpe = c->cp + bt->size_in_chunk;
      if (bt->size_in_chunk) {
        uint32_t o;
        SEN_B_DEC(o, c->cp);
        c->cpe = c->cpp = c->cp + o;
      }
      c->flags = 0;
      c->pc.rid = 0;
      c->pc.sid = 0;
    }
    c->nextb = bt->pos_in_buffer;
    c->stat = CHUNK_USED|BUFFER_USED;
  }
exit :
  array_unref(inv, key);
  return c;
}

#ifdef USE_AIO
sen_inv_cursor *
sen_inv_cursor_openv1(sen_inv *inv, uint32_t key)
{
  sen_inv_cursor *c  = NULL;
  uint32_t pos, *a = array_at(inv, key);
  if (!a) { return NULL; }
  if (!(pos = *a)) { goto exit; }
  if (!(c = SEN_MALLOC(sizeof(sen_inv_cursor)))) { goto exit; }
  memset(c, 0, sizeof(sen_inv_cursor));
  c->inv = inv;
  if (pos & 1) {
    c->stat = 0;
    c->pb.rid = BIT31_12(pos);
    c->pb.tf = 1;
    c->pb.score = 0;
    if (inv->lexicon->flags & SEN_INDEX_SHARED_LEXICON) {
      c->pb.sid = 1;
      c->pb.pos = BIT11_01(pos);
    } else {
      c->pb.sid = BIT11_01(pos);
      c->pb.pos = sen_sym_pocket_get(inv->lexicon, key);
    }
  } else {
    buffer_term *bt;
    c->pb.rid = 0; c->pb.sid = 0;
    if ((c->buffer_pseg = buffer_open(inv, pos, &bt, &c->buf)) == SEG_NOT_ASSIGNED) {
      SEN_FREE(c);
      c = NULL;
      goto exit;
    }
    c->iw.io = inv->chunk;
    c->iw.mode = sen_io_rdonly;
    c->iw.segment = c->buf->header.chunk;
    c->iw.offset = bt->pos_in_chunk;
    c->iw.size = bt->size_in_chunk;
    c->nextb = bt->pos_in_buffer;
    c->stat = CHUNK_USED|BUFFER_USED;
  }
exit :
  array_unref(inv, key);
  return c;
}

sen_rc
sen_inv_cursor_openv2(sen_inv_cursor **cursors, int ncursors)
{
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  sen_rc rc = sen_success;
  int i, j = 0;
  sen_inv_cursor *c;
  sen_io_win **iws = SEN_MALLOC(sizeof(sen_io_win *) * ncursors);
  if (!iws) { return sen_memory_exhausted; }
  for (i = 0; i < ncursors; i++) {
    c = cursors[i];
    if (c->stat && c->iw.size && c->iw.segment != CHUNK_NOT_ASSIGNED) {
      iws[j++] = &c->iw;
    }
  }
  if (j) { rc = sen_io_win_mapv(iws, ctx, j); }
  for (i = 0; i < ncursors; i++) {
    c = cursors[i];
    if (c->iw.addr) {
      c->cp = c->iw.addr + c->iw.diff;
      c->cpe = c->cp + c->iw.size;
      c->pc.rid = 0;
      c->pc.sid = 0;
    }
  }
  SEN_FREE(iws);
  return rc;
}
#endif /* USE_AIO */

sen_rc
sen_inv_cursor_next(sen_inv_cursor *c)
{
  if (c->inv->v08p) {
    return sen_inv_cursor_next08(c);
  }
  if (c->buf) {
    for (;;) {
      if (c->stat & CHUNK_USED) {
        if (c->cp < c->cpe) {
          uint32_t dgap;
          if (c->with_pos) { while (c->pc.rest--) { SEN_B_SKIP(c->cpp); } }
          if (*c->cp == 0x8c) { c->flags |= 1; c->cp++; } else { c->flags &= ~1; }
          if (*c->cp == 0x8d) { c->flags ^= 2; c->cp++; }
          if (*c->cp == 0x8e) { c->flags ^= 4; c->cp++; }
          SEN_C_DEC(dgap, c->pc.tf, c->cp);
          c->pc.rid += dgap;
          if (dgap) { c->pc.sid = 0; }
          if (c->flags & 4) { SEN_B_DEC(dgap, c->cp); } else { dgap = 0; }
          if (c->flags & 3) { SEN_B_DEC(c->pc.score, c->cp); } else { c->pc.score = 0; }
          c->pc.sid += dgap + 1;
          c->pc.rest = c->pc.tf;
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
            return sen_abnormal_error;
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
          return sen_abnormal_error;
        }
      }
    }
  } else {
    if (c->stat & SOLE_DOC_USED) {
      c->post = NULL;
      return sen_abnormal_error;
    } else {
      c->post = &c->pb;
      c->stat |= SOLE_DOC_USED;
    }
  }
  return sen_success;
}

sen_rc
sen_inv_cursor_next_pos(sen_inv_cursor *c)
{
  uint32_t gap;
  sen_rc rc = sen_success;
  if (c->inv->v08p) {
    return sen_inv_cursor_next_pos08(c);
  }
  if (c->with_pos) {
    if (c->buf) {
      if (c->post == &c->pc) {
        if (c->pc.rest) {
          c->pc.rest--;
          SEN_B_DEC(gap, c->cpp);
          c->pc.pos += gap;
        } else {
          rc = sen_abnormal_error;
        }
      } else if (c->post == &c->pb) {
        if (c->pb.rest) {
          c->pb.rest--;
          SEN_B_DEC(gap, c->bp);
          c->pb.pos += gap;
        } else {
          rc = sen_abnormal_error;
        }
      } else {
        rc = sen_abnormal_error;
      }
    } else {
      if (c->stat & SOLE_POS_USED) {
        rc = sen_abnormal_error;
      } else {
        c->stat |= SOLE_POS_USED;
      }
    }
  }
  return rc;
}

sen_rc
sen_inv_cursor_close(sen_inv_cursor *c)
{
  sen_ctx *ctx = c->iw.ctx;
  if (c->inv->v08p) {
    return sen_inv_cursor_close08(c);
  }
  if (!c) { return sen_invalid_argument; }
  if (c->cp) { sen_io_win_unmap(&c->iw); }
  if (c->buf) { buffer_close(c->inv, c->buffer_pseg); }
  SEN_FREE(c);
  return sen_success;
}

uint32_t
sen_inv_estimate_size(sen_inv *inv, uint32_t key)
{
  uint32_t res, pos, *a;
  if (inv->v08p) {
    return sen_inv_estimate_size08(inv, key);
  }
  a = array_at(inv, key);
  if (!a) { return 0; }
  if ((pos = *a)) {
    if (pos & 1) {
      res = 1;
    } else {
      buffer *buf;
      uint16_t pseg;
      buffer_term *bt;
      if ((pseg = buffer_open(inv, pos, &bt, &buf)) == SEG_NOT_ASSIGNED) {
        res = 0;
      } else {
        res = (bt->size_in_chunk >> 1) + bt->size_in_buffer + 2;
        buffer_close(inv, pseg);
      }
    }
  } else {
    res = 0;
  }
  array_unref(inv, key);
  return res;
}

int
sen_inv_entry_info(sen_inv *inv, unsigned key, unsigned *a, unsigned *pocket,
                   unsigned *chunk, unsigned *chunk_size, unsigned *buffer_free,
                   unsigned *nterms, unsigned *nterms_void, unsigned *tid,
                   unsigned *size_in_chunk, unsigned *pos_in_chunk,
                   unsigned *size_in_buffer, unsigned *pos_in_buffer)
{
  buffer *b;
  buffer_term *bt;
  uint32_t *ap;
  uint16_t pseg;
  ERRCLR(NULL);
  if (inv->v08p) {
    return sen_inv_entry_info08(inv, key, a, pocket,
                                chunk, chunk_size, buffer_free,
                                nterms, nterms_void, tid,
                                size_in_chunk, pos_in_chunk,
                                size_in_buffer, pos_in_buffer);
  }
  ap = array_at(inv, key);
  *pocket = sen_sym_pocket_get(inv->lexicon, key);
  if (!ap) { return 0; }
  *a = *ap;
  array_unref(inv, key);
  if (!*a) { return 1; }
  if (*a & 1) { return 2; }
  if ((pseg = buffer_open(inv, *a, &bt, &b)) == SEG_NOT_ASSIGNED) { return 3; }
  *chunk = b->header.chunk;
  *chunk_size = b->header.chunk_size;
  *buffer_free = b->header.buffer_free;
  *nterms = b->header.nterms;
  *tid = bt->tid;
  *size_in_chunk = bt->size_in_chunk;
  *pos_in_chunk = bt->pos_in_chunk;
  *size_in_buffer = bt->size_in_buffer;
  *pos_in_buffer = bt->pos_in_buffer;
  buffer_close(inv, pseg);
  return 4;
}

const char *
sen_inv_path(sen_inv *inv)
{
  return sen_io_path(inv->seg);
}

uint32_t
sen_inv_max_section(sen_inv *inv)
{
  if (inv->v08p) {
    return SEN_SYM_MAX_ID;
  }
  return inv->header->smax;
}
