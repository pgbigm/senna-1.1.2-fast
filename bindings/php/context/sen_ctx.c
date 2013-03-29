/* Copyright(C) 2007 Brazil

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

/* $ Id: $ */

#include "php_sen_ctx.h"

#if HAVE_SEN_CTX

int le_sen_ctx;
void sen_ctx_dtor(zend_rsrc_list_entry *rsrc TSRMLS_DC)
{
  sen_ctx * resource = (sen_ctx *)(rsrc->ptr);
  sen_ctx_close(resource);
}

int le_sen_pctx;
void sen_pctx_dtor(zend_rsrc_list_entry *rsrc TSRMLS_DC)
{
  sen_ctx * resource = (sen_ctx *)(rsrc->ptr);
  sen_ctx_close(resource);
}

int le_sen_db;
void sen_db_dtor(zend_rsrc_list_entry *rsrc TSRMLS_DC)
{
  sen_db * resource = (sen_db *)(rsrc->ptr);
  sen_db_close(resource);
}


function_entry sen_ctx_functions[] = {
  PHP_FE(sen_db_create       , NULL)
  PHP_FE(sen_db_open         , NULL)
  PHP_FE(sen_db_close        , NULL)
  PHP_FE(sen_ctx_open        , NULL)
  PHP_FE(sen_ctx_connect     , NULL)
  PHP_FE(sen_ctx_pconnect    , NULL)
  PHP_FE(sen_ctx_load        , NULL)
  PHP_FE(sen_ctx_send        , NULL)
  PHP_FE(sen_ctx_recv        , NULL)
  PHP_FE(sen_ctx_close       , NULL)
  PHP_FE(sen_ctx_info_get    , NULL)
  { NULL, NULL, NULL }
};


zend_module_entry sen_ctx_module_entry = {
  STANDARD_MODULE_HEADER,
  "sen_ctx",
  sen_ctx_functions,
  PHP_MINIT(sen_ctx),     /* Replace with NULL if there is nothing to do at php startup   */
  PHP_MSHUTDOWN(sen_ctx), /* Replace with NULL if there is nothing to do at php shutdown  */
  PHP_RINIT(sen_ctx),     /* Replace with NULL if there is nothing to do at request start */
  PHP_RSHUTDOWN(sen_ctx), /* Replace with NULL if there is nothing to do at request end   */
  PHP_MINFO(sen_ctx),
  "0.0.1",
  STANDARD_MODULE_PROPERTIES
};

#ifdef COMPILE_DL_SEN_CTX
ZEND_GET_MODULE(sen_ctx)
#endif


PHP_MINIT_FUNCTION(sen_ctx)
{
  REGISTER_LONG_CONSTANT("SEN_SYM_WITH_SIS", SEN_SYM_WITH_SIS, CONST_PERSISTENT | CONST_CS);
  REGISTER_LONG_CONSTANT("SEN_CTX_MORE", SEN_CTX_MORE, CONST_PERSISTENT | CONST_CS);
  REGISTER_LONG_CONSTANT("SEN_CTX_USEQL", SEN_CTX_USEQL, CONST_PERSISTENT | CONST_CS);
  REGISTER_LONG_CONSTANT("SEN_CTX_BATCHMODE", SEN_CTX_BATCHMODE, CONST_PERSISTENT | CONST_CS);
  REGISTER_LONG_CONSTANT("SEN_ENC_DEFAULT", 0, CONST_PERSISTENT | CONST_CS);
  REGISTER_LONG_CONSTANT("SEN_ENC_NONE", 1, CONST_PERSISTENT | CONST_CS);
  REGISTER_LONG_CONSTANT("SEN_ENC_EUC_JP", 2, CONST_PERSISTENT | CONST_CS);
  REGISTER_LONG_CONSTANT("SEN_ENC_UTF8", 3, CONST_PERSISTENT | CONST_CS);
  REGISTER_LONG_CONSTANT("SEN_ENC_SJIS", 4, CONST_PERSISTENT | CONST_CS);
  REGISTER_LONG_CONSTANT("SEN_ENC_LATIN1", 5, CONST_PERSISTENT | CONST_CS);
  REGISTER_LONG_CONSTANT("SEN_ENC_KOI8R", 6, CONST_PERSISTENT | CONST_CS);
  le_sen_ctx = zend_register_list_destructors_ex(sen_ctx_dtor,
               NULL, "sen_ctx", module_number);
  le_sen_pctx = zend_register_list_destructors_ex(NULL,
               sen_pctx_dtor, "sen_pctx", module_number);
  le_sen_db = zend_register_list_destructors_ex(sen_db_dtor,
               NULL, "sen_db", module_number);
  sen_init();
  return SUCCESS;
}


PHP_MSHUTDOWN_FUNCTION(sen_ctx)
{
  sen_fin();
  return SUCCESS;
}


PHP_RINIT_FUNCTION(sen_ctx)
{
  return SUCCESS;
}


PHP_RSHUTDOWN_FUNCTION(sen_ctx)
{
  return SUCCESS;
}


PHP_MINFO_FUNCTION(sen_ctx)
{
  php_info_print_box_start(0);
  php_printf("<p>senna binding for PHP.</p>\n");
  php_printf("<p>Version 0.1-devel (2007-08-16)</p>\n");
  php_info_print_box_end();
}


PHP_FUNCTION(sen_db_create)
{
  sen_db * return_res;
  long return_res_id = -1;
  const char * path = NULL;
  int path_len = 0;
  long flags = 0;
  long encoding = 0;

  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "sll", &path, &path_len, &flags, &encoding) == FAILURE) {
    return;
  }
  do {
    return_res = sen_db_create(path,flags,encoding);
    if(!return_res) RETURN_FALSE;
  } while (0);
  return_res_id = ZEND_REGISTER_RESOURCE(return_value, return_res, le_sen_db);
}


PHP_FUNCTION(sen_db_open)
{
  sen_db * return_res;
  long return_res_id = -1;
  const char * path = NULL;
  int path_len = 0;

  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s", &path, &path_len) == FAILURE) {
    return;
  }
  return_res = sen_db_open(path);
  if(!return_res) RETURN_FALSE;
  return_res_id = ZEND_REGISTER_RESOURCE(return_value, return_res, le_sen_db);
}


PHP_FUNCTION(sen_db_close)
{
  zval * res = NULL;
  int res_id = -1;
  sen_db * res_res;

  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &res) == FAILURE) {
    return;
  }
  ZEND_FETCH_RESOURCE(res_res, sen_db *, &res, res_id, "sen_db", le_sen_db);
	FREE_RESOURCE(res);
  RETURN_TRUE;
}


PHP_FUNCTION(sen_ctx_open)
{
  sen_ctx * return_res;
  long return_res_id = -1;
  zval * res = NULL;
  int res_id = -1;
  sen_db * res_res;
  long flags = 0;

  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rl", &res, &flags) == FAILURE) {
    return;
  }
  ZEND_FETCH_RESOURCE(res_res, sen_db *, &res, res_id, "sen_db", le_sen_db);

  return_res = sen_ctx_open(res_res,flags);
  if(!return_res) RETURN_FALSE;

  return_res_id = ZEND_REGISTER_RESOURCE(return_value, return_res, le_sen_ctx);
}


PHP_FUNCTION(sen_ctx_connect)
{
  sen_ctx * return_res;
  long return_res_id = -1;
  const char * host = NULL;
  int host_len = 0;
  long port = 0;
  long flags = 0;

  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "sll", &host, &host_len, &port, &flags) == FAILURE) {
    return;
  }
  return_res = sen_ctx_connect(host,port,flags);
  if(!return_res) RETURN_FALSE;
  return_res_id = ZEND_REGISTER_RESOURCE(return_value, return_res, le_sen_ctx);
}


PHP_FUNCTION(sen_ctx_pconnect)
{
  sen_ctx * return_res;
  long return_res_id = -1;
  const char * host = NULL;
  char *hash_key = NULL;
  int hash_key_len;
  int host_len = 0;
  long port = 0;
  long flags = 0;
  zend_rsrc_list_entry *le;

  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "sll", &host, &host_len, &port, &flags) == FAILURE) {
    return;
  }

  hash_key_len = spprintf(&hash_key, 0, "ctx_%s_%d_%d", host, port, flags);

  if (zend_hash_find(&EG(persistent_list), hash_key, hash_key_len+1, (void **) &le)==FAILURE) {
    zend_rsrc_list_entry new_le;
	  return_res = sen_ctx_connect(host,port,flags);
    Z_TYPE(new_le) = le_sen_pctx;
    new_le.ptr = return_res;
    if (zend_hash_update(&EG(persistent_list), hash_key, hash_key_len+1, (void *) &new_le, sizeof(zend_rsrc_list_entry), NULL)==FAILURE) {
      sen_ctx_close(return_res);
      efree(hash_key);
      RETURN_FALSE;
    }
  } else {
    if (Z_TYPE_P(le) != le_sen_pctx) {
      RETURN_FALSE;
    }
    return_res = (sen_ctx *) le->ptr;
  }
  if(!return_res) RETURN_FALSE;
  return_res_id = ZEND_REGISTER_RESOURCE(return_value, return_res, le_sen_pctx);
}


PHP_FUNCTION(sen_ctx_load)
{
  zval * res = NULL;
  int res_id = -1;
  sen_ctx * res_res;
  const char * path = NULL;
  int path_len = 0;

  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rs", &res, &path, &path_len) == FAILURE) {
    return;
  }
  ZEND_FETCH_RESOURCE2(res_res, sen_ctx *, &res, res_id, "sen_ctx", le_sen_ctx, le_sen_pctx);

  int rc;
  rc = sen_ctx_load(res_res,path);
  RETURN_LONG(rc);
}


PHP_FUNCTION(sen_ctx_send)
{
  zval * res = NULL;
  int res_id = -1;
  sen_ctx * res_res;
  char * query = NULL;
  int query_len = 0;
  long flags = 0;

  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rsl", &res, &query, &query_len, &flags) == FAILURE) {
    return;
  }
  ZEND_FETCH_RESOURCE2(res_res, sen_ctx *, &res, res_id, "sen_ctx", le_sen_ctx, le_sen_pctx);

  int rc;
  rc = sen_ctx_send(res_res, query, query_len, flags);
  RETURN_LONG(rc);
}


PHP_FUNCTION(sen_ctx_recv)
{
  zval * res = NULL;
  int res_id = -1;
  sen_ctx * res_res;

  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &res) == FAILURE) {
    return;
  }
  ZEND_FETCH_RESOURCE2(res_res, sen_ctx *, &res, res_id, "sen_ctx", le_sen_ctx, le_sen_pctx);


  array_init(return_value);

  char *str;
  int rc,str_len,flags;
  rc=sen_ctx_recv(res_res, &str, &str_len, &flags);
  add_next_index_long(return_value, rc);
  add_next_index_stringl(return_value, str, str_len, 1);
  add_next_index_long(return_value, flags);
}


PHP_FUNCTION(sen_ctx_close)
{
  zval * res = NULL;
  int res_id = -1;
  sen_ctx * res_res;

  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &res) == FAILURE) {
    return;
  }
  ZEND_FETCH_RESOURCE2(res_res, sen_ctx *, &res, res_id, "sen_ctx", le_sen_ctx, le_sen_pctx);

	FREE_RESOURCE(res);
  RETURN_TRUE;
}


PHP_FUNCTION(sen_ctx_info_get)
{
  zval * res = NULL;
  int res_id = -1;
  sen_ctx * res_res;

  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &res) == FAILURE) {
    return;
  }
  ZEND_FETCH_RESOURCE2(res_res, sen_ctx *, &res, res_id, "sen_ctx", le_sen_ctx, le_sen_pctx);


  array_init(return_value);

  int rc;
  sen_ctx_info info;
  rc=sen_ctx_info_get(res_res, &info);
  add_assoc_long(return_value,"rc",rc);
  add_assoc_long(return_value,"fd",info.fd);
  add_assoc_long(return_value,"status",info.com_status);
  add_assoc_long(return_value,"info",info.com_info);
}

#endif /* HAVE_SEN_CTX */
