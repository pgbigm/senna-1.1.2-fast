/* Copyright(C) 2004-2006 Brazil

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

#ifdef STANDARD
#include <stdio.h>
#include <string.h>
#ifdef __WIN__
typedef unsigned __int64 ulonglong;     /* Microsofts 64 bit types */
typedef __int64 longlong;
#else
typedef unsigned long long ulonglong;
typedef long long longlong;
#endif /*__WIN__*/
#else
/* avoid warning */
#ifdef PACKAGE
#undef PACKAGE
#endif
#ifdef PACKAGE_BUGREPORT
#undef PACKAGE_BUGREPORT
#endif
#ifdef PACKAGE_NAME
#undef PACKAGE_NAME
#endif
#ifdef PACKAGE_STRING
#undef PACKAGE_STRING
#endif
#ifdef PACKAGE_TARNAME
#undef PACKAGE_TARNAME
#endif
#ifdef PACKAGE_VERSION
#undef PACKAGE_VERSION
#endif
#ifdef VERSION
#undef VERSION
#endif
#include <my_global.h>
#include <my_sys.h>
#endif
#include <mysql.h>
#include <m_ctype.h>
#include <m_string.h>           // To get strmov()

#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <senna.h>

#ifdef HAVE_DLOPEN

#define MINIMUM_RESULT_LENGTH 64

my_bool snippet_init(UDF_INIT * initid, UDF_ARGS * args, char *message);
void snippet_deinit(UDF_INIT * initid);
char *snippet(UDF_INIT * initid, UDF_ARGS * args, char *result,
              unsigned long *length, char *is_null, char *error);

typedef struct
{
  char *result;
  char *buffer;
  size_t result_len;
  size_t buffer_len;
  char *keytags;
  sen_snip *snip;
} snip_struct;

my_bool
snippet_init(UDF_INIT * initid, UDF_ARGS * args, char *message)
{

  snip_struct *ptr;
  sen_encoding enc;
  sen_snip *snip;
  sen_snip_mapping *map;
  unsigned int i;
  long long slength, snumber, htmlflag;
  const enum Item_result arg_types[] =
    {STRING_RESULT, INT_RESULT, INT_RESULT, STRING_RESULT, INT_RESULT, STRING_RESULT, STRING_RESULT};

  initid->ptr = NULL;

  if (args->arg_count < 10) {
    strcpy(message, "snippet function requires at least 10 parameters.");
    return 1;
  }
  if ((args->arg_count % 3) != 1) {
    strcpy(message, "number of parameters for snippet function is wrong.");
    return 1;
  }
  for (i = 1; i <= 6; i++) {
    if (!args->args[i]) {
      sprintf(message, "parameter number %d is NULL!", i + 1);
      return 1;
    }
    if (args->arg_type[i] != arg_types[i]) {
      sprintf(message, "type of parameter number %d is wrong.", i + 1);
      return 1;
    }
    if (arg_types[i] == STRING_RESULT && args->lengths[i] > INT_MAX) {
      sprintf(message, "parameter number %d is too long!", i + 1);
      return 1;
    }
  }

  for (i = 7; i < args->arg_count; i++) {
    if (args->arg_type[i] != STRING_RESULT) {
      sprintf(message, "parameter number %d is invalid!", i + 1);
      return 1;
    }
  }

  slength = *((long long *) args->args[1]);
  snumber = *((long long *) args->args[2]);
  htmlflag = *((long long *) args->args[4]);

  if (slength < 0) {
    strcpy(message, "too short length");
    return 1;
  }

  if (snumber < 0) {
    strcpy(message, "too small number");
    return 1;
  }

  if (strcmp(args->args[3], "ujis") == 0) {
    enc = sen_enc_euc_jp;
  } else if (strcmp(args->args[3], "utf8") == 0) {
    enc = sen_enc_utf8;
  } else if (strcmp(args->args[3], "sjis") == 0) {
    enc = sen_enc_sjis;
  } else if (strcmp(args->args[3], "default") == 0) {
    enc = sen_enc_default;
  } else if (strcmp(args->args[3], "latin1") == 0) {
    enc = sen_enc_latin1;
  } else if (strcmp(args->args[3], "koi8r") == 0) {
    enc = sen_enc_koi8r;
  } else {
    strcpy(message, "encode is invalid");
    return 1;
  }

  map = NULL;
  if (htmlflag) {
    map = (sen_snip_mapping *) -1;
  }

  if (!(ptr = (snip_struct *) malloc(sizeof(snip_struct)))) {
    strcpy(message, "memory allocation error !(snip_struct)");
    return 1;
  }
  memset(ptr, 0, sizeof(snip_struct));

  initid->ptr = (char *) ptr;
  initid->max_length = 65535;   /* represents blob */
  initid->maybe_null = FALSE;

  ptr->snip = sen_snip_open
    (enc, SEN_SNIP_NORMALIZE, slength, snumber, "", 0, "", 0, map);
  ptr->result_len = (size_t) ((long double) slength * snumber * 1.3);
  if (ptr->result_len < MINIMUM_RESULT_LENGTH) {
    ptr->result_len = MINIMUM_RESULT_LENGTH;
  }

  ptr->buffer_len = (size_t) ((long double) slength * 1.3);
  if (ptr->buffer_len >= INT_MAX) {
    snippet_deinit(initid);
    strcpy(message, "too long snippet length!");
    return 1;
  }

  if (!(ptr->result = (char *) malloc(sizeof(char) * ptr->result_len))) {
    snippet_deinit(initid);
    strcpy(message, "memory allocation error !(result)");
    return 1;
  }

  if (!(ptr->buffer = (char *) malloc(sizeof(char) * ptr->buffer_len))) {
    snippet_deinit(initid);
    strcpy(message, "memory allocation error !(buffer)");
    return 1;
  }

  return 0;
}

void
snippet_deinit(UDF_INIT * initid)
{
  snip_struct *ptr;

  ptr = (snip_struct *) initid->ptr;
  if (ptr) {
    if (ptr->snip) {
      sen_snip_close(ptr->snip);
    }
    if (ptr->result) {
      free(ptr->result);
    }
    if (ptr->buffer) {
      free(ptr->buffer);
    }
    if (ptr->keytags) {
      free(ptr->keytags);
    }
    free(ptr);
  }
}

char *
snippet(UDF_INIT * initid, UDF_ARGS * args, char *result,
        unsigned long *length, char *is_null, char *error)
{
  snip_struct *ptr;
  sen_snip *snip;
  unsigned int i, n_results, estimated_length, max_tagged_len;
  char *p;

  ptr = (snip_struct *) initid->ptr;
  snip = ptr->snip;

  if (!ptr->keytags){
    unsigned int keytagslength;
    char *q, *r;

    keytagslength = 0;
    for (i = 7; i < args->arg_count; i++) {
      if (args->lengths[i] >= INT_MAX) {
        sprintf(ptr->result, "parameter number %d is too long!", i + 1);
        goto exit;
      }
      keytagslength += args->lengths[i];
    }

    if (!(ptr->keytags = (char *) malloc(sizeof(char) * keytagslength))) {
      sprintf(ptr->result, "memory allocation error !(tags)");
      goto exit;
    }

    p = ptr->keytags;
    for (i = 7; i < args->arg_count; i += 3) {

      memcpy(p, args->args[i], args->lengths[i]); /* keyword copy */
      q = p + args->lengths[i];
      memcpy(q, args->args[i + 1], args->lengths[i + 1]); /* opentag copy */
      r = q + args->lengths[i + 1];
      memcpy(r, args->args[i + 2], args->lengths[i + 2]); /* closetag copy */

      if (sen_snip_add_cond(snip, p, args->lengths[i],
                            q, args->lengths[i + 1],
                            r, args->lengths[i + 2]) != sen_success) {
        strcpy(ptr->result, "cannot add conditions");
        goto exit;
      }
      p = r + args->lengths[i + 2];
    }
  }

  if (args->arg_type[0] != STRING_RESULT) {
    strcpy(ptr->result, "cannot make snippet");
    goto exit;
  }
  if (args->lengths[0] >= INT_MAX) {
    strcpy(ptr->result, "string is too long");
    goto exit;
  }
  if (args->args[0]) {
    if (sen_snip_exec(snip, args->args[0], args->lengths[0], &n_results, &max_tagged_len)
        != sen_success) {
      strcpy(ptr->result, "cannot make snippet");
      goto exit;
    }

    /* buffer realloc */
    if (ptr->buffer_len < max_tagged_len) {
      char *p = (char *) realloc(ptr->buffer, sizeof(char) * max_tagged_len);
      if (!p) {
        strcpy(ptr->buffer, "cannot reallocate memory for buffer");
        goto exit;
      }
      ptr->buffer = p;
      ptr->buffer_len = max_tagged_len;
    }

    /* result realloc */
    estimated_length =
      (args->lengths[5] + args->lengths[6] + max_tagged_len - 1) * n_results + 1;
    if (ptr->result_len < estimated_length) {
      char *p = (char *) realloc(ptr->result, sizeof(char) * estimated_length);
      if (!p) {
        strcpy(ptr->result, "cannot reallocate memory for result");
        goto exit;
      }
      ptr->result = p;
      ptr->result_len = estimated_length;
    }

    p = ptr->result;
    for (i = 0; i < n_results; i++) {
      unsigned int r_len;
      if (sen_snip_get_result(snip, i, ptr->buffer, &r_len) != sen_success) {
        strcpy(ptr->result, "cannot get result");
        goto exit;
      }
      memcpy(p, args->args[5], args->lengths[5]);
      p += args->lengths[5];
      memcpy(p, ptr->buffer, r_len);
      p += r_len;
      memcpy(p, args->args[6], args->lengths[6]);
      p += args->lengths[6];
    }
    *length = (size_t)(p - ptr->result);
  }
  else {
    *length = 0;
  }
  return ptr->result;
exit:
  *length = strlen(ptr->result);
  return ptr->result;
}

#endif /* HAVE_DLOPEN */
