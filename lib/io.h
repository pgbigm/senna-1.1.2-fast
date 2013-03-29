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
#ifndef SEN_IO_H
#define SEN_IO_H

#ifndef SENNA_H
#include "senna_in.h"
#endif /* SENNA_H */

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef WIN32
#ifdef WIN32_FMO_EACH
#define SEN_IO_FILE_SIZE               1073741824UL
#else /* FMO_EACH */
#define SEN_IO_FILE_SIZE               134217728L
#endif /* FMO_EACH */
#define SEN_IO_COPY sen_io_rdonly
#define SEN_IO_UPDATE sen_io_wronly
#else /* WIN32 */
#define SEN_IO_FILE_SIZE               1073741824UL
#define SEN_IO_COPY sen_io_rdwr
#define SEN_IO_UPDATE sen_io_rdwr
#endif /* WIN32 */

typedef enum {
  sen_io_rdonly,
  sen_io_wronly,
  sen_io_rdwr
} sen_io_rw_mode;

typedef enum {
  sen_io_auto,
  sen_io_manual
} sen_io_mode;

typedef struct _sen_io sen_io;

typedef struct {
  sen_io *io;
  sen_ctx *ctx;
  sen_io_rw_mode mode;
  uint32_t segment;
  uint32_t offset;
  uint32_t size;
  uint32_t nseg;
  off_t pos;
  void *addr;
  uint32_t diff;
  int cached;
#if defined(WIN32) && defined(WIN32_FMO_EACH)
  HANDLE fmo;
#endif /* defined(WIN32) && defined(WIN32_FMO_EACH) */
} sen_io_win;

typedef struct {
  void *map;
  uint32_t nref;
  uint32_t count;
#if defined(WIN32) && defined(WIN32_FMO_EACH)
  HANDLE fmo;
#endif /* defined(WIN32) && defined(WIN32_FMO_EACH) */
} sen_io_mapinfo;

struct _sen_io {
  char path[PATH_MAX];
  struct _sen_io_header *header;
  byte *user_header;
  sen_io_mapinfo *maps;
  uint32_t *nrefs;
  uint32_t base;
  uint32_t base_seg;
  sen_io_mode mode;
  uint32_t cache_size;
  struct _sen_io_fileinfo *fis;
  uint32_t nmaps;
  uint32_t count;
  uint8_t v08p;
};

sen_io *sen_io_create(const char *path, uint32_t header_size, uint32_t segment_size,
		      uint32_t max_segment, sen_io_mode mode, unsigned int cache_size);
sen_io *sen_io_open(const char *path, sen_io_mode mode, uint32_t cache_size);
sen_rc sen_io_close(sen_io *io);
sen_rc sen_io_remove(const char *path);
sen_rc sen_io_size(sen_io *io, uint64_t *size);
sen_rc sen_io_rename(const char *old_name, const char *new_name);
void *sen_io_header(sen_io *io);

void *sen_io_win_map(sen_io *io, sen_ctx *ctx, sen_io_win *iw, uint32_t segment,
		     uint32_t offset, uint32_t size, sen_io_rw_mode mode);
sen_rc sen_io_win_mapv(sen_io_win **list, sen_ctx *ctx, int nent);
sen_rc sen_io_win_unmap(sen_io_win *iw);

void * sen_io_seg_ref(sen_io *io, uint32_t segno);
sen_rc sen_io_seg_unref(sen_io *io, uint32_t segno);
sen_rc sen_io_seg_expire(sen_io *io, uint32_t segno, uint32_t nretry);

typedef struct _sen_io_ja_einfo sen_io_ja_einfo;
typedef struct _sen_io_ja_ehead sen_io_ja_ehead;

struct _sen_io_ja_einfo {
  uint32_t pos;
  uint32_t size;
};

struct _sen_io_ja_ehead {
  uint32_t size;
  uint32_t key;
};

sen_rc sen_io_read_ja(sen_io *io, sen_ctx *ctx, sen_io_ja_einfo *einfo, uint32_t epos,
                      uint32_t key, uint32_t segment, uint32_t offset,
                      void **value, uint32_t *value_len);
sen_rc sen_io_write_ja(sen_io *io, sen_ctx *ctx,
                       uint32_t key, uint32_t segment, uint32_t offset,
                       void *value, uint32_t value_len);

sen_rc sen_io_write_ja_ehead(sen_io *io, sen_ctx *ctx, uint32_t key,
                             uint32_t segment, uint32_t offset, uint32_t value_len);

#define SEN_IO_MAX_REF 0x80000000
#define SEN_IO_MAX_RETRY 0x10000

void sen_io_seg_map_(sen_io *io, uint32_t segno, sen_io_mapinfo *info);

/* arguments must be validated by caller;
 * io mustn't be NULL;
 * segno must be in valid range;
 * addr must be set NULL;
 */
#define SEN_IO_SEG_REF(io,segno,addr) {\
  uint32_t retry;\
  sen_io_mapinfo *info = &(io)->maps[segno];\
  /* uint32_t *pnref = &(io)->nrefs[segno]; */\
  uint32_t *pnref = &info->nref;\
  for (retry = 0;; retry++) {\
    uint32_t nref;\
    SEN_ATOMIC_ADD_EX(pnref, 1, nref);\
    if (nref > 10000) {\
      SEN_LOG(sen_log_alert, "strange nref value! in SEN_IO_SEG_REF(%p, %u, %u)", io, segno, nref);\
    }\
    if (nref >= SEN_IO_MAX_REF) {\
      SEN_ATOMIC_ADD_EX(pnref, -1, nref);\
      if (retry >= SEN_IO_MAX_RETRY) {\
	SEN_LOG(sen_log_crit, "deadlock detected! in SEN_IO_SEG_REF(%p, %u, %u)", io, segno, nref);\
        *pnref = 0; /* force reset */ \
	break;\
      }\
      usleep(1000);\
      continue;\
    }\
    if (!info->map) {\
      if (nref) {\
	SEN_ATOMIC_ADD_EX(pnref, -1, nref);\
	if (retry >= SEN_IO_MAX_RETRY) {\
	  SEN_LOG(sen_log_crit, "deadlock detected!! in SEN_IO_SEG_REF(%p, %u, %u)", io, segno, nref);\
	  break;\
	}\
	usleep(1000);\
	continue;\
      } else {\
	sen_io_seg_map_(io, segno, info);\
        if (!info->map) {\
          SEN_ATOMIC_ADD_EX(pnref, -1, nref);\
          SEN_LOG(sen_log_crit, "mmap failed!! in SEN_IO_SEG_REF(%p, %u, %u)", io, segno, nref);\
        }\
      }\
    }\
    addr = info->map;\
    break;\
  }\
  info->count = io->count++;\
}

/* arguments must be validated by caller;
 * io mustn't be NULL;
 * segno must be in valid range;
 */
#define SEN_IO_SEG_UNREF(io,segno) {\
  uint32_t nref;\
  /* uint32_t *pnref = &(io)->nrefs[segno]; */\
  uint32_t *pnref = &(io)->maps[segno].nref;\
  SEN_ATOMIC_ADD_EX(pnref, -1, nref);\
}

/* simply map segment to addr.
   it can't be used with ref/unref/expire */
#define SEN_IO_SEG_MAP(io,segno,addr) {\
  sen_io_mapinfo *info = &(io)->maps[segno];\
  if (!info->map) {\
    uint32_t *pnref = &info->nref;\
    uint32_t retry;\
    for (retry = 0;; retry++) {\
      uint32_t nref;\
      SEN_ATOMIC_ADD_EX(pnref, 1, nref);\
      if (nref) {\
	SEN_ATOMIC_ADD_EX(pnref, -1, nref);\
	if (retry >= SEN_IO_MAX_RETRY) {\
	  SEN_LOG(sen_log_crit, "deadlock detected!! in SEN_IO_SEG_MAP(%p, %u)", io, segno);\
	  break;\
	}\
	usleep(1000);\
	continue;\
      } else {\
	sen_io_seg_map_(io, segno, info);\
        if (!info->map) {\
          SEN_ATOMIC_ADD_EX(pnref, -1, nref);\
          SEN_LOG(sen_log_crit, "mmap failed!! in SEN_IO_SEG_MAP(%p, %u, %u)", io, segno, nref);\
        }\
        break;\
      }\
    }\
  }\
  addr = info->map;\
}

uint32_t sen_io_base_seg(sen_io *io);
const char *sen_io_path(sen_io *io);

#ifdef __cplusplus
}
#endif

/* encode/decode */

#define SEN_B_ENC(v,p) \
{ \
  uint8_t *_p = (uint8_t *)p; \
  uint32_t _v = v; \
  if (_v < 0x8f) { \
    *_p++ = _v; \
  } else if (_v < 0x408f) { \
    _v -= 0x8f; \
    *_p++ = 0xc0 + (_v >> 8); \
    *_p++ = _v & 0xff; \
  } else if (_v < 0x20408f) { \
    _v -= 0x408f; \
    *_p++ = 0xa0 + (_v >> 16); \
    *_p++ = (_v >> 8) & 0xff; \
    *_p++ = _v & 0xff; \
  } else if (_v < 0x1020408f) { \
    _v -= 0x20408f; \
    *_p++ = 0x90 + (_v >> 24); \
    *_p++ = (_v >> 16) & 0xff; \
    *_p++ = (_v >> 8) & 0xff; \
    *_p++ = _v & 0xff; \
  } else { \
    *_p++ = 0x8f; \
    *((uint32_t *)_p) = _v; \
    _p += sizeof(uint32_t); \
  } \
  p = _p; \
}

#define SEN_B_DEC(v,p) \
{ \
  uint8_t *_p = (uint8_t *)p; \
  uint32_t _v = *_p++; \
  switch (_v >> 4) { \
  case 0x08 : \
    if (_v == 0x8f) { \
      _v = *((uint32_t *)_p); \
      _p += sizeof(uint32_t); \
    } \
    break; \
  case 0x09 : \
    _v = (_v - 0x90) * 0x100 + *_p++; \
    _v = _v * 0x100 + *_p++; \
    _v = _v * 0x100 + *_p++ + 0x20408f; \
    break; \
  case 0x0a : \
  case 0x0b : \
    _v = (_v - 0xa0) * 0x100 + *_p++; \
    _v = _v * 0x100 + *_p++ + 0x408f; \
    break; \
  case 0x0c : \
  case 0x0d : \
  case 0x0e : \
  case 0x0f : \
    _v = (_v - 0xc0) * 0x100 + *_p++ + 0x8f; \
    break; \
  } \
  v = _v; \
  p = _p; \
}

#define SEN_B_SKIP(p) \
{ \
  uint8_t *_p = (uint8_t *)p; \
  uint32_t _v = *_p++; \
  switch (_v >> 4) { \
  case 0x08 : \
    if (_v == 0x8f) { \
      _p += sizeof(uint32_t); \
    } \
    break; \
  case 0x09 : \
    _p += 3; \
    break; \
  case 0x0a : \
  case 0x0b : \
    _p += 2; \
    break; \
  case 0x0c : \
  case 0x0d : \
  case 0x0e : \
  case 0x0f : \
    _p += 1; \
    break; \
  } \
  p = _p; \
}

#define SEN_B_COPY(p2,p1) \
{ \
  uint32_t size = 0, _v = *p1++; \
  *p2++ = _v; \
  switch (_v >> 4) { \
  case 0x08 : \
    size = (_v == 0x8f) ? 4 : 0; \
    break; \
  case 0x09 : \
    size = 3; \
    break; \
  case 0x0a : \
  case 0x0b : \
    size = 2; \
    break; \
  case 0x0c : \
  case 0x0d : \
  case 0x0e : \
  case 0x0f : \
    size = 1; \
    break; \
  } \
  while (size--) { *p2++ = *p1++; } \
}

#endif /* SEN_IO_H */
