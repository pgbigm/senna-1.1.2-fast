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
#include <stdio.h> /* for debug */
#include <string.h>
#include <sys/stat.h>
#include "sym.h"

#define SEN_SYM_HEADER_SIZE 0x10000
#define SEN_IDSTR "SENNA:SYM:00.00"
#define SEN_SYM_DELETED (SEN_SYM_MAX_ID + 1)

#define SEN_SYM_SEGMENT_SIZE 0x400000
#define W_OF_KEY_IN_A_SEGMENT 22
#define W_OF_PAT_IN_A_SEGMENT 18
#define W_OF_SIS_IN_A_SEGMENT 19
#define KEY_MASK_IN_A_SEGMENT 0x3fffff
#define PAT_MASK_IN_A_SEGMENT 0x3ffff
#define SIS_MASK_IN_A_SEGMENT 0x7ffff

struct sen_sym_header {
  char idstr[16];
  uint32_t flags;
  sen_encoding encoding;
  uint32_t key_size;
  uint32_t nrecords;
  uint32_t curr_rec;
  int32_t curr_key;
  int32_t curr_del;
  int32_t curr_del2;
  int32_t curr_del3;
  uint8_t segments[SEN_SYM_MAX_SEGMENT];
  sen_sym_delinfo delinfos[SEN_SYM_NDELINFOS];
  sen_id garbages[(SEN_SYM_MAX_KEY_LENGTH + 1) / 8];
  uint32_t lock;
};

typedef struct {
  sen_id r;
  sen_id l;
  uint32_t key;
  uint16_t check;
  /* deleting : 1, immediate : 1, pocket : 14 */
  uint16_t bitfield;
} pat_node;

typedef struct {
  int32_t segno;
  void *addr;
} sen_sym_seginfo;

typedef struct {
  uint8_t v08p;
  sen_io *io;
  struct sen_sym_header *header;
  uint32_t flags;
  sen_encoding encoding;
  uint32_t key_size;
  uint32_t nref;
  uint32_t *lock;
  sen_sym_seginfo keyarray[SEN_SYM_MAX_SEGMENT];
  sen_sym_seginfo patarray[SEN_SYM_MAX_SEGMENT];
  sen_sym_seginfo sisarray[SEN_SYM_MAX_SEGMENT];
}
sen_sym08;

#define SYM ((sen_sym08 *)sym)

#define PAT_DELETING 1
#define PAT_IMMEDIATE 2
#define PAT_POCKET_MAX 0x3fff
#define PAT_DEL(x) ((x)->bitfield & PAT_DELETING)
#define PAT_IMD(x) ((x)->bitfield & PAT_IMMEDIATE)
#define PAT_PKT(x) ((x)->bitfield >> 2)
#define SET_PAT_DEL(x,v) ((x)->bitfield = ((x)->bitfield & 0xfffe) | (v))
#define SET_PAT_IMD(x,v) ((x)->bitfield = ((x)->bitfield & 0xfffd) | (v))
#define SET_PAT_PKT(x,v) ((x)->bitfield = ((x)->bitfield & 3) | ((v) << 2))

typedef struct {
  sen_id children;
  sen_id sibling;
} sis_node;

enum {
  segment_empty = 0,
  segment_key,
  segment_pat,
  segment_sis
};

/* bit operation */

inline static uint32_t
nth_bit(const uint8_t *key, uint32_t n)
{
  return key[n >> 3] & (0x80 >> (n & 7));
}

/* segment operation */

inline static sen_rc
load_all_segments(sen_sym08 *sym)
{
  sen_rc rc = sen_success;
  int seg, nkey = 0, npat = 0, nsis = 0, empty = 0;
  for (seg = 0; seg < SEN_SYM_MAX_SEGMENT; seg++) {
    switch (sym->header->segments[seg]) {
    case segment_empty :
      empty++;
      break;
    case segment_key :
      sym->keyarray[nkey++].segno = seg;
      break;
    case segment_pat :
      sym->patarray[npat++].segno = seg;
      break;
    case segment_sis :
      sym->sisarray[nsis++].segno = seg;
      break;
    default :
      rc = sen_invalid_format;
      break;
    }
  }
  return rc;
}

inline static sen_rc
segment_new(sen_sym08 *sym, int segtype)
{
  int seg = 0, type, n = 0;
  while ((type = sym->header->segments[seg])) {
    if (type == segtype) { n++; }
    if (++seg >= SEN_SYM_MAX_SEGMENT) { return sen_memory_exhausted; }
  }
  sym->header->segments[seg] = segtype;
  switch (segtype) {
  case segment_key :
    sym->keyarray[n].segno = seg;
    sym->header->curr_key = n * SEN_SYM_SEGMENT_SIZE;
    break;
  case segment_pat :
    sym->patarray[n].segno = seg;
    break;
  case segment_sis :
    sym->sisarray[n].segno = seg;
    break;
  }
  return sen_success;
}

#define SEG_LOAD(io,si)\
{\
  int32_t segno = si->segno;\
  if (0 <= segno && segno < SEN_SYM_MAX_SEGMENT) {\
    SEN_IO_SEG_REF(io, segno, si->addr);\
    SEN_IO_SEG_UNREF(io, segno);\
  }\
}

/* patricia array operation */

inline static pat_node *
pat_at(sen_sym08 *sym, sen_id id)
{
  sen_sym_seginfo *si;
  if (id > SEN_SYM_MAX_ID) { return NULL; }
  si = &sym->patarray[id >> W_OF_PAT_IN_A_SEGMENT];
  if (!si->addr) {
    if (si->segno == -1) { load_all_segments(sym); }
    SEG_LOAD(sym->io, si);
    if (!si->addr) { return NULL; }
  }
  return
    (pat_node *)(((byte *)si->addr) + ((id & PAT_MASK_IN_A_SEGMENT) * sizeof(pat_node)));
}

inline static pat_node *
pat_get(sen_sym08 *sym, sen_id id)
{
  sen_sym_seginfo *si;
  if (id > SEN_SYM_MAX_ID) { return NULL; }
  si = &sym->patarray[id >> W_OF_PAT_IN_A_SEGMENT];
  if (!si->addr) {
    if (si->segno == -1) { load_all_segments(sym); }
    while (si->segno == -1) {
      if (segment_new(sym, segment_pat)) { return NULL; }
    }
    SEG_LOAD(sym->io, si);
    if (!si->addr) { return NULL; }
  }
  return
    (pat_node *)(((byte *)si->addr) + ((id & PAT_MASK_IN_A_SEGMENT) * sizeof(pat_node)));
}

inline static pat_node *
pat_node_new(sen_sym08 *sym, sen_id *id)
{
  uint32_t n = sym->header->curr_rec + 1;
  pat_node *res;
  if (n > SEN_SYM_MAX_ID) { return NULL; }
  if ((res = pat_get(sym, n))) {
    sym->header->curr_rec = n;
    sym->header->nrecords++;
  }
  if (id) { *id = n; }
  return res;
}

/* sis operation */

inline static sis_node *
sis_at(sen_sym08 *sym, sen_id id)
{
  sen_sym_seginfo *si;
  if (id > SEN_SYM_MAX_ID) { return NULL; }
  si = &sym->sisarray[id >> W_OF_SIS_IN_A_SEGMENT];
  if (!si->addr) {
    if (si->segno == -1) { load_all_segments(sym); }
    SEG_LOAD(sym->io, si);
    if (!si->addr) { return NULL; }
  }
  return
    (sis_node *)(((byte *)si->addr) + ((id & SIS_MASK_IN_A_SEGMENT) * sizeof(sis_node)));
}

inline static sis_node *
sis_get(sen_sym08 *sym, sen_id id)
{
  sen_sym_seginfo *si;
  if (id == SEN_SYM_NIL || SEN_SYM_MAX_ID < id) { return NULL; }
  si = &sym->sisarray[id >> W_OF_SIS_IN_A_SEGMENT];
  if (!si->addr) {
    if (si->segno == -1) { load_all_segments(sym); }
    while (si->segno == -1) {
      if (segment_new(sym, segment_sis)) { return NULL; }
    }
    SEG_LOAD(sym->io, si);
    if (!si->addr) { return NULL; }
  }
  return
    (sis_node *)(((byte *)si->addr) + ((id & SIS_MASK_IN_A_SEGMENT) * sizeof(sis_node)));
}

#define MAX_LEVEL 16

static void
sis_collect(sen_sym08 *sym, sen_set *h, sen_id id, uint32_t level)
{
  uint32_t *offset;
  sis_node *sl = sis_at(sym, id);
  if (sl) {
    sen_id sid = sl->children;
    while (sid && sid != id) {
      sen_set_get(h, &sid, (void **) &offset);
      *offset = level;
      if (level < MAX_LEVEL) { sis_collect(sym, h, sid, level + 1); }
      if (!(sl = sis_at(sym, sid))) { break; }
      sid = sl->sibling;
    }
  }
}

/* key operation */

#define KEY_AT(sym,pos,ptr) \
{\
  sen_sym_seginfo *si = &sym->keyarray[pos >> W_OF_KEY_IN_A_SEGMENT];\
  if (si->addr) {\
    ptr = (char *)(((byte *)si->addr) + (pos & KEY_MASK_IN_A_SEGMENT));\
  } else {\
    if (si->segno == -1) { load_all_segments(sym); }\
    SEG_LOAD(sym->io, si);\
    ptr = si->addr\
      ? (char *)(((byte *)si->addr) + (pos & KEY_MASK_IN_A_SEGMENT))\
      : NULL;\
  }\
}

inline static uint32_t
key_put(sen_sym08 *sym, const uint8_t *key, int len)
{
  int lpos;
  uint32_t res = sym->header->curr_key;
  if (len >= SEN_SYM_SEGMENT_SIZE) { return 0; }
  lpos = res & KEY_MASK_IN_A_SEGMENT;
  if (lpos + len > SEN_SYM_SEGMENT_SIZE || !lpos) {
    if (segment_new(sym, segment_key)) { return 0; }
    res = sym->header->curr_key;
  }
  {
    char *dest;
    KEY_AT(sym, res, dest);
    if (!dest) { return 0; }
    memcpy(dest, key, len);
  }
  sym->header->curr_key += len;
  return res;
}

inline static char *
pat_node_get_key(sen_sym08 *sym, pat_node *n)
{
  if (PAT_IMD(n)) {
    return (char *)&n->key;
  } else {
    char *res;
    KEY_AT(sym, n->key, res);
    return res;
  }
}

inline static sen_rc
pat_node_set_key(sen_sym08 *sym, pat_node *n, const uint8_t *key, unsigned int len)
{
  if (!key || !len) { return sen_invalid_argument; }
  if (len <= sizeof(uint32_t)) {
    SET_PAT_IMD(n, PAT_IMMEDIATE);
    memcpy(&n->key, key, len);
  } else {
    SET_PAT_IMD(n, 0);
    n->key = key_put(sym, key, len);
  }
  return sen_success;
}

/* delinfo operation */

#define DL_EMPTY  0x00000000
#define DL_PHASE1 0x00000001
#define DL_PHASE2 0x00000002
#define DL_STMASK 0x00000003
#define DL_SHARED 0x80000000
#define DL_LD(x) (((x)->bitfield >> 2) & SEN_SYM_MAX_ID)
#define DL_ST(x) ((x)->bitfield & DL_STMASK)
#define DL_SH(x) ((x)->bitfield & DL_SHARED)
#define SET_DL_LD(x,v) ((x)->bitfield = ((x)->bitfield & (DL_STMASK|DL_SHARED)) | \
                                        (((v) & SEN_SYM_MAX_ID) << 2))
#define SET_DL_ST(x,v) ((x)->bitfield = ((x)->bitfield & ~DL_STMASK) | ((v) & DL_STMASK))
#define SET_DL_SH(x,v) ((x)->bitfield = ((x)->bitfield & ~DL_SHARED) | ((v) & DL_SHARED))

inline static sen_sym_delinfo *
delinfo_search(sen_sym08 *sym, sen_id id)
{
  int i;
  sen_sym_delinfo *di;
  for (i = (sym->header->curr_del2) & SEN_SYM_MDELINFOS;
       i != sym->header->curr_del;
       i = (i + 1) & SEN_SYM_MDELINFOS) {
    di = &sym->header->delinfos[i];
    if (DL_ST(di) != DL_PHASE1) { continue; }
    if (DL_LD(di) == id) { return di; }
    if (di->d == id) { return di; }
  }
  return NULL;
}

inline static sen_rc
delinfo_turn_2(sen_sym08 *sym, sen_sym_delinfo *di)
{
  sen_id d, *p = NULL;
  pat_node *ln, *dn;
  // sen_log("delinfo_turn_2> di->d=%d di->ld=%d stat=%d", di->d, DL_LD(di), DL_ST(di));
  if (DL_ST(di) != DL_PHASE1) { return sen_success; }
  if (!(ln = pat_at(sym, DL_LD(di)))) { return sen_invalid_argument; }
  if (!(d = di->d)) { return sen_invalid_argument; }
  if (!(dn = pat_at(sym, d))) { return sen_invalid_argument; }
  SET_PAT_DEL(ln, 0);
  SET_PAT_DEL(dn, 0);
  {
    sen_id r, *p0;
    pat_node *rn;
    int c;
    size_t len = sym->key_size * 8;
    const char *key = pat_node_get_key(sym, dn);
    if (!key) { return sen_invalid_argument; }
    if (!len) { len = (strlen(key) + 1) * 8; }
    rn = pat_at(sym, 0);
    c = -1;
    p0 = &rn->r;
    while ((r = *p0)) {
      if (r == d) {
        p = p0;
        break;
      }
      if (!(rn = pat_at(sym, r))) { return sen_invalid_format; }
      if (rn->check <= c || len <= rn->check) { break; }
      c = rn->check;
      p0 = nth_bit((uint8_t *)key, c) ? &rn->r : &rn->l;
    }
  }
  if (p) {
    ln->check = dn->check;
    ln->r = dn->r;
    ln->l = dn->l;
    *p = DL_LD(di);
  } else {
    /* debug */
    int j;
    sen_id dd;
    sen_sym_delinfo *ddi;
    SEN_LOG(sen_log_debug, "failed to find d=%d", d);
    for (j = (sym->header->curr_del2 + 1) & SEN_SYM_MDELINFOS;
         j != sym->header->curr_del;
         j = (j + 1) & SEN_SYM_MDELINFOS) {
      ddi = &sym->header->delinfos[j];
      if (DL_ST(ddi) != DL_PHASE1) { continue; }
      if (!(ln = pat_at(sym, DL_LD(ddi)))) { continue; }
      if (!(dd = ddi->d)) { continue; }
      if (d == DL_LD(ddi)) {
        SEN_LOG(sen_log_debug, "found!!!, d(%d) become ld of (%d)", d, dd);
      }
    }
    /* debug */
  }
  SET_DL_ST(di, DL_PHASE2);
  di->d = d;
  // sen_log("delinfo_turn_2< di->d=%d di->ld=%d", di->d, DL_LD(di));
  return sen_success;
}

inline static sen_rc
delinfo_turn_3(sen_sym08 *sym, sen_sym_delinfo *di)
{
  pat_node *dn;
  uint32_t size;
  if (DL_ST(di) != DL_PHASE2) { return sen_success; }
  if (!(dn = pat_at(sym, di->d))) { return sen_invalid_argument; }
  if (DL_SH(di)) {
    SET_PAT_IMD(dn, PAT_IMMEDIATE);
    size = 0;
  } else {
    if (sym->key_size) {
      size = sym->key_size;
    } else {
      // size = dn->check = (uint16_t)(strlen(key) + 1);
      if (PAT_IMD(dn)) {
        size = 0;
      } else {
        const char *key = pat_node_get_key(sym, dn);
        if (!key) { return sen_invalid_argument; }
        size = strlen(key) + 1;
      }
    }
  }
  SET_DL_ST(di, DL_EMPTY);
  dn->r = SEN_SYM_DELETED;
  dn->l = sym->header->garbages[size];
  sym->header->garbages[size] = di->d;
  return sen_success;
}

inline static sen_sym_delinfo *
delinfo_new(sen_sym08 *sym)
{
  sen_sym_delinfo *res = &sym->header->delinfos[sym->header->curr_del];
  uint32_t n = (sym->header->curr_del + 1) & SEN_SYM_MDELINFOS;
  int gap = ((n + SEN_SYM_NDELINFOS - sym->header->curr_del2) & SEN_SYM_MDELINFOS)
            - (SEN_SYM_NDELINFOS / 2);
  while (gap-- > 0) {
    if (delinfo_turn_2(sym, &sym->header->delinfos[sym->header->curr_del2])) {
      SEN_LOG(sen_log_crit, "d2 failed: %d", DL_LD(&sym->header->delinfos[sym->header->curr_del2]));
    }
    sym->header->curr_del2 = (sym->header->curr_del2 + 1) & SEN_SYM_MDELINFOS;
  }
  if (n == sym->header->curr_del3) {
    if (delinfo_turn_3(sym, &sym->header->delinfos[sym->header->curr_del3])) {
      SEN_LOG(sen_log_crit, "d3 failed: %d", DL_LD(&sym->header->delinfos[sym->header->curr_del3]));
    }
    sym->header->curr_del3 = (sym->header->curr_del3 + 1) & SEN_SYM_MDELINFOS;
  }
  sym->header->curr_del = n;
  return res;
}

/* sym operation */

sen_sym *
sen_sym_create08(const char *path, uint32_t key_size,
               uint32_t flags, sen_encoding encoding)
{
  int i;
  sen_io *io;
  sen_sym08 *sym = NULL;
  pat_node *node0;
  struct sen_sym_header *header;
  io = sen_io_create(path, SEN_SYM_HEADER_SIZE, SEN_SYM_SEGMENT_SIZE,
                     SEN_SYM_MAX_SEGMENT, sen_io_auto, SEN_SYM_MAX_SEGMENT);
  if (!io) { return NULL; }
  header = sen_io_header(io);
  memcpy(header->idstr, SEN_IDSTR, 16);
  header->flags = flags;
  header->encoding = encoding;
  header->key_size = key_size;
  header->nrecords = 0;
  header->curr_rec = 0;
  header->curr_key = -1;
  header->curr_del = 0;
  header->curr_del2 = 0;
  header->curr_del3 = 0;
  header->lock = 0;
  for (i = 0; i < SEN_SYM_MAX_SEGMENT; i++) {
    header->segments[i] = segment_empty;
  }
  if (!(sym = SEN_GMALLOC(sizeof(sen_sym08)))) {
    sen_io_close(io);
    return NULL;
  }
  sym->v08p = 1;
  sym->io = io;
  sym->header = header;
  sym->key_size = key_size;
  sym->encoding = encoding;
  sym->flags = flags;
  sym->lock = &header->lock;
  for (i = 0; i < SEN_SYM_MAX_SEGMENT; i++) {
    sym->keyarray[i].segno = -1;
    sym->keyarray[i].addr = NULL;
    sym->patarray[i].segno = -1;
    sym->patarray[i].addr = NULL;
    sym->sisarray[i].segno = -1;
    sym->sisarray[i].addr = NULL;
  }
  node0 = pat_get(sym, 0);
  node0->r = 0;
  node0->l = 0;
  node0->key = 0;
  return (sen_sym *)sym;
}

sen_sym *
sen_sym_open08(const char *path)
{
  int i;
  sen_io *io;
  sen_sym08 *sym = NULL;
  struct sen_sym_header *header;
  io = sen_io_open(path, sen_io_auto, 8192);
  if (!io) { return NULL; }
  header = sen_io_header(io);
  /*
  if (memcmp(header->idstr, SEN_IDSTR, 16)) {
    puts("id not match");
    sen_io_close(io);
    return NULL;
  }
  */
  if (!(sym = SEN_GMALLOC(sizeof(sen_sym08)))) {
    puts("malloc failed");
    sen_io_close(io);
    return NULL;
  }
  sym->v08p = 1;
  sym->io = io;
  sym->header = header;
  sym->key_size = header->key_size;
  sym->encoding = header->encoding;
  sym->flags = header->flags;
  sym->lock = &header->lock;
  for (i = 0; i < SEN_SYM_MAX_SEGMENT; i++) {
    sym->keyarray[i].segno = -1;
    sym->keyarray[i].addr = NULL;
    sym->patarray[i].segno = -1;
    sym->patarray[i].addr = NULL;
    sym->sisarray[i].segno = -1;
    sym->sisarray[i].addr = NULL;
  }
  load_all_segments(sym);
  return (sen_sym *)sym;
}

inline static sen_id
_sen_sym_get(sen_sym08 *sym, const uint8_t *key, uint32_t *new, uint32_t *lkey)
{
  sen_id r, r0, *p0, *p1 = NULL;
  pat_node *rn, *rn0;
  int c = -1, c0 = -1, c1 = -1, len;
  size_t size = sym->key_size;
  *new = 0;
  if (!key) { return SEN_SYM_NIL; }
  if (!size) { size = strlen((char *)key) + 1; }
  len = (int)size * 8;
  if (len > SEN_SYM_MAX_KEY_LENGTH) { return SEN_SYM_NIL; }
  rn0 = pat_at(sym, 0); p0 = &rn0->r;
  if (*p0) {
    char xor, mask;
    const char *s, *d;
    while ((r0 = *p0)) {
      if (!(rn0 = pat_at(sym, r0))) { return 0; }
      if (c0 < rn0->check && rn0->check < len) {
        c1 = c0; c0 = rn0->check;
        p1 = p0; p0 = nth_bit(key, c0) ? &rn0->r : &rn0->l;
      } else {
        if (rn0->check < len && !memcmp(pat_node_get_key(sym, rn0), key, size)) {
          return r0;
        }
        break;
      }
    }
    for (c = 0, s = pat_node_get_key(sym, rn0), d = (char *)key; *s == *d; c += 8, s++, d++);
    for (xor = *s ^ *d, mask = 0x80; !(xor & mask); mask >>= 1, c++);

    if (c == c0 && !*p0) {
      if (c < len - 1) { c++; }
    } else {
      if (c < c0) {
        if (c > c1) {
          p0 = p1;
        } else {
          rn0 = pat_at(sym, 0); p0 = &rn0->r;
          while ((r0 = *p0)) {
            if (!(rn0 = pat_at(sym, r0))) { return 0; }
            if (c < rn0->check) { break; }
            p0 = nth_bit(key, rn0->check) ? &rn0->r : &rn0->l;
          }
        }
      }
    }
    if (c >= len) { return 0; }
  } else {
    c = (len - 1 > SEN_SYM_MAX_KEY_LENGTH) ? SEN_SYM_MAX_KEY_LENGTH : len - 1;
  }
  {
    size_t size2 = size > sizeof(uint32_t) ? size : 0;
    if (*lkey && size2) {
      if (sym->header->garbages[0]) {
        r = sym->header->garbages[0];
        sym->header->nrecords++;
        rn = pat_at(sym, r);
        sym->header->garbages[0] = rn->l;
      } else {
        if (!(rn = pat_node_new(sym, &r))) { return 0; }
      }
      SET_PAT_IMD(rn, 0);
      rn->key = *lkey;
    } else {
      if (sym->header->garbages[size2]) {
        r = sym->header->garbages[size2];
        sym->header->nrecords++;
        rn = pat_at(sym, r);
        sym->header->garbages[size2] = rn->l;
        memcpy(pat_node_get_key(sym, rn), key, size);
      } else {
        if (!(rn = pat_node_new(sym, &r))) { return 0; }
        pat_node_set_key(sym, rn, key, (unsigned int)size);
      }
      *lkey = rn->key;
    }
  }
  rn->check = c;
  SET_PAT_DEL(rn, 0);
  SET_PAT_PKT(rn, 0);
  if (nth_bit(key, c)) {
    rn->r = r;
    rn->l = *p0;
  } else {
    rn->r = *p0;
    rn->l = r;
  }
  *p0 = r;
  *new = 1;
  return r;
}

inline static int
chop(sen_sym08 *sym, const char **key, uint32_t *lkey)
{
  size_t len = sen_str_charlen(*key, sym->encoding);
  if (len) {
    *lkey += len;
    *key += len;
    return **key;
  } else {
    return 0;
  }
}

sen_id
sen_sym_get08(sen_sym *sym, const void *key)
{
  uint32_t new, lkey = 0;
  sen_id r0;
  r0 = _sen_sym_get(SYM, (uint8_t *)key, &new, &lkey);
  if ((SYM->flags & SEN_SYM_WITH_SIS) && (*((uint8_t *)key) & 0x80)) { // todo: refine!!
    sis_node *sl, *sr;
    sen_id l = r0, r;
    if (new && (sl = sis_get(SYM, l))) {
      const char *sis = key;
      sl->children = l;
      sl->sibling = 0;
      while (chop(SYM, &sis, &lkey)) {
        if (!(*sis & 0x80)) { break; }
        r = _sen_sym_get(SYM, (uint8_t *)sis, &new, &lkey);
        if (!(sr = sis_get(SYM, r))) { break; }
        if (new) {
          sl->sibling = r;
          sr->children = l;
          sr->sibling = 0;
        } else {
          sl->sibling = sr->children;
          sr->children = l;
          break;
        }
        l = r;
        sl = sr;
      }
    }
  }
  return r0;
}

sen_id
sen_sym_at08(sen_sym *sym, const void *key)
{
  sen_id r;
  pat_node *rn;
  int c = -1;
  size_t len = SYM->key_size * 8;
  if (!key) { return SEN_SYM_NIL; }
  if (!len) { len = (strlen(key) + 1) * 8; }
  for (r = pat_at(SYM, 0)->r; r; r = nth_bit((uint8_t *)key, c) ? rn->r : rn->l) {
    if (!(rn = pat_at(SYM, r))) { break; /* corrupt? */ }
    if (len <= rn->check) { break; }
    if (rn->check <= c) {
      if (!memcmp(pat_node_get_key(SYM, rn), key, len >> 3)) { return r; }
      break;
    }
    c = rn->check;
  }
  return SEN_SYM_NIL;
}

static void
get_tc(sen_sym08 *sym, sen_set *h, pat_node *rn, unsigned int len)
{
  sen_id id;
  pat_node *node;
  id = rn->r;
  if (id && (node = pat_at(sym, id))) {
    if (node->check > rn->check) {
      sen_set_get(h, &id, NULL);
      get_tc(sym, h, node, len);
    } else if (node->check < len) {
      sen_set_get(h, &id, NULL);
    }
  }
  id = rn->l;
  if (id && (node = pat_at(sym, id))) {
    if (node->check > rn->check) {
      sen_set_get(h, &id, NULL);
      get_tc(sym, h, node, len);
    } else if (node->check < len) {
      sen_set_get(h, &id, NULL);
    }
  }
}

sen_set *
sen_sym_prefix_search08(sen_sym *sym, const void *key)
{
  int c;
  size_t len;
  sen_id r;
  pat_node *rn;
  if (!key || SYM->key_size) { return NULL; } /* only for string */
  len = strlen(key) * 8;
  rn = pat_at(SYM, 0);
  c = -1;
  r = rn->r;
  while (r) {
    if (!(rn = pat_at(SYM, r))) { return NULL; }
    if (c < rn->check && len > rn->check) {
      c = rn->check;
      r = nth_bit((uint8_t *)key, c) ? rn->r : rn->l;
      continue;
    }
    if (!memcmp(pat_node_get_key(SYM, rn), key, len >> 3)) {
      sen_set *h = sen_set_open(sizeof(sen_id), 0, 0);
      SEN_ASSERT(h);
//      if (!h) { return NULL; }
      sen_set_get(h, &r, NULL);
      if (rn->check >= len) { get_tc(SYM, h, rn, (unsigned int)len); }
      return h;
    }
    break;
  }
  return NULL;
}

sen_set *
sen_sym_suffix_search08(sen_sym *sym, const void *key)
{
  sen_id r;
  uint32_t *offset;
  if (!key || SYM->key_size) { return NULL; }
  if ((r = sen_sym_at08(sym, key))) {
    sen_set *h = sen_set_open(sizeof(sen_id), sizeof(uint32_t), 0);
    SEN_ASSERT(h);
//    if (!h) { return NULL; }
    sen_set_get(h, &r, (void **) &offset);
    *offset = 0;
    if (SYM->flags & SEN_SYM_WITH_SIS) { sis_collect(SYM, h, r, 1); }
    return h;
  }
  return NULL;
}

sen_id
sen_sym_common_prefix_search08(sen_sym *sym, const void *key)
{
  pat_node *rn, *rnl;
  sen_id r, rl, r2 = SEN_SYM_NIL;
  int c = -1, cl, len = 0, n, len2 = 8, nth_bit;
  if (!key) { return SEN_SYM_NIL; }
  if (SYM->key_size) { return SEN_SYM_NIL; /* only for string */ }
  // len = (strlen(key) + 1) * 8;
  for (r = pat_at(SYM, 0)->r; r;) {
    if (!(rn = pat_at(SYM, r))) { break; /* corrupt? */ }
    // if (len <= rn->check) { break; }
    if (rn->check <= c) {
      char *p = pat_node_get_key(SYM, rn);
      if (!memcmp(p, key, strlen(p))) { return r; }
      break;
    }
    c = rn->check;
    n = c >> 3;
    while (len < n) { if (!((char *)key)[len++]) { goto exit; } }
    nth_bit = ((uint8_t *)key)[n] & (0x80 >> (c & 7));
    if (nth_bit) {
      if (len2 <= c) {
        len2 = (n + 1) * 8;
        for (cl = c, rl = rn->l; rl; cl = rnl->check, rl = rnl->l) {
          if (!(rnl = pat_at(SYM, rl))) { break; /* corrupt? */ }
          if (len2 <= rnl->check) { break; }
          if (rnl->check <= cl) {
            char *p = pat_node_get_key(SYM, rnl);
            if (!p[n] && !memcmp(p, key, n)) { r2 = rl; }
            break;
          }
        }
      }
      r = rn->r;
    } else {
      r = rn->l;
    }
  }
exit :
  return r2;
}

inline static sen_rc
_sen_sym_del(sen_sym08 *sym, const char *key, int shared)
{
  sen_sym_delinfo *di;
  uint8_t direction;
  pat_node *rn, *rn0 = NULL, *rno;
  int c, c0 = -1;
  size_t len = sym->key_size * 8;
  sen_id r, otherside, *p, *p0 = NULL;
  if (!key) { return sen_invalid_argument; }
  if (!len) { len = (strlen(key) + 1) * 8; }
  di = delinfo_new(sym); /* must be called before find rn */
  SET_DL_SH(di, shared ? DL_SHARED : 0);
  rn = pat_at(sym, 0);
  c = -1;
  p = &rn->r;
  for (;;) {
    if (!(r = *p)) { return sen_invalid_argument; }
    if (!(rn = pat_at(sym, r))) { return sen_invalid_format; }
    if (len <= rn->check) { return sen_invalid_argument; }
    if (c >= rn->check) {
      if (!memcmp(pat_node_get_key(sym, rn), key, len >> 3)) {
        break; /* found */
      } else {  return sen_invalid_argument; }
    }
    c0 = c; c = rn->check;
    p0 = p; p = nth_bit((uint8_t *)key, c) ? &rn->r : &rn->l;
    rn0 = rn;
  }
  direction = (rn0->r == r);
  otherside = direction ? rn0->l : rn0->r;
  if (rn == rn0) {
    SET_DL_ST(di, DL_PHASE2);
    di->d = r;
    if (otherside) {
      rno = pat_at(sym, otherside);
      if (rno && c0 < rno->check && rno->check <= c) {
        if (!delinfo_search(sym, otherside)) {
          SEN_LOG(sen_log_error, "no delinfo found %d", otherside);
        }
        rno->check = 0;
      }
    }
    *p0 = otherside;
  } else {
    sen_sym_delinfo *ldi = NULL, *ddi = NULL;
    if (PAT_DEL(rn)) { ldi = delinfo_search(sym, r); }
    if (PAT_DEL(rn0)) { ddi = delinfo_search(sym, *p0); }
    if (ldi) {
      SET_PAT_DEL(rn, 0);
      SET_DL_ST(di, DL_PHASE2);
      if (ddi) {
        SET_PAT_DEL(rn0, 0);
        SET_DL_ST(ddi, DL_PHASE2);
        if (ddi == ldi) {
          if (r != DL_LD(ddi)) {
            SEN_LOG(sen_log_error, "r(%d) != DL_LD(ddi)(%d)", r, DL_LD(ddi));
          }
          di->d = r;
        } else {
          SET_DL_LD(ldi, DL_LD(ddi));
          di->d = r;
        }
      } else {
        SET_PAT_DEL(rn0, PAT_DELETING);
        SET_DL_LD(ldi, *p0);
        di->d = r;
      }
    } else {
      SET_PAT_DEL(rn, PAT_DELETING);
      if (ddi) {
        if (ddi->d != *p0) {
          SEN_LOG(sen_log_error, "ddi->d(%d) != *p0(%d)", ddi->d, *p0);
        }
        SET_PAT_DEL(rn0, 0);
        SET_DL_ST(ddi, DL_PHASE2);
        SET_DL_ST(di, DL_PHASE1);
        SET_DL_LD(di, DL_LD(ddi));
        di->d = r;
        /*
        SET_PAT_DEL(rn0, 0);
        ddi->d = r;
        SET_DL_ST(di, DL_PHASE2);
        di->d = *p0;
        */
      } else {
        SET_PAT_DEL(rn0, PAT_DELETING);
        SET_DL_ST(di, DL_PHASE1);
        SET_DL_LD(di, *p0);
        di->d = r;
        // sen_log("sym_del d=%d ld=%d stat=%d", r, *p0, DL_PHASE1);
      }
    }
    if (*p0 == otherside) {
      rn0->check = 0;
    } else {
      if (otherside) {
        rno = pat_at(sym, otherside);
        if (rno && c0 < rno->check && rno->check <= c) {
          if (!delinfo_search(sym, otherside)) {
            SEN_LOG(sen_log_error, "no delinfo found %d", otherside);
          }
          rno->check = 0;
        }
      }
      *p0 = otherside;
    }
  }
  sym->header->nrecords--;
  return sen_success;
}

sen_rc
sen_sym_del08(sen_sym *sym, const void *key)
{
  return _sen_sym_del(SYM, key, 0);
}

const char *
_sen_sym_key08(sen_sym *sym, sen_id id)
{
  pat_node *node = pat_at(SYM, id);
  if (!node) { return NULL; }
  return pat_node_get_key(SYM, node);
}

int
sen_sym_key08(sen_sym *sym, sen_id id, void *keybuf, int bufsize)
{
  int len;
  char *key;
  pat_node *node;
  if (!(node = pat_at(SYM, id))) { return 0; }
  if (!(key = pat_node_get_key(SYM, node))) { return 0; }
  if (!(len = SYM->key_size)) { len = (int)(strlen(key) + 1); }
  if (keybuf && bufsize >= len) { memcpy(keybuf, key, len); }
  return len;
}

int
sen_sym_pocket_get08(sen_sym *sym, sen_id id)
{
  pat_node *node = pat_at(SYM, id);
  if (!node) { return -1; }
  return PAT_PKT(node);
}

sen_rc
sen_sym_pocket_set08(sen_sym *sym, sen_id id, unsigned int value)
{
  pat_node *node = pat_at(SYM, id);
  if (value > PAT_POCKET_MAX || !node) { return sen_invalid_argument; }
  SET_PAT_PKT(node, value);
  return sen_success;
}

int
sen_sym_del_with_sis08(sen_sym *sym, sen_id id,
                     int(*func)(sen_id, void *), void *func_arg)
{
  int level = 0, shared;
  const char *key = NULL, *_key;
  sis_node *sp, *ss = NULL, *si = sis_at(SYM, id);
  while (id) {
    if ((si && si->children && si->children != id) || !func(id, func_arg)) { break; }
    if (!(_key = _sen_sym_key08(sym, id))) { break; }
    if (_key == key) {
      shared = 1;
    } else {
      key = _key;
      shared = 0;
    }
    _sen_sym_del(SYM, key, shared);
    if (si) {
      sen_id *p, sid;
      uint32_t lkey = 0;
      if ((*key & 0x80) && chop(SYM, &key, &lkey)) {
        if ((sid = sen_sym_at08(sym, key)) && (ss = sis_at(SYM, sid))) {
          for (p = &ss->children; *p && *p != sid; p = &sp->sibling) {
            if (*p == id) {
              *p = si->sibling;
              break;
            }
            sp = sis_at(SYM, *p);
          }
        }
      } else {
        sid = SEN_SYM_NIL;
      }
      si->sibling = 0;
      si->children = 0;
      id = sid;
      si = ss;
    } else {
      id = SEN_SYM_NIL;
    }
    level++;
  }
  if (level) {
    uint32_t lkey = 0;
    while (id && key) {
      if (_sen_sym_key08(sym, id) != key) { break; }
      {
        pat_node *rn = pat_at(SYM, id);
        if (lkey) {
          rn->key = lkey;
        } else {
          pat_node_set_key(SYM, rn, (uint8_t *)key, (unsigned int)(strlen(key) + 1));
          lkey = rn->key;
        }
      }
      if (!((*key & 0x80) && chop(SYM, &key, &lkey))) { break; }
      id = sen_sym_at08(sym, key);
    }
  }
  return level;
}
