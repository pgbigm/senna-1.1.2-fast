/* Copyright(C) 2005 Takuo Kitame <kitame @ valinux. co. jp>

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

#ifdef USE_AIO

#ifndef SEN_CACHE_H
#define SEN_CACHE_H

/* feature switch */
extern int sen_aio_enabled;
extern int sen_debug_print;
extern int sen_cache_block;

#include <aio.h>

/* CacheData structure */
typedef struct _CacheData CacheData;
#pragma pack(1)
struct _CacheData {
    int flag; /* status */
    unsigned int stamp; /* global counter stamp */

    /* key */
    dev_t dev;
    ino_t inode;
    unsigned int offset;
    unsigned int size;
    /* end key */

    /* number */
    unsigned int num;
    int next; /* -1 : no more next */

    /* number of references */
    int ref;
};
#pragma pack()

/* status of CacheData */
enum {
    CACHE_INVALID, /* empty, unused  */
    CACHE_VALID,   /* data in */
    CACHE_READ,    /* reading */
    CACHE_LOCKED   /* locked */
};

/* Cache IO Operation */
typedef struct _CacheIOOper CacheIOOper;
struct _CacheIOOper {
    CacheData *cd;
    struct aiocb *iocb; /* AIO control block */
    int read; /* real read() is reqired or not.*/
};

/* open and initialize */
void sen_cache_open (void);

/* read data via cache */
void *sen_cache_read (CacheIOOper *oper, dev_t dev, ino_t inode, size_t offset, size_t vsize);

/* reference counter operations */
void sen_cache_data_unref (int number);
void sen_cache_data_ref (int number);


/* use when write a segment */
void
sen_cache_mark_invalid (dev_t dev, ino_t inode, off_t offset, size_t size);

/* used for debug */
void sen_cache_dump (void);

#include <stdarg.h>

inline static void
dp(char *fmt, ...)
{
  if (sen_debug_print) {
    va_list argp;
    va_start(argp, fmt);
    vfprintf(stderr, fmt, argp);
    va_end(argp);
  }
}

#endif /* SEN_CACHE_H */

#endif /* USE_AIO */
