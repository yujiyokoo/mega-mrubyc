/*! @file
  @brief
  Constant and global variables.

  <pre>
  Copyright (C) 2015-2018 Kyushu Institute of Technology.
  Copyright (C) 2015-2018 Shimane IT Open-Innovation Center.

  This file is distributed under BSD 3-Clause License.

  </pre>
*/

#include "vm_config.h"
#include "value.h"
#include "global.h"
#include "keyvalue.h"
#include "console.h"


static mrbc_kv_handle handle_const;	//!< for global(Object) constants.
static mrbc_kv_handle handle_global;	//!< for global variables.


//================================================================
/*! initialize const and global table with default value.
*/
void mrbc_init_global(void)
{
  mrbc_kv_init_handle( 0, &handle_const, 15 );
  mrbc_kv_init_handle( 0, &handle_global, 0 );
}


//================================================================
/*! setter constant

  @param  sym_id	symbol ID.
  @param  v		pointer to mrbc_value.
  @return		mrbc_error_code.
*/
int mrbc_set_const( mrbc_sym sym_id, mrbc_value *v )
{
  mrb_value *already = mrbc_kv_get( &handle_const, sym_id );
  if( already != NULL ) {
    console_printf( "warning: already initialized constant.\n" );
    mrbc_release( already );
  }

  return mrbc_kv_set( &handle_const, sym_id, v );
}


//================================================================
/*! getter constant

  @param  sym_id	symbol ID.
  @return		pointer to mrbc_value or NULL.
*/
mrbc_value * mrbc_get_const( mrbc_sym sym_id )
{
  return mrbc_kv_get( &handle_const, sym_id );
}


//================================================================
/*! setter global variable.

  @param  sym_id	symbol ID.
  @param  v		pointer to mrbc_value.
  @return		mrbc_error_code.
*/
int mrbc_set_global( mrbc_sym sym_id, mrbc_value *v )
{
  return mrbc_kv_set( &handle_global, sym_id, v );
}


//================================================================
/*! getter global variable.

  @param  sym_id	symbol ID.
  @return		pointer to mrbc_value or NULL.
*/
mrbc_value * mrbc_get_global( mrbc_sym sym_id )
{
  return mrbc_kv_get( &handle_global, sym_id );
}


//================================================================
/*! clear vm_id in global object for process terminated.
*/
void mrbc_global_clear_vm_id(void)
{
  int i;
  mrbc_kv *p;

  p = handle_const.data;
  for( i = 0; i < mrbc_kv_size(&handle_const); i++, p++ ) {
    mrbc_clear_vm_id( &p->value );
  }

  p = handle_global.data;
  for( i = 0; i < mrbc_kv_size(&handle_global); i++, p++ ) {
    mrbc_clear_vm_id( &p->value );
  }
}


#ifdef MRBC_DEBUG
#include "class.h"
#include "symbol.h"

//================================================================
/*! clear vm_id in global object for process terminated.
*/
void mrbc_global_debug_dump(void)
{
  console_print("<< Const table dump. >>\n(s_id:identifier = value)\n");
  mrbc_kv_iterator ite = mrbc_kv_iterator_new( &handle_const );
  while( mrbc_kv_i_has_next( &ite ) ) {
    mrbc_kv *kv = mrbc_kv_i_next( &ite );

    console_printf(" %04x:%s = ", kv->sym_id, symid_to_str(kv->sym_id));
    mrbc_p_sub( &kv->value );
    console_printf(" .tt=%d\n", kv->value.tt);
  }

  console_print("<< Global table dump. >>\n(s_id:identifier = value)\n");
  ite = mrbc_kv_iterator_new( &handle_global );
  while( mrbc_kv_i_has_next( &ite ) ) {
    mrbc_kv *kv = mrbc_kv_i_next( &ite );

    console_printf(" %04x:%s = ", kv->sym_id, symid_to_str(kv->sym_id));
    mrbc_p_sub( &kv->value );
    console_printf(" .tt=%d\n", kv->value.tt);
  }
}

#endif
