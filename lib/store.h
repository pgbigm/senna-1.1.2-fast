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
#ifndef SEN_STORE_H
#define SEN_STORE_H

#ifndef SENNA_H
#include "senna_in.h"
#endif /* SENNA_H */

#ifndef SEN_SET_H
#include "set.h"
#endif /* SEN_SET_H */

#ifndef SEN_IO_H
#include "io.h"
#endif /* SEN_IO_H */

#ifdef __cplusplus
extern "C" {
#endif

#define SEN_ST_APPEND 1

/**** fixed sized elements ****/

typedef struct _sen_ra sen_ra;

struct _sen_ra {
  sen_io *io;
  int element_width;
  int element_mask;
  struct sen_ra_header *header;
};

struct sen_ra_header {
  char idstr[16];
  unsigned element_size;
  sen_id curr_max;
  uint32_t nrecords; /* nrecords is not maintained by default */
  uint32_t reserved[9];
};

sen_ra *sen_ra_create(const char *path, unsigned int element_size);
sen_ra *sen_ra_open(const char *path);
sen_rc sen_ra_info(sen_ra *ra, unsigned int *element_size, sen_id *curr_max);
sen_rc sen_ra_close(sen_ra *ra);
sen_rc sen_ra_remove(const char *path);
void *sen_ra_get(sen_ra *ra, sen_id id);
void *sen_ra_at(sen_ra *ra, sen_id id);

/**** variable sized elements ****/

typedef struct _sen_ja sen_ja;

sen_ja *sen_ja_create(const char *path, unsigned int max_element_size);
sen_ja *sen_ja_open(const char *path);
sen_rc sen_ja_info(sen_ja *ja, unsigned int *max_element_size);
sen_rc sen_ja_close(sen_ja *ja);
sen_rc sen_ja_remove(const char *path);
sen_rc sen_ja_put(sen_ja *ja, sen_id id, void *value, int value_len, int flags);
int sen_ja_at(sen_ja *ja, sen_id id, void *valbuf, int buf_size);

void *sen_ja_ref(sen_ja *ja, sen_id id, uint32_t *value_len);
sen_rc sen_ja_unref(sen_ja *ja, sen_id id, void *value, uint32_t value_len);
int sen_ja_defrag(sen_ja *ja, int threshold);

/**** db ****/

#define sen_db_raw_class  0x01 /* unstructured data class (no slots, not enumerable) */
#define sen_db_class      0x02 /* structured data class (has slots, enumerable) */
#define sen_db_obj_slot   0x03 /* slot storing a reference to a structured object */
#define sen_db_ra_slot    0x04 /* slot storing a flat data, equal or less than 8 bytes */
#define sen_db_ja_slot    0x05 /* slot storing a flat data, more than 8 bytes */
#define sen_db_idx_slot   0x06 /* slot storing an inverted index */
#define sen_db_vslot      0x07 /* virtual slot  */
#define sen_db_pslot      0x08 /* pseudo slot  */
#define sen_db_rel1       0x09 /* relations the pkey consists of single slot */
#define sen_db_rel2       0x0a /* relations the pkey consists of two or more slots  */

#define SEN_DB_PSLOT_FLAG 0x80000000
#define SEN_DB_PSLOT_ID         1
#define SEN_DB_PSLOT_SCORE      2
#define SEN_DB_PSLOT_NSUBRECS   3
#define SEN_DB_PSLOT_MASK 0xff

typedef struct _sen_array sen_array;

typedef struct _sen_db_store_rel_spec sen_db_store_rel_spec;
typedef struct _sen_db_store_spec sen_db_store_spec;
typedef struct _sen_db_store sen_db_store;
typedef struct _sen_db_trigger sen_db_trigger;

typedef enum {
  sen_db_before_update_trigger = 0,
  sen_db_after_update_trigger,
  sen_db_index_target
} sen_db_rel_type;

struct _sen_db_store_rel_spec {
  sen_db_rel_type type;
  sen_id target;
};

struct _sen_db_store_spec {
  uint32_t type;
  uint32_t n_triggers;
  union {
    struct {
      unsigned int size;
      unsigned int flags;
      sen_encoding encoding;
    } c;
    struct {
      sen_id class;
      unsigned int size;
      unsigned int collection_type; // not in used
    } s;
  } u;
  sen_db_store_rel_spec triggers[1];
};

enum {
  sen_array_clear = 1
};

struct _sen_array {
  uint16_t element_size;
  uint16_t flags;
  sen_mutex lock;
  void *elements[8];
};

struct _sen_db {
  sen_sym *keys;
  sen_ja *values;
  sen_array stores;
  //  sen_set *stores;
  sen_mutex lock; /* todo */
};

struct _sen_db_trigger {
  sen_db_trigger *next;
  sen_db_rel_type type;
  sen_id target;
};

struct _sen_db_store {
  uint8_t type;
  sen_db *db;
  sen_id id;
  sen_db_trigger *triggers;
  union {
    struct {
      unsigned int element_size;
    } bc;
    struct {
      sen_sym *keys;
    } c;
    struct {
      sen_id class;
      sen_ra *ra;
    } o;
    struct {
      sen_id class;
      sen_ra *ra;
    } f;
    struct {
      sen_id class;
      sen_ja *ja;
    } v;
    struct {
      sen_id class;
      sen_index *index;
    } i;
  } u;
};

#define SEN_DB_STORE_SPEC_SIZE(n) \
  ((intptr_t)(&((sen_db_store_spec *)0)->triggers[n]))

sen_db_store *sen_db_store_create(sen_db *s, const char *name, sen_db_store_spec *spec);
sen_db_store *sen_db_store_open(sen_db *s, const char *name);
sen_rc sen_db_store_remove(sen_db *s, const char *name);
sen_rc sen_db_store_add_trigger(sen_db_store *s, sen_db_store_rel_spec *r);
sen_rc sen_db_store_del_trigger(sen_db_store *s, sen_db_store_rel_spec *r);

sen_db_store *sen_db_store_by_id(sen_db *s, sen_id id);
sen_db_store *sen_db_slot_class_by_id(sen_db *s, sen_id slot);
sen_db_store *sen_db_class_slot(sen_db *s, sen_id class, const char *name);
sen_db_store *sen_db_class_add_slot(sen_db *s, sen_id class, const char *name,
                                    sen_db_store_spec *spec);
sen_rc sen_db_class_del_slot(sen_db *s, sen_id class, const char *name);
sen_rc sen_db_class_slotpath(sen_db *s, sen_id class, const char *name, char *buf);

sen_rc sen_db_idx_slot_build(sen_db *s, sen_db_store *store);
sen_rc sen_db_lock(sen_db *s, int timeout);
sen_rc sen_db_unlock(sen_db *s);
sen_rc sen_db_clear_lock(sen_db *s);

/**** vgram ****/

typedef struct _sen_vgram_vnode
{
  struct _sen_vgram_vnode *car;
  struct _sen_vgram_vnode *cdr;
  sen_id tid;
  sen_id vid;
  int freq;
  int len;
} sen_vgram_vnode;

struct _sen_vgram {
  sen_sym *vgram;
};

struct _sen_vgram_buf {
  size_t len;
  sen_id *tvs;
  sen_id *tvp;
  sen_id *tve;
  sen_vgram_vnode *vps;
  sen_vgram_vnode *vpp;
  sen_vgram_vnode *vpe;
};

sen_vgram *sen_vgram_create(const char *path);
sen_vgram *sen_vgram_open(const char *path);
sen_rc sen_vgram_close(sen_vgram *vgram);
sen_rc sen_vgram_update(sen_vgram *vgram, sen_id rid, sen_vgram_buf *b, sen_set *terms);

sen_vgram_buf *sen_vgram_buf_open(size_t len);
sen_rc sen_vgram_buf_add(sen_vgram_buf *b, sen_id tid);
sen_rc sen_vgram_buf_close(sen_vgram_buf *b);

#ifdef __cplusplus
}
#endif

#endif /* SEN_STORE_H */
