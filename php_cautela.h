/*
  +----------------------------------------------------------------------+
  | Cautela                                                              |
  +----------------------------------------------------------------------+
  | Copyright (c) 2007 John Coggeshall                                   |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author: John Coggeshall <john@php.net>                               |
  +----------------------------------------------------------------------+
*/

/* $Id: header,v 1.16.2.1.2.1 2007/01/01 19:32:09 iliaa Exp $ */

#ifndef PHP_CAUTELA_H
#define PHP_CAUTELA_H

#define CAUTELA_NAME      "Cautela"
#define CAUTELA_AUTHOR    "John Coggeshall"
#define CAUTELA_COPYRIGHT "Copyright (c) 2007 by John Coggeshall"
#define CAUTELA_URL       "http://www.cautela.org"
#define CAUTELA_VERSION   "0.1-dev"

extern zend_module_entry cautela_module_entry;
#define phpext_cautela_ptr &cautela_module_entry

#ifdef PHP_WIN32
#define PHP_CAUTELA_API __declspec(dllexport)
#else
#define PHP_CAUTELA_API
#endif

#ifdef ZTS
#include "TSRM.h"
#endif

#define CAUTELA_TYPE_FUNCTION   0x01
#define CAUTELA_TYPE_METHOD     0x02
#define CAUTELA_TYPE_STATIC     0x03

#define PRINTED_VALUE Z_STRVAL((*EG(opline_ptr))->op1.u.constant)

#define MAKE_NOP(opline)        { opline->opcode = ZEND_NOP;  memset(&opline->result,0,sizeof(znode)); memset(&opline->op1,0,sizeof(znode)); memset(&opline->op2,0,sizeof(znode)); opline->result.op_type=opline->op1.op_type=opline->op2.op_type=IS_UNUSED;  }
#define CAUTELA_SKIP_OP() { \
        zend_op *nop; \
        nop = emalloc(sizeof(zend_op)); \
        MAKE_NOP(nop); \
        cautela_replace_zend_op(execute_data->opline, nop); \
        efree(nop); \
        return ZEND_USER_OPCODE_DISPATCH; \
}

#define CAUTELA_CALLBACK_DTOR(ident) \
if(cg->ident##_callback_context != NULL) zval_dtor(cg->ident##_callback_context); \
if(cg->ident##_callback != NULL) zval_dtor(cg->ident##_callback);

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

PHP_MINIT_FUNCTION(cautela);
PHP_MSHUTDOWN_FUNCTION(cautela);
PHP_RINIT_FUNCTION(cautela);
PHP_RSHUTDOWN_FUNCTION(cautela);
PHP_MINFO_FUNCTION(cautela);
ZEND_MODULE_POST_ZEND_DEACTIVATE_D(cautela);

ZEND_DLEXPORT int cautela_zend_startup(zend_extension *extension);
ZEND_DLEXPORT int cautela_zend_api_no_check(int api_no);

PHP_FUNCTION(cautela_enable);
PHP_FUNCTION(cautela_disable);
PHP_FUNCTION(cautela_set_print_callback);

static void cautela_clear_znode(znode* n);
static cautela_move_znode(znode* dst, znode* src);
static void cautela_clear_zend_op(zend_op* op);
static void cautela_move_zend_op(zend_op* dst, zend_op* src);
static inline zval *_cautela_get_zval_ptr_cv(znode *node, temp_variable *Ts);

static zend_bool cautela_do_print_callback(ZEND_OPCODE_HANDLER_ARGS);
static int cautela_print_handler(ZEND_OPCODE_HANDLER_ARGS);

ZEND_BEGIN_MODULE_GLOBALS(cautela)
    zend_bool           enabled;
    zend_bool           monitor;
    
    zval *              print_callback;
    zval *              print_callback_context;
ZEND_END_MODULE_GLOBALS(cautela)

#ifdef ZTS
#define ZCG(v) TSRMG(cautela_globals_id, zend_cautela_globals *, v)
#else
#define ZCG(v) (cautela_globals.v)
#endif

#endif	/* PHP_CAUTELA_H */


/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
