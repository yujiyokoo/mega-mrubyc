/*! @file
  @brief
  exception classes

  <pre>
  Copyright (C) 2015-2021 Kyushu Institute of Technology.
  Copyright (C) 2015-2021 Shimane IT Open-Innovation Center.

  This file is distributed under BSD 3-Clause License.

  Fetch mruby VM bytecodes, decode and execute.

  </pre>
*/

/***** Feature test switches ************************************************/
/***** System headers *******************************************************/
#include "vm_config.h"
#include <string.h>

/***** Local headers ********************************************************/
#include "alloc.h"
#include "value.h"
#include "class.h"
#include "symbol.h"
#include "vm.h"
#include "c_string.h"
#include "error.h"


/***** Constat values *******************************************************/
/***** Macros ***************************************************************/
/***** Typedefs *************************************************************/
/***** Function prototypes **************************************************/
/***** Local variables ******************************************************/
/***** Global variables *****************************************************/
/***** Functions ************************************************************/
//================================================================
/*! constructor

  @param  vm		pointer to VM.
  @param  exc_cls	pointer to Exception class.
  @param  message	message.
  @param  len		message length.
  @return		exception object.
*/
mrbc_value mrbc_exception_new(struct VM *vm, struct RClass *exc_cls, const void *message, int len )
{
  // allocate memory.
  mrbc_exception *ex = mrbc_alloc( vm, sizeof(mrbc_exception) );
  if( !ex ) goto ERROR_RETURN;	// ENOMEM

  MRBC_INIT_OBJECT_HEADER( ex, "EX" );
  ex->cls = exc_cls;

  if( message ) {
    ex->message = mrbc_alloc( vm, len+1 );
    if( !ex->message ) {
      mrbc_free( vm, ex );
      goto ERROR_RETURN;	// ENOMEM
    }

    // copy source string.
    memcpy( ex->message, message, len );
    ex->message[len] = '\0';
    ex->message_size = len;
  } else {
    ex->message = 0;
    ex->message_size = 0;
  }

  return (mrbc_value){.tt = MRBC_TT_EXCEPTION, .exception = ex};

 ERROR_RETURN:
  return mrbc_nil_value();
}


//================================================================
/*! destructor

  @param  value		target.
*/
void mrbc_exception_delete(mrbc_value *value)
{
  if( value->exception->message ) mrbc_raw_free( value->exception->message );
  mrbc_raw_free( value->exception );
}


//================================================================
/*! message setter.

  @param  vm		pointer to VM.
  @param  value		target.
  @param  message	message.
  @param  len		message length.
*/
void mrbc_exception_set_message(struct VM *vm, mrbc_value *value, const void *message, int len)
{
  mrbc_exception *ex = value->exception;

  if( ex->message ) {
    mrbc_raw_free( ex->message );
    ex->message = NULL;
    ex->message_size = 0;
  }

  if( message ) {
    ex->message = mrbc_alloc( vm, len+1 );
    if( !ex->message ) return;	// ENOMEM

    // copy source string.
    memcpy( ex->message, message, len );
    ex->message[len] = '\0';

    ex->message_size = len;
  }
}


//================================================================
/*! raise exception

  @param  vm		pointer to VM.
  @param  exc_cls	pointer to Exception class.
  @param  msg		message.
  @note	(usage) mrbc_raise(vm, MRBC_CLASS(TypeError), "message here.");
*/
void mrbc_raise( struct VM *vm, struct RClass *exc_cls, const char *msg )
{
  vm->exception = mrbc_exception_new( vm,
			exc_cls ? exc_cls : MRBC_CLASS(RuntimeError),
			msg,
			msg ? strlen(msg) : 0 );
  vm->flag_preemption = 2;
}


//----------------------------------------------------------------
// Exception class
//----------------------------------------------------------------
//================================================================
/*! (method) new
 */
static void c_exception_new(struct VM *vm, mrbc_value v[], int argc)
{
  assert( mrbc_type(v[0]) == MRBC_TT_CLASS );

  mrbc_value value;
  if( argc == 1 && mrbc_type(v[1]) == MRBC_TT_STRING ) {
    value = mrbc_exception_new(vm, v[0].cls, mrbc_string_cstr(&v[1]), mrbc_string_size(&v[1]));
  } else {
    value = mrbc_exception_new(vm, v[0].cls, NULL, 0);
  }

  SET_RETURN(value);
}


//================================================================
/*! (method) message
 */
static void c_exception_message(struct VM *vm, mrbc_value v[], int argc)
{
  mrbc_value value;

  if( v[0].exception->message ) {
    value = mrbc_string_new( vm, v[0].exception->message, v[0].exception->message_size );
  } else {
    value = mrbc_string_new_cstr(vm, symid_to_str(v->exception->cls->sym_id));
  }

  mrbc_decref( &v[0] );
  v[0] = value;
}


/* mruby/c Exception class hierarchy.

    Exception
      NoMemoryError
      StandardError
        ArgumentError
        IndexError
        NameError
          NoMethodError
        RangeError
        RuntimeError
        TypeError
        ZeroDivisionError
*/

/* MRBC_AUTOGEN_METHOD_TABLE
  CLASS("Exception")
  FILE("method_table_exception.h")

  METHOD("new", c_exception_new )
  METHOD("message", c_exception_message )
*/
#include "method_table_exception.h"

/* MRBC_AUTOGEN_METHOD_TABLE
  CLASS("NoMemoryError")
  SUPER("Exception")
  FILE("method_table_nomemoryerror.h")
*/
#include "method_table_nomemoryerror.h"

/* MRBC_AUTOGEN_METHOD_TABLE
  CLASS("StandardError")
  SUPER("Exception")
  FILE("method_table_standarderror.h")
*/
#include "method_table_standarderror.h"

/* MRBC_AUTOGEN_METHOD_TABLE
  CLASS("ArgumentError")
  SUPER("StandardError")
  FILE("method_table_argumenterror.h")
*/
#include "method_table_argumenterror.h"

/* MRBC_AUTOGEN_METHOD_TABLE
  CLASS("IndexError")
  SUPER("StandardError")
  FILE("method_table_indexerror.h")
*/
#include "method_table_indexerror.h"

/* MRBC_AUTOGEN_METHOD_TABLE
  CLASS("NameError")
  SUPER("StandardError")
  FILE("method_table_nameerror.h")
*/
#include "method_table_nameerror.h"

/* MRBC_AUTOGEN_METHOD_TABLE
  CLASS("NoMethodError")
  SUPER("NameError")
  FILE("method_table_nomethoderror.h")
*/
#include "method_table_nomethoderror.h"

/* MRBC_AUTOGEN_METHOD_TABLE
  CLASS("RangeError")
  SUPER("StandardError")
  FILE("method_table_rangeerror.h")
*/
#include "method_table_rangeerror.h"

/* MRBC_AUTOGEN_METHOD_TABLE
  CLASS("RuntimeError")
  SUPER("StandardError")
  FILE("method_table_runtimeerror.h")
*/
#include "method_table_runtimeerror.h"

/* MRBC_AUTOGEN_METHOD_TABLE
  CLASS("TypeError")
  SUPER("StandardError")
  FILE("method_table_typeerror.h")
*/
#include "method_table_typeerror.h"

/* MRBC_AUTOGEN_METHOD_TABLE
  CLASS("ZeroDivisionError")
  SUPER("StandardError")
  FILE("method_table_zerodivisionerror.h")
*/
#include "method_table_zerodivisionerror.h"
