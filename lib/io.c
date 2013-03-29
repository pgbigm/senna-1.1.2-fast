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

#define __USE_GNU /* O_DIRECT */
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>

#include "ctx.h"
#include "io.h"
#include "cache.h"

#define SEN_IO_IDSTR "SENNA:IO:01.000"

/* VA hack */
/* max aio request (/proc/sys/fs/aio-max-nr) */
#define MAX_REQUEST     (64*1024)

#define MEM_ALIGN    sen_cache_block

/* end VA hack */

static unsigned int pagesize = 0;

typedef struct _sen_io_fileinfo {
#ifdef WIN32
  HANDLE fh;
  HANDLE fmo;
  sen_mutex mutex;
#else /* WIN32 */
  int fd;
  dev_t dev;
  ino_t inode;
#endif /* WIN32 */
} fileinfo;

typedef struct _sen_io_header08 {
  char idstr[16];
  uint32_t header_size;
  uint32_t segment_size;
  uint32_t max_segment;
} io_header08;

typedef struct _sen_io_header {
  char idstr[16];
  uint32_t header_size;
  uint32_t segment_size;
  uint32_t max_segment;
  uint32_t reserved;
  uint64_t curr_size;
} io_header;

#define IO_HEADER_SIZE 64

inline static unsigned int sen_get_pagesize(void);
inline static sen_rc sen_open(fileinfo *fi, const char *path, int flags, size_t maxsize);
inline static void sen_fileinfo_init(fileinfo *fis, int nfis);
inline static int sen_opened(fileinfo *fi);
inline static sen_rc sen_close(fileinfo *fi);
#if defined(WIN32) && defined(WIN32_FMO_EACH)
inline static void * sen_mmap(HANDLE *fmo, fileinfo *fi, off_t offset, size_t length);
inline static int sen_munmap(HANDLE *fmo, void *start, size_t length);
#define SEN_MMAP(fmo,fi,offset,length) (sen_mmap((fmo), (fi), (offset), (length)))
#define SEN_MUNMAP(fmo,start,length) (sen_munmap((fmo), (start), (length)))
#else /* defined(WIN32) && defined(WIN32_FMO_EACH) */
inline static void * sen_mmap(fileinfo *fi, off_t offset, size_t length);
inline static int sen_munmap(void *start, size_t length);
#define SEN_MUNMAP(fmo,start,length) (sen_munmap((start), (length)))
#ifdef USE_FAIL_MALLOC
inline static void * sen_fail_mmap(fileinfo *fi, off_t offset, size_t length,
                                   const char* file, int line, const char *func);
#define SEN_MMAP(fmo,fi,offset,length) \
  (sen_fail_mmap((fi), (offset), (length), __FILE__, __LINE__, __FUNCTION__))
#else /* USE_FAIL_MALLOC */
#define SEN_MMAP(fmo,fi,offset,length) (sen_mmap((fi), (offset), (length)))
#endif /* USE_FAIL_MALLOC */
#endif  /* defined(WIN32) && defined(WIN32_FMO_EACH) */
inline static int sen_msync(void *start, size_t length);
inline static sen_rc sen_pread(fileinfo *fi, void *buf, size_t count, off_t offset);
inline static sen_rc sen_pwrite(fileinfo *fi, void *buf, size_t count, off_t offset);

sen_io *
sen_io_create(const char *path, uint32_t header_size, uint32_t segment_size,
              uint32_t max_segment, sen_io_mode mode, uint32_t cache_size)
{
  sen_io *io;
  fileinfo *fis;
  unsigned int b, max_nfiles;
  uint32_t bs, total_header_size;
  io_header *header;
  if (!path || !*path || (strlen(path) > PATH_MAX - 4)) { return NULL; }
  if (!pagesize) { pagesize = sen_get_pagesize(); }
  total_header_size = IO_HEADER_SIZE + max_segment * sizeof(uint32_t) + header_size;
  for (b = pagesize; b < total_header_size; b += pagesize);
  bs = (b + segment_size - 1) / segment_size;
  max_nfiles = (unsigned int)(
    ((uint64_t)segment_size * (max_segment + bs) + SEN_IO_FILE_SIZE - 1)
    / SEN_IO_FILE_SIZE);
  if ((fis = SEN_GMALLOCN(fileinfo, max_nfiles))) {
    sen_fileinfo_init(fis, max_nfiles);
    if (!sen_open(fis, path, O_RDWR|O_CREAT|O_TRUNC, SEN_IO_FILE_SIZE)) {
      if ((header = (io_header *)SEN_MMAP(&fis->fmo, fis, 0, b))) {
        header->header_size = header_size;
        header->segment_size = segment_size;
        header->max_segment = max_segment;
        memcpy(header->idstr, SEN_IO_IDSTR, 16);
        sen_msync(header, b);
        if ((io = SEN_GMALLOCN(sen_io, 1))) {
          if ((io->maps = SEN_GMALLOCN(sen_io_mapinfo, max_segment))) {
            memset(io->maps, 0, sizeof(sen_io_mapinfo) * max_segment);
            strncpy(io->path, path, PATH_MAX);
            io->header = header;
            io->nrefs = (uint32_t *)(((byte *) header) + IO_HEADER_SIZE);
            io->user_header = ((byte *) io->nrefs) + max_segment * sizeof(uint32_t);
            io->base = b;
            io->base_seg = bs;
            io->mode = mode;
            io->cache_size = cache_size;
            io->header->curr_size = b;
            io->fis = fis;
            io->nmaps = 0;
            io->count = 0;
            io->v08p = 0;
            return io;
          }
          SEN_GFREE(io);
        }
        SEN_MUNMAP(&fis->fmo, header, b);
      }
      sen_close(fis);
    }
    SEN_GFREE(fis);
  }
  return NULL;
}

sen_io *
sen_io_open(const char *path, sen_io_mode mode, uint32_t cache_size)
{
  sen_io *io;
  struct stat s;
  fileinfo *fis;
  unsigned int b, max_nfiles;
  uint32_t total_header_size;
  uint32_t header_size = 0, segment_size = 0, max_segment = 0, bs, v08p = 0;
  if (!path || !*path || (strlen(path) > PATH_MAX - 4)) { return NULL; }
  if (!pagesize) { pagesize = sen_get_pagesize(); }
  {
    io_header h;
    int fd = open(path, O_RDWR);
    if (fd == -1) { GSERR(path); return NULL; }
    if (fstat(fd, &s) != -1 && s.st_size >= sizeof(io_header)) {
      if (read(fd, &h, sizeof(io_header)) == sizeof(io_header)) {
        if (memcmp(h.idstr, SEN_IO_IDSTR, 16)) {
          /* regard as v0.8 format */
          v08p = 1;
        }
        header_size = h.header_size;
        segment_size = h.segment_size;
        max_segment = h.max_segment;
      }
    }
    close(fd);
    if (!segment_size) { return NULL; }
  }
  if (v08p) {
    total_header_size = IO_HEADER_SIZE + header_size;
  } else {
    total_header_size = IO_HEADER_SIZE + header_size + max_segment * sizeof(uint32_t);
  }
  for (b = pagesize; b < total_header_size; b += pagesize);
  bs = (b + segment_size - 1) / segment_size;
  max_nfiles = (unsigned int)(
    ((uint64_t)segment_size * (max_segment + bs) + SEN_IO_FILE_SIZE - 1)
    / SEN_IO_FILE_SIZE);
  if (!(fis = SEN_GMALLOCN(fileinfo, max_nfiles))) { return NULL; }
  sen_fileinfo_init(fis, max_nfiles);
  if (!sen_open(fis, path, O_RDWR, SEN_IO_FILE_SIZE)) {
    io_header *header;
    if ((header = SEN_MMAP(&fis->fmo, fis, 0, b))) {
      if ((io = SEN_GMALLOC(sizeof(sen_io)))) {
        if ((io->maps = SEN_GCALLOC(sizeof(sen_io_mapinfo) * max_segment))) {
          strncpy(io->path, path, PATH_MAX);
          io->header = header;
          if (v08p) {
            io->nrefs = SEN_GCALLOC(sizeof(uint32_t) * max_segment);
            io->user_header = ((byte *) header) + sizeof(io_header08);
          } else {
            io->nrefs = (uint32_t *)(((byte *) header) + IO_HEADER_SIZE);
            io->user_header = ((byte *) io->nrefs) + max_segment * sizeof(uint32_t);
          }
          if (io->nrefs) {
            io->base = b;
            io->base_seg = bs;
            io->mode = mode;
            io->cache_size = cache_size;
            io->fis = fis;
            io->nmaps = 0;
            io->count = 0;
            io->v08p = v08p;
            return io;
          }
          SEN_GFREE(io->maps);
        }
        SEN_GFREE(io);
      }
      SEN_MUNMAP(&fis->fmo, header, b);
    }
    sen_close(fis);
  }
  SEN_GFREE(fis);
  return NULL;
}

sen_rc
sen_io_close(sen_io *io)
{
  int i;
  sen_io_mapinfo *mi;
  fileinfo *fi;
  uint32_t bs = io->base_seg;
  uint32_t max_segment = io->header->max_segment;
  uint32_t segment_size = io->header->segment_size;
  unsigned int max_nfiles = (unsigned int)(
    ((uint64_t)segment_size * (max_segment + bs) + SEN_IO_FILE_SIZE - 1)
    / SEN_IO_FILE_SIZE);
  for (mi = io->maps, i = max_segment; i; mi++, i--) {
    if (mi->map) {
      /* if (atomic_read(mi->nref)) { return STILL_IN_USE ; } */
      SEN_MUNMAP(&mi->fmo, mi->map, segment_size);
    }
  }
  SEN_MUNMAP(&io->fis->fmo, io->header, io->base);
  for (fi = io->fis, i = max_nfiles; i; fi++, i--) { sen_close(fi); }
  SEN_GFREE(io->fis);
  SEN_GFREE(io->maps);
  SEN_GFREE(io);
  return sen_success;
}

uint32_t
sen_io_base_seg(sen_io *io)
{
  return io->base_seg;
}

const char *
sen_io_path(sen_io *io)
{
  return io->path;
}

void *
sen_io_header(sen_io *io)
{
  return io->user_header;
}

inline static void
gen_pathname(const char *path, char *buffer, int fno)
{
  size_t len = strlen(path);
  memcpy(buffer, path, len);
  if (fno) {
    buffer[len] = '.';
    sen_str_itoh(fno, buffer + len + 1, 3);
  } else {
    buffer[len] = '\0';
  }
}

sen_rc
sen_io_size(sen_io *io, uint64_t *size)
{
  int fno;
  struct stat s;
  uint64_t tsize = 0;
  char buffer[PATH_MAX];
  uint32_t nfiles;
  if (io->header->curr_size) {
    nfiles = (uint32_t) ((io->header->curr_size + SEN_IO_FILE_SIZE - 1) / SEN_IO_FILE_SIZE);
  } else {
    uint32_t bs = io->base_seg;
    uint32_t max_segment = io->header->max_segment;
    uint32_t segment_size = io->header->segment_size;
    nfiles = (uint32_t) (((uint64_t)segment_size * (max_segment + bs) + SEN_IO_FILE_SIZE - 1)
                    / SEN_IO_FILE_SIZE);
  }
  for (fno = 0; fno < nfiles; fno++) {
    gen_pathname(io->path, buffer, fno);
    if (stat(buffer, &s)) {
      GSERR(buffer);
    } else {
      tsize += s.st_size;
    }
  }
  *size = tsize;
  return sen_success;
}

sen_rc
sen_io_remove(const char *path)
{
  struct stat s;
  if (stat(path, &s)) {
    SEN_LOG(sen_log_info, "stat failed '%s' (%s)", path, strerror(errno));
    return sen_file_operation_error;
  } else if (unlink(path)) {
    GSERR(path);
    return sen_file_operation_error;
  } else {
    int fno;
    char buffer[PATH_MAX];
    for (fno = 1; ; fno++) {
      gen_pathname(path, buffer, fno);
      if (!stat(buffer, &s)) {
        if (unlink(buffer)) { GSERR(buffer); }
      } else {
        break;
      }
    }
    return sen_success;
  }
}

sen_rc
sen_io_rename(const char *old_name, const char *new_name)
{
  struct stat s;
  if (stat(old_name, &s)) {
    SEN_LOG(sen_log_info, "stat failed '%s' (%s)", old_name, strerror(errno));
    return sen_file_operation_error;
  } else if (rename(old_name, new_name)) {
    GSERR(old_name);
    return sen_file_operation_error;
  } else {
    int fno;
    char old_buffer[PATH_MAX];
    char new_buffer[PATH_MAX];
    for (fno = 1; ; fno++) {
      gen_pathname(old_name, old_buffer, fno);
      if (!stat(old_buffer, &s)) {
        gen_pathname(new_name, new_buffer, fno);
        if (rename(old_buffer, new_buffer)) { GSERR(old_buffer); }
      } else {
        break;
      }
    }
    return sen_success;
  }
  return sen_file_operation_error;
}

typedef struct {
  sen_io_ja_ehead head;
  char body[256];
} ja_element;

sen_rc
sen_io_read_ja(sen_io *io, sen_ctx *ctx, sen_io_ja_einfo *einfo, uint32_t epos, uint32_t key,
               uint32_t segment, uint32_t offset, void **value, uint32_t *value_len)
{
  uint32_t rest = 0, size = *value_len + sizeof(sen_io_ja_ehead);
  uint32_t segment_size = io->header->segment_size;
  uint32_t segments_per_file = SEN_IO_FILE_SIZE / segment_size;
  uint32_t bseg = segment + io->base_seg;
  int fno = bseg / segments_per_file;
  fileinfo *fi = &io->fis[fno];
  int base = fno ? 0 : io->base - io->base_seg * segment_size;
  off_t pos = (bseg % segments_per_file) * segment_size + offset + base;
  ja_element *v = SEN_MALLOC(size);
  if (!v) {
    *value = NULL;
    *value_len = 0;
    return sen_memory_exhausted;
  }
  if (pos + size > SEN_IO_FILE_SIZE) {
    rest = pos + size - SEN_IO_FILE_SIZE;
    size = SEN_IO_FILE_SIZE - pos;
  }
  if (!sen_opened(fi)) {
    char path[PATH_MAX];
    gen_pathname(io->path, path, fno);
    if (sen_open(fi, path, O_RDWR|O_CREAT, SEN_IO_FILE_SIZE)) {
      *value = NULL;
      *value_len = 0;
      SEN_FREE(v);
      return sen_file_operation_error;
    }
  }
  if (sen_pread(fi, v, size, pos)) {
    *value = NULL;
    *value_len = 0;
    SEN_FREE(v);
    return sen_file_operation_error;
  }
  if (einfo->pos != epos) {
    SEN_LOG(sen_log_warning, "einfo pos changed %x => %x", einfo->pos, epos);
    *value = NULL;
    *value_len = 0;
    SEN_FREE(v);
    return sen_internal_error;
  }
  if (einfo->size != *value_len) {
    SEN_LOG(sen_log_warning, "einfo size changed %d => %d", einfo->size, *value_len);
    *value = NULL;
    *value_len = 0;
    SEN_FREE(v);
    return sen_internal_error;
  }
  if (v->head.key != key) {
    SEN_LOG(sen_log_error, "ehead key unmatch %x => %x", key, v->head.key);
    *value = NULL;
    *value_len = 0;
    SEN_FREE(v);
    return sen_invalid_format;
  }
  if (v->head.size != *value_len) {
    SEN_LOG(sen_log_error, "ehead size unmatch %d => %d", *value_len, v->head.size);
    *value = NULL;
    *value_len = 0;
    SEN_FREE(v);
    return sen_invalid_format;
  }
  if (rest) {
    byte *vr = (byte *)v + size;
    do {
      fi = &io->fis[++fno];
      if (!sen_opened(fi)) {
        char path[PATH_MAX];
        gen_pathname(io->path, path, fno);
        if (sen_open(fi, path, O_RDWR|O_CREAT, SEN_IO_FILE_SIZE)) {
          *value = NULL;
          *value_len = 0;
          SEN_FREE(v);
          return sen_file_operation_error;
        }
      }
      size = rest > SEN_IO_FILE_SIZE ? SEN_IO_FILE_SIZE : rest;
      if (sen_pread(fi, vr, size, 0)) {
        *value = NULL;
        *value_len = 0;
        SEN_FREE(v);
        return sen_file_operation_error;
      }
      vr += size;
      rest -= size;
    } while (rest);
  }
  *value = v->body;
  return sen_success;
}

sen_rc
sen_io_write_ja(sen_io *io, sen_ctx *ctx, uint32_t key,
                uint32_t segment, uint32_t offset, void *value, uint32_t value_len)
{
  sen_rc rc;
  uint32_t rest = 0, size = value_len + sizeof(sen_io_ja_ehead);
  uint32_t segment_size = io->header->segment_size;
  uint32_t segments_per_file = SEN_IO_FILE_SIZE / segment_size;
  uint32_t bseg = segment + io->base_seg;
  int fno = bseg / segments_per_file;
  fileinfo *fi = &io->fis[fno];
  int base = fno ? 0 : io->base - io->base_seg * segment_size;
  off_t pos = (bseg % segments_per_file) * segment_size + offset + base;
  if (pos + size > SEN_IO_FILE_SIZE) {
    rest = pos + size - SEN_IO_FILE_SIZE;
    size = SEN_IO_FILE_SIZE - pos;
  }
  if (!sen_opened(fi)) {
    char path[PATH_MAX];
    gen_pathname(io->path, path, fno);
    if ((rc = sen_open(fi, path, O_RDWR|O_CREAT, SEN_IO_FILE_SIZE))) { return rc; }
  }
  if (value_len <= 256) {
    ja_element je;
    je.head.size = value_len;
    je.head.key = key;
    memcpy(je.body, value, value_len);
    rc = sen_pwrite(fi, &je, size, pos);
  } else {
    sen_io_ja_ehead eh;
    eh.size = value_len;
    eh.key =  key;
    if ((rc = sen_pwrite(fi, &eh, sizeof(sen_io_ja_ehead), pos))) { return rc; }
    pos += sizeof(sen_io_ja_ehead);
    rc = sen_pwrite(fi, value, size - sizeof(sen_io_ja_ehead), pos);
  }
  if (rc) { return rc; }
  if (rest) {
    byte *vr = (byte *)value + size - sizeof(sen_io_ja_ehead);
    do {
      fi = &io->fis[++fno];
      if (!sen_opened(fi)) {
        char path[PATH_MAX];
        gen_pathname(io->path, path, fno);
        if ((rc = sen_open(fi, path, O_RDWR|O_CREAT, SEN_IO_FILE_SIZE))) { return rc; }
      }
      size = rest > SEN_IO_FILE_SIZE ? SEN_IO_FILE_SIZE : rest;
      if ((rc = sen_pwrite(fi, vr, size, 0))) { return rc; }
      vr += size;
      rest -= size;
    } while (rest);
  }
  return rc;
}

sen_rc
sen_io_write_ja_ehead(sen_io *io, sen_ctx *ctx, uint32_t key,
                      uint32_t segment, uint32_t offset, uint32_t value_len)
{
  sen_rc rc;
  uint32_t segment_size = io->header->segment_size;
  uint32_t segments_per_file = SEN_IO_FILE_SIZE / segment_size;
  uint32_t bseg = segment + io->base_seg;
  int fno = bseg / segments_per_file;
  fileinfo *fi = &io->fis[fno];
  int base = fno ? 0 : io->base - io->base_seg * segment_size;
  off_t pos = (bseg % segments_per_file) * segment_size + offset + base;
  if (!sen_opened(fi)) {
    char path[PATH_MAX];
    gen_pathname(io->path, path, fno);
    if ((rc = sen_open(fi, path, O_RDWR|O_CREAT, SEN_IO_FILE_SIZE))) { return rc; }
  }
  {
    sen_io_ja_ehead eh;
    eh.size = value_len;
    eh.key =  key;
    return sen_pwrite(fi, &eh, sizeof(sen_io_ja_ehead), pos);
  }
}

void *
sen_io_win_map(sen_io *io, sen_ctx *ctx, sen_io_win *iw, uint32_t segment,
               uint32_t offset, uint32_t size, sen_io_rw_mode mode)
{
  byte *p;
  off_t pos;
  int fno, base;
  uint32_t nseg, bseg;
  uint32_t segment_size = io->header->segment_size;
  uint32_t segments_per_file = SEN_IO_FILE_SIZE / segment_size;
  iw->ctx = ctx;
  iw->diff = 0;
  if (offset >= segment_size) {
    segment += offset / segment_size;
    offset = offset % segment_size;
  }
  nseg = (offset + size + segment_size - 1) / segment_size;
  bseg = segment + io->base_seg;
  fno = bseg / segments_per_file;
  base = fno ? 0 : io->base - io->base_seg * segment_size;
  pos = (bseg % segments_per_file) * segment_size + offset + base;
  if (!size || !io || segment + nseg > io->header->max_segment ||
      fno != (bseg + nseg - 1) / segments_per_file) {
    return NULL;
  }
  switch (mode) {
  case sen_io_rdonly:
    {
      fileinfo *fi = &io->fis[fno];
      if (!sen_opened(fi)) {
        char path[PATH_MAX];
        gen_pathname(io->path, path, fno);
        if (sen_open(fi, path, O_RDWR|O_CREAT, SEN_IO_FILE_SIZE)) {
          return NULL;
        }
      }
      if (!(p = SEN_MALLOC(size))) { return NULL; }
      if (sen_pread(fi, p, size, pos)) {
        SEN_FREE(p);
        return NULL;
      }
      iw->addr = p;
    }
    break;
  case sen_io_rdwr:
    // if (nseg > 1) { /* auto unmap is not implemented yet */
    if (nseg > 0) {
      fileinfo *fi = &io->fis[fno];
      if (!sen_opened(fi)) {
        char path[PATH_MAX];
        gen_pathname(io->path, path, fno);
        if (sen_open(fi, path, O_RDWR|O_CREAT, SEN_IO_FILE_SIZE)) {
          return NULL;
        }
      }
      if (!(p = SEN_MMAP(&iw->fmo, fi, pos, nseg * segment_size))) {
        return NULL;
      }
      if (!io->v08p) {
        uint64_t tail = io->base + segment * segment_size + offset + size;
        if (tail > io->header->curr_size) { io->header->curr_size = tail; }
      }
    } else {
      SEN_LOG(sen_log_alert, "nseg == 0! in sen_io_win_map(%p, %u, %u, %u)", io, segment, offset, size);
      return NULL;
      // if (!(p = sen_io_seg_ref(io, segment))) { return NULL; }
    }
    iw->addr = p + offset;
    break;
  case sen_io_wronly:
    if (!(p = SEN_MALLOC(size))) { return NULL; }
    memset(p, 0, size);
    iw->cached = 0;
    iw->addr = p;
    break;
  default :
    return NULL;
  }
  iw->io = io;
  iw->mode = mode;
  iw->segment = segment;
  iw->offset = offset;
  iw->nseg = nseg;
  iw->size = size;
  iw->pos = pos;
  return iw->addr;
}

#ifdef USE_AIO
sen_rc
sen_io_win_mapv(sen_io_win **list, sen_ctx *ctx, int nent)
{
  int i;
  sen_io_win *iw;
  struct aiocb *iocbs[MAX_REQUEST];
  struct aiocb iocb[MAX_REQUEST];
  CacheIOOper oper[MAX_REQUEST];
  int count = 0;

  sen_io_win **clist = list;
  int cl = 0;

retry:
  for (i = 0; i < nent; i++) {
    iw = list[i];
    if (sen_aio_enabled && iw->mode == sen_io_rdonly) {
        /* this block is same as sen_io_win_map() */
        sen_io *io = iw->io;
        uint32_t segment = iw->segment, offset = iw->offset, size = iw->size;
        byte *p;
        off_t pos;
        int fno, base;
        uint32_t nseg, bseg;
        uint32_t segment_size = io->header->segment_size;
        uint32_t segments_per_file = SEN_IO_FILE_SIZE / segment_size;
        fileinfo *fi;
        iw->diff = 0;
        if (offset >= segment_size) {
            segment += offset / segment_size;
            offset = offset % segment_size;
        }
        nseg = (offset + size + segment_size - 1) / segment_size;
        bseg = segment + io->base_seg;
        fno = bseg / segments_per_file;
        base = fno ? 0 : io->base - io->base_seg * segment_size;
        pos = (bseg % segments_per_file) * segment_size + offset + base;
        if (!size || !io || segment + nseg > io->header->max_segment ||
            fno != (bseg + nseg - 1) / segments_per_file) {
          return sen_abnormal_error;
        }
        fi = &io->fis[fno];
        if (!sen_opened(fi)) {
            char path[PATH_MAX];
            gen_pathname(io->path, path, fno);
            if (sen_open(fi, path, O_RDWR|O_CREAT|O_DIRECT, SEN_IO_FILE_SIZE)) {
                return sen_internal_error;
            }
        }
        {
            /* AIO + DIO + cache hack */
            /* calc alignment */
            // todo : calculate curr_size.
            off_t voffset = pos - (pos % MEM_ALIGN);
            uint32_t vsize = pos + size;

            vsize = ((vsize - 1) / MEM_ALIGN + 1) * MEM_ALIGN;
            vsize = vsize - voffset;

            /* diff of aligned offset */
            iw->diff = pos - voffset;

            dp ("pos: %lu, allocate: %d, really needed: %d\n", voffset, vsize, size);
            memset(&oper[count], 0, sizeof (CacheIOOper));
            memset(&iocb[count], 0, sizeof (struct aiocb));
            oper[count].iocb = &iocb[count];
            iocb[count].aio_fildes = fi->fd;
            iocb[count].aio_lio_opcode = LIO_READ;

            if (vsize <= MEM_ALIGN &&
                (p = sen_cache_read (&oper[count], fi->dev, fi->inode, voffset, vsize)) != NULL) {
                /* use cache process  */
                iw->cached = oper[count].cd->num;

                /* Cache require aio_read() or
                   already aio_read() by other process */
                if (oper[count].read == 1) {
                    iocbs[count] = &iocb[count];
                    count++; /* aio count */
                } else if (oper[count].cd->flag == CACHE_READ) {
                    /* this iw is ignored in this loop.
                       should re-check after AIO */
                    clist[cl++] = iw;
                }
            } else {
                /* This size cannot use Cache */
                dp ("Wont use cache offset=%lu size=%u\n", voffset, vsize);

                /* allocate aligned memory */
                if (posix_memalign(&p, MEM_ALIGN, vsize) != 0) {
                    return sen_external_error;
                }
                iocb[count].aio_buf = p;
                iocb[count].aio_nbytes = vsize;
                iocb[count].aio_offset = voffset;
                iocbs[count] = &iocb[count];

                /* This is not cached  */
                oper[count].cd = NULL;
                iw->cached = -1;

                /* aio count up */
                count++;
            }
            iw->addr = p;
            iw->segment = segment;
            iw->offset = offset;
            iw->nseg = nseg;
            iw->size = size;
            iw->pos = pos;
        } /* End  AIO + DIO + cache hack */
    } else {
        if (!sen_io_win_map(iw->io, ctx, iw, iw->segment, iw->offset, iw->size, iw->mode)) {
            return sen_internal_error;
        }
    }
  }

  if (sen_aio_enabled) {
      if (count > 0) {
          int c;

          /* aio_read () */
          if (lio_listio (LIO_WAIT, iocbs, count, NULL) < 0) {
              perror ("lio_listio");
              return sen_external_error;
          }
          for (c=0;c<count;c++) {
              /* cache data is now VALID */
              if (oper[c].cd) oper[c].cd->flag = CACHE_VALID;
          }
      }
      if (cl > 0) {
          /*
           *  if previous loop have CACHE_READ CacheData,
           *  it should retry.
           */
          dp ("-- Validate Reading state CacheData (%d) --\n", cl);
          /* update list and nent for loop */
          list = clist; /* list of iw which use CACHE_READ CacheData */
          nent = cl;    /* number of iw */
          cl = 0;
          count = 0;
          usleep(1);
          goto retry;
      } else
          dp("-- No Reading state CacheData. --\n");
  }
  return sen_success;
}
#endif /* USE_AIO */

sen_rc
sen_io_win_unmap(sen_io_win *iw)
{
  sen_rc rc = sen_success;
  sen_io *io = iw->io;
  sen_ctx *ctx = iw->ctx;
  uint32_t segment_size = io->header->segment_size;
  uint32_t segments_per_file = SEN_IO_FILE_SIZE / segment_size;
  int nseg = iw->nseg;
  switch (iw->mode) {
  case sen_io_rdonly:
#ifdef USE_AIO
    /* VA hack */
    if (!sen_aio_enabled || (iw->cached < 0 && iw->addr)) { SEN_FREE(iw->addr); }
    else if (iw->cached >= 0){
      /* this data is cached */
      sen_cache_data_unref (iw->cached);
      iw->cached = -1;
    }
    /* end VA hack */
#else /* USE_AIO */
    if (iw->addr) { SEN_FREE(iw->addr); }
#endif /* USE_AIO */
    iw->addr = NULL;
    break;
  case sen_io_rdwr:
    // if (nseg > 1) { /* auto unmap is not implemented yet */
    if (nseg > 0) {
      SEN_MUNMAP(&iw->fmo, ((byte *)iw->addr) - iw->offset, nseg * segment_size);
    } else {
      rc = sen_io_seg_unref(io, iw->segment);
#ifdef USE_AIO
      if (sen_aio_enabled) {
        int fno = (iw->segment + io->base_seg) / segments_per_file;
        fileinfo *fi = &io->fis[fno];
        sen_cache_mark_invalid(fi->dev, fi->inode, iw->pos, iw->size);
      }
#endif /* USE_AIO */
    }
    iw->addr = NULL;
    break;
  case sen_io_wronly:
    {
      int fno = (iw->segment + io->base_seg) / segments_per_file;
      fileinfo *fi = &io->fis[fno];
      if (!sen_opened(fi)) {
        char path[PATH_MAX];
        gen_pathname(io->path, path, fno);
        rc = sen_open(fi, path, O_RDWR|O_CREAT, SEN_IO_FILE_SIZE);
      }
      if (!rc) {
        if (!(rc = sen_pwrite(fi, iw->addr, iw->size, iw->pos))) {
          if (!io->v08p) {
            uint64_t tail = io->base + iw->segment * segment_size + iw->offset + iw->size;
            if (tail > io->header->curr_size) { io->header->curr_size = tail; }
          }
          if (!iw->cached) { SEN_FREE(iw->addr); }
          iw->addr = NULL;
        }
#ifdef USE_AIO
        if (sen_aio_enabled) {
          sen_cache_mark_invalid(fi->dev, fi->inode, iw->pos, iw->size);
        }
#endif /* USE_AIO */
      }
    }
    break;
  default :
    rc = sen_invalid_argument;
  }
  return rc;
}

#define SEG_MAP(io,segno,info)\
{\
  uint32_t segment_size = io->header->segment_size;\
  uint32_t segments_per_file = SEN_IO_FILE_SIZE / segment_size;\
  uint32_t bseg = segno + io->base_seg;\
  uint32_t fno = bseg / segments_per_file;\
  uint32_t base = fno ? 0 : io->base - io->base_seg * segment_size;\
  off_t pos = (bseg % segments_per_file) * segment_size + base;\
  fileinfo *fi = &io->fis[fno];\
  if (!sen_opened(fi)) {\
    char path[PATH_MAX];\
    gen_pathname(io->path, path, fno);\
    if (!sen_open(fi, path, O_RDWR|O_CREAT, SEN_IO_FILE_SIZE)) {\
      if ((info->map = SEN_MMAP(&info->fmo, fi, pos, segment_size))) {\
        uint32_t nmaps;\
        SEN_ATOMIC_ADD_EX(&io->nmaps, 1, nmaps);\
        if (!io->v08p) {\
          uint64_t tail = io->base + (segno + 1) * segment_size;\
          if (tail > io->header->curr_size) { io->header->curr_size = tail; }\
        }\
      }\
    }\
  } else {\
    if ((info->map = SEN_MMAP(&info->fmo, fi, pos, segment_size))) {\
      uint32_t nmaps;\
      SEN_ATOMIC_ADD_EX(&io->nmaps, 1, nmaps);\
      if (!io->v08p) {\
        uint64_t tail = io->base + (segno + 1) * segment_size;\
        if (tail > io->header->curr_size) { io->header->curr_size = tail; }\
      }\
    }\
  }\
}

void
sen_io_seg_map_(sen_io *io, uint32_t segno, sen_io_mapinfo *info)
{
  SEG_MAP(io, segno, info);
}

void *
sen_io_seg_ref(sen_io *io, uint32_t segno)
{
  uint32_t retry, *pnref;
  sen_io_mapinfo *info;
  if (segno >= io->header->max_segment) { return NULL; }
  info = &io->maps[segno];
  /* pnref = &io->nrefs[segno]; */
  pnref = &info->nref;
  for (retry = 0;; retry++) {
    uint32_t nref;
    SEN_ATOMIC_ADD_EX(pnref, 1, nref);
    if (nref >= SEN_IO_MAX_REF) {
      SEN_ATOMIC_ADD_EX(pnref, -1, nref);
      if (retry >= SEN_IO_MAX_RETRY) {
        SEN_LOG(sen_log_crit, "deadlock detected! in sen_io_seg_ref(%p, %u)", io, segno);
        return NULL;
      }
      usleep(1000);
      continue;
    }
    if (!info->map) {
      if (nref) {
        SEN_ATOMIC_ADD_EX(pnref, -1, nref);
        if (retry >= SEN_IO_MAX_RETRY) {
          SEN_LOG(sen_log_crit, "deadlock detected!! in sen_io_seg_ref(%p, %u)", io, segno);
          return NULL;
        }
        usleep(1000);
        continue;
      } else {
        SEG_MAP(io, segno, info);
      }
    }
    break;
  }
  info->count = io->count++;
  return info->map;
}

sen_rc
sen_io_seg_unref(sen_io *io, uint32_t segno)
{
  uint32_t nref;
  uint32_t *pnref;
  if (segno >= io->header->max_segment) { return sen_invalid_argument; }
  pnref = &io->maps[segno].nref;
  /* pnref = &io->nrefs[segno]; */
  SEN_ATOMIC_ADD_EX(pnref, -1, nref);
  return sen_success;
}

sen_rc
sen_io_seg_expire(sen_io *io, uint32_t segno, uint32_t nretry)
{
  uint32_t retry, *pnref;
  sen_io_mapinfo *info;
  if (segno >= io->header->max_segment) { return sen_invalid_argument; }
  info = &io->maps[segno];
  /* pnref = &io->nrefs[segno]; */
  pnref = &info->nref;
  for (retry = 0;; retry++) {
    uint32_t nref;
    SEN_ATOMIC_ADD_EX(pnref, 1, nref);
    if (nref) {
      SEN_ATOMIC_ADD_EX(pnref, -1, nref);
      if (retry >= SEN_IO_MAX_RETRY) {
        SEN_LOG(sen_log_crit, "deadlock detected! in sen_io_seg_expire(%p, %u, %u)", io, segno, nref);
        return sen_abnormal_error;
      }
    } else {
      SEN_ATOMIC_ADD_EX(pnref, SEN_IO_MAX_REF, nref);
      if (nref > 1) {
        SEN_ATOMIC_ADD_EX(pnref, -(SEN_IO_MAX_REF + 1), nref);
        if (retry >= SEN_IO_MAX_RETRY) {
          SEN_LOG(sen_log_crit, "deadlock detected!! in sen_io_seg_expire(%p, %u, %u)", io, segno, nref);
          return sen_abnormal_error;
        }
      } else {
        uint32_t nmaps;
        SEN_MUNMAP(&info->fmo, info->map, io->header->segment_size);
        info->map = NULL;
        SEN_ATOMIC_ADD_EX(pnref, -(SEN_IO_MAX_REF + 1), nref);
        SEN_ATOMIC_ADD_EX(&io->nmaps, -1, nmaps);
        return sen_success;
      }
    }
    if (retry >= nretry) { return sen_abnormal_error; }
    usleep(1000);
  }
}

/** mmap abstraction **/

static size_t mmap_size = 0;

#ifdef WIN32

inline static unsigned int
sen_get_pagesize(void)
{
  SYSTEM_INFO si;
  GetSystemInfo( &si );
  return (unsigned int)si.dwAllocationGranularity;
}

#ifdef WIN32_FMO_EACH

inline static sen_rc
sen_open(fileinfo *fi, const char *path, int flags, size_t maxsize)
{
  fi->fh = CreateFile(path, GENERIC_READ | GENERIC_WRITE,
                      FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                      OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
  if (fi->fh == INVALID_HANDLE_VALUE) {
    GERR(sen_file_operation_error, "CreateFile failed");
    return sen_file_operation_error;
  }
  if ((flags & O_TRUNC)) {
    CloseHandle(fi->fh);
    fi->fh = CreateFile(path, GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                        TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (fi->fh == INVALID_HANDLE_VALUE) {
      GERR(sen_file_operation_error, "CreateFile failed");
      return sen_file_operation_error;
    }
  }
  MUTEX_INIT(fi->mutex);
  return sen_success;
}

inline static void *
sen_mmap(HANDLE *fmo, fileinfo *fi, off_t offset, size_t length)
{
  void *res;

  /* MUTEX_LOCK(fi->mutex); */
  /* try to create fmo */
  *fmo = CreateFileMapping(fi->fh, NULL, PAGE_READWRITE, 0, offset + length, NULL);
  if (!*fmo) { return NULL; }
  res = MapViewOfFile(*fmo, FILE_MAP_WRITE, 0, (DWORD)offset, (SIZE_T)length);
  if (!res) {
    sen_index_expire();
    res = MapViewOfFile(*fmo, FILE_MAP_WRITE, 0, (DWORD)offset, (SIZE_T)length);
    if (!res) {
      GMERR("MapViewOfFile failed #%d <%d>", GetLastError(), mmap_size);
      return NULL;
    }
  }
  /* MUTEX_UNLOCK(fi->mutex); */
  mmap_size += length;
  return res;
}

inline static int
sen_munmap(HANDLE *fmo, void *start, size_t length)
{
  int r = 0;
  if (UnmapViewOfFile(start)) {
    mmap_size -= length;
  } else {
    SEN_LOG(sen_log_alert, "munmap(%p,%d) failed #%d <%d>", start, length, GetLastError(), mmap_size);
    r = -1;
  }
  if (*fmo) {
    if (!CloseHandle(*fmo)) {
      SEN_LOG(sen_log_alert, "closehandle(%p,%d) failed #%d", start, length, GetLastError());
    }
    *fmo = NULL;
  } else {
    SEN_LOG(sen_log_alert, "fmo not exists <%p,%d>", start, length);
  }
  return r;
}

inline static sen_rc
sen_close(fileinfo *fi)
{
  if (fi->fmo != NULL) {
    SEN_LOG(sen_log_alert, "file mapping object exists");
  }
  if (fi->fh != INVALID_HANDLE_VALUE) {
    CloseHandle(fi->fh);
    MUTEX_DESTROY(fi->mutex);
    fi->fh = INVALID_HANDLE_VALUE;
  }
  return sen_success;
}

#else /* WIN32_FMO_EACH */
inline static sen_rc
sen_open(fileinfo *fi, const char *path, int flags, size_t maxsize)
{
  /* may be wrong if flags is just only O_RDWR */
  fi->fh = CreateFile(path, GENERIC_READ | GENERIC_WRITE,
                      FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                      OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
  if (fi->fh == INVALID_HANDLE_VALUE) {
    GERR(sen_file_operation_error, "CreateFile failed");
    return sen_file_operation_error;
  }
  if ((flags & O_TRUNC)) {
    CloseHandle(fi->fh);
    /* unable to assign OPEN_ALWAYS and TRUNCATE_EXISTING at once */
    fi->fh = CreateFile(path, GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                        TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (fi->fh == INVALID_HANDLE_VALUE) {
      GERR(sen_file_operation_error, "CreateFile failed");
      return sen_file_operation_error;
    }
  }
  /* signature may be wrong.. */
  fi->fmo = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, NULL);
  /* open failed */
  if (fi->fmo == NULL) {
    // flock
    /* retry to open */
    fi->fmo = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, NULL);
    /* failed again */
    if (fi->fmo == NULL) {
      /* try to create fmo */
      fi->fmo = CreateFileMapping(fi->fh, NULL, PAGE_READWRITE, 0, SEN_IO_FILE_SIZE, NULL);
    }
    // funlock
  }
  if (fi->fmo != NULL) {
    if (GetLastError() != ERROR_ALREADY_EXISTS ) {
      MUTEX_INIT(fi->mutex);
      return sen_success;
    } else {
      SEN_LOG(sen_log_error, "fmo object already exists! handle=%d", fi->fh);
      CloseHandle(fi->fmo);
    }
  } else {
    SEN_LOG(sen_log_alert, "failed to get FileMappingObject #%d", GetLastError());
  }
  CloseHandle(fi->fh);
  GERR(sen_file_operation_error, "OpenFileMapping failed");
  return sen_file_operation_error;
}

inline static void *
sen_mmap(fileinfo *fi, off_t offset, size_t length)
{
  void *res;
  /* file must be exceeded to SEN_IO_FILE_SIZE when FileMappingObject created.
     and, after fmo created, it's not allowed to expand the size of file.
  DWORD tail = (DWORD)(offset + length);
  DWORD filesize = GetFileSize(fi->fh, NULL);
  if (filesize < tail) {
    if (SetFilePointer(fi->fh, tail, NULL, FILE_BEGIN) != tail) {
      sen_log("SetFilePointer failed");
      return NULL;
    }
    if (!SetEndOfFile(fi->fh)) {
      sen_log("SetEndOfFile failed");
      return NULL;
    }
    filesize = tail;
  }
  */
  res = MapViewOfFile(fi->fmo, FILE_MAP_WRITE, 0, (DWORD)offset, (SIZE_T)length);
  if (!res) {
    sen_index_expire();
    res = MapViewOfFile(fi->fmo, FILE_MAP_WRITE, 0, (DWORD)offset, (SIZE_T)length);
    if (!res) {
      GMERR("MapViewOfFile failed #%d  <%d>", GetLastError(), mmap_size);
      return NULL;
    }
  }
  mmap_size += length;
  return res;
}

inline static int
sen_munmap(void *start, size_t length)
{
  if (UnmapViewOfFile(start)) {
    mmap_size -= length;
    return 0;
  } else {
    SEN_LOG(sen_log_alert, "munmap(%p,%d) failed <%d>", start, length, mmap_size);
    return -1;
  }
}

inline static sen_rc
sen_close(fileinfo *fi)
{
  if (fi->fmo != NULL) {
    CloseHandle(fi->fmo);
    fi->fmo = NULL;
  }
  if (fi->fh != INVALID_HANDLE_VALUE) {
    CloseHandle(fi->fh);
    MUTEX_DESTROY(fi->mutex);
    fi->fh = INVALID_HANDLE_VALUE;
  }
  return sen_success;
}
#endif /* WIN32_FMO_EACH */

inline static void
sen_fileinfo_init(fileinfo *fis, int nfis)
{
  for (; nfis--; fis++) {
    fis->fh = INVALID_HANDLE_VALUE;
    fis->fmo = NULL;
  }
}

inline static int
sen_opened(fileinfo *fi)
{
  return fi->fh != INVALID_HANDLE_VALUE;
}

inline static int
sen_msync(void *start, size_t length)
{
  /* return value may be wrong... */
  return FlushViewOfFile(start, length);
}

inline static sen_rc
sen_pread(fileinfo *fi, void *buf, size_t count, off_t offset)
{
  DWORD r, len;
  sen_rc rc = sen_success;
  MUTEX_LOCK(fi->mutex);
  r = SetFilePointer(fi->fh, offset, NULL, FILE_BEGIN);
  if (r == INVALID_SET_FILE_POINTER) {
    rc = sen_file_operation_error;
    SEN_LOG(sen_log_alert, "SetFilePointer error(%d)", GetLastError());
  } else {
    if (!ReadFile(fi->fh, buf, (DWORD)count, &len, NULL)) {
      rc = sen_file_operation_error;
      SEN_LOG(sen_log_alert, "ReadFile error(%d)", GetLastError());
    } else if (len != count) {
      SEN_LOG(sen_log_alert, "ReadFile %d != %d", count, len);
      rc = sen_file_operation_error;
    }
  }
  MUTEX_UNLOCK(fi->mutex);
  return rc;
}

inline static sen_rc
sen_pwrite(fileinfo *fi, void *buf, size_t count, off_t offset)
{
  DWORD r, len;
  sen_rc rc = sen_success;
  MUTEX_LOCK(fi->mutex);
  r = SetFilePointer(fi->fh, offset, NULL, FILE_BEGIN);
  if (r == INVALID_SET_FILE_POINTER) {
    rc = sen_file_operation_error;
    SEN_LOG(sen_log_alert, "SetFilePointer error(%d)", GetLastError());
  } else {
    if (!WriteFile(fi->fh, buf, (DWORD)count, &len, NULL)) {
      SEN_LOG(sen_log_alert, "WriteFile error(%d)", GetLastError());
      rc = sen_file_operation_error;
    } else if (len != count) {
      SEN_LOG(sen_log_alert, "WriteFile %d != %d", count, len);
      rc = sen_file_operation_error;
    }
  }
  MUTEX_UNLOCK(fi->mutex);
  return rc;
}

#else /* WIN32 */

inline static unsigned int
sen_get_pagesize(void)
{
  long v = sysconf(_SC_PAGESIZE);
  if (v == -1) {
    GSERR("_SC_PAGESIZE");
    return 0;
  }
  return (unsigned int) v;
}

inline static sen_rc
sen_open(fileinfo *fi, const char *path, int flags, size_t maxsize)
{
  struct stat st;
  if ((fi->fd = open(path, flags, 0666)) == -1) {
    GSERR(path);
    return sen_file_operation_error;
  }
  if (fstat(fi->fd, &st) == -1) {
    GSERR(path);
    return sen_file_operation_error;
  }
  fi->dev = st.st_dev;
  fi->inode = st.st_ino;
  return sen_success;
}

inline static void
sen_fileinfo_init(fileinfo *fis, int nfis)
{
  for (; nfis--; fis++) { fis->fd = -1; }
}

inline static int
sen_opened(fileinfo *fi)
{
  return fi->fd != -1;
}

inline static sen_rc
sen_close(fileinfo *fi)
{
  if (fi->fd != -1) {
    if (close(fi->fd) == -1) {
      GSERR("close");
      return sen_file_operation_error;
    }
    fi->fd = -1;
  }
  return sen_success;
}

inline static void *
sen_mmap(fileinfo *fi, off_t offset, size_t length)
{
  void *res;
  struct stat s;
  off_t tail = offset + length;
  if ((fstat(fi->fd, &s) == -1) ||
      (s.st_size < tail && ftruncate(fi->fd, tail) == -1)) {
    SEN_LOG(sen_log_alert, "fstat or ftruncate failed %d", fi->fd);
    return NULL;
  }
  res = mmap(NULL, length, PROT_READ|PROT_WRITE, MAP_SHARED, fi->fd, offset);
  if (MAP_FAILED == res) {
    sen_index_expire();
    res = mmap(NULL, length, PROT_READ|PROT_WRITE, MAP_SHARED, fi->fd, offset);
    if (MAP_FAILED == res) {
      GMERR("mmap(%zu,%d,%d)=%s <%zu>", length, fi->fd, offset, strerror(errno), mmap_size);
    }
    return NULL;
  }
  mmap_size += length;
  return res;
}

#ifdef USE_FAIL_MALLOC
inline static void *
sen_fail_mmap(fileinfo *fi, off_t offset, size_t length,
              const char* file, int line, const char *func)
{
  if (fail_malloc_check(length, file, line, func)) {
    return sen_mmap(fi, offset, length);
  } else {
    sen_index_expire();
    GMERR("fail_mmap(%zu,%d,%d) (%s:%d@%s) <%zu>",
          length, fi->fd, offset, file, line, func, mmap_size);
    return NULL;
  }
}
#endif /* USE_FAIL_MALLOC */

inline static int
sen_msync(void *start, size_t length)
{
  int r = msync(start, length, MS_SYNC);
  if (r == -1) { GSERR("msync"); }
  return r;
}

inline static int
sen_munmap(void *start, size_t length)
{
  int res;
  res = munmap(start, length);
  if (res) {
    SEN_LOG(sen_log_alert, "munmap(%p,%zu) failed <%zu>", start, length, mmap_size);
  } else {
    mmap_size -= length;
  }
  return res;
}

inline static sen_rc
sen_pread(fileinfo *fi, void *buf, size_t count, off_t offset)
{
  ssize_t r = pread(fi->fd, buf, count, offset);
  if (r != count) {
    if (r == -1) {
      GSERR("pread");
    } else {
      GERR(sen_file_operation_error, "pread returned %d != %d", r, count);
    }
    return sen_file_operation_error;
  }
  return sen_success;
}

inline static sen_rc
sen_pwrite(fileinfo *fi, void *buf, size_t count, off_t offset)
{
  ssize_t r = pwrite(fi->fd, buf, count, offset);
  if (r != count) {
    if (r == -1) {
      GSERR("pwrite");
    } else {
      GERR(sen_file_operation_error, "pwrite returned %d != %d", r, count);
    }
    return sen_file_operation_error;
  }
  return sen_success;
}

#endif /* WIN32 */
