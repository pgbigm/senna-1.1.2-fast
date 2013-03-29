/* Copyright(C) 2006 Brazil
     All rights reserved.
     This is free software with ABSOLUTELY NO WARRANTY.

 You can redistribute it and/or modify it under the terms of 
 the GNU General Public License version 2.
*/

#include "myisam.h"
#include "sql/mysql_priv.h"
#include "mysql.h"
#include "sql/ha_myisam.h"
#include "myisam/myisamdef.h"
#include "myisenna.h"

/*
#include <stdio.h>
#include <sys/time.h>
#include <time.h>
*/

struct _myis_table {
  TABLE table;
  MI_INFO *info;
  myis_column *columns;
  myis_index *indexes;
};

struct _myis_index {
  int n;
  KEY *key;
};

struct _myis_cursor {
  myis_table *table;
  int n;
  char *record;
  myis_id id;
};

struct _myis_column {
  Field *field;
};

int
myis_init(void)
{
  char *server_default_groups[] = { "server", "embedded", "mysql_SERVER", 0 };
  char *pargv[] = {"--skip-innodb", 0};
  mysql_server_init(1, pargv, server_default_groups);
  sen_init();
  return 0;
}

myis_table *
myis_table_open(const char *path)
{
  int i, error;
  myis_table *table = (myis_table *)malloc(sizeof(myis_table));
  if (!table) { return NULL; }
  error = openfrm(path, "", 
		  HA_OPEN_KEYFILE|HA_OPEN_RNDFILE|HA_GET_INDEX|HA_TRY_READ_ONLY,
		  READ_KEYINFO|COMPUTE_TYPES|EXTRA_RECORD, 0, &table->table);
  table->columns = (myis_column *) malloc(sizeof(myis_column) * table->table.fields);
  for (i = 0; i < table->table.fields; i++) {
    table->columns[i].field = table->table.field[i];
  }
  table->indexes = (myis_index *) malloc(sizeof(myis_index) * table->table.keys);
  for (i = 0; i < table->table.keys; i++) {
    table->indexes[i].n = i;
    table->indexes[i].key = &table->table.key_info[i];
  }
  table->info = ((ha_myisam *)table->table.file)->getfile();
  return table;
}

int 
myis_table_close(myis_table *table)
{
  int error = closefrm(&table->table);
  free(table);
  return error;
}

int 
myis_table_info(myis_table *table, int *n_columns, int *n_indexes)
{
  if (!table) { return 1; }
  if (n_columns) { *n_columns = table->table.fields; }
  if (n_indexes) { *n_indexes = table->table.keys; }
  return 0;
}

myis_index *
myis_table_index(myis_table *table, int n)
{
  if (!table || table->table.keys <= n) { return NULL; }
  return &table->indexes[n];
}

sen_index *
myis_table_fti(myis_table *table, int n)
{
  if (!table || table->table.keys <= n) { return NULL; }
  return table->info->s->keyinfo[n].senna;
}

static ha_rkey_function myis_key_opt2ha_rkey_function[] = {
  HA_READ_KEY_EXACT,
  HA_READ_KEY_OR_NEXT,
  HA_READ_KEY_OR_PREV,
  HA_READ_AFTER_KEY,
  HA_READ_BEFORE_KEY,
  HA_READ_PREFIX,
  HA_READ_PREFIX_LAST,
  HA_READ_MBR_CONTAIN,
  HA_READ_MBR_INTERSECT,
  HA_READ_MBR_WITHIN,
  HA_READ_MBR_DISJOINT,
  HA_READ_MBR_EQUAL
};

int 
myis_index_rkey(myis_index *index, myis_cursor *cursor,
		const char *key, unsigned int key_len, myis_key_opt opt)
{
  cursor->n = index->n;
  ha_rkey_function flag = myis_key_opt2ha_rkey_function[opt];
  //  mi_extra(cursor->table->info, HA_EXTRA_NO_KEYREAD, 0);
  //  mi_extra(cursor->table->info, HA_EXTRA_NO_CACHE, 0);
  //  mi_extra(cursor->table->info, HA_EXTRA_KEYREAD, 0);
  return mi_rkey(cursor->table->info, cursor->record, cursor->n, key, key_len, flag);
}

int 
myis_index_rfirst(myis_index *index, myis_cursor *cursor)
{
  cursor->n = index->n;
  return mi_rfirst(cursor->table->info, cursor->record, cursor->n);
}

int 
myis_index_rlast(myis_index *index, myis_cursor *cursor)
{
  cursor->n = index->n;
  return mi_rlast(cursor->table->info, cursor->record, cursor->n);
}

const char *
myis_index_name(myis_index *index)
{
  if (!index) { return NULL; }
  return index->key->name;
}

myis_cursor *
myis_cursor_open(myis_table *table)
{
  myis_cursor *cursor;
  if (!table || !(cursor = (myis_cursor *)malloc(sizeof(myis_cursor)))) { return NULL; }
  cursor->table = table;
  cursor->record = table->table.record[0];
  return cursor;
}

int 
myis_cursor_close(myis_cursor *cursor)
{
  if (!cursor) { return 1; }
  free(cursor);
  return 0;
}

int 
myis_cursor_next(myis_cursor *cursor)
{
  // mi_extra(cursor->table->info, HA_EXTRA_NO_KEYREAD, 0);
  // mi_extra(cursor->table->info, HA_EXTRA_NO_CACHE, 0);
  return mi_rnext(cursor->table->info, cursor->record, cursor->n);
}

int 
myis_cursor_prev(myis_cursor *cursor)
{
  return mi_rprev(cursor->table->info, cursor->record, cursor->n);
}

int 
myis_cursor_scan_init(myis_cursor *cursor)
{
  return mi_scan_init(cursor->table->info);
}

int 
myis_cursor_scan(myis_cursor *cursor)
{
  return mi_scan(cursor->table->info, cursor->record);
}

int 
myis_cursor_rrnd(myis_cursor *cursor, myis_id id)
{
  cursor->id = id;
  return mi_rrnd(cursor->table->info, cursor->record, id);
}

myis_column *
myis_table_column(myis_table *table, int n)
{
  if (!table || table->table.fields <= n) { return NULL; }
  return &table->columns[n];
}

const char *
myis_column_name(myis_column *column)
{
  if (!column) { return NULL; }
  return column->field->field_name;
}

int 
myis_column_val_str(myis_column *column, myis_cursor *cursor,
		    char *buf, size_t bufsize)
{
  int len;
  String tmp(buf, bufsize);
  column->field->val_str(&tmp, &tmp);
  len = tmp.length();
  memcpy(buf, tmp.ptr(), len);
  buf[len] = '\0';
  return 0;
}
