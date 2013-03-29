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
#ifndef SEN_SET_H
#define SEN_SET_H

#ifndef SENNA_H
#include "senna_in.h"
#endif /* SENNA_H */

#ifndef SEN_CTX_H
#include "ctx.h"
#endif /* SEN_CTX_H */

#ifdef	__cplusplus
extern "C" {
#endif

#define SEN_SET_MAX_CHUNK 22

struct _sen_set {
  uint32_t key_size;
  uint32_t value_size;
  uint32_t entry_size;
  uint32_t max_offset;
  int32_t n_entries;
  uint32_t n_garbages;
  uint32_t curr_entry;
  uint32_t curr_chunk;
  sen_set_eh garbages;
  sen_set_eh *index;
  uint8_t arrayp;
  byte *chunks[SEN_SET_MAX_CHUNK + 1];
};

struct _sen_set_cursor {
  sen_set *set;
  sen_set_eh *index;
  sen_set_eh *curr;
  uint32_t rest;
};

sen_rc sen_set_reset(sen_set * set, uint32_t ne);
sen_rc sen_set_array_init(sen_set *set, uint32_t size);

struct _sen_set_element {
  uint32_t key;
  uint8_t dummy[1];
};

struct _sen_set_element_str {
  char *str;
  uint32_t key;
  uint8_t dummy[1];
};

#define SEN_SET_INTKEY(e) (&(e)->key)
#define SEN_SET_INTVAL(e) ((e)->dummy)

#define SEN_SET_BINKEY(e) ((e)->dummy)
#define SEN_SET_BINVAL(e,set) (&(e)->dummy[(set)->key_size])

#define SEN_SET_STRKEY(e) (((struct _sen_set_element_str *)(e))->str)
#define SEN_SET_STRVAL(e) (((struct _sen_set_element_str *)(e))->dummy)
#define SEN_SET_STRKEY_BY_VAL(v)\
 SEN_SET_STRKEY(((uintptr_t)v) - ((uintptr_t)&SEN_SET_STRVAL(0)))

#define SEN_SET_INT_ADD(set,k,v)\
{\
  sen_set *_set = set;\
  sen_set_eh *eh = _set->index + _set->n_entries++;\
  byte *chunk = _set->chunks[SEN_SET_MAX_CHUNK];\
  struct _sen_set_element *e = (void *)(chunk + _set->entry_size * _set->curr_entry++);\
  e->key = *((uint32_t *)k);\
  *eh = (sen_set_eh)e;\
  v = (void *)e->dummy;\
}

#define SEN_SET_EACH(set,eh,key,val,block) do {\
  sen_set_cursor *_sc = sen_set_cursor_open(set);\
  if (_sc) {\
    sen_set_eh *eh;\
    while ((eh = sen_set_cursor_next(_sc, (void **) (key), (void **) (val)))) block\
    sen_set_cursor_close(_sc);\
  }\
} while (0)

#ifdef __cplusplus
}
#endif

#endif /* SEN_SET_H */
