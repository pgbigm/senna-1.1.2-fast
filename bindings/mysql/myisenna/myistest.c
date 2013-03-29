#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "myisenna.h"

int
main(int argc, char **argv)
{
  int i, score;
  myis_id id;
  myis_table *table;
  myis_index *index;
  myis_cursor *cursor;
  myis_column *column;
  char buff[65536];
  sen_index *sen;
  if (argc < 5) {
    fputs("usage: test indexname keynumber colnumber querystring \n", stderr);
    return -1;
  }
  myis_init();
  table = myis_table_open(argv[1]);
  cursor = myis_cursor_open(table);
  sen = myis_table_fti(table, atoi(argv[2]));
  index = myis_table_index(table, atoi(argv[2]));
  column = myis_table_column(table, atoi(argv[3]));
  printf("key='%s' (%s)\n", myis_index_name(index), sen ? "fulltext" : "btree");

  if (sen) {
    sen_records *records = sen_index_sel(sen, argv[4]);
    if (records) {
      printf("nhits=%d\n", sen_records_nhits(records));
      sen_records_sort(records, 10, NULL);
      printf("%8s: %s\n", "score", myis_column_name(column));
      while (sen_records_next(records, &id, sizeof(myis_id), &score)) {
	i = myis_cursor_rrnd(cursor, id);
	myis_column_val_str(column, cursor, buff, 65536);
	printf("%8d: %s\n", score, buff);
      }
      sen_records_close(records);
    }
  } else {
    int len = strlen(argv[4]);
    unsigned short *lenp = (unsigned short *)buff;
    if (len > 65535) { len = 65535; }
    *lenp = (unsigned short) len;
    memcpy(buff + 2, argv[4], len);
    int r = myis_index_rkey(index, cursor, buff, len + 2, MYIS_KEY_OR_NEXT);
    //    int r = myis_index_rfirst(index, cursor);

    printf("r=%d (%s)\n", r, argv[4]);
    for (i = 0; i < 10; i++) {
      r = myis_cursor_next(cursor);
      myis_column_val_str(column, cursor, buff, 65536);
      printf("%d %8d: %s\n", r, i, buff);
    }
  }

  myis_cursor_close(cursor);
  myis_table_close(table);
  return 0;
}
