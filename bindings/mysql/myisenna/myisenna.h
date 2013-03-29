/* Copyright(C) 2006 Brazil
     All rights reserved.
     This is free software with ABSOLUTELY NO WARRANTY.

 You can redistribute it and/or modify it under the terms of 
 the GNU General Public License version 2.
*/

#include "senna.h"

#ifndef _MYISENNA_H
#define _MYISENNA_H

#ifdef __cplusplus
extern "C" {
#endif

enum myis_key_opt {
  MYIS_KEY_EXACT = 0,
  MYIS_KEY_OR_NEXT,
  MYIS_KEY_OR_PREV,
  MYIS_AFTER_KEY,
  MYIS_BEFORE_KEY,
  MYIS_PREFIX,
  MYIS_PREFIX_LAST
};

typedef unsigned long long myis_id;
typedef struct _myis_table myis_table;
typedef struct _myis_index myis_index;
typedef struct _myis_cursor myis_cursor;
typedef struct _myis_column myis_column;

int myis_init(void);
myis_table *myis_table_open(const char *path);
int myis_table_close(myis_table *table);
int myis_table_info(myis_table *table, int *n_columns, int *n_indexes);

myis_index *myis_table_index(myis_table *table, int n);
sen_index *myis_table_fti(myis_table *table, int n);
int myis_index_rkey(myis_index *index, myis_cursor *cursor,
		    const char *key, unsigned int key_len, enum myis_key_opt opt);
int myis_index_rfirst(myis_index *index, myis_cursor *cursor);
int myis_index_rlast(myis_index *index, myis_cursor *cursor);
const char *myis_index_name(myis_index *index);

myis_cursor *myis_cursor_open(myis_table *table);
int myis_cursor_close(myis_cursor *cursor);
int myis_cursor_next(myis_cursor *cursor);
int myis_cursor_prev(myis_cursor *cursor);
int myis_cursor_scan_init(myis_cursor *cursor);
int myis_cursor_scan(myis_cursor *cursor);
int myis_cursor_rrnd(myis_cursor *cursor, myis_id id);

myis_column *myis_table_column(myis_table *table, int n);
const char *myis_column_name(myis_column *column);
int myis_column_val_str(myis_column *column, myis_cursor *cursor,
			char *buf, size_t bufsize);

#ifdef __cplusplus
}
#endif

#endif /* _MYISENNA_H */
