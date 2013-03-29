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
//#include <sys/param.h>

#ifndef SEN_SYM_H
#define SEN_SYM_H

#ifndef SENNA_IN_H
#include "senna_in.h"
#endif /* SENNA_IN_H */

#ifndef SEN_SET_H
#include "set.h"
#endif /* SEN_SET_H */

#ifndef SEN_CTX_H
#include "ctx.h"
#endif /* SEN_CTX_H */

#ifndef SEN_IO_H
#include "io.h"
#endif /* SEN_IO_H */

#ifdef  __cplusplus
extern "C" {
#endif

  /* SEN_SYM_SEGMENT_SIZE * SEN_SYM_MAX_SEGMENT == 0x100000000 */
#define SEN_SYM_MAX_SEGMENT 0x400

  /* sizeof(pat_node) * (SEN_SYM_MAX_ID + 1) == 0x100000000 */
#define SEN_SYM_MAX_ID 0xfffffff

#define SEN_SYM_MAX_KEY_LENGTH 0xffff
  /* must be 2 ^ n */
#define SEN_SYM_NDELINFOS 0x100
#define SEN_SYM_MDELINFOS (SEN_SYM_NDELINFOS - 1)

typedef struct {
  /* stat : 2, ld : 30 */
  sen_id bitfield;
  sen_id d;
} sen_sym_delinfo;

struct _sen_sym {
  uint8_t v08p;
  sen_io *io;
  struct sen_sym_header *header;
  uint32_t flags;
  sen_encoding encoding;
  uint32_t key_size;
  uint32_t nref;
  uint32_t *lock;
  void *keyaddrs[SEN_SYM_MAX_SEGMENT];
  void *pataddrs[SEN_SYM_MAX_SEGMENT];
  void *sisaddrs[SEN_SYM_MAX_SEGMENT];
};

const char *_sen_sym_key(sen_sym *sym, sen_id id);
// sen_id sen_sym_del_with_sis(sen_sym *sym, sen_id id);
int sen_sym_del_with_sis(sen_sym *sym, sen_id id,
                         int(*func)(sen_id, void *), void *func_arg);
sen_id sen_sym_curr_id(sen_sym *sym);
sen_rc sen_sym_prefix_search_with_set(sen_sym *sym, const void *key, sen_set *h);
sen_rc sen_sym_suffix_search_with_set(sen_sym *sym, const void *key, sen_set *h);

sen_rc sen_sym_pocket_incr(sen_sym *sym, sen_id id);
sen_rc sen_sym_pocket_decr(sen_sym *sym, sen_id id);

sen_rc sen_sym_lock(sen_sym *sym, int timeout);
sen_rc sen_sym_unlock(sen_sym *sym);
sen_rc sen_sym_clear_lock(sen_sym *sym);

sen_id sen_sym_nextid(sen_sym *sym, const void *key);

#ifdef __cplusplus
}
#endif

#endif /* SEN_SYM_H */
