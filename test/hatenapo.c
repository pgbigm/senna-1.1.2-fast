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

#include <ctype.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include "sym.h"

#define BUFSIZE 65536

char keybuf[SEN_SYM_MAX_KEY_SIZE];
char buffer[BUFSIZE];

static void
encodeURL(const char *str)
{
  static const char *digits = "0123456789abcdef";
  for (; *str; str++) {
    if (isascii(*str) && (isdigit(*str) || isalpha(*str))) {
      putchar(*str);
    } else {
      putchar('%');
      putchar(digits[(*str >> 4) & 0x0f]);
      putchar(digits[*str & 0x0f]);
    }
  }
}

static sen_sym *
sym_open(const char *filename)
{
  sen_sym *sym;
  if (!(sym = sen_sym_open(filename))) {
    sym = sen_sym_create(filename, 0, SEN_INDEX_NORMALIZE, sen_enc_euc_jp);
  }
  return sym;
}

static char *
chomp(char *string)
{
  int l = strlen(string);
  if (l) {
    char *p = string + l - 1;
    if (*p == '\n') { *p = '\0'; }
  }
  return string;
}

static int
do_insert(const char *filename, const char *string)
{
  sen_sym *sym;
  if (!(sym = sym_open(filename))) { return -1; }
  if (string) {
    if (sym->flags & SEN_INDEX_NORMALIZE) {
      sen_nstr *nstr;
      if (!(nstr = sen_nstr_open(string, strlen(string), sym->encoding, 0))) {
        return -1;
      }
      {
        char *key = nstr->norm;
        sen_sym_get(sym, chomp(key));
      }
      sen_nstr_close(nstr);
    } else {
      sen_sym_get(sym, string);
    }
  } else {
    while (!feof(stdin)) {
      if (!fgets(keybuf, SEN_SYM_MAX_KEY_SIZE, stdin)) { break; }
      if (sym->flags & SEN_INDEX_NORMALIZE) {
        sen_nstr *nstr;
        if (!(nstr = sen_nstr_open(keybuf, strlen(keybuf), sym->encoding, 0))) {
          return -1;
        }
        {
          char *key = nstr->norm;
          sen_sym_get(sym, chomp(key));
        }
        sen_nstr_close(nstr);
      } else {
        sen_sym_get(sym, chomp(keybuf));
      }
    }
  }
  return 0;
}

static int
do_delete(const char *filename, const char *string)
{
  sen_sym *sym;
  if (!(sym = sym_open(filename))) { return -1; }
  if (string) {
    sen_sym_del(sym, string);
  } else {
    while (!feof(stdin)) {
      if (!fgets(keybuf, SEN_SYM_MAX_KEY_SIZE, stdin)) { break; }
      sen_sym_del(sym, chomp(keybuf));
    }
  }
  return 0;
}

inline static void
doit(sen_sym *sym, const char *string)
{
  off_t off = 0;
  const char *rest;
  sen_sym_scan_hit sh[100];
  size_t len = strlen(string);
  while (off < len) {
    int i, nhits = sen_sym_scan(sym, string + off, len - off, sh, 100, &rest);
    for (i = 0, off = 0; i < nhits; i++) {
      if (sh[i].offset < off) { continue; } /* skip overlapping region. */
      fwrite(string + off, 1, sh[i].offset - off, stdout);
      fputs("<a href=\"http://d.hatena.ne.jp/keyword/", stdout);
      sen_sym_key(sym, sh[i].id, keybuf, SEN_SYM_MAX_KEY_SIZE);
      encodeURL(keybuf);
      fputs("\">", stdout);
      fwrite(string + sh[i].offset, 1, sh[i].length, stdout);
      fputs("</a>", stdout);
      off = sh[i].offset + sh[i].length;
    }
    if (string + off < rest) {
      fwrite(string + off, 1, rest - (string + off), stdout);
    }
    off = rest - string;
  }
}

static int
do_select(const char *filename, const char *string)
{
  sen_sym *sym;
  if (!(sym = sym_open(filename))) { return -1; }
  if (string) {
    doit(sym, string);
    putchar('\n');
  } else {
    while (!feof(stdin)) {
      if (!fgets(buffer, BUFSIZE, stdin)) { break; }
      doit(sym, buffer);
    }
  }
  return 0;
}

int
main(int argc, char **argv)
{
  if (argc >= 3) {
    switch (*argv[2]) {
    case 'i' :
    case 'I' :
      return do_insert(argv[1], argc > 3 ? argv[3] : NULL);
    case 'd' :
    case 'D' :
      return do_delete(argv[1], argc > 3 ? argv[3] : NULL);
    case 's' :
    case 'S' :
      return do_select(argv[1], argc > 3 ? argv[3] : NULL);
    default :
      break;
    }
  }
  puts("Usage: hatenapo indexname ins|del|sel [string]");
  return 1;
}
