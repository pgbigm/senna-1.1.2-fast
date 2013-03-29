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

#ifndef PHP_SEN_CTX_H
#define PHP_SEN_CTX_H

#ifdef  __cplusplus
extern "C" {
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <php.h>

#ifdef HAVE_SEN_CTX

#include <php_ini.h>
#include <SAPI.h>
#include <ext/standard/info.h>
#include <Zend/zend_extensions.h>
#ifdef  __cplusplus
} // extern "C"
#endif
#include <senna/senna.h>
#ifdef  __cplusplus
extern "C" {
#endif

extern zend_module_entry sen_ctx_module_entry;
#define phpext_sen_ctx_ptr &sen_ctx_module_entry

#ifdef PHP_WIN32
#define PHP_SEN_CTX_API __declspec(dllexport)
#else
#define PHP_SEN_CTX_API
#endif

PHP_MINIT_FUNCTION(sen_ctx);
PHP_MSHUTDOWN_FUNCTION(sen_ctx);
PHP_RINIT_FUNCTION(sen_ctx);
PHP_RSHUTDOWN_FUNCTION(sen_ctx);
PHP_MINFO_FUNCTION(sen_ctx);

#ifdef ZTS
#include "TSRM.h"
#endif

#define FREE_RESOURCE(resource) zend_list_delete(Z_LVAL_P(resource))

#define PROP_GET_LONG(name)    Z_LVAL_P(zend_read_property(_this_ce, _this_zval, #name, strlen(#name), 1 TSRMLS_CC))
#define PROP_SET_LONG(name, l) zend_update_property_long(_this_ce, _this_zval, #name, strlen(#name), l TSRMLS_CC)

#define PROP_GET_DOUBLE(name)    Z_DVAL_P(zend_read_property(_this_ce, _this_zval, #name, strlen(#name), 1 TSRMLS_CC))
#define PROP_SET_DOUBLE(name, d) zend_update_property_double(_this_ce, _this_zval, #name, strlen(#name), d TSRMLS_CC)

#define PROP_GET_STRING(name)    Z_STRVAL_P(zend_read_property(_this_ce, _this_zval, #name, strlen(#name), 1 TSRMLS_CC))
#define PROP_GET_STRLEN(name)    Z_STRLEN_P(zend_read_property(_this_ce, _this_zval, #name, strlen(#name), 1 TSRMLS_CC))
#define PROP_SET_STRING(name, s) zend_update_property_string(_this_ce, _this_zval, #name, strlen(#name), s TSRMLS_CC)
#define PROP_SET_STRINGL(name, s, l) zend_update_property_string(_this_ce, _this_zval, #name, strlen(#name), s, l TSRMLS_CC)


PHP_FUNCTION(sen_db_create);
PHP_FUNCTION(sen_db_open);
PHP_FUNCTION(sen_db_close);
PHP_FUNCTION(sen_ctx_open);
PHP_FUNCTION(sen_ctx_connect);
PHP_FUNCTION(sen_ctx_pconnect);
PHP_FUNCTION(sen_ctx_load);
PHP_FUNCTION(sen_ctx_send);
PHP_FUNCTION(sen_ctx_recv);
PHP_FUNCTION(sen_ctx_close);
PHP_FUNCTION(sen_ctx_info_get);
#ifdef  __cplusplus
} // extern "C"
#endif

#endif /* PHP_HAVE_SEN_CTX */

#endif /* PHP_SEN_CTX_H */
