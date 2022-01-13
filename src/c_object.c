/*! @file
  @brief
  Object, Proc, Nil, True and False class.

  <pre>
  Copyright (C) 2015-2021 Kyushu Institute of Technology.
  Copyright (C) 2015-2021 Shimane IT Open-Innovation Center.

  This file is distributed under BSD 3-Clause License.

  </pre>
*/


/***** Feature test switches ************************************************/
/***** System headers *******************************************************/
#include "vm_config.h"
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

/***** Local headers ********************************************************/
#include "alloc.h"
#include "value.h"
#include "vm.h"
#include "class.h"
#include "symbol.h"
#include "c_string.h"
#include "c_array.h"
#include "c_hash.h"
#include "console.h"
#include "opcode.h"
#include "error.h"

/***** Functions ************************************************************/

//----------------------------------------------------------------
// Object class
//----------------------------------------------------------------
//================================================================
/*! (method) new
 */
static void c_object_new(struct VM *vm, mrbc_value v[], int argc)
{
  mrbc_value new_obj = mrbc_instance_new(vm, v->cls, 0);
  if( new_obj.instance == NULL ) return;	// ENOMEM

  mrbc_method method;
  if( mrbc_find_method( &method, v->cls, MRBC_SYM(initialize) ) == 0 ) {
    goto DONE;
  }

  mrbc_irep *irep = mrbc_alloc( vm, sizeof(mrbc_irep) + sizeof(mrbc_sym) );
  if( !irep ) goto DONE;		// ENOMEM

  memset( irep, 0, sizeof(mrbc_irep) );

  uint8_t code[] = {
    OP_SEND, 0, 0, argc,
    OP_ABORT,
  };
  irep->ilen = sizeof(code);
  irep->inst = code;
  *((mrbc_sym *)irep->data) = MRBC_SYM(initialize);
  mrbc_class *cls = v->cls;

  mrbc_decref(&v[0]);
  v[0] = new_obj;
  mrbc_incref(&new_obj);

  const mrbc_irep *org_cur_irep = vm->cur_irep;
  mrbc_value* org_regs = vm->cur_regs;
  const uint8_t *org_inst = vm->inst;

  vm->cur_irep = irep;
  vm->cur_regs = v;
  vm->inst = irep->inst;

  while( mrbc_vm_run(vm) == 0 )
    ;

  vm->flag_preemption = 0;
  vm->cur_irep = org_cur_irep;
  vm->inst = org_inst;
  vm->cur_regs = org_regs;

  new_obj.instance->cls = cls;
  mrbc_free( vm, irep );

DONE:
  SET_RETURN( new_obj );
  return;
}


//================================================================
/*! (operator) !
 */
static void c_object_not(struct VM *vm, mrbc_value v[], int argc)
{
  SET_BOOL_RETURN( mrbc_type(v[0]) == MRBC_TT_NIL || mrbc_type(v[0]) == MRBC_TT_FALSE );
}


//================================================================
/*! (operator) !=
 */
static void c_object_neq(struct VM *vm, mrbc_value v[], int argc)
{
  int result = mrbc_compare( &v[0], &v[1] );
  SET_BOOL_RETURN( result != 0 );
}


//================================================================
/*! (operator) <=>
 */
static void c_object_compare(struct VM *vm, mrbc_value v[], int argc)
{
  int result = mrbc_compare( &v[0], &v[1] );
  SET_INT_RETURN( result );
}


//================================================================
/*! (operator) ===
 */
static void c_object_equal3(struct VM *vm, mrbc_value v[], int argc)
{
  int result;

  if( mrbc_type(v[0]) == MRBC_TT_CLASS ) {
    result = mrbc_obj_is_kind_of( &v[1], v[0].cls );
  } else {
    result = (mrbc_compare( &v[0], &v[1] ) == 0);
  }

  SET_BOOL_RETURN( result );
}


//================================================================
/*! (method) class
 */
static void c_object_class(struct VM *vm, mrbc_value v[], int argc)
{
  mrbc_value value = {.tt = MRBC_TT_CLASS};
  value.cls = find_class_by_object( v );
  SET_RETURN( value );
}


//================================================================
/*! (method) dup
 */
static void c_object_dup(struct VM *vm, mrbc_value v[], int argc)
{
  if( mrbc_type(v[0]) == MRBC_TT_OBJECT ) {
    mrbc_value new_obj = mrbc_instance_new(vm, v->instance->cls, 0);
    mrbc_kv_dup( &v->instance->ivar, &new_obj.instance->ivar );

    mrbc_decref( v );
    *v = new_obj;
    return;
  }


  // TODO: need support TT_PROC and TT_RANGE. but really need?
  return;
}


//================================================================
/*! (method) block_given?
 */
static void c_object_block_given(struct VM *vm, mrbc_value v[], int argc)
{
  mrbc_callinfo *callinfo = vm->callinfo_tail;
  if( !callinfo ) goto RETURN_FALSE;

  mrbc_value *regs = callinfo->cur_regs + callinfo->reg_offset;

  if( mrbc_type(regs[0]) == MRBC_TT_PROC ) {
    callinfo = regs[0].proc->callinfo_self;
    if( !callinfo ) goto RETURN_FALSE;

    regs = callinfo->cur_regs + callinfo->reg_offset;
  }

  SET_BOOL_RETURN( mrbc_type(regs[callinfo->n_args]) == MRBC_TT_PROC );
  return;

 RETURN_FALSE:
  SET_FALSE_RETURN();
}


//================================================================
/*! (method) is_a, kind_of
 */
static void c_object_kind_of(struct VM *vm, mrbc_value v[], int argc)
{
  int result = 0;
  if( mrbc_type(v[1]) != MRBC_TT_CLASS ) goto DONE;	// TypeError. raise?

  result = mrbc_obj_is_kind_of( &v[0], v[1].cls );

 DONE:
  SET_BOOL_RETURN( result );
}


//================================================================
/*! (method) nil?
 */
static void c_object_nil(struct VM *vm, mrbc_value v[], int argc)
{
  SET_BOOL_RETURN( mrbc_type(v[0]) == MRBC_TT_NIL );
}


//================================================================
/*! (method) p
 */
static void c_object_p(struct VM *vm, mrbc_value v[], int argc)
{
  int i;
  for( i = 1; i <= argc; i++ ) {
    mrbc_p_sub( &v[i] );
    mrbc_putchar('\n');
  }

  if (argc == 0) {
    SET_NIL_RETURN();
  } else if (argc == 1) {
    mrbc_incref( &v[1] );
    SET_RETURN(v[1]);
  } else {
    mrbc_value value = mrbc_array_new(vm, argc);
    if( value.array == NULL ) {
      SET_NIL_RETURN();  // ENOMEM
    } else {
      for ( i = 1; i <= argc; i++ ) {
        mrbc_incref( &v[i] );
        value.array->data[i-1] = v[i];
      }
      value.array->n_stored = argc;
      SET_RETURN(value);
    }
  }
}


//================================================================
/*! (method) print
 */
static void c_object_print(struct VM *vm, mrbc_value v[], int argc)
{
  int i;
  for( i = 1; i <= argc; i++ ) {
    mrbc_print_sub( &v[i] );
  }
}


//================================================================
/*! (method) puts
 */
static void c_object_puts(struct VM *vm, mrbc_value v[], int argc)
{
  int i;
  if( argc ){
    for( i = 1; i <= argc; i++ ) {
      if( mrbc_puts_sub( &v[i] ) == 0 ) mrbc_putchar('\n');
    }
  } else {
    mrbc_putchar('\n');
  }
  SET_NIL_RETURN();
}


//================================================================
/*! (method) raise

  case 1. raise
  case 2. raise "message"
  case 3. raise Exception
  case 4. raise Exception, "message"
*/
static void c_object_raise(struct VM *vm, mrbc_value v[], int argc)
{
  assert( !mrbc_israised(vm) );

  // case 1. raise (no argument)
  if( argc == 0 ) {
    vm->exception = mrbc_exception_new( vm,
					MRBC_CLASS(RuntimeError),
					"",
					0 );
  } else

  // case 2. raise "message"
  if( argc == 1 && mrbc_type(v[1]) == MRBC_TT_STRING ) {
    vm->exception = mrbc_exception_new( vm,
					MRBC_CLASS(RuntimeError),
					mrbc_string_cstr(&v[1]),
					mrbc_string_size(&v[1]) );
  } else

  // case 3-1. raise ExceptionClass
  if( argc == 1 && mrbc_type(v[1]) == MRBC_TT_CLASS ) {
    vm->exception = mrbc_exception_new( vm,
					v[1].cls,
					NULL,
					0 );
  } else

  // case 3-2. raise ExceptionObject
  if( argc == 1 && mrbc_type(v[1]) == MRBC_TT_EXCEPTION ) {
    mrbc_incref( &v[1] );
    vm->exception = v[1];
  } else

  // case 4-1. raise Exception, "param"
  if( argc == 2 && mrbc_type(v[1]) == MRBC_TT_CLASS
                && mrbc_type(v[2]) == MRBC_TT_STRING ) {
    vm->exception = mrbc_exception_new( vm,
					v[1].cls,
					mrbc_string_cstr(&v[2]),
					mrbc_string_size(&v[2]) );
  } else

  // case 4-2. raise ExceptionObject, "param"
  if( argc == 2 && mrbc_type(v[1]) == MRBC_TT_EXCEPTION
                && mrbc_type(v[2]) == MRBC_TT_STRING ) {
    mrbc_incref( &v[1] );
    vm->exception = v[1];
    mrbc_exception_set_message( vm, &vm->exception,
				mrbc_string_cstr(&v[2]),
				mrbc_string_size(&v[2]) );
  } else {

    // fail.
    vm->exception = mrbc_exception_new( vm, MRBC_CLASS(ArgumentError), NULL, 0 );
  }

  vm->flag_preemption = 2;
}


#ifdef MRBC_DEBUG
//================================================================
/*! (method - debug) object_id
 */
static void c_object_object_id(struct VM *vm, mrbc_value v[], int argc)
{
  // tiny implementation.
  SET_INT_RETURN( mrbc_integer(v[0]) );
}


//================================================================
/*! (method - debug) instance_methods
 */
static void c_object_instance_methods(struct VM *vm, mrbc_value v[], int argc)
{
  // TODO: check argument.

  // temporary code for operation check.
  mrbc_printf("[");
  int flag_first = 1;

  mrbc_class *cls = find_class_by_object( v );
  mrbc_method *method = cls->method_link;
  while( method ) {
    mrbc_printf("%s:%s", (flag_first ? "" : ", "),
		symid_to_str(method->sym_id) );
    flag_first = 0;
    method = method->next;
  }

  mrbc_printf("]");

  SET_NIL_RETURN();
}


//================================================================
/*! (method - debug) instance_variables
 */
static void c_object_instance_variables(struct VM *vm, mrbc_value v[], int argc)
{
  // temporary code for operation check.
#if 1
  mrbc_kv_handle *kvh = &v[0].instance->ivar;

  mrbc_printf("n = %d/%d ", kvh->n_stored, kvh->data_size);
  mrbc_printf("[");

  int i;
  for( i = 0; i < kvh->n_stored; i++ ) {
    mrbc_printf("%s:@%s", (i == 0 ? "" : ", "),
		symid_to_str( kvh->data[i].sym_id ));
  }

  mrbc_printf("]\n");
#endif
  SET_NIL_RETURN();
}


//================================================================
/*! (method - debug) memory_statistics
 */
#if !defined(MRBC_ALLOC_LIBC)
static void c_object_memory_statistics(struct VM *vm, mrbc_value v[], int argc)
{
  int total, used, free, frag;
  mrbc_alloc_statistics(&total, &used, &free, &frag);

  mrbc_printf("Memory Statistics\n");
  mrbc_printf("  Total: %d\n", total);
  mrbc_printf("  Used : %d\n", used);
  mrbc_printf("  Free : %d\n", free);
  mrbc_printf("  Frag.: %d\n", frag);

  SET_NIL_RETURN();
}
#endif  // MRBC_ALLOC_LIBC)
#endif  // MRBC_DEBUG


//================================================================
/*! (method) instance variable getter used by attr_reader.
 */
static void c_object_getiv(struct VM *vm, mrbc_value v[], int argc)
{
  mrbc_sym sym_id = mrbc_get_callee_symid(vm);
  mrbc_value ret = mrbc_instance_getiv(&v[0], sym_id);

  SET_RETURN(ret);
}


//================================================================
/*! (method) instance variable setter used by attr_accessor.
 */
static void c_object_setiv(struct VM *vm, mrbc_value v[], int argc)
{
  const char *name = mrbc_get_callee_name(vm);
  int len = strlen(name);
  char *namebuf = mrbc_alloc(vm, len);
  if( !namebuf ) return;

  memcpy( namebuf, name, len-1 );
  namebuf[len-1] = '\0';	// delete '='
  mrbc_sym sym_id = str_to_symid(namebuf);

  mrbc_instance_setiv(&v[0], sym_id, &v[1]);
  mrbc_raw_free(namebuf);
}


//================================================================
/*! (class method) access method 'attr_reader'
 */
static void c_object_attr_reader(struct VM *vm, mrbc_value v[], int argc)
{
  int i;
  for( i = 1; i <= argc; i++ ) {
    if( mrbc_type(v[i]) != MRBC_TT_SYMBOL ) continue;	// TypeError raise?

    // define reader method
    const char *name = mrbc_symbol_cstr(&v[i]);
    mrbc_define_method(vm, v[0].cls, name, c_object_getiv);
  }
}


//================================================================
/*! (class method) access method 'attr_accessor'
 */
static void c_object_attr_accessor(struct VM *vm, mrbc_value v[], int argc)
{
  int i;
  for( i = 1; i <= argc; i++ ) {
    if( mrbc_type(v[i]) != MRBC_TT_SYMBOL ) continue;	// TypeError raise?

    // define reader method
    const char *name = mrbc_symbol_cstr(&v[i]);
    mrbc_define_method(vm, v[0].cls, name, c_object_getiv);

    // make string "....=" and define writer method.
    char *namebuf = mrbc_alloc(vm, strlen(name)+2);
    if( !namebuf ) return;
    strcpy(namebuf, name);
    strcat(namebuf, "=");
    mrbc_symbol_new(vm, namebuf);
    mrbc_define_method(vm, v[0].cls, namebuf, c_object_setiv);
    mrbc_free(vm, namebuf);
  }
}


#if MRBC_USE_STRING
//================================================================
/*! (method) sprintf
*/
static void c_object_sprintf(struct VM *vm, mrbc_value v[], int argc)
{
  static const int BUF_INC_STEP = 32;	// bytes.

  mrbc_value *format = &v[1];
  if( mrbc_type(*format) != MRBC_TT_STRING ) {
    mrbc_printf("TypeError\n");		// raise?
    return;
  }

  int buflen = BUF_INC_STEP;
  char *buf = mrbc_alloc(vm, buflen);
  if( !buf ) { return; }	// ENOMEM raise?

  mrbc_printf_t pf;
  mrbc_printf_init( &pf, buf, buflen, mrbc_string_cstr(format) );

  int i = 2;
  int ret;
  while( 1 ) {
    mrbc_printf_t pf_bak = pf;
    ret = mrbc_printf_main( &pf );
    if( ret == 0 ) break;	// normal break loop.
    if( ret < 0 ) goto INCREASE_BUFFER;

    if( i > argc ) {mrbc_print("ArgumentError\n"); break;}	// raise?

    // maybe ret == 1
    switch(pf.fmt.type) {
    case 'c':
      if( mrbc_type(v[i]) == MRBC_TT_INTEGER ) {
	ret = mrbc_printf_char( &pf, v[i].i );
      } else if( mrbc_type(v[i]) == MRBC_TT_STRING ) {
	ret = mrbc_printf_char( &pf, mrbc_string_cstr(&v[i])[0] );
      }
      break;

    case 's':
      if( mrbc_type(v[i]) == MRBC_TT_STRING ) {
	ret = mrbc_printf_bstr( &pf, mrbc_string_cstr(&v[i]), mrbc_string_size(&v[i]),' ');
      } else if( mrbc_type(v[i]) == MRBC_TT_SYMBOL ) {
	ret = mrbc_printf_str( &pf, mrbc_symbol_cstr( &v[i] ), ' ');
      }
      break;

    case 'd':
    case 'i':
    case 'u':
      if( mrbc_type(v[i]) == MRBC_TT_INTEGER ) {
	ret = mrbc_printf_int( &pf, v[i].i, 10);
#if MRBC_USE_FLOAT
      } else if( mrbc_type(v[i]) == MRBC_TT_FLOAT ) {
	ret = mrbc_printf_int( &pf, (mrbc_int)v[i].d, 10);
#endif
      } else if( mrbc_type(v[i]) == MRBC_TT_STRING ) {
	mrbc_int ival = atol(mrbc_string_cstr(&v[i]));
	ret = mrbc_printf_int( &pf, ival, 10 );
      }
      break;

    case 'b':
    case 'B':
      if( mrbc_type(v[i]) == MRBC_TT_INTEGER ) {
	ret = mrbc_printf_bit( &pf, v[i].i, 1);
      }
      break;

    case 'x':
    case 'X':
      if( mrbc_type(v[i]) == MRBC_TT_INTEGER ) {
	ret = mrbc_printf_bit( &pf, v[i].i, 4);
      }
      break;

    case 'o':
      if( mrbc_type(v[i]) == MRBC_TT_INTEGER ) {
	ret = mrbc_printf_bit( &pf, v[i].i, 3);
      }
      break;

#if MRBC_USE_FLOAT
    case 'f':
    case 'e':
    case 'E':
    case 'g':
    case 'G':
      if( mrbc_type(v[i]) == MRBC_TT_FLOAT ) {
	ret = mrbc_printf_float( &pf, v[i].d );
      } else if( mrbc_type(v[i]) == MRBC_TT_INTEGER ) {
	ret = mrbc_printf_float( &pf, v[i].i );
      }
      break;
#endif

    default:
      break;
    }
    if( ret >= 0 ) {
      i++;
      continue;		// normal next loop.
    }

    // maybe buffer full. (ret == -1)
    if( pf.fmt.width > BUF_INC_STEP ) buflen += pf.fmt.width;
    pf = pf_bak;

  INCREASE_BUFFER:
    buflen += BUF_INC_STEP;
    buf = mrbc_realloc(vm, pf.buf, buflen);
    if( !buf ) { return; }	// ENOMEM raise? TODO: leak memory.
    mrbc_printf_replace_buffer(&pf, buf, buflen);
  }
  mrbc_printf_end( &pf );

  buflen = mrbc_printf_len( &pf );
  mrbc_realloc(vm, pf.buf, buflen+1);	// shrink suitable size.

  mrbc_value value = mrbc_string_new_alloc( vm, pf.buf, buflen );

  SET_RETURN(value);
}


//================================================================
/*! (method) printf
*/
static void c_object_printf(struct VM *vm, mrbc_value v[], int argc)
{
  c_object_sprintf(vm, v, argc);
  mrbc_nprint( mrbc_string_cstr(v), mrbc_string_size(v) );
  SET_NIL_RETURN();
}


//================================================================
/*! (method) to_s
 */
static void c_object_to_s(struct VM *vm, mrbc_value v[], int argc)
{
  char buf[32];
  const char *s = buf;

  switch( mrbc_type(v[0]) ) {
  case MRBC_TT_CLASS:
    s = symid_to_str( v->cls->sym_id );
    break;

  case MRBC_TT_OBJECT:{
    // (NOTE) address part assumes 32bit. but enough for this.
    mrbc_printf_t pf;

    mrbc_printf_init( &pf, buf, sizeof(buf), "#<%s:%08x>" );
    while( mrbc_printf_main( &pf ) > 0 ) {
      switch(pf.fmt.type) {
      case 's':
	mrbc_printf_str( &pf, symid_to_str(v->instance->cls->sym_id), ' ' );
	break;
      case 'x':
#if defined(UINTPTR_MAX)
	mrbc_printf_int( &pf, (uint32_t)(uintptr_t)v->instance, 16 );
#else
	mrbc_printf_int( &pf, (uint32_t)v->instance, 16 );
#endif
	break;
      }
    }
    mrbc_printf_end( &pf );
  } break;

  default:
    s = "";
    break;
  }

  SET_RETURN( mrbc_string_new_cstr( vm, s ) );
}
#endif  // MRBC_USE_STRING


/* MRBC_AUTOGEN_METHOD_TABLE

  CLASS("Object")
  FILE("method_table_object.h")
  SUPER(0)

  METHOD( "new",	c_object_new )
  METHOD( "!",		c_object_not )
  METHOD( "!=",		c_object_neq )
  METHOD( "<=>",	c_object_compare )
  METHOD( "===",	c_object_equal3 )
  METHOD( "class",	c_object_class )
  METHOD( "dup",	c_object_dup )
  METHOD( "block_given?", c_object_block_given )
  METHOD( "is_a?",	c_object_kind_of )
  METHOD( "kind_of?",	c_object_kind_of )
  METHOD( "nil?",	c_object_nil )
  METHOD( "p",		c_object_p )
  METHOD( "print",	c_object_print )
  METHOD( "puts",	c_object_puts )
  METHOD( "raise",	c_object_raise )
  METHOD( "attr_reader",c_object_attr_reader )
  METHOD( "attr_accessor", c_object_attr_accessor )

#if MRBC_USE_STRING
  METHOD( "sprintf",	c_object_sprintf )
  METHOD( "printf",	c_object_printf )
  METHOD( "inspect",	c_object_to_s )
  METHOD( "to_s",	c_object_to_s )
#endif

#if defined(MRBC_DEBUG)
  METHOD( "object_id",		c_object_object_id )
  METHOD( "instance_methods",	c_object_instance_methods )
  METHOD( "instance_variables",	c_object_instance_variables )
#if !defined(MRBC_ALLOC_LIBC)
  METHOD( "memory_statistics",	c_object_memory_statistics )
#endif
#endif
*/
#include "method_table_object.h"



//----------------------------------------------------------------
// Proc class
//----------------------------------------------------------------
//================================================================
/*! (method) new
*/
static void c_proc_new(struct VM *vm, mrbc_value v[], int argc)
{
  if( mrbc_type(v[1]) != MRBC_TT_PROC ) {
    mrbc_printf("Not support Proc.new without block.\n");	// raise?
    return;
  }

  v[0] = v[1];
  v[1].tt = MRBC_TT_EMPTY;
}


//================================================================
/*! (method) call
*/
void c_proc_call(struct VM *vm, mrbc_value v[], int argc)
{
  assert( mrbc_type(v[0]) == MRBC_TT_PROC );

  mrbc_callinfo *callinfo_self = v[0].proc->callinfo_self;
  mrbc_callinfo *callinfo = mrbc_push_callinfo(vm,
				(callinfo_self ? callinfo_self->method_id : 0),
				v - vm->cur_regs, argc);
  if( !callinfo ) return;

  if( callinfo_self ) {
    callinfo->own_class = callinfo_self->own_class;
  }

  // target irep
  vm->cur_irep = v[0].proc->irep;
  vm->inst = vm->cur_irep->inst;

  vm->cur_regs = v;
}


#if MRBC_USE_STRING
//================================================================
/*! (method) to_s
*/
static void c_proc_to_s(struct VM *vm, mrbc_value v[], int argc)
{
  // (NOTE) address part assumes 32bit. but enough for this.
  char buf[32];
  mrbc_printf_t pf;

  mrbc_printf_init( &pf, buf, sizeof(buf), "#<Proc:%08x>" );
  while( mrbc_printf_main( &pf ) > 0 ) {
#if defined(UINTPTR_MAX)
    mrbc_printf_int( &pf, (uint32_t)(uintptr_t)v->proc, 16 );
#else
    mrbc_printf_int( &pf, (uint32_t)v->proc, 16 );
#endif
  }
  mrbc_printf_end( &pf );

  SET_RETURN( mrbc_string_new_cstr( vm, buf ) );
}
#endif // MRBC_USE_STRING


/* MRBC_AUTOGEN_METHOD_TABLE

  CLASS("Proc")
  FILE("method_table_proc.h")

  METHOD( "new",	c_proc_new )
  METHOD( "call",	c_proc_call )

#if MRBC_USE_STRING
  METHOD( "inspect",	c_proc_to_s )
  METHOD( "to_s",	c_proc_to_s )
#endif
*/
#include "method_table_proc.h"



//----------------------------------------------------------------
// Nil class
//----------------------------------------------------------------
//================================================================
/*! (method) to_i
*/
static void c_nil_to_i(struct VM *vm, mrbc_value v[], int argc)
{
  v[0] = mrbc_integer_value(0);
}


//================================================================
/*! (method) to_a
*/
static void c_nil_to_a(struct VM *vm, mrbc_value v[], int argc)
{
  v[0] = mrbc_array_new(vm, 0);
}


//================================================================
/*! (method) to_h
*/
static void c_nil_to_h(struct VM *vm, mrbc_value v[], int argc)
{
  v[0] = mrbc_hash_new(vm, 0);
}


#if MRBC_USE_FLOAT
//================================================================
/*! (method) to_f
*/
static void c_nil_to_f(struct VM *vm, mrbc_value v[], int argc)
{
  v[0] = mrbc_float_value(vm,0);
}
#endif


#if MRBC_USE_STRING
//================================================================
/*! (method) inspect
*/
static void c_nil_inspect(struct VM *vm, mrbc_value v[], int argc)
{
  v[0] = mrbc_string_new_cstr(vm, "nil");
}


//================================================================
/*! (method) to_s
*/
static void c_nil_to_s(struct VM *vm, mrbc_value v[], int argc)
{
  v[0] = mrbc_string_new(vm, NULL, 0);
}
#endif  // MRBC_USE_STRING


/* MRBC_AUTOGEN_METHOD_TABLE

  CLASS("NilClass")
  FILE("method_table_nil.h")

  METHOD( "to_i",	c_nil_to_i )
  METHOD( "to_a",	c_nil_to_a )
  METHOD( "to_h",	c_nil_to_h )

#if MRBC_USE_FLOAT
  METHOD( "to_f",	c_nil_to_f )
#endif

#if MRBC_USE_STRING
  METHOD( "inspect",	c_nil_inspect )
  METHOD( "to_s",	c_nil_to_s )
#endif
*/
#include "method_table_nil.h"



//----------------------------------------------------------------
// True class
//----------------------------------------------------------------

#if MRBC_USE_STRING
//================================================================
/*! (method) to_s
*/
static void c_true_to_s(struct VM *vm, mrbc_value v[], int argc)
{
  v[0] = mrbc_string_new_cstr(vm, "true");
}
#endif


/* MRBC_AUTOGEN_METHOD_TABLE

  CLASS("TrueClass")
  FILE("method_table_true.h")

#if MRBC_USE_STRING
  METHOD( "inspect",	c_true_to_s )
  METHOD( "to_s",	c_true_to_s )
#endif
*/
#include "method_table_true.h"



//----------------------------------------------------------------
// False class
//----------------------------------------------------------------

#if MRBC_USE_STRING
//================================================================
/*! (method) False#to_s
*/
static void c_false_to_s(struct VM *vm, mrbc_value v[], int argc)
{
  v[0] = mrbc_string_new_cstr(vm, "false");
}
#endif  // MRBC_USE_STRING


/* MRBC_AUTOGEN_METHOD_TABLE

  CLASS("FalseClass")
  FILE("method_table_false.h")

#if MRBC_USE_STRING
  METHOD( "inspect",	c_false_to_s )
  METHOD( "to_s",	c_false_to_s )
#endif
*/
#include "method_table_false.h"
