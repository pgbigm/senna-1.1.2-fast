/* Copyright(C) 2004-2007 Brazil

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
#include "str.h"
#include "inv.h"
#include "sym.h"
#include "store.h"
#include "ctx.h"
#include <string.h>

/* simple array */

void
sen_array_init(sen_array *a, uint16_t element_size, uint16_t flags)
{
  a->element_size = element_size;
  a->flags = flags;
  MUTEX_INIT(a->lock);
  memset(a->elements, 0, sizeof(a->elements));
}

void
sen_array_fin(sen_array *a)
{
  int i;
  for (i = 0; i < 8; i++) {
    void **e = &a->elements[i];
    if (*e) {
      SEN_GFREE(*e);
      *e = NULL;
    }
  }
}

void *
sen_array_at(sen_array *a, sen_id id)
{
  int l, i, o;
  byte **e;
  SEN_BIT_SCAN_REV(id, l);
  i = l >> 2;
  o = 1 << (i << 2);
  e = (byte **)&a->elements[i];
  if (!*e) {
    MUTEX_LOCK(a->lock);
    if (!*e) {
      if (a->flags & sen_array_clear) {
        *e = SEN_GCALLOC(0x0f * o * a->element_size);
      } else {
        *e = SEN_GMALLOC(0x0f * o * a->element_size);
      }
    }
    MUTEX_UNLOCK(a->lock);
    if (!*e) { return NULL; }
  }
  return *e + (id - o) * a->element_size;
}

#define SEN_ARRAY_EACH(a,k,v,block) do {\
  int _ei;\
  for (_ei = 0, (k) = 1; _ei < 8; _ei++) {\
    int _ej = 0x0f * (1 << (_ei << 2));\
    if (((v) = (a)->elements[_ei])) {\
      for (; _ej--; (k)++, (v) = (void *)((byte *)(v) + (a)->element_size)) block\
    } else {\
      (k) += _ej;\
    }\
  }\
} while (0)

/* rectangular arrays */

#define SEN_RA_IDSTR "SENNA:RA:01.000"
#define SEN_RA_SEGMENT_SIZE (1 << 22)

#define SEN_RA_MAX_CACHE (4294967295U)

sen_ra *
sen_ra_create(const char *path, unsigned int element_size)
{
  sen_io *io;
  int max_segments, n_elm, w_elm;
  sen_ra *ra = NULL;
  struct sen_ra_header *header;
  unsigned actual_size;
  if (element_size > SEN_RA_SEGMENT_SIZE) {
    SEN_LOG(sen_log_error, "element_size too large (%d)", element_size);
    return NULL;
  }
  for (actual_size = 1; actual_size < element_size; actual_size *= 2) ;
  max_segments = ((SEN_ID_MAX + 1) / SEN_RA_SEGMENT_SIZE) * actual_size;
  io = sen_io_create(path, sizeof(struct sen_ra_header),
                     SEN_RA_SEGMENT_SIZE, max_segments, sen_io_auto, SEN_RA_MAX_CACHE);
  if (!io) { return NULL; }
  header = sen_io_header(io);
  memcpy(header->idstr, SEN_RA_IDSTR, 16);
  header->element_size = actual_size;
  header->curr_max = 0;
  if (!(ra = SEN_GMALLOC(sizeof(sen_ra)))) {
    sen_io_close(io);
    return NULL;
  }
  n_elm = SEN_RA_SEGMENT_SIZE / header->element_size;
  for (w_elm = 22; (1 << w_elm) > n_elm; w_elm--);
  ra->io = io;
  ra->header = header;
  ra->element_mask =  n_elm - 1;
  ra->element_width = w_elm;
  return ra;
}

sen_ra *
sen_ra_open(const char *path)
{
  sen_io *io;
  int n_elm, w_elm;
  sen_ra *ra = NULL;
  struct sen_ra_header *header;
  io = sen_io_open(path, sen_io_auto, SEN_RA_MAX_CACHE);
  if (!io) { return NULL; }
  header = sen_io_header(io);
  if (memcmp(header->idstr, SEN_RA_IDSTR, 16)) {
    SEN_LOG(sen_log_error, "ra_idstr (%s)", header->idstr);
    sen_io_close(io);
    return NULL;
  }
  if (!(ra = SEN_GMALLOC(sizeof(sen_ra)))) {
    sen_io_close(io);
    return NULL;
  }
  n_elm = SEN_RA_SEGMENT_SIZE / header->element_size;
  for (w_elm = 22; (1 << w_elm) > n_elm; w_elm--);
  ra->io = io;
  ra->header = header;
  ra->element_mask =  n_elm - 1;
  ra->element_width = w_elm;
  return ra;
}

sen_rc
sen_ra_info(sen_ra *ra, unsigned int *element_size, sen_id *curr_max)
{
  if (!ra) { return sen_invalid_argument; }
  if (element_size) { *element_size = ra->header->element_size; }
  if (curr_max) { *curr_max = ra->header->curr_max; }
  return sen_success;
}

sen_rc
sen_ra_close(sen_ra *ra)
{
  sen_rc rc;
  if (!ra) { return sen_invalid_argument; }
  rc = sen_io_close(ra->io);
  SEN_GFREE(ra);
  return rc;
}

sen_rc
sen_ra_remove(const char *path)
{
  if (!path) { return sen_invalid_argument; }
  return sen_io_remove(path);
}

void *
sen_ra_get(sen_ra *ra, sen_id id)
{
  void *p;
  uint16_t seg;
  if (id > SEN_ID_MAX) { return NULL; }
  seg = id >> ra->element_width;
  SEN_IO_SEG_MAP(ra->io, seg, p);
  if (!p) { return NULL; }
  if (id > ra->header->curr_max) { ra->header->curr_max = id; }
  return (void *)(((byte *)p) + ((id & ra->element_mask) * ra->header->element_size));
}

void *
sen_ra_at(sen_ra *ra, sen_id id)
{
  void *p;
  uint16_t seg;
  static char buf[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  if (id > ra->header->curr_max) { return (void *) buf; }
  seg = id >> ra->element_width;
  SEN_IO_SEG_MAP(ra->io, seg, p);
  if (!p) { return NULL; }
  return (void *)(((byte *)p) + ((id & ra->element_mask) * ra->header->element_size));
}

/**** jagged arrays ****/

#define SEN_JA_MAX_CACHE (4294967295U)
#define SEG_NOT_ASSIGNED 0xffffffff

#ifdef USE_JA01

#define USE_SEG_MAP

#define SEN_JA_IDSTR "SENNA:JA:01.000"

#define W_OF_JA_MAX 38
#define W_OF_JA_SEGMENT 22
#define W_OF_JA_MAX_SEGMENTS (W_OF_JA_MAX - W_OF_JA_SEGMENT)

#define W_OF_JA_EINFO 3
#define W_OF_JA_EINFO_IN_A_SEGMENT (W_OF_JA_SEGMENT - W_OF_JA_EINFO)
#define N_OF_JA_EINFO_IN_A_SEGMENT (1U << W_OF_JA_EINFO_IN_A_SEGMENT)
#define JA_EINFO_MASK (N_OF_JA_EINFO_IN_A_SEGMENT - 1)

#define JA_SEGMENT_SIZE (1U << W_OF_JA_SEGMENT)
#define JA_MAX_SEGMENTS (1U << W_OF_JA_MAX_SEGMENTS)

#define JA_BSA_SIZE (1U << (W_OF_JA_SEGMENT - 7))
#define JA_N_BSEGMENTS (1U << (W_OF_JA_MAX_SEGMENTS - 7))

#define JA_N_ESEGMENTS (1U << 9)

typedef struct _sen_ja_einfo sen_ja_einfo;

struct _sen_ja_einfo {
  union {
    uint64_t ll;
    struct {
      uint16_t seg;
      uint16_t pos;
      uint16_t size;
      uint8_t tail[2];
    } s;
  } u;
};

#define EINFO_SET(e,_seg,_pos,_size) {\
  (e)->u.s.seg = _seg;\
  (e)->u.s.pos = (_pos) >> 4;\
  (e)->u.s.size = _size;\
  (e)->u.s.tail[0] = (((_pos) >> 14) & 0xc0) + ((_size) >> 16);\
  (e)->u.s.tail[1] = 0;\
}

#define EINFO_GET(e,_seg,_pos,_size) {\
  _seg = (e)->u.s.seg;\
  _pos = ((e)->u.s.pos + (((e)->u.s.tail[0] & 0xc0) << 10)) << 4;\
  _size = (e)->u.s.size + (((e)->u.s.tail[0] & 0x3f) << 16);\
}

typedef struct {
  uint32_t seg;
  uint32_t pos;
} ja_pos;

struct _sen_ja {
  sen_io *io;
  struct sen_ja_header *header;
};

struct sen_ja_header {
  char idstr[16];
  unsigned max_element_size;
  unsigned max_segments;
  ja_pos free_elements[24];
  uint8_t segments[JA_MAX_SEGMENTS];
  uint32_t esegs[JA_N_ESEGMENTS];
  uint32_t bsegs[JA_N_BSEGMENTS];
};


#define JA_SEG_ESEG 1;
#define JA_SEG_BSEG 2;

sen_ja *
sen_ja_create(const char *path, unsigned int max_element_size)
{
  int i;
  sen_io *io;
  int max_segments;
  sen_ja *ja = NULL;
  struct sen_ja_header *header;
  if (max_element_size > JA_SEGMENT_SIZE) {
    SEN_LOG(sen_log_error, "max_element_size too large (%d)", max_element_size);
    return NULL;
  }
  max_segments = max_element_size * 128;
  if (max_segments > JA_MAX_SEGMENTS) { max_segments = JA_MAX_SEGMENTS; }
  io = sen_io_create(path, sizeof(struct sen_ja_header),
                     JA_SEGMENT_SIZE, max_segments, sen_io_auto, SEN_JA_MAX_CACHE);
  if (!io) { return NULL; }
  header = sen_io_header(io);
  memcpy(header->idstr, SEN_JA_IDSTR, 16);
  for (i = 0; i < JA_N_ESEGMENTS; i++) { header->esegs[i] = SEG_NOT_ASSIGNED; }
  for (i = 0; i < JA_N_BSEGMENTS; i++) { header->bsegs[i] = SEG_NOT_ASSIGNED; }
  header->max_element_size = max_element_size;
  header->max_segments = max_segments;
  header->segments[0] = JA_SEG_ESEG;
  header->esegs[0] = 0;
  if (!(ja = SEN_GMALLOC(sizeof(sen_ja)))) {
    sen_io_close(io);
    return NULL;
  }
  ja->io = io;
  ja->header = header;
  return ja;
}

sen_ja *
sen_ja_open(const char *path)
{
  sen_io *io;
  sen_ja *ja = NULL;
  struct sen_ja_header *header;
  io = sen_io_open(path, sen_io_auto, SEN_JA_MAX_CACHE);
  if (!io) { return NULL; }
  header = sen_io_header(io);
  if (memcmp(header->idstr, SEN_JA_IDSTR, 16)) {
    SEN_LOG(sen_log_error, "ja_idstr (%s)", header->idstr);
    sen_io_close(io);
    return NULL;
  }
  if (!(ja = SEN_GMALLOC(sizeof(sen_ja)))) {
    sen_io_close(io);
    return NULL;
  }
  ja->io = io;
  ja->header = header;
  return ja;
}

sen_rc
sen_ja_info(sen_ja *ja, unsigned int *max_element_size)
{
  if (!ja) { return sen_invalid_argument; }
  if (max_element_size) { *max_element_size = ja->header->max_element_size; }
  return sen_success;
}

sen_rc
sen_ja_close(sen_ja *ja)
{
  sen_rc rc;
  if (!ja) { return sen_invalid_argument; }
  rc = sen_io_close(ja->io);
  SEN_GFREE(ja);
  return rc;
}

sen_rc
sen_ja_remove(const char *path)
{
  if (!path) { return sen_invalid_argument; }
  return sen_io_remove(path);
}

void *
sen_ja_ref(sen_ja *ja, sen_id id, uint32_t *value_len)
{
  sen_ja_einfo *einfo;
  uint32_t lseg, *pseg, pos;
  lseg = id >> W_OF_JA_EINFO_IN_A_SEGMENT;
  pos = id & JA_EINFO_MASK;
  pseg = &ja->header->esegs[lseg];
  if (*pseg == SEG_NOT_ASSIGNED) { *value_len = 0; return NULL; }
  SEN_IO_SEG_MAP(ja->io, *pseg, einfo);
  if (!einfo) { *value_len = 0; return NULL; }
  if (einfo[pos].u.s.tail[1] & 1) {
    *value_len = einfo[pos].u.s.tail[1] >> 1;
    return (void *) &einfo[pos];
  }
  {
    void *value;
    uint32_t jag, vpos, vsize;
#ifdef USE_SEG_MAP
    EINFO_GET(&einfo[pos], jag, vpos, vsize);
    SEN_IO_SEG_MAP(ja->io, jag, value);
    // printf("at id=%d value=%p jag=%d vpos=%d ei=%p(%d:%d)\n", id, value, jag, vpos, &einfo[pos], einfo[pos].u.s.pos, einfo[pos].u.s.tail[0]);
    if (!value) { *value_len = 0; return NULL; }
    *value_len = vsize;
    return (byte *)value + vpos;
#else /* USE_SEG_MAP */
    sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
    sen_io_win iw;
    EINFO_GET(&einfo[pos], jag, vpos, vsize);
    value = sen_io_win_map(ja->io, ctx, &iw, jag, vpos, vsize, sen_io_rdonly);
    if (!value) { *value_len = 0; return NULL; }
    *value_len = vsize;
    return value;
#endif /* USE_SEG_MAP */
  }
}

sen_rc
sen_ja_unref(sen_ja *ja, sen_id id, void *value, uint32_t value_len)
{
  if (!value) { return sen_invalid_argument; }
#ifndef USE_SEG_MAP
  {
    sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
    SEN_FREE(value);
  }
#endif /* USE_SEG_MAP */
  return sen_success;
}

sen_rc sen_ja_alloc(sen_ja *ja, int element_size, sen_ja_einfo *einfo, sen_io_win *iw);

sen_rc
sen_ja_put(sen_ja *ja, sen_id id, void *value, int value_len, int flags)
{
  int rc;
  sen_io_win iw;
  sen_ja_einfo einfo;
  if ((flags & SEN_ST_APPEND)) {
    if (value_len) {
      uint32_t old_len;
      void *oldvalue = sen_ja_ref(ja, id, &old_len);
      if (oldvalue) {
#ifdef USE_SEG_MAP
        if ((rc = sen_ja_alloc(ja, value_len + old_len, &einfo, &iw))) { return rc; }
        memcpy(iw.addr, oldvalue, old_len);
        memcpy((byte *)iw.addr + old_len, value, value_len);
#else /* USE_SEG_MAP */
        void *buf;
        sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
        if (!(buf = SEN_MALLOC(old_len + value_len))) { return sen_memory_exhausted; }
        if ((rc = sen_ja_alloc(ja, old_len + value_len, &einfo, &iw))) {
          SEN_FREE(buf);
          return rc;
        }
        memcpy(buf, oldvalue, old_len);
        memcpy((byte *)buf + old_len, value, value_len);
        iw.addr = buf;
        sen_io_win_unmap(&iw);
#endif /* USE_SEG_MAP */
        sen_ja_unref(ja, id, oldvalue, old_len);
      } else {
#ifdef USE_SEG_MAP
        if ((rc = sen_ja_alloc(ja, value_len, &einfo, &iw))) { return rc; }
        memcpy(iw.addr, value, value_len);
#else /* USE_SEG_MAP */
        sen_io_win iw;
        if ((rc = sen_ja_alloc(ja, value_len, &einfo, &iw))) { return rc; }
        iw.addr = value;
        sen_io_win_unmap(&iw);
#endif /* USE_SEG_MAP */
      }
    }
  } else {
    if (value_len) {
#ifdef USE_SEG_MAP
      if ((rc = sen_ja_alloc(ja, value_len, &einfo, &iw))) { return rc; }
      // printf("put id=%d, value_len=%d value=%p ei=%p(%d:%d)\n", id, value_len, buf, &einfo, einfo.u.s.pos, einfo.u.s.tail[0]);
      memcpy(iw.addr, value, value_len);
#else /* USE_SEG_MAP */
      sen_io_win iw;
      if ((rc = sen_ja_alloc(ja, value_len, &einfo, &iw))) { return rc; }
      iw.addr = value;
      sen_io_win_unmap(&iw);
#endif /* USE_SEG_MAP */
    } else {
      memset(&einfo, 0, sizeof(sen_ja_einfo));
    }
  }
  return sen_ja_replace(ja, id, &einfo);
}

int
sen_ja_size(sen_ja *ja, sen_id id)
{
  sen_ja_einfo *einfo;
  uint32_t lseg, *pseg, pos;
  lseg = id >> W_OF_JA_EINFO_IN_A_SEGMENT;
  pos = id & JA_EINFO_MASK;
  pseg = &ja->header->esegs[lseg];
  if (*pseg == SEG_NOT_ASSIGNED) { return -1; }
  SEN_IO_SEG_MAP(ja->io, *pseg, einfo);
  if (!einfo) { return -1; }
  if (einfo[pos].u.s.tail[1] & 1) {
    return einfo[pos].u.s.tail[1] >> 1;
  } else {
    return einfo[pos].u.s.size + ((einfo[pos].u.s.tail[0] & 0x3f) << 16);
  }
}

sen_rc
sen_ja_alloc(sen_ja *ja, int element_size, sen_ja_einfo *einfo, sen_io_win *iw)
{
  int m, size;
  ja_pos *vp;
#ifdef USE_SEG_MAP
  void *addr;
  if (element_size < 8) {
    einfo->u.s.tail[1] = element_size * 2 + 1;
    iw->addr = (void *)einfo;
    return sen_success;
  }
#endif /* USE_SEG_MAP */
  if (element_size >= ja->header->max_element_size) {
    return sen_invalid_argument;
  }
  for (m = 4, size = 16; size < element_size; m++, size *= 2);
  vp = &ja->header->free_elements[m];
  if (!vp->seg) {
    int i = 0;
    while (ja->header->segments[i]) {
      if (++i >= ja->header->max_segments) { return sen_memory_exhausted; }
    }
    ja->header->segments[i] = m;
    vp->seg = i;
    vp->pos = 0;
  }
  EINFO_SET(einfo, vp->seg, vp->pos, element_size);
#ifdef USE_SEG_MAP
  SEN_IO_SEG_MAP(ja->io, vp->seg, addr);
  // printf("addr=%p seg=%d pos=%d\n", addr, vp->seg, vp->pos);
  if (!addr) { return sen_memory_exhausted; }
  iw->addr = (byte *)addr + vp->pos;
#else /* USE_SEG_MAP */
  iw->io = ja->io;
  iw->ctx = &sen_gctx;
  iw->mode = sen_io_wronly;
  iw->segment = vp->seg;
  iw->offset = vp->pos;
  iw->size = element_size;
  iw->cached = 1;
  {
    int fno, base;
    uint32_t nseg, bseg;
    uint32_t segment_size = JA_SEGMENT_SIZE;
    uint32_t segments_per_file = SEN_IO_FILE_SIZE / segment_size;
    nseg = (iw->offset + element_size + segment_size - 1) / segment_size;
    bseg = iw->segment + ja->io->base_seg;
    fno = bseg / segments_per_file;
    base = fno ? 0 : ja->io->base - ja->io->base_seg * segment_size;
    iw->pos = (bseg % segments_per_file) * segment_size + iw->offset + base;
    iw->nseg = nseg;
  }
#endif /* USE_SEG_MAP */
  if ((vp->pos += size) == JA_SEGMENT_SIZE) {
    vp->seg = 0;
    vp->pos = 0;
  }
  return sen_success;
}

sen_rc
sen_ja_free(sen_ja *ja, sen_ja_einfo *einfo)
{
  uint32_t seg, pos, size;
  if (einfo->u.s.tail[1] & 1) { return sen_success; }
  EINFO_GET(einfo, seg, pos, size);
  // free
  return sen_success;
}

sen_rc
sen_ja_replace(sen_ja *ja, sen_id id, sen_ja_einfo *ei)
{
  uint32_t lseg, *pseg, pos;
  sen_ja_einfo *einfo, eback;
  lseg = id >> W_OF_JA_EINFO_IN_A_SEGMENT;
  pos = id & JA_EINFO_MASK;
  pseg = &ja->header->esegs[lseg];
  if (*pseg == SEG_NOT_ASSIGNED) {
    int i = 0;
    while (ja->header->segments[i]) {
      if (++i >= ja->header->max_segments) { return sen_memory_exhausted; }
    }
    ja->header->segments[i] = 1;
    *pseg = i;
  }
  SEN_IO_SEG_MAP(ja->io, *pseg, einfo);
  if (!einfo) { return sen_memory_exhausted; }
  eback = einfo[pos];
  einfo[pos] = *ei;
  SEN_SET_64BIT(&einfo[pos], *ei);
  sen_ja_free(ja, &eback);
  return sen_success;
}

#else /* USE_JA01 */

#define SEN_JA_IDSTR "SENNA:JA:02.000"

struct _sen_ja {
  sen_io *io;
  struct sen_ja_header *header;
  uint32_t *dsegs;
  uint32_t *esegs;
};

struct sen_ja_header {
  char idstr[16];
  uint32_t flags;
  uint32_t align_width;
  uint32_t segment_width;
  uint32_t element_size;
  uint32_t curr_pos;
};

#define SEN_JA_DEFAULT_ALIGN_WIDTH    4
#define SEN_JA_DEFAULT_SEGMENT_WIDTH 20

#define JA_SEGMENT_SIZE (1 << SEN_JA_DEFAULT_SEGMENT_WIDTH)

#define JA_ALIGN_WIDTH (ja->header->align_width)
#define JA_ALIGN_MASK ((1U << JA_ALIGN_WIDTH) - 1)

#define JA_DSEG_WIDTH (ja->header->segment_width)
#define JA_DSEG_MASK ((1U << JA_DSEG_WIDTH) - 1)

#define JA_ESEG_WIDTH (JA_DSEG_WIDTH - 3)
#define JA_ESEG_MASK ((1U << JA_ESEG_WIDTH) - 1)

#define JA_EPOS_WIDTH (JA_DSEG_WIDTH - JA_ALIGN_WIDTH)
#define JA_EPOS_MASK ((1U << JA_EPOS_WIDTH) - 1)

// segment_width, align_width, flags
sen_ja *
sen_ja_create(const char *path, unsigned int max_element_size)
{
  int i;
  sen_io *io;
  sen_ja *ja;
  struct sen_ja_header *header;
  uint32_t align_width = SEN_JA_DEFAULT_ALIGN_WIDTH;
  uint32_t segment_width = SEN_JA_DEFAULT_SEGMENT_WIDTH;
  uint32_t max_dsegs = 1 << (32 - (segment_width - align_width));
  uint32_t max_esegs = 1 << (31 - segment_width);
  if (align_width > segment_width || align_width + 32 < segment_width) {
    SEN_LOG(sen_log_error, "invalid align_width, segment_width value");
    return NULL;
  }
  io = sen_io_create(path,
                     sizeof(struct sen_ja_header) +
                     max_dsegs * sizeof(int32_t) +
                     max_esegs * sizeof(int32_t),
                     1 << segment_width, max_dsegs, sen_io_auto, SEN_JA_MAX_CACHE);
  if (!io) { return NULL; }
  header = sen_io_header(io);
  memcpy(header->idstr, SEN_JA_IDSTR, 16);
  header->flags = 0;
  header->align_width = align_width;
  header->segment_width = segment_width;
  header->element_size = max_element_size;
  header->curr_pos = 0;
  if (!(ja = SEN_GMALLOC(sizeof(sen_ja)))) {
    sen_io_close(io);
    return NULL;
  }
  ja->io = io;
  ja->header = header;
  ja->dsegs = (uint32_t *)(((uintptr_t) header) + sizeof(struct sen_ja_header));
  ja->esegs = &ja->dsegs[max_dsegs];
  for (i = 0; i < max_esegs; i++) { ja->esegs[i] = SEG_NOT_ASSIGNED; }
  return ja;
}

sen_ja *
sen_ja_open(const char *path)
{
  sen_io *io;
  sen_ja *ja = NULL;
  struct sen_ja_header *header;
  io = sen_io_open(path, sen_io_auto, SEN_JA_MAX_CACHE);
  if (!io) { return NULL; }
  header = sen_io_header(io);
  if (memcmp(header->idstr, SEN_JA_IDSTR, 16)) {
    SEN_LOG(sen_log_error, "ja_idstr (%s)", header->idstr);
    sen_io_close(io);
    return NULL;
  }
  if (!(ja = SEN_GMALLOC(sizeof(sen_ja)))) {
    sen_io_close(io);
    return NULL;
  }
  ja->io = io;
  ja->header = header;
  ja->dsegs = (uint32_t *)(((uintptr_t) header) + sizeof(struct sen_ja_header));
  ja->esegs = &ja->dsegs[1 << (32 - JA_EPOS_WIDTH)];
  return ja;
}

sen_rc
sen_ja_info(sen_ja *ja, unsigned int *max_element_size)
{
  if (!ja) { return sen_invalid_argument; }
  if (max_element_size) { *max_element_size = 4294967295U; }
  return sen_success;
}

sen_rc
sen_ja_close(sen_ja *ja)
{
  sen_rc rc;
  if (!ja) { return sen_invalid_argument; }
  rc = sen_io_close(ja->io);
  SEN_GFREE(ja);
  return rc;
}

sen_rc
sen_ja_remove(const char *path)
{
  if (!path) { return sen_invalid_argument; }
  return sen_io_remove(path);
}

void *
sen_ja_ref(sen_ja *ja, sen_id id, uint32_t *value_len)
{
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  sen_io_ja_einfo *ep;
  uint32_t lseg = id >> JA_ESEG_WIDTH;
  uint32_t lpos = id & JA_ESEG_MASK;
  uint32_t *pseg = &ja->esegs[lseg];
  if (*pseg == SEG_NOT_ASSIGNED) { *value_len = 0; return NULL; }
  SEN_IO_SEG_MAP(ja->io, *pseg, ep);
  if (!ep) { *value_len = 0; return NULL; }
  ep += lpos;
  for (;;) {
    void *value;
    uint32_t epos = ep->pos;
    uint32_t dseg = epos >> JA_EPOS_WIDTH;
    uint32_t doff = (epos & JA_EPOS_MASK) << JA_ALIGN_WIDTH;
    if (!(*value_len = ep->size)) { return NULL; }
    if (sen_io_read_ja(ja->io, ctx, ep, epos, id, dseg, doff, &value, value_len)
        != sen_internal_error) {
      return value;
    }
  }
}

sen_rc
sen_ja_unref(sen_ja *ja, sen_id id, void *value, uint32_t value_len)
{
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  SEN_FREE((void *)(((uintptr_t) value) - sizeof(sen_io_ja_ehead)));
  return sen_success;
}

#define SEG_ESEG 0xffffffff

sen_rc
assign_eseg(sen_ja *ja, uint32_t lseg)
{
  int i;
  uint32_t max_dsegs = 1 << (32 - JA_EPOS_WIDTH);
  for (i = 0; i < max_dsegs; i++) {
    if (ja->dsegs[i] == 0) {
      if (ja->header->curr_pos && i == (ja->header->curr_pos >> JA_EPOS_WIDTH)) {
        // todo : curr_pos must be moved.
      }
      ja->dsegs[i] = SEG_ESEG;
      ja->esegs[lseg] = i;
      return sen_success;
    }
  }
  return sen_internal_error;
}

static sen_rc
assign_rec(sen_ja *ja, int value_len)
{
  if (ja->header->curr_pos) {
    uint32_t doff = (ja->header->curr_pos & JA_EPOS_MASK) << JA_ALIGN_WIDTH;
    if (doff + value_len + sizeof(sen_io_ja_ehead) <= (1 << JA_DSEG_WIDTH)) {
      return sen_success;
    }
  }
  {
    uint32_t max_dsegs = 1 << (32 - JA_EPOS_WIDTH);
    /* if (value_len <= JA_DSEG_MASK) { todo : } else */
    {
      uint32_t i, j;
      uint32_t nseg = (value_len + sizeof(sen_io_ja_ehead) + JA_DSEG_MASK) >> JA_DSEG_WIDTH;
      for (i = 0, j = 0; i < max_dsegs; i++) {
        if (ja->dsegs[i] == 0) {
          if (++j == nseg) {
            ja->header->curr_pos = (i + 1 - j) << JA_EPOS_WIDTH;
            return sen_success;
          }
        } else {
          j = 0;
        }
      }
    }
    return sen_other_error;
  }
}

sen_rc
sen_ja_put(sen_ja *ja, sen_id id, void *value, int value_len, int flags)
{
  sen_rc rc = sen_success;
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  uint32_t newpos = 0;
  sen_io_ja_einfo *ep;
  {
    uint32_t lseg = id >> JA_ESEG_WIDTH;
    uint32_t lpos = id & JA_ESEG_MASK;
    uint32_t *pseg = &ja->esegs[lseg];
    if (*pseg == SEG_NOT_ASSIGNED && (rc = assign_eseg(ja, lseg))) { return rc; }
    SEN_IO_SEG_MAP(ja->io, *pseg, ep);
    if (!ep) { return sen_other_error; }
    ep += lpos;
  }
  if (value_len) {
    uint32_t dseg, doff, inc;
    if ((rc = assign_rec(ja, value_len))) { return rc; }
    newpos = ja->header->curr_pos;
    dseg = newpos >> JA_EPOS_WIDTH;
    doff = (newpos & JA_EPOS_MASK) << JA_ALIGN_WIDTH;
    rc = sen_io_write_ja(ja->io, ctx, id, dseg, doff, value, value_len);
    if (rc) { return rc; }
    inc = ((value_len + sizeof(sen_io_ja_ehead) + JA_ALIGN_MASK) >> JA_ALIGN_WIDTH);
    ja->header->curr_pos = ((newpos + inc) & JA_EPOS_MASK) ? (newpos + inc) : 0;
    {
      uint32_t max = 1U << JA_EPOS_WIDTH;
      while (ja->dsegs[dseg] + inc > max) {
        inc -= max - ja->dsegs[dseg];
        ja->dsegs[dseg++] = max;
      }
      ja->dsegs[dseg] += inc;
    }
  }
  {
    sen_io_ja_einfo ne, oe;
    oe = *ep;
    ne.pos = newpos;
    ne.size = value_len;
    *ep = ne; // atomic_set64 maybe more suitable.
    if (oe.size) {
      uint32_t dseg, off, dec, max = 1U << JA_EPOS_WIDTH;
      dseg = oe.pos >> JA_EPOS_WIDTH;
      off = (oe.pos & JA_EPOS_MASK);
      dec = ((oe.size + sizeof(sen_io_ja_ehead) + JA_ALIGN_MASK) >> JA_ALIGN_WIDTH);
      if (off + dec > max) {
        dec -= max - off;
        ja->dsegs[dseg++] -= max - off;
        while (dec > max) {
          dec -= max;
          ja->dsegs[dseg++] = 0;
        }
        {
          uint32_t vsize = (dec << JA_ALIGN_WIDTH) - sizeof(sen_io_ja_ehead);
          rc = sen_io_write_ja_ehead(ja->io, ctx, 0, dseg, 0, vsize);
          // todo : handle rc..
        }
      }
      ja->dsegs[dseg] -= dec;
    }
  }
  return rc;
}

static sen_rc
sen_ja_defrag_seg(sen_ja *ja, uint32_t dseg)
{
  sen_rc rc = sen_success;
  sen_ctx *ctx = &sen_gctx; /* todo : replace it with the local ctx */
  sen_io_win iw;
  uint32_t epos = dseg << JA_EPOS_WIDTH;
  uint32_t segsize = (1 << JA_DSEG_WIDTH);
  byte *v = sen_io_win_map(ja->io, ctx, &iw, dseg, 0, segsize, sen_io_rdonly);
  byte *ve = v + segsize;
  uint32_t *segusage = &ja->dsegs[dseg];
  if (!v) { return sen_internal_error; }
  while (v < ve && *segusage) {
    sen_io_ja_ehead *eh = (sen_io_ja_ehead *) v;
    uint32_t inc = ((eh->size + sizeof(sen_io_ja_ehead) + JA_ALIGN_MASK) >> JA_ALIGN_WIDTH);
    sen_io_ja_einfo *ep;
    uint32_t lseg = eh->key >> JA_ESEG_WIDTH;
    uint32_t lpos = eh->key & JA_ESEG_MASK;
    uint32_t *pseg = &ja->esegs[lseg];
    if (*pseg == SEG_NOT_ASSIGNED) {
      SEN_LOG(sen_log_error, "ep is not assigned (%x)", lseg);
      rc = sen_internal_error;
      goto exit;
    }
    SEN_IO_SEG_MAP(ja->io, *pseg, ep);
    if (!ep) {
      SEN_LOG(sen_log_error, "ep map failed (%x)", lseg);
      rc = sen_internal_error;
      goto exit;
    }
    ep += lpos;
    if (ep->pos == epos) {
      if (ep->size != eh->size) {
        SEN_LOG(sen_log_error, "ep->size(%d) != eh->size(%d)", ep->size, eh->size);
        rc = sen_internal_error;
        goto exit;
      }
      if ((rc = sen_ja_put(ja, eh->key, v + sizeof(sen_io_ja_ehead), eh->size, 0))) {
        goto exit;
      }
    }
    epos += inc;
    v += (inc << JA_ALIGN_WIDTH);
  }
  if (*segusage) {
    SEN_LOG(sen_log_error, "ja->dsegs[dseg] = %d after defrag", ja->dsegs[dseg]);
    rc = sen_internal_error;
  }
exit :
  sen_io_win_unmap(&iw);
  return rc;
}

int
sen_ja_defrag(sen_ja *ja, int threshold)
{
  int nsegs = 0;
  uint32_t ts = 1U << (JA_EPOS_WIDTH - threshold);
  uint32_t dseg, max_dsegs = 1 << (32 - JA_EPOS_WIDTH);
  for (dseg = 0; dseg < max_dsegs; dseg++) {
    if (dseg == ja->header->curr_pos >> JA_EPOS_WIDTH) { continue; }
    if (ja->dsegs[dseg] && ja->dsegs[dseg] < ts) {
      if (!sen_ja_defrag_seg(ja, dseg)) { nsegs++; }
    }
  }
  return nsegs;
}

#endif /* USE_JA01 */

/**** db ****/

sen_rc
sen_db_lock(sen_db *s, int timeout)
{
  return sen_sym_lock(s->keys, timeout);
}

sen_rc
sen_db_unlock(sen_db *s)
{
  return sen_sym_unlock(s->keys);
}

sen_rc
sen_db_clear_lock(sen_db *s)
{
  return sen_sym_clear_lock(s->keys);
}

inline static void
gen_pathname(const char *path, char *buffer, int fno)
{
  size_t len = strlen(path);
  memcpy(buffer, path, len);
  if (fno >= 0) {
    buffer[len] = '.';
    sen_str_itoh(fno, buffer + len + 1, 7);
  } else {
    buffer[len] = '\0';
  }
}

sen_db_store *
sen_db_store_by_id(sen_db *s, sen_id id)
{
  const char *name;
  sen_db_store *slot;
  if (!id) { return NULL; }
  slot = sen_array_at(&s->stores, id);
  if (slot->type) { return slot; }
  if (!(name = _sen_sym_key(s->keys, id))) { return NULL; }
  return sen_db_store_open(s, name);
}

// todo : slot should has cache class id
sen_db_store *
sen_db_slot_class(sen_db *s, const char *slot)
{
  int i = SEN_SYM_MAX_KEY_SIZE;
  char buf[SEN_SYM_MAX_KEY_SIZE], *dst = buf;
  while (*slot != '.') {
    if (!*slot || !--i) { return NULL; }
    *dst++ = *slot++;
  }
  *dst = '\0';
  return sen_db_store_open(s, buf);
}

sen_rc
sen_db_idx_slot_build(sen_db *s, sen_db_store *store)
{
  sen_index *index = store->u.i.index;
  sen_id id = SEN_SYM_NIL; /* maxid = sen_sym_curr_id(index->keys); */
  sen_db_store *target = sen_db_store_by_id(s, store->triggers->target);
  if (!target) { return sen_invalid_argument; }
  while ((id = sen_sym_next(index->keys, id))) {
    uint32_t vs;
    const char *key = _sen_sym_key(index->keys, id);
    void *vp = (void *)sen_ja_ref(target->u.v.ja, id, &vs);
    if (vp) {
      if (key && vs && sen_index_upd(index, key, NULL, 0, vp, vs)) {
        SEN_LOG(sen_log_error, "sen_index_upd failed. id=%d key=(%s)", id, key);
      }
      sen_ja_unref(target->u.v.ja, id, vp, vs);
    }
  }
  return sen_success;
}

sen_db_store *
sen_db_store_open(sen_db *s, const char *name)
{
  sen_id id;
  uint32_t spec_len;
  sen_db_store *e, *k, *l, *r = NULL;
  char buffer[PATH_MAX];
  sen_db_store_spec *spec;
  if (!s || !(id = sen_sym_at(s->keys, name))) {
    return NULL;
  }
  if (!(e = sen_array_at(&s->stores, id))) { return NULL; }
  if (e->type) { return e; }

  if (!(spec = sen_ja_ref(s->values, id, &spec_len))) {
    return NULL;
  }
  if (spec->type == sen_db_idx_slot) {
    if (!(k = sen_db_store_by_id(s, spec->u.s.class)) ||
        !(l = sen_db_slot_class(s, name))) {
      sen_ja_unref(s->values, id, spec, spec_len);
      return NULL;
    }
  }
  MUTEX_LOCK(s->lock);
  e->db = s;
  e->id = id;
  e->triggers = NULL;
  gen_pathname(s->keys->io->path, buffer, id);
  switch (spec->type) {
  case sen_db_raw_class :
    e->u.bc.element_size = spec->u.c.size;
    break;
  case sen_db_class :
    if (!(e->u.c.keys = sen_sym_open(buffer))) { goto exit; }
    break;
  case sen_db_obj_slot :
    e->u.o.class = spec->u.s.class;
    if (!(e->u.o.ra = sen_ra_open(buffer))) { goto exit; }
    break;
  case sen_db_ra_slot :
    e->u.f.class = spec->u.s.class;
    if (!(e->u.f.ra = sen_ra_open(buffer))) { goto exit; }
    break;
  case sen_db_ja_slot :
    e->u.v.class = spec->u.s.class;
    if (!(e->u.v.ja = sen_ja_open(buffer))) { goto exit; }
    break;
  case sen_db_idx_slot :
    e->u.i.class = spec->u.s.class;
    if (!(e->u.i.index =
          sen_index_open_with_keys_lexicon(buffer, k->u.c.keys, l->u.c.keys))) {
      goto exit;
    }
    break;
  case sen_db_rel1 :
    e->u.f.class = spec->u.s.class;
    if (!(e->u.f.ra = sen_ra_open(buffer))) { goto exit; }
    break;
  default :
    goto exit;
  }
  {
    int i;
    for (i = 0; i < spec->n_triggers; i++) {
      sen_id target = spec->triggers[i].target;
      if (target) {
        sen_db_trigger *r = SEN_GMALLOC(sizeof(sen_db_trigger));
        if (!r) { goto exit; }
        r->next = e->triggers;
        r->type = spec->triggers[i].type;
        r->target = target;
        e->triggers = r;
      }
    }
  }
  e->type = spec->type;
  r = e;
exit :
  sen_ja_unref(s->values, id, spec, spec_len);
  MUTEX_UNLOCK(s->lock);
  return r;
}

sen_db_store *
sen_db_store_create(sen_db *s, const char *name, sen_db_store_spec *spec)
{
  sen_id id;
  sen_db_store *e, *k, *l, *r = NULL;
  char buffer[PATH_MAX];
  if (strlen(name) >= SEN_SYM_MAX_KEY_SIZE) {
    GERR(sen_invalid_argument, "too long store name (%s)", name);
    return NULL;
  }
  if (strchr(name, '.') &&
      ((spec->type == sen_db_raw_class) || (spec->type == sen_db_class))) {
    GERR(sen_invalid_argument, "class name must not include '.' (%s)", name);
    return NULL;
  }
  if (spec->type == sen_db_idx_slot) {
    if (!(k = sen_db_store_by_id(s, spec->u.s.class)) ||
        !(l = sen_db_slot_class(s, name))) {
      return NULL;
    }
  }

  if (sen_db_lock(s, -1)) {
    SEN_LOG(sen_log_crit, "sen_db_store_create: lock failed");
    return NULL;
  }

  if (sen_sym_at(s->keys, name)) {
    sen_db_unlock(s);
    GERR(sen_invalid_argument, "sen_db_store_create: existing name (%s)", name);
    return NULL;
  }

  if (!(id = sen_sym_nextid(s->keys, name))) {
    sen_db_unlock(s);
    return NULL;
  }

  MUTEX_LOCK(s->lock);

  if (!(e = sen_array_at(&s->stores, id))) { goto exit; }
  if (e->type) {
    SEN_LOG(sen_log_crit, "sen_db corrupt: %s", name);
    goto exit;
  }

  spec->n_triggers = 0;
  if (sen_ja_put(s->values, id, spec, SEN_DB_STORE_SPEC_SIZE(0), 0)) { goto exit; }
  e->db = s;
  e->id = id;
  e->triggers = NULL;
  gen_pathname(s->keys->io->path, buffer, id);
  switch (spec->type) {
  case sen_db_raw_class :
    e->u.bc.element_size = spec->u.c.size;
    break;
  case sen_db_class :
    if (!(e->u.c.keys = sen_sym_create(buffer,
                                       spec->u.c.size,
                                       spec->u.c.flags,
                                       spec->u.c.encoding))) { goto exit; }
    break;
  case sen_db_obj_slot :
    e->u.o.class = spec->u.s.class;
    if (!(e->u.o.ra = sen_ra_create(buffer, sizeof(sen_id)))) { goto exit; }
    break;
  case sen_db_ra_slot :
    e->u.f.class = spec->u.s.class;
    if (!(e->u.f.ra = sen_ra_create(buffer, spec->u.s.size))) { goto exit; }
    break;
  case sen_db_ja_slot :
    e->u.v.class = spec->u.s.class;
    if (!(e->u.v.ja = sen_ja_create(buffer, spec->u.s.size))) { goto exit; }
    break;
  case sen_db_idx_slot :
    e->u.i.class = spec->u.s.class;
    if (!(e->u.i.index =
          sen_index_create_with_keys_lexicon(buffer, k->u.c.keys, l->u.c.keys,
                                             spec->u.s.size))) {
      goto exit;
    }
    break;
  case sen_db_rel1 :
    e->u.f.class = spec->u.s.class;
    if (!(e->u.f.ra = sen_ra_create(buffer, spec->u.s.size))) { goto exit; }
    break;
  default :
    goto exit;
  }
  if ((id != sen_sym_get(s->keys, name))) {
    SEN_LOG(sen_log_crit, "sen_db id unmatch: %d", id);
  }
  e->type = spec->type;
  r = e;
exit :
  MUTEX_UNLOCK(s->lock);
  sen_db_unlock(s);
  // todo : sen_ja_put(s->values, id, NULL, 0, 0);
  return r;
}

sen_rc
sen_db_store_add_trigger(sen_db_store *e, sen_db_store_rel_spec *t)
{
  sen_rc rc;
  sen_db *s = e->db;
  uint32_t spec_len, newspec_len;
  sen_db_store_spec *spec;
  sen_db_store_spec *newspec;
  sen_db_store *target = sen_db_store_by_id(s, t->target);
  if (!target) { return sen_invalid_argument; }

  if (sen_db_lock(s, -1)) {
    SEN_LOG(sen_log_crit, "sen_db_store_add_trigger: lock failed");
    return sen_abnormal_error;
  }
  if (!(spec = sen_ja_ref(s->values, e->id, &spec_len))) {
    sen_db_unlock(s);
    return sen_invalid_argument;
  }
  newspec_len = SEN_DB_STORE_SPEC_SIZE(spec->n_triggers + 1);
  if (!(newspec = SEN_GMALLOC(newspec_len))) {
    sen_db_unlock(s);
    return sen_memory_exhausted;
  }
  memcpy(newspec, spec, spec_len);
  memcpy(&newspec->triggers[spec->n_triggers], t, sizeof(sen_db_store_rel_spec));
  newspec->n_triggers++;
  sen_ja_unref(s->values, e->id, spec, spec_len);
  rc = sen_ja_put(s->values, e->id, newspec, newspec_len, 0);
  sen_db_unlock(s);
  SEN_GFREE(newspec);
  if (rc) { return rc; }
  {
    sen_db_trigger *r = SEN_GMALLOC(sizeof(sen_db_trigger));
    if (r) {
      MUTEX_LOCK(s->lock);
      r->next = e->triggers;
      r->type = t->type;
      r->target = t->target;
      e->triggers = r;
      MUTEX_UNLOCK(s->lock);
      if (t->type == sen_db_index_target) {
        sen_db_store_rel_spec invrs;
        invrs.type = sen_db_before_update_trigger;
        invrs.target = e->id;
        rc = sen_db_store_add_trigger(target, &invrs);
      }
    } else { rc = sen_memory_exhausted; }
  }
  return rc;
}

sen_rc
sen_db_store_del_trigger(sen_db_store *e, sen_db_store_rel_spec *t)
{
  sen_rc rc;
  sen_db *s = e->db;
  uint32_t spec_len, newspec_len;
  uint32_t n_triggers = 0;
  sen_db_store_spec *spec;
  sen_db_store_spec *newspec;
  sen_db_trigger *r, **rp = &e->triggers;
  if (sen_db_lock(s, -1)) {
    SEN_LOG(sen_log_crit, "sen_db_del_trigger: lock failed");
    return sen_abnormal_error;
  }
  if (!(spec = sen_ja_ref(s->values, e->id, &spec_len))) {
    sen_db_unlock(s);
    return sen_invalid_argument;
  }
  MUTEX_LOCK(s->lock);
  while ((r = *rp)) {
    if (r->target == t->target) {
      *rp = r->next;
      SEN_GFREE(r);
    } else {
      n_triggers++;
      rp = &r->next;
    }
  }
  MUTEX_UNLOCK(s->lock);
  newspec_len = SEN_DB_STORE_SPEC_SIZE(n_triggers);
  if (!(newspec = SEN_GMALLOC(newspec_len))) {
    sen_db_unlock(s);
    sen_ja_unref(s->values, e->id, spec, spec_len);
    return sen_memory_exhausted;
  }
  memcpy(newspec, spec, newspec_len);
  newspec->n_triggers = n_triggers;
  sen_ja_unref(s->values, e->id, spec, spec_len);
  for (t = newspec->triggers, r = e->triggers; r; t++, r = r->next) {
    t->type = r->type;
    t->target = r->target;
  }
  rc = sen_ja_put(s->values, e->id, newspec, newspec_len, 0);
  sen_db_unlock(s);
  SEN_GFREE(newspec);
  return rc;
}

sen_db_store *
sen_db_slot_class_by_id(sen_db *s, sen_id slot)
{
  const char *slotname = _sen_sym_key(s->keys, slot);
  if (!slotname) { return NULL; }
  return sen_db_slot_class(s, slotname);
}

sen_rc
sen_db_class_slotpath(sen_db *s, sen_id class, const char *name, char *buf)
{
  char *dst;
  const char *src = _sen_sym_key(s->keys, class);
  if (!src) { return sen_invalid_argument; }
  strcpy(buf, src);
  dst = buf + strlen(src);
  *dst++ = '.';
  strcpy(dst, name);
  return sen_success;
}

sen_db_store *
sen_db_class_slot(sen_db *s, sen_id class, const char *name)
{
  char buf[SEN_SYM_MAX_KEY_SIZE];
  if (sen_db_class_slotpath(s, class, name, buf)) { return NULL; }
  return sen_db_store_open(s, buf);
}

sen_db_store *
sen_db_class_add_slot(sen_db *s, sen_id class, const char *name, sen_db_store_spec *spec)
{
  char buf[SEN_SYM_MAX_KEY_SIZE];
  if (sen_db_class_slotpath(s, class, name, buf)) { return NULL; }
  return sen_db_store_create(s, buf, spec);
}

sen_rc
sen_db_store_close(sen_db_store *slot)
{
  sen_db_trigger *t, *t_;
  uint8_t type = slot->type;
  slot->type = 0;
  switch (type) {
  case sen_db_obj_slot :
    // sen_db_class_close(slot->u.o.class);
    sen_ra_close(slot->u.o.ra);
    break;
  case sen_db_ra_slot :
    sen_ra_close(slot->u.f.ra);
    break;
  case sen_db_ja_slot :
    sen_ja_close(slot->u.v.ja);
    break;
  case sen_db_idx_slot :
    sen_index_close(slot->u.i.index);
    break;
  case sen_db_class :
    sen_sym_close(slot->u.c.keys);
    break;
  case sen_db_rel1 :
    sen_ra_close(slot->u.f.ra);
    break;
  default :
    return sen_invalid_argument;
  }
  for (t = slot->triggers; t; t = t_) {
    t_ = t->next;
    SEN_GFREE(t);
  }
  return sen_success;
}

sen_rc
sen_db_store_remove(sen_db *s, const char *name)
{
  sen_rc rc;
  sen_id id;
  sen_db_store *e;
  uint32_t spec_len;
  char buffer[PATH_MAX];
  sen_db_store_spec *spec;
  if (!s || !(id = sen_sym_at(s->keys, name))) { return sen_invalid_argument; }
  if ((e = sen_array_at(&s->stores, id)) && (e->type)) { sen_db_store_close(e); }
  if (!(spec = sen_ja_ref(s->values, id, &spec_len))) {
    return sen_invalid_format;
  }
  {
    int i;
    sen_db_store *target;
    const sen_db_store_rel_spec *t;
    sen_db_store_rel_spec invrs;
    invrs.target = id;
    for (t = spec->triggers, i = spec->n_triggers; i; t++, i--) {
      if (t->target && (target = sen_db_store_by_id(s, t->target))) {
        if ((rc = sen_db_store_del_trigger(target, &invrs))) {
          SEN_LOG(sen_log_notice, "sen_db_store_del_trigger failed(%d)", t->target);
        }
      }
    }
  }
  if (sen_db_lock(s, -1)) {
    SEN_LOG(sen_log_crit, "sen_db_store_remove: lock failed");
    sen_ja_unref(s->values, id, spec, spec_len);
    return sen_abnormal_error;
  }
  MUTEX_LOCK(s->lock);
  gen_pathname(s->keys->io->path, buffer, id);
  switch (spec->type) {
  case sen_db_raw_class :
    /* todo */
    break;
  case sen_db_class :
    rc = sen_sym_remove(buffer);
    break;
  case sen_db_obj_slot :
    rc = sen_ra_remove(buffer);
    break;
  case sen_db_ra_slot :
    rc = sen_ra_remove(buffer);
    break;
  case sen_db_ja_slot :
    rc = sen_ja_remove(buffer);
    break;
  case sen_db_idx_slot :
    rc = sen_inv_remove(buffer);
    break;
  case sen_db_rel1 :
    rc = sen_ra_remove(buffer);
    break;
  default :
    break;
  }
  sen_ja_unref(s->values, id, spec, spec_len);
  rc = sen_ja_put(s->values, id, NULL, 0, 0);
  rc = sen_sym_del(s->keys, name);
  MUTEX_UNLOCK(s->lock);
  sen_db_unlock(s);
  return rc;
}

sen_rc
sen_db_class_del_slot(sen_db *s, sen_id class, const char *name)
{
  char buf[SEN_SYM_MAX_KEY_SIZE];
  if (sen_db_class_slotpath(s, class, name, buf)) { return sen_invalid_argument; }
  return sen_db_store_remove(s, buf);
}

sen_rc
sen_db_prepare_builtin_class(sen_db *db)
{
  sen_db_store_spec spec;
  spec.type = sen_db_raw_class;
  spec.n_triggers = 0;
  spec.u.c.flags = 0;
  spec.u.c.encoding = db->keys->encoding;
  /* 1 */
  spec.u.c.size = sizeof(int32_t);
  if (!sen_db_store_create(db, "<int>", &spec)) { return sen_memory_exhausted; }
  /* 2 */
  spec.u.c.size = sizeof(uint32_t);
  if (!sen_db_store_create(db, "<uint>", &spec)) { return sen_memory_exhausted; }
  /* 3 */
  spec.u.c.size = sizeof(int64_t);
  if (!sen_db_store_create(db, "<int64>", &spec)) { return sen_memory_exhausted; }
  /* 4 */
  spec.u.c.size = sizeof(double);
  if (!sen_db_store_create(db, "<float>", &spec)) { return sen_memory_exhausted; }
  /* 5 */
  spec.u.c.size = SEN_SYM_MAX_KEY_SIZE;
  if (!sen_db_store_create(db, "<shorttext>", &spec)) { return sen_memory_exhausted; }
  /* 6 */
  spec.u.c.size = 1 << 16;
  if (!sen_db_store_create(db, "<text>", &spec)) { return sen_memory_exhausted; }
  /* 7 */
  spec.u.c.size = JA_SEGMENT_SIZE;
  if (!sen_db_store_create(db, "<longtext>", &spec)) { return sen_memory_exhausted; }
  /* 8 */
  spec.u.c.size = sizeof(sen_timeval);
  if (!sen_db_store_create(db, "<time>", &spec)) { return sen_memory_exhausted; }
  return sen_success;
}

sen_db *
sen_db_create(const char *path, int flags, sen_encoding encoding)
{
  sen_db *s;
  ERRCLR(NULL);
  if (strlen(path) <= PATH_MAX - 14) {
    if ((s = SEN_GMALLOC(sizeof(sen_db)))) {
      sen_array_init(&s->stores, sizeof(sen_db_store), sen_array_clear);
      if ((s->keys = sen_sym_create(path, 0, flags, encoding))) {
        char buffer[PATH_MAX];
        gen_pathname(path, buffer, 0);
        if ((s->values = sen_ja_create(buffer, JA_SEGMENT_SIZE))) {
          MUTEX_INIT(s->lock);
          sen_db_prepare_builtin_class(s);
          if (!ERRP(&sen_gctx, SEN_ERROR)) {
            SEN_LOG(sen_log_notice, "db created (%s) flags=%x", path, s->keys->flags);
            sen_gctx.db = s;
            sen_gctx.encoding = encoding;
            return s;
          }
        } else {
          GERR(sen_memory_exhausted, "ja create failed");
        }
        sen_sym_close(s->keys);
      } else {
        GERR(sen_memory_exhausted, "s->keys create failed");
      }
      sen_array_fin(&s->stores);
      SEN_GFREE(s);
    } else {
      GERR(sen_memory_exhausted, "sen_db alloc failed");
    }
  } else {
    GERR(sen_invalid_argument, "too long path");
  }
  return NULL;
}

sen_db *
sen_db_open(const char *path)
{
  sen_db *s;
  ERRCLR(NULL);
  if (strlen(path) <= PATH_MAX - 14) {
    if ((s = SEN_GMALLOC(sizeof(sen_db)))) {
      sen_array_init(&s->stores, sizeof(sen_db_store), sen_array_clear);
      if ((s->keys = sen_sym_open(path))) {
        char buffer[PATH_MAX];
        gen_pathname(path, buffer, 0);
        if ((s->values = sen_ja_open(buffer))) {
          SEN_LOG(sen_log_notice, "db opened (%s) flags=%x", path, s->keys->flags);
          sen_gctx.db = s;
          sen_gctx.encoding = s->keys->encoding;
          MUTEX_INIT(s->lock);
          return s;
        } else {
          GERR(sen_memory_exhausted, "ja open failed");
        }
        sen_sym_close(s->keys);
      } else {
        GERR(sen_memory_exhausted, "s->keys open failed");
      }
      sen_array_fin(&s->stores);
      SEN_GFREE(s);
    } else {
      GERR(sen_memory_exhausted, "sen_db alloc failed");
    }
  } else {
    GERR(sen_invalid_argument, "too long path");
  }
  return NULL;
}

sen_rc
sen_db_close(sen_db *s)
{
  if (!s) { return sen_invalid_argument; }
  sen_sym_close(s->keys);
  sen_ja_close(s->values);
  {
    sen_id id;
    sen_db_store *sp;
    SEN_ARRAY_EACH(&s->stores, id, sp, {
      if (sp->type) { sen_db_store_close(sp); }
    });
  }
  sen_array_fin(&s->stores);
  SEN_GFREE(s);
  return sen_success;
}

/**** vgram ****/

static int len_sum = 0;
static int img_sum = 0;
static int simple_sum = 0;
static int skip_sum = 0;

sen_vgram *
sen_vgram_create(const char *path)
{
  sen_vgram *s;
  if (!(s = SEN_GMALLOC(sizeof(sen_vgram)))) { return NULL; }
  s->vgram = sen_sym_create(path, sizeof(sen_id) * 2, 0, sen_enc_none);
  if (!s->vgram) {
    SEN_GFREE(s);
    return NULL;
  }
  return s;
}

sen_vgram *
sen_vgram_open(const char *path)
{
  sen_vgram *s;
  if (!(s = SEN_GMALLOC(sizeof(sen_vgram)))) { return NULL; }
  s->vgram = sen_sym_open(path);
  if (!s->vgram) {
    SEN_GFREE(s);
    return NULL;
  }
  return s;
}

sen_vgram_buf *
sen_vgram_buf_open(size_t len)
{
  sen_vgram_buf *b;
  if (!(b = SEN_GMALLOC(sizeof(sen_vgram_buf)))) { return NULL; }
  b->len = len;
  b->tvs = b->tvp = SEN_GMALLOC(sizeof(sen_id) * len);
  if (!b->tvp) { SEN_GFREE(b); return NULL; }
  b->tve = b->tvs + len;
  b->vps = b->vpp = SEN_GMALLOC(sizeof(sen_vgram_vnode) * len * 2);
  if (!b->vpp) { SEN_GFREE(b->tvp); SEN_GFREE(b); return NULL; }
  b->vpe = b->vps + len;
  return b;
}

sen_rc
sen_vgram_buf_add(sen_vgram_buf *b, sen_id tid)
{
  uint8_t dummybuf[8], *dummyp;
  if (b->tvp < b->tve) { *b->tvp++ = tid; }
  dummyp = dummybuf;
  SEN_B_ENC(tid, dummyp);
  simple_sum += dummyp - dummybuf;
  return sen_success;
}

typedef struct {
  sen_id vid;
  sen_id tid;
} vgram_key;

sen_rc
sen_vgram_update(sen_vgram *vgram, sen_id rid, sen_vgram_buf *b, sen_set *terms)
{
  sen_inv_updspec **u;
  if (b && b->tvs < b->tvp) {
    sen_id *t0, *tn;
    for (t0 = b->tvs; t0 < b->tvp - 1; t0++) {
      sen_vgram_vnode *v, **vp;
      if (sen_set_at(terms, t0, (void **) &u)) {
        vp = &(*u)->vnodes;
        for (tn = t0 + 1; tn < b->tvp; tn++) {
          for (v = *vp; v && v->tid != *tn; v = v->cdr) ;
          if (!v) {
            if (b->vpp < b->vpe) {
              v = b->vpp++;
            } else {
              // todo;
              break;
            }
            v->car = NULL;
            v->cdr = *vp;
            *vp = v;
            v->tid = *tn;
            v->vid = 0;
            v->freq = 0;
            v->len = tn - t0;
          }
          v->freq++;
          if (v->vid) {
            vp = &v->car;
          } else {
            break;
          }
        }
      }
    }
    {
      sen_set *th = sen_set_open(sizeof(sen_id), sizeof(int), 0);
      if (!th) { return sen_memory_exhausted; }
      if (t0 == b->tvp) { SEN_LOG(sen_log_debug, "t0 == tvp"); }
      for (t0 = b->tvs; t0 < b->tvp; t0++) {
        sen_id vid, vid0 = *t0, vid1 = 0;
        sen_vgram_vnode *v, *v2 = NULL, **vp;
        if (sen_set_at(terms, t0, (void **) &u)) {
          vp = &(*u)->vnodes;
          for (tn = t0 + 1; tn < b->tvp; tn++) {
            for (v = *vp; v; v = v->cdr) {
              if (!v->vid && (v->freq < 2 || v->freq * v->len < 4)) {
                *vp = v->cdr;
                v->freq = 0;
              }
              if (v->tid == *tn) { break; }
              vp = &v->cdr;
            }
            if (v) {
              if (v->freq) {
                v2 = v;
                vid1 = vid0;
                vid0 = v->vid;
              }
              if (v->vid) {
                vp = &v->car;
                continue;
              }
            }
            break;
          }
        }
        if (v2) {
          if (!v2->vid) {
            vgram_key key;
            key.vid = vid1;
            key.tid = v2->tid;
            if (!(v2->vid = sen_sym_get(vgram->vgram, (char *)&key))) {
              sen_set_close(th);
              return sen_memory_exhausted;
            }
          }
          vid = *t0 = v2->vid * 2 + 1;
          memset(t0 + 1, 0, sizeof(sen_id) * v2->len);
          t0 += v2->len;
        } else {
          vid = *t0 *= 2;
        }
        {
          int *tf;
          if (!sen_set_get(th, &vid, (void **) &tf)) {
            sen_set_close(th);
            return sen_memory_exhausted;
          }
          (*tf)++;
        }
      }
      if (!th->n_entries) { SEN_LOG(sen_log_debug, "th->n_entries == 0"); }
      {
        int j = 0;
        int skip = 0;
        sen_set_eh *ehs, *ehp, *ehe;
        sen_set_sort_optarg arg;
        uint8_t *ps = SEN_GMALLOC(b->len * 2), *pp, *pe;
        if (!ps) {
          sen_set_close(th);
          return sen_memory_exhausted;
        }
        pp = ps;
        pe = ps + b->len * 2;
        arg.mode = sen_sort_descending;
        arg.compar = NULL;
        arg.compar_arg = (void *)(intptr_t)sizeof(sen_id);
        arg.compar_arg0 = NULL;
        ehs = sen_set_sort(th, 0, &arg);
        if (!ehs) {
          SEN_GFREE(ps);
          sen_set_close(th);
          return sen_memory_exhausted;
        }
        SEN_B_ENC(th->n_entries, pp);
        for (ehp = ehs, ehe = ehs + th->n_entries; ehp < ehe; ehp++, j++) {
          int *id = (int *)SEN_SET_INTVAL(*ehp);
          SEN_B_ENC(*SEN_SET_INTKEY(*ehp), pp);
          *id = j;
        }
        for (t0 = b->tvs; t0 < b->tvp; t0++) {
          if (*t0) {
            int *id;
            if (!sen_set_at(th, t0, (void **) &id)) {
              SEN_LOG(sen_log_error, "lookup error (%d)", *t0);
            }
            SEN_B_ENC(*id, pp);
          } else {
            skip++;
          }
        }
        len_sum += b->len;
        img_sum += pp - ps;
        skip_sum += skip;
        SEN_GFREE(ehs);
        SEN_GFREE(ps);
      }
      sen_set_close(th);
    }
  }
  return sen_success;
}

sen_rc
sen_vgram_buf_close(sen_vgram_buf *b)
{
  if (!b) { return sen_invalid_argument; }
  if (b->tvs) { SEN_GFREE(b->tvs); }
  if (b->vps) { SEN_GFREE(b->vps); }
  SEN_GFREE(b);
  return sen_success;
}

sen_rc
sen_vgram_close(sen_vgram *vgram)
{
  if (!vgram) { return sen_invalid_argument; }
  SEN_LOG(sen_log_debug, "len=%d img=%d skip=%d simple=%d", len_sum, img_sum, skip_sum, simple_sum);
  sen_sym_close(vgram->vgram);
  SEN_GFREE(vgram);
  return sen_success;
}
