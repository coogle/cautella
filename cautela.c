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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "main/php_version.h"

#ifndef PHP_WIN32
#include <sys/time.h>
#include <unistd.h>
#else
#include "win32/time.h"
#include <process.h>
#endif

#include "TSRM.h"
#include "SAPI.h"
#include "main/php_ini.h"
#include "ext/standard/head.h"
#include "ext/standard/html.h"
#include "ext/standard/info.h"
#include "ext/standard/php_smart_str.h"
#include "php_globals.h"
#include "ext/standard/php_var.h"

#include "zend.h"
#include "zend_API.h"
#include "zend_alloc.h"
#include "zend_execute.h"
#include "zend_compile.h"
#include "zend_constants.h"
#include "zend_extensions.h"
#include "zend_exceptions.h"
#include "zend_vm.h"

#include "php_cautela.h"

// Just used to make sure we actually *are* a Zend Engine Extension
int zend_cautela_initialized = 0;

// TODO: Hook these to capture function calls and do a callback when a call
// is made. This will allow us to introspect the parameters of the call to see
// if our data got through.. 
void (*cautela_orig_execute)(zend_op_array *op_array TSRMLS_DC);
void cautela_execute(zend_op_array *op_array TSRMLS_DC);

// Internal functions are the most interesting (i.e. mysql_query())
// to monitor..
void (*cautela_orig_execute_internal)(zend_execute_data *current_execute_data, int return_value_used TSRMLS_DC);
void cautela_execute_internal(zend_execute_data *current_execute_data, int return_value_used TSRMLS_DC);

zend_function_entry cautela_functions[] = {
        PHP_FE(cautela_enable,                        NULL)
        PHP_FE(cautela_disable,                       NULL)
        PHP_FE(cautela_set_print_callback,            NULL)
	{NULL, NULL, NULL}	
};

zend_module_dep php_cautela_ext_deps[] = {
        {NULL, NULL, NULL}
};

zend_module_entry cautela_module_entry = {
	STANDARD_MODULE_HEADER_EX,
        NULL,
        php_cautela_ext_deps,
	"cautela",
	cautela_functions,
	PHP_MINIT(cautela),
	PHP_MSHUTDOWN(cautela),
	PHP_RINIT(cautela),	
	PHP_RSHUTDOWN(cautela),	
	PHP_MINFO(cautela),
	CAUTELA_VERSION, 
	NO_MODULE_GLOBALS,
	ZEND_MODULE_POST_ZEND_DEACTIVATE_N(cautela),
	STANDARD_MODULE_PROPERTIES_EX
};

ZEND_DECLARE_MODULE_GLOBALS(cautela)

#ifdef COMPILE_DL_CAUTELA
ZEND_GET_MODULE(cautela)
#endif

PHP_INI_BEGIN()
        STD_PHP_INI_BOOLEAN("cautela.enabled", "0", PHP_INI_ALL, OnUpdateBool, enabled, zend_cautela_globals, cautela_globals)
PHP_INI_END()

static void cautela_clear_znode(znode* n) {
    if (n->op_type == IS_CONST) {
        zval_dtor(&n->u.constant);
    }
    SET_UNUSED(*n);
}

static cautela_replace_znode(znode* dst, znode* src) {
    memcpy(dst, src, sizeof(znode));
    SET_UNUSED(*src);
}
 
static void cautela_clear_zend_op(zend_op* op) {
     op->opcode = ZEND_NOP;
     cautela_clear_znode(&op->result);
     cautela_clear_znode(&op->op1);
     cautela_clear_znode(&op->op2);
}
 
static void cautela_replace_zend_op(zend_op* dst, zend_op* src) {
     cautela_clear_zend_op(dst);
     dst->opcode = src->opcode;
     src->opcode = ZEND_NOP;
     dst->extended_value = src->extended_value;
     dst->lineno = src->lineno;
     
     cautela_replace_znode(&dst->result, &src->result);
     cautela_replace_znode(&dst->op1, &src->op1);
     cautela_replace_znode(&dst->op2, &src->op2);
}

static inline zval *_cautela_get_zval_ptr_cv(znode *node, temp_variable *Ts) {
        zval ***ptr = &EG(current_execute_data)->CVs[node->u.var];

        if (!*ptr) {
                zend_compiled_variable *cv = &EG(active_op_array)->vars[node->u.var];
                if (zend_hash_quick_find(EG(active_symbol_table), cv->name, cv->name_len+1, cv->hash_value, (void **)ptr)==FAILURE) {
                        zend_error(E_ERROR, "Could not find zval of printed variable");
                        return NULL;
                }
        }
        return **ptr;
}


#define T(offset) (*(temp_variable *)((char *) execute_data->Ts + offset))

static zend_bool cautela_do_print_callback(ZEND_OPCODE_HANDLER_ARGS) {
        zval *retval;
        zval *args[4];
        zval *file, *line, value, *context;
        char *callable = NULL;
        char *filename, *print_value = NULL;
        static int in_callback = 0;
        int i;
        int use_copy = 0;
        zval expr_copy;
        zend_op *opline = execute_data->opline;
        zval *printed_zval;

        if(ZCG(print_callback) && ZCG(monitor) && ZCG(enabled)) {

                if(in_callback) {
                        return TRUE;
                }

                if(zend_is_callable(ZCG(print_callback), 0, &callable)) {
                        efree(callable);

                        MAKE_STD_ZVAL(file);
                        MAKE_STD_ZVAL(line);
                        MAKE_STD_ZVAL(context);
                        MAKE_STD_ZVAL(retval);
                        ZVAL_TRUE(retval);
                        
                        if(execute_data->op_array->filename) {
                                filename = estrdup(execute_data->op_array->filename);
                        } else {
                                filename = estrdup("unknown");
                        }
                        
                        // Figure out where the value being printed is actually
                        // stored and go grab a pointer to it
                        if(opline->op1.op_type == IS_CONST) {                   // Constant
                                printed_zval = (zval *)&opline->op1.u.constant;
                        } else if(opline->op1.op_type == IS_TMP_VAR) {          // Temp Var 
                                printed_zval = (zval *)&T(opline->op1.u.var).tmp_var;
                        } else if(opline->op1.op_type == IS_VAR) {              // Var
                                printed_zval = (zval *)T(opline->op1.u.var).var.ptr;
                        } else if(opline->op1.op_type == IS_CV) {               // Compiled Var
                                printed_zval = (zval *)_cautela_get_zval_ptr_cv(&opline->op1, execute_data->Ts);
                        }
                        
                        value = *printed_zval;
                        zval_copy_ctor(&value);
                        
                        ZVAL_STRING(file, filename, 0);
                        ZVAL_LONG(line, (*EG(opline_ptr))->lineno);
                        
                        if(ZCG(print_callback_context) != NULL) {
                                ZVAL_ZVAL(context, ZCG(print_callback_context), TRUE, FALSE);
                        } else {
                                ZVAL_NULL(context);
                        }
                        
                        args[0] = file;
                        args[1] = line;
                        args[2] = &value;
                        args[3] = context;
                        
                        in_callback = 1;
                        /*
                         Call user function in the form:
                                callback($file, $line, $value, $context = null);
                         
                         Return value from callback (true/false) determines of text is displayed
                        */
                        if(call_user_function(CG(function_table), NULL, ZCG(print_callback),
                                             retval, 4, args TSRMLS_CC) == FAILURE) {
                                zend_error(E_WARNING, "Could not call print callback function");
                        }

                        in_callback = 0;
                
                        zval_ptr_dtor(&file);
                        zval_ptr_dtor(&line);
                        zval_ptr_dtor(&context);
                        
                        if(filename) {
                                efree(filename);
                        }
                        
                        if(print_value) {
                                efree(print_value);
                        }
                        
                        convert_to_boolean(retval);
                        
                        if(Z_BVAL_P(retval)) {
                                zval_ptr_dtor(&retval);
                                return TRUE;
                        }
                        
                        zval_ptr_dtor(&retval);
                        return FALSE;
                } else {
                        efree(callable);
                }
        }
        
        return TRUE;
}

static int cautela_print_handler(ZEND_OPCODE_HANDLER_ARGS) {

        /* Allow userspace function to return false and cause the print-op
           to be skipped entirely. */
        if(!cautela_do_print_callback(ZEND_OPCODE_HANDLER_ARGS_PASSTHRU)) {
                CAUTELA_SKIP_OP();      // Converts current op to NOP
        }   
        
        return ZEND_USER_OPCODE_DISPATCH;
}

static void php_cautela_init_globals(zend_cautela_globals *cg) {
        cg->enabled = 0;
        cg->monitor = 0;
        
        cg->print_callback = NULL;        
        cg->print_callback_context = NULL;
}

static void php_cautela_shutdown_globals (zend_cautela_globals *cg TSRMLS_DC) {
        /* Do nothing */
}

PHP_MINIT_FUNCTION(cautela) {
        
      	zend_set_user_opcode_handler(ZEND_PRINT, cautela_print_handler);
        zend_set_user_opcode_handler(ZEND_ECHO, cautela_print_handler);
        
	if (zend_cautela_initialized == 0) {
		zend_error(E_WARNING, "Cautela must be loaded as a Zend extension");
	}
        
        ZEND_INIT_MODULE_GLOBALS(cautela, php_cautela_init_globals, php_cautela_shutdown_globals);
	REGISTER_INI_ENTRIES();
        
        return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(cautela) {
#ifdef ZTS
	ts_free_id(cautela_globals_id);
#else
	php_cautela_shutdown_globals(&cautela_globals TSRMLS_CC);
#endif

	return SUCCESS;
}

PHP_RINIT_FUNCTION(cautela) {
        
	return SUCCESS;
}

PHP_RSHUTDOWN_FUNCTION(cautela) {
        ZCG(monitor) = 0;
        
        if(ZCG(print_callback)) {
                zval_ptr_dtor(&ZCG(print_callback));
                ZCG(print_callback) = NULL;
        }
        
        if(ZCG(print_callback_context)) {
                zval_ptr_dtor(&ZCG(print_callback_context));
                ZCG(print_callback_context) = NULL;
        }
}

PHP_FUNCTION(cautela_enable) {
        ZCG(monitor) = 1;
}

PHP_FUNCTION(cautela_disable) {
        ZCG(monitor) = 0;
}


#define CAUTELA_CALLBACK(ident) \
PHP_FUNCTION(cautela_set_##ident##_callback) \
{\
        zval *callback, *data = NULL; \
        char *fname; \
        \
        if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "z|z", &callback, &data) == FAILURE) { \
                return; \
        } \
\
        if(!zend_is_callable(callback, 0, &fname)) { \
                zend_error(E_ERROR, "First parameter must be a valid callback"); \
                efree(fname); \
                return; \
        } \
        \
        efree(fname); \
        \
        if(ZCG(ident##_callback) != NULL) { \
                zval_ptr_dtor(&ZCG(ident##_callback)); \
        } \
        \
        if(data) { \
                if(ZCG(ident##_callback_context) != NULL) { \
                        zval_ptr_dtor(&ZCG(ident##_callback_context)); \
                } \
                MAKE_STD_ZVAL(ZCG(ident##_callback_context)); \
                ZVAL_ZVAL(ZCG(ident##_callback_context), data, TRUE, FALSE); \
        } \
        \
        MAKE_STD_ZVAL(ZCG(ident##_callback)); \
        ZVAL_ZVAL(ZCG(ident##_callback), callback, TRUE, FALSE);\
}

CAUTELA_CALLBACK(print);

ZEND_MODULE_POST_ZEND_DEACTIVATE_D(cautela) {
	return SUCCESS;
}

PHP_MINFO_FUNCTION(cautela) {
	php_info_print_table_start();
	php_info_print_table_header(2, "cautela support", "enabled");
	php_info_print_table_end();
}

ZEND_DLEXPORT int cautela_zend_startup(zend_extension *extension) {
	zend_cautela_initialized = 1;
	return zend_startup_module(&cautela_module_entry);
}

ZEND_DLEXPORT int cautela_zend_api_no_check(int api_no) {
        switch(api_no) {
                case 220060519:
                        return SUCCESS;
        }
        
        return FAILURE;
}

#ifndef ZEND_EXT_API
#define ZEND_EXT_API    ZEND_DLEXPORT
#endif

ZEND_DLEXPORT zend_extension_version_info extension_version_info = {
        220060519, // The Zend Engine API version we wrote against..
                   // If we don't match then the api_check function will validate what we have..
        "2.2.0",
        FALSE,     // We aren't going to even claim the ext is thread safe
        TRUE       // Pass through our debug build
};

ZEND_DLEXPORT zend_extension zend_extension_entry = {
	CAUTELA_NAME,
	CAUTELA_VERSION,
	CAUTELA_AUTHOR,
	CAUTELA_URL,
	"Copyright (c) 2007",
	cautela_zend_startup,
	NULL,                                   /* shutdown_func_t */
	NULL,                                   /* activate_func_t */
	NULL,                                   /* deactivate_func_t */
	NULL,                                   /* message_handler_func_t */
	NULL,                                   /* op_array_handler_func_t */
	NULL,                                   /* statement_handler_func_t */
	NULL,                                   /* fcall_begin_handler_func_t */
	NULL,                                   /* fcall_end_handler_func_t */
	NULL,                                   /* op_array_ctor_func_t */
	NULL,                                   /* op_array_dtor_func_t */
        cautela_zend_api_no_check,              /* api_no_check_func */
	COMPAT_ZEND_EXTENSION_PROPERTIES
};


/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
