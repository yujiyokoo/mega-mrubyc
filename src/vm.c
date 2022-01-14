/*! @file
  @brief
  mruby bytecode executor.

  <pre>
  Copyright (C) 2015-2021 Kyushu Institute of Technology.
  Copyright (C) 2015-2021 Shimane IT Open-Innovation Center.

  This file is distributed under BSD 3-Clause License.

  Fetch mruby VM bytecodes, decode and execute.

  </pre>
*/
#include "vm_config.h"
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include "vm.h"
#include "alloc.h"
#include "load.h"
#include "global.h"
#include "opcode.h"
#include "class.h"
#include "symbol.h"
#include "console.h"
#include "error.h"

#include "c_object.h"
#include "c_string.h"
#include "c_range.h"
#include "c_array.h"
#include "c_hash.h"


static uint16_t free_vm_bitmap[MAX_VM_COUNT / 16 + 1];

#define CALL_MAXARGS 255


//================================================================
/*! display "not supported" message
*/
static void not_supported(void)
{
  mrbc_printf("Not supported!\n");
}


//================================================================
/*! Method call by method name's id

  @param  vm		pointer to VM.
  @param  sym_id	method name symbol id
  @param  regs		pointer to regs
  @param  a		operand a
  @param  c		operand c
  @param  is_sendb	Is called from OP_SENDB?
  @retval 0  No error.
*/
static int send_by_name( struct VM *vm, mrbc_sym sym_id, mrbc_value *regs, int a, int c, int is_sendb )
{
  mrbc_value *recv = &regs[a];

  // if SENDV or SENDVB, params are in one Array
  int flag_array_arg = ( c == CALL_MAXARGS );
  if( flag_array_arg ) c = 1;

  // if not OP_SENDB, blcok does not exist
  int bidx = a + c + 1;
  if( !is_sendb ) {
    mrbc_decref( &regs[bidx] );
    regs[bidx].tt = MRBC_TT_NIL;
  }

  mrbc_class *cls = find_class_by_object(recv);
  mrbc_method method;

  if( mrbc_find_method( &method, cls, sym_id ) == 0 ) {
    mrbc_printf("Undefined local variable or method '%s' for %s\n",
		symid_to_str(sym_id), symid_to_str( cls->sym_id ));
    return 1;
  }

  // call C method.
  if( method.c_func ) {
    method.func(vm, regs + a, c);
    if( method.func == c_proc_call ) return 0;

    int release_reg = a+1;
    while( release_reg <= bidx ) {
      mrbc_decref_empty(&regs[release_reg]);
      release_reg++;
    }
    return 0;
  }

  // call Ruby method.
  if( flag_array_arg ) c = CALL_MAXARGS;
  mrbc_callinfo *callinfo = mrbc_push_callinfo(vm, sym_id, a, c);
  callinfo->own_class = method.cls;

  // target irep
  vm->cur_irep = method.irep;
  vm->inst = method.irep->inst;

  // new regs
  vm->cur_regs += a;

  return 0;
}


//================================================================
/*! cleanup
*/
void mrbc_cleanup_vm(void)
{
  memset(free_vm_bitmap, 0, sizeof(free_vm_bitmap));
}


//================================================================
/*! get callee symbol id

  @param  vm	Pointer to VM
  @return	string
*/
mrbc_sym mrbc_get_callee_symid( struct VM *vm )
{
  uint8_t rb = vm->inst[-2];
  /* NOTE
     -2 is not always better value.
     This value is OP_SEND operator's B register.
  */
  return mrbc_irep_symbol_id(vm->cur_irep, rb);
}


//================================================================
/*! get callee name

  @param  vm	Pointer to VM
  @return	string
*/
const char *mrbc_get_callee_name( struct VM *vm )
{
  uint8_t rb = vm->inst[-2];
  return mrbc_irep_symbol_cstr(vm->cur_irep, rb);
}


//================================================================
/*! Push current status to callinfo stack
*/
mrbc_callinfo * mrbc_push_callinfo( struct VM *vm, mrbc_sym method_id, int reg_offset, int n_args )
{
  mrbc_callinfo *callinfo = mrbc_alloc(vm, sizeof(mrbc_callinfo));
  if( !callinfo ) return callinfo;

  callinfo->cur_irep = vm->cur_irep;
  callinfo->inst = vm->inst;
  callinfo->cur_regs = vm->cur_regs;
  callinfo->target_class = vm->target_class;
  callinfo->own_class = 0;
  callinfo->method_id = method_id;
  callinfo->reg_offset = reg_offset;
  callinfo->n_args = n_args;

  callinfo->prev = vm->callinfo_tail;
  vm->callinfo_tail = callinfo;

  return callinfo;
}


//================================================================
/*! Pop current status from callinfo stack
*/
void mrbc_pop_callinfo( struct VM *vm )
{

  assert( vm->callinfo_tail );

  // clear used register.
  int i;
  for( i = 1; i < vm->cur_irep->nregs; i++ ) {
    mrbc_decref_empty( vm->cur_regs + i );
  }

  // copy callinfo to vm
  mrbc_callinfo *callinfo = vm->callinfo_tail;
  vm->cur_irep = callinfo->cur_irep;
  vm->inst = callinfo->inst;
  vm->cur_regs = callinfo->cur_regs;
  vm->target_class = callinfo->target_class;
  vm->callinfo_tail = callinfo->prev;

  mrbc_free(vm, callinfo);
}


// filters for "catch_handler_find"
#define MRBC_CATCH_FILTER_RESCUE (0x01 << 0)
#define MRBC_CATCH_FILTER_ENSURE (0x01 << 1)
#define MRBC_CATCH_FILTER_ALL (MRBC_CATCH_FILTER_ENSURE | MRBC_CATCH_FILTER_RESCUE)


//================================================================
/*! Find exception, catch handler

*/
static const mrbc_irep_catch_handler *catch_handler_find(mrbc_vm *vm, const mrbc_callinfo *callinfo, int filter )
{
  const mrbc_irep *irep;
  uint32_t inst;

  if( callinfo ) {
    irep = callinfo->cur_irep;
    inst = callinfo->inst - irep->inst;
  } else {
    irep = vm->cur_irep;
    inst = vm->inst - irep->inst;
  }

  const mrbc_irep_catch_handler *catch_table =
    (const mrbc_irep_catch_handler *)(irep->inst + irep->ilen);
  int cnt = irep->clen - 1;

  for( ; cnt >= 0 ; cnt-- ) {
    const mrbc_irep_catch_handler *handler = catch_table + cnt;
    // Catch type and range check
    if( (filter & (1 << handler->type)) &&
	(bin_to_uint32(handler->begin) < inst) &&
	(inst <= bin_to_uint32(handler->end)) ) {
      return handler;
    }
  }

  return NULL;
}


//================================================================
/*! get the self object
*/
static mrbc_value * mrbc_get_self( struct VM *vm, mrbc_value *regs )
{
  mrbc_value *self = &regs[0];
  if( self->tt == MRBC_TT_PROC ) {
    mrbc_callinfo *callinfo = regs[0].proc->callinfo_self;
    if( callinfo ) {
      self = callinfo->cur_regs + callinfo->reg_offset;
    } else {
      self = &vm->regs[0];
    }
    assert( self->tt != MRBC_TT_PROC );
  }

  return self;
}


//================================================================
/*! OP_NOP

  No operation

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_nop( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_Z();
  return 0;
}


//================================================================
/*! OP_MOVE

  R(a) = R(b)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_move( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BB();

  mrbc_incref(&regs[b]);
  mrbc_decref(&regs[a]);
  regs[a] = regs[b];

  return 0;
}


//================================================================
/*! OP_LOADL

  R(a) = Pool(b)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_loadl( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BB();

  mrbc_decref(&regs[a]);
  regs[a] = mrbc_irep_pool_value(vm, b);

  return 0;
}


//================================================================
/*! OP_LOADL16

  R(a) = Pool(b)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_loadl16( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BS();

  mrbc_decref(&regs[a]);
  regs[a] = mrbc_irep_pool_value(vm, b);

  return 0;
}


//================================================================
/*! OP_LOADI

  R(a) = mrb_int(b)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_loadi( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BB();

  mrbc_decref(&regs[a]);
  mrbc_set_integer(&regs[a], b);

  return 0;
}


//================================================================
/*! OP_LOADINEG

  R(a) = mrb_int(-b)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_loadineg( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BB();

  mrbc_decref(&regs[a]);
  mrbc_set_integer(&regs[a], -b);

  return 0;
}


//================================================================
/*! OP_LOADI_n (n=-1,0,1..7)

  R(a) = mrb_int(n)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error and exit from vm.
*/
static inline int op_loadi_n( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_B();

  // get n
  int opcode = vm->inst[-2];
  int n = opcode - OP_LOADI_0;

  mrbc_decref(&regs[a]);
  mrbc_set_integer(&regs[a], n);

  return 0;
}


//================================================================
/*! OP_LOADI16

  R(a) = mrb_int(b)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error and exit from vm.
*/
static inline int op_loadi16( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BS();

  mrbc_decref(&regs[a]);
  int16_t signed_b = (int16_t)b;
  mrbc_set_integer(&regs[a], signed_b);

  return 0;
}


//================================================================
/*! OP_LOADI32

  R(a) = mrb_int((b<<16)+c)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error and exit from vm.
*/
static inline int op_loadi32( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BSS();

  mrbc_decref(&regs[a]);
  mrbc_set_integer(&regs[a], (((int32_t)b<<16)+c));

  return 0;
}


//================================================================
/*! OP_LOADSYM

  R(a) = Syms(b)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_loadsym( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BB();

  mrbc_decref(&regs[a]);
  mrbc_set_symbol(&regs[a], mrbc_irep_symbol_id(vm->cur_irep, b));

  return 0;
}


//================================================================
/*! OP_LOADSYM16

  R(a) = Syms(b)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_loadsym16( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BS();

  mrbc_decref(&regs[a]);
  mrbc_set_symbol(&regs[a], mrbc_irep_symbol_id(vm->cur_irep, b));

  return 0;
}


//================================================================
/*! OP_LOADNIL

  R(a) = nil

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_loadnil( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_B();

  mrbc_decref(&regs[a]);
  mrbc_set_nil(&regs[a]);

  return 0;
}


//================================================================
/*! OP_LOADSELF

  R(a) = self

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_loadself( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_B();

  mrbc_value *self = mrbc_get_self( vm, regs );

  mrbc_incref(self);
  mrbc_decref(&regs[a]);
  regs[a] = *self;

  return 0;
}


//================================================================
/*! OP_LOADT

  R(a) = true

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_loadt( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_B();

  mrbc_decref(&regs[a]);
  mrbc_set_true(&regs[a]);

  return 0;
}


//================================================================
/*! OP_LOADF

  R(a) = false

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_loadf( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_B();

  mrbc_decref(&regs[a]);
  mrbc_set_false(&regs[a]);

  return 0;
}


//================================================================
/*! OP_GETGV

  R(a) = getglobal(Syms(b))

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_getgv( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BB();

  mrbc_decref(&regs[a]);
  mrbc_value *v = mrbc_get_global( mrbc_irep_symbol_id(vm->cur_irep, b) );
  if( v == NULL ) {
    mrbc_set_nil(&regs[a]);
  } else {
    mrbc_incref(v);
    regs[a] = *v;
  }

  return 0;
}


//================================================================
/*! OP_SETGV

  setglobal(Syms(b), R(a))

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_setgv( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BB();

  mrbc_incref(&regs[a]);
  mrbc_set_global( mrbc_irep_symbol_id(vm->cur_irep, b), &regs[a] );

  return 0;
}


//================================================================
/*! OP_GETIV

  R(a) = ivget(Syms(b))

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_getiv( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BB();

  const char *sym_name = mrbc_irep_symbol_cstr(vm->cur_irep, b);
  mrbc_sym sym_id = str_to_symid(sym_name+1);   // skip '@'
  mrbc_value *self = mrbc_get_self( vm, regs );

  mrbc_decref(&regs[a]);
  regs[a] = mrbc_instance_getiv(self, sym_id);

  return 0;
}


//================================================================
/*! OP_SETIV

  ivset(Syms(b),R(a))

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_setiv( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BB();

  const char *sym_name = mrbc_irep_symbol_cstr(vm->cur_irep, b);
  mrbc_sym sym_id = str_to_symid(sym_name+1);   // skip '@'
  mrbc_value *self = mrbc_get_self( vm, regs );

  mrbc_instance_setiv(self, sym_id, &regs[a]);

  return 0;
}


//================================================================
/*! OP_GETCONST

  R(a) = constget(Syms(b))

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_getconst( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BB();

  mrbc_sym sym_id = mrbc_irep_symbol_id(vm->cur_irep, b);
  mrbc_class *cls = NULL;
  mrbc_value *v;

  if( vm->callinfo_tail ) cls = vm->callinfo_tail->own_class;
  while( cls != NULL ) {
    v = mrbc_get_class_const(cls, sym_id);
    if( v != NULL ) goto DONE;
    cls = cls->super;
  }

  v = mrbc_get_const(sym_id);
  if( v == NULL ) {		// raise?
    mrbc_printf("NameError: uninitialized constant %s\n",
		symid_to_str(sym_id) );
    return 0;
  }

 DONE:
  mrbc_incref(v);
  mrbc_decref(&regs[a]);
  regs[a] = *v;

  return 0;
}


//================================================================
/*! OP_SETCONST

  constset(Syms(b),R(a))

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_setconst( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BB();

  mrbc_sym sym_id = mrbc_irep_symbol_id(vm->cur_irep, b);

  mrbc_incref(&regs[a]);
  if( mrbc_type(regs[0]) == MRBC_TT_CLASS ) {
    mrbc_set_class_const(regs[0].cls, sym_id, &regs[a]);
  } else {
    mrbc_set_const(sym_id, &regs[a]);
  }

  return 0;
}


//================================================================
/*! OP_GETMCNST

  R(a) = R(a)::Syms(b)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_getmcnst( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BB();

  mrbc_sym sym_id = mrbc_irep_symbol_id(vm->cur_irep, b);
  mrbc_class *cls = regs[a].cls;
  mrbc_value *v;

  while( !(v = mrbc_get_class_const(cls, sym_id)) ) {
    cls = cls->super;
    if( !cls ) {	// raise?
      mrbc_printf("NameError: uninitialized constant %s::%s\n",
		  symid_to_str( regs[a].cls->sym_id ), symid_to_str( sym_id ));
      return 0;
    }
  }

  mrbc_incref(v);
  mrbc_decref(&regs[a]);
  regs[a] = *v;

  return 0;
}


//================================================================
/*! OP_GETUPVAR

  R(a) = uvget(b,c)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_getupvar( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BBB();

  assert( regs[0].tt == MRBC_TT_PROC );
  mrbc_callinfo *callinfo = regs[0].proc->callinfo;

  int i;
  for( i = 0; i < c; i++ ) {
    assert( callinfo );
    mrbc_value *regs0 = callinfo->cur_regs + callinfo->reg_offset;
    assert( regs0->tt == MRBC_TT_PROC );
    callinfo = regs0->proc->callinfo;
  }

  mrbc_value *p_val;
  if( callinfo == 0 ) {
    p_val = vm->regs + b;
  } else {
    p_val = callinfo->cur_regs + callinfo->reg_offset + b;
  }
  mrbc_incref( p_val );

  mrbc_decref( &regs[a] );
  regs[a] = *p_val;

  return 0;
}


//================================================================
/*! OP_SETUPVAR

  uvset(b,c,R(a))

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_setupvar( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BBB();

  assert( regs[0].tt == MRBC_TT_PROC );
  mrbc_callinfo *callinfo = regs[0].proc->callinfo;

  int i;
  for( i = 0; i < c; i++ ) {
    assert( callinfo );
    mrbc_value *regs0 = callinfo->cur_regs + callinfo->reg_offset;
    assert( regs0->tt == MRBC_TT_PROC );
    callinfo = regs0->proc->callinfo;
  }

  mrbc_value *p_val;
  if( callinfo == 0 ) {
    p_val = vm->regs + b;
  } else {
    p_val = callinfo->cur_regs + callinfo->reg_offset + b;
  }
  mrbc_decref( p_val );

  mrbc_incref( &regs[a] );
  *p_val = regs[a];

  return 0;
}


//================================================================
/*! OP_JMP

  pc+=a

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_jmp( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_S();

  vm->inst += (int16_t)a;

  return 0;
}


//================================================================
/*! OP_JMPIF

  if R(a) pc+=b

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_jmpif( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BS();

  if( regs[a].tt > MRBC_TT_FALSE ) {
    vm->inst += (int16_t)b;
  }

  return 0;
}


//================================================================
/*! OP_JMPNOT

  if !R(a) pc+=b

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_jmpnot( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BS();

  if( regs[a].tt <= MRBC_TT_FALSE ) {
    vm->inst += (int16_t)b;
  }

  return 0;
}


//================================================================
/*! OP_JMPNIL

  if R(a)==nil pc+=b

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_jmpnil( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BS();

  if( regs[a].tt == MRBC_TT_NIL ) {
    vm->inst += (int16_t)b;
  }

  return 0;
}


//================================================================
/*! OP_JMPUW

  unwind_and_jump_to(a)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_jmpuw( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_S();

  // check catch handler (ensure)
  const mrbc_irep_catch_handler *handler = catch_handler_find(vm, 0, MRBC_CATCH_FILTER_ENSURE);
  if( !handler ) goto JUMP_TO_A_REG;

  // check whether the jump point is inside or outside the catch handler.
  uint32_t jump_point = vm->inst - vm->cur_irep->inst + (int16_t)a;
  if( (bin_to_uint32(handler->begin) < jump_point) &&
      (jump_point <= bin_to_uint32(handler->end)) ) goto JUMP_TO_A_REG;

  // jump point is outside, thus jump to ensure.
  assert( vm->exception.tt == MRBC_TT_NIL );
  vm->exception.tt = MRBC_TT_JMPUW;
  vm->exception.jmpuw = vm->inst + (int16_t)a;
  vm->inst = vm->cur_irep->inst + bin_to_uint32(handler->target);
  return 0;


 JUMP_TO_A_REG:
  vm->inst += (int16_t)a;
  return 0;
}


//================================================================
/*! OP_EXCEPT

  R(a) = exc

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_except( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_B();

  mrbc_decref( &regs[a] );
  regs[a] = vm->exception;
  mrbc_set_nil( &vm->exception );

  return 0;
}


//================================================================
/*! OP_RESCUE

  R(b) = R(a).isa?(R(b))

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_rescue( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BB();

  assert( regs[a].tt == MRBC_TT_EXCEPTION );
  assert( regs[b].tt == MRBC_TT_CLASS );

  int res = mrbc_obj_is_kind_of( &regs[a], regs[b].cls );
  mrbc_set_bool( &regs[b], res );

  return 0;
}


//================================================================
/*! OP_RAISEIF

  raise(R(a)) if R(a)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_raiseif( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_B();

  if( regs[a].tt == MRBC_TT_JMPUW ) {
    const mrbc_irep_catch_handler *handler =
      catch_handler_find(vm, 0, MRBC_CATCH_FILTER_ENSURE);
    if( !handler ) {
      vm->inst = regs[a].jmpuw;
      return 0;
    }

    uint32_t jump_point = regs[a].jmpuw - vm->cur_irep->inst;
    if( (bin_to_uint32(handler->begin) < jump_point) &&
	(jump_point <= bin_to_uint32(handler->end)) ) {
      vm->inst = regs[a].jmpuw;
      return 0;
    }

    mrbc_incref( &regs[a] );
    vm->exception = regs[a];
    vm->inst = vm->cur_irep->inst + bin_to_uint32(handler->target);
    return 0;
  }

  if( regs[a].tt == MRBC_TT_RETBLK ) {
    const mrbc_irep_catch_handler *handler =
      catch_handler_find(vm, 0, MRBC_CATCH_FILTER_ENSURE);
    mrbc_incref( &regs[a] );
    vm->exception = regs[a];
    if( handler ) {
      vm->inst = vm->cur_irep->inst + bin_to_uint32(handler->target);
    } else {
      vm->inst = regs[a].jmpuw;
    }
    return 0;
  }

  assert( mrbc_type(regs[a]) == MRBC_TT_EXCEPTION ||
	  mrbc_type(regs[a]) == MRBC_TT_NIL );
  mrbc_incref( &regs[a] );
  vm->exception = regs[a];
  if( mrbc_israised(vm) ) vm->flag_preemption = 2;
  return 0;
}


//================================================================
/*! OP_SENDV

  R(a) = call(R(a),Syms(b),*R(a+1))

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_sendv( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BB();

  return send_by_name( vm, mrbc_irep_symbol_id(vm->cur_irep, b), regs, a, CALL_MAXARGS, 0 );
}


//================================================================
/*! OP_SENDVB

  R(a) = call(R(a),Syms(b),*R(a+1),&R(a+2))

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_sendvb( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BB();

  return send_by_name( vm, mrbc_irep_symbol_id(vm->cur_irep, b), regs, a, CALL_MAXARGS, 1 );
}


//================================================================
/*! OP_SEND

  R(a) = call(R(a),Syms(b),R(a+1),...,R(a+c))

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_send( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BBB();

  return send_by_name( vm, mrbc_irep_symbol_id(vm->cur_irep, b), regs, a, c, 0 );
}


//================================================================
/*! OP_SENDB

  R(a) = call(R(a),Syms(b),R(a+1),...,R(a+c),&R(a+c+1))

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_sendb( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BBB();

  return send_by_name( vm, mrbc_irep_symbol_id(vm->cur_irep, b), regs, a, c, 1 );
}


//================================================================
/*! OP_SUPER

  R(a) = super(R(a+1),... ,R(a+b+1))

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_super( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BB();

  // set self to new regs[0]
  mrbc_value *recv = mrbc_get_self(vm, regs);
  assert( recv->tt != MRBC_TT_PROC );

  mrbc_incref( recv );
  mrbc_decref( &regs[a] );
  regs[a] = *recv;

  if( b == 127 ) {	// 127 is CALL_MAXARGS in mruby
    // expand array
    assert( regs[a+1].tt == MRBC_TT_ARRAY );

    mrbc_value argary = regs[a+1];
    regs[a+1].tt = MRBC_TT_EMPTY;
    mrbc_value proc = regs[a+2];
    regs[a+2].tt = MRBC_TT_EMPTY;

    int argc = mrbc_array_size(&argary);
    int i, j;
    for( i = 0, j = a+1; i < argc; i++, j++ ) {
      mrbc_decref( &regs[j] );
      regs[j] = argary.array->data[i];
    }
    mrbc_array_delete_handle(&argary);

    regs[j] = proc;
    b = argc;
  }

  // find super class
  mrbc_callinfo *callinfo = vm->callinfo_tail;
  mrbc_class *cls = callinfo->own_class;
  mrbc_method method;

  assert( cls );
  cls = cls->super;
  assert( cls );
  if( mrbc_find_method( &method, cls, callinfo->method_id ) == 0 ) {
    mrbc_printf("Undefined method '%s' for %s\n",
		symid_to_str(callinfo->method_id), symid_to_str(cls->sym_id));
    return 1;
  }

  if( method.c_func ) {
    mrbc_printf("Not support.\n");	// TODO
    return 1;
  }

  callinfo = mrbc_push_callinfo(vm, callinfo->method_id, a, b);
  callinfo->own_class = method.cls;

  // target irep
  vm->cur_irep = method.irep;
  vm->inst = method.irep->inst;

  // new regs
  vm->cur_regs += a;

  return 0;
}


//================================================================
/*! OP_ARGARY

  R(a) = argument array (16=m5:r1:m5:d1:lv4)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_argary( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BS();

  int m1 = (b >> 11) & 0x3f;
  int d  = (b >>  4) & 0x01;

  if( b & 0x400 ) {	// check REST parameter.
    // TODO: want to support.
    mrbc_printf("Not support rest parameter by super.\n");
    return 1;
  }
  if( b & 0x3e0 ) {	// check m2 parameter.
    mrbc_printf("ArgumentError: not support m2 or keyword argument.\n");
    return 1;		// raise?
  }

  int array_size = m1 + d;
  mrbc_value val = mrbc_array_new( vm, array_size );
  if( !val.array ) return 1;	// ENOMEM raise?

  int i;
  for( i = 0; i < array_size; i++ ) {
    mrbc_array_push( &val, &regs[i+1] );
    mrbc_incref( &regs[i+1] );
  }

  mrbc_decref(&regs[a]);
  regs[a] = val;

  mrbc_incref(&regs[m1+1]);
  mrbc_decref(&regs[a+1]);
  regs[a+1] = regs[m1+1];

  return 0;
}


//================================================================
/*! OP_ENTER

  arg setup according to flags (23=m5:o5:r1:m5:k5:d1:b1)

  flags: 0mmm_mmoo_ooor_mmmm_mkkk_kkdb

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_enter( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_W();

  // Check the number of registers to use.
  int reg_use_max = regs - vm->regs + vm->cur_irep->nregs;
  if( reg_use_max >= MAX_REGS_SIZE ) {
    mrbc_raise(vm, MRBC_CLASS(Exception), "MAX_REGS_SIZE overflow.");
    return -1;
  }

  int m1 = (a >> 18) & 0x1f;	// # of required parameters 1
  int o  = (a >> 13) & 0x1f;	// # of optional parameters
#define FLAG_REST_PARAM (a & 0x1000)
#define FLAG_DICT_PARAM (a & 0x2)
  int argc = vm->callinfo_tail->n_args;

  // OP_SENDV or OP_SENDVB
  int flag_sendv_pattern = ( argc == CALL_MAXARGS );
  if( flag_sendv_pattern ) {
    argc = 1;
  }

  if( a & 0xffc ) {	// check m2 and k parameter.
    mrbc_printf("ArgumentError: not support m2 or keyword argument.\n");
    return 1;		// raise?
  }

  if( !flag_sendv_pattern && argc < m1 && regs[0].tt != MRBC_TT_PROC ) {
    mrbc_printf("ArgumentError: wrong number of arguments.\n");
    return 1;		// raise?
  }

  // save proc (or nil) object.
  mrbc_value proc = regs[argc + 1];
  regs[argc + 1].tt = MRBC_TT_EMPTY;

  // support yield [...] pattern
  // support op_sendv pattern
  int flag_yield_pattern = ( regs[0].tt == MRBC_TT_PROC &&
			     regs[1].tt == MRBC_TT_ARRAY && argc != m1 );
  if( flag_yield_pattern || flag_sendv_pattern ) {
    mrbc_value argary = regs[1];
    regs[1].tt = MRBC_TT_EMPTY;

    int i;
    int copy_size;
    if( flag_sendv_pattern ) {
      copy_size = mrbc_array_size(&argary);
    } else {
      copy_size = m1;
    }
    for( i = 0; i < copy_size; i++ ) {
      if( mrbc_array_size(&argary) <= i ) break;
      regs[i+1] = argary.array->data[i];
      mrbc_incref( &regs[i+1] );
    }
    //    mrbc_array_delete( &argary );
    argc = i;
  }

  // dictionary parameter if exists.
  mrbc_value dict;
  if( FLAG_DICT_PARAM ) {
    if( (argc - m1) > 0 && regs[argc].tt == MRBC_TT_HASH ) {
      dict = regs[argc];
      regs[argc--].tt = MRBC_TT_EMPTY;
    } else {
      dict = mrbc_hash_new( vm, 0 );
    }
  }

  // rest parameter if exists.
  mrbc_value rest;
  if( FLAG_REST_PARAM ) {
    int rest_size = argc - m1 - o;
    if( rest_size < 0 ) rest_size = 0;
    rest = mrbc_array_new(vm, rest_size);
    if( !rest.array ) return 0;	// ENOMEM raise?

    int rest_reg = m1 + o + 1;
    int i;
    for( i = 0; i < rest_size; i++ ) {
      mrbc_array_push( &rest, &regs[rest_reg] );
      regs[rest_reg++].tt = MRBC_TT_EMPTY;
    }
  }

  // reorder arguments.
  int i;
  if( argc < m1 ) {
    for( i = argc+1; i <= m1; i++ ) {
      regs[i].tt = MRBC_TT_NIL;
    }
  } else {
    i = m1 + 1;
  }
  i += o;
  if( FLAG_REST_PARAM ) {
    regs[i++] = rest;
  }
  if( FLAG_DICT_PARAM ) {
    regs[i++] = dict;
  }

  // proc の位置を求める
  if( proc.tt == MRBC_TT_PROC ) {
    if( flag_sendv_pattern ) {
      // Nothing
    } else {
      if( argc >= i ) i = argc + 1;
    }
    regs[i] = proc;
  }
  vm->callinfo_tail->n_args = i;

  // prepare for get default arguments.
  int jmp_ofs = argc - m1;
  if( jmp_ofs > 0 ) {
    if( jmp_ofs > o ) {
      if( !FLAG_REST_PARAM && regs[0].tt != MRBC_TT_PROC ) {
	mrbc_printf("ArgumentError: wrong number of arguments.\n");
	return 1;	// raise?
      }
      jmp_ofs = o;
    }
    vm->inst += jmp_ofs * 3;	// 3 = bytecode size of OP_JMP
  }

  return 0;
#undef FLAG_REST_PARAM
#undef FLAG_DICT_PARAM
}


//================================================================
/*! OP_RETURN

  return R(a) (normal)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_return( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_B();

  // check return from ensure.
  if( vm->exception.tt == MRBC_TT_RETBLK ) {
    vm->exception.tt = MRBC_TT_NIL;
    goto RETURN;
  }

  // if in catch handler, jump to ensure.
  // and then return back via OP_RAISEIF.
  const mrbc_irep_catch_handler *handler = catch_handler_find(vm, 0, MRBC_CATCH_FILTER_ENSURE);
  if( handler ) {
    assert( vm->exception.tt == MRBC_TT_NIL );
    vm->exception.tt = MRBC_TT_RETBLK;
    vm->exception.jmpuw = vm->inst - 2;	// 2 is size of OP_RETURN
    vm->inst = vm->cur_irep->inst + bin_to_uint32(handler->target);
    return 0;
  }


 RETURN:
  // return without anything if top level.
  if( vm->callinfo_tail == NULL ) {
    if ( vm->flag_permanence ) {
      mrbc_incref(&regs[a]);
    } else {
      vm->flag_preemption = 1;
    }
    return -1;
  }

  // set return value
  mrbc_decref(&regs[0]);
  regs[0] = regs[a];
  regs[a].tt = MRBC_TT_EMPTY;

  mrbc_pop_callinfo(vm);

  return 0;
}


//================================================================
/*! OP_RETURN_BLK

  return R(a) (in-block return)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_return_blk( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_B();

  // check return from ensure.
  if( vm->exception.tt == MRBC_TT_RETBLK ) {
    vm->exception.tt = MRBC_TT_NIL;
    goto RETURN_TO_OUT_OF_BLOCK;
  }

  // if in catch handler, jump to ensure.
  // and then return back via OP_RAISEIF.
  const mrbc_irep_catch_handler *handler = catch_handler_find(vm, 0, MRBC_CATCH_FILTER_ENSURE);
  if( handler ) {
    assert( vm->exception.tt == MRBC_TT_NIL );
    vm->exception.tt = MRBC_TT_RETBLK;
    vm->exception.jmpuw = vm->inst - 2;	// 2 is size of OP_RETURN_BRK
    vm->inst = vm->cur_irep->inst + bin_to_uint32(handler->target);
    return 0;
  }


 RETURN_TO_OUT_OF_BLOCK:;
  // save return value.
  mrbc_value ret_val = regs[a];
  regs[a].tt = MRBC_TT_EMPTY;

  mrbc_value *ret_reg;

  if( regs[0].tt == MRBC_TT_PROC ) {
    mrbc_callinfo *callinfo = vm->callinfo_tail;
    mrbc_callinfo *caller_callinfo = regs[0].proc->callinfo_self;

    // trace back to caller
    do {
      mrbc_pop_callinfo(vm);
      callinfo = vm->callinfo_tail;
    } while( callinfo != caller_callinfo );

    // return without anything if top level.
    if( callinfo == NULL ) {
      vm->flag_preemption = 1;
      return -1;
    }

    ret_reg = callinfo->cur_regs + callinfo->reg_offset;

  } else {
    ret_reg = &regs[0];
  }

  // return without anything if top level.
  if( vm->callinfo_tail == NULL ) {
    vm->flag_preemption = 1;
    return -1;
  }

  // set return value
  mrbc_decref( ret_reg );
  *ret_reg = ret_val;

  mrbc_pop_callinfo(vm);

  return 0;
}


//================================================================
/*! OP_BREAK

  break R(a)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_break( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_B();

  assert( regs[0].tt == MRBC_TT_PROC );

  // check return from ensure.
  if( vm->exception.tt == MRBC_TT_RETBLK ) {
    vm->exception.tt = MRBC_TT_NIL;
    goto RETURN_TO_OUT_OF_BLOCK;
  }

  // if in catch handler, jump to ensure.
  // and then return back via OP_RAISEIF.
  const mrbc_irep_catch_handler *handler = catch_handler_find(vm, 0, MRBC_CATCH_FILTER_ENSURE);
  if( handler ) {
    assert( vm->exception.tt == MRBC_TT_NIL );
    vm->exception.tt = MRBC_TT_RETBLK;
    vm->exception.jmpuw = vm->inst - 2;	// 2 is size of OP_BREAK
    vm->inst = vm->cur_irep->inst + bin_to_uint32(handler->target);
    return 0;
  }


 RETURN_TO_OUT_OF_BLOCK:;
  // save return value.
  mrbc_value ret_val = regs[a];
  regs[a].tt = MRBC_TT_EMPTY;

  // trace back to caller
  mrbc_callinfo *callinfo = vm->callinfo_tail;
  mrbc_callinfo *caller_callinfo = regs[0].proc->callinfo;
  mrbc_value *ret_reg;

  do {
    ret_reg = callinfo->cur_regs + callinfo->reg_offset;
    mrbc_pop_callinfo(vm);
    callinfo = vm->callinfo_tail;
  } while( callinfo != caller_callinfo );

  // set return value
  mrbc_decref( ret_reg );
  *ret_reg = ret_val;

  return 0;
}


//================================================================
/*! OP_BLKPUSH

  R(a) = block (16=m5:r1:m5:d1:lv4)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_blkpush( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BS();

  int m1 = (b >> 11) & 0x3f;
  int r  = (b >> 10) & 0x01;
  int m2 = (b >>  5) & 0x1f;
  int d  = (b >>  4) & 0x01;
  int lv = (b      ) & 0x0f;

  if( m2 ) {
    mrbc_printf("ArgumentError: not support m2 or keyword argument.\n");
    return 1;		// raise?
  }

  int offset = m1 + r + d + 1;
  mrbc_value *blk;

  if( lv == 0 ) {
    // current env
    blk = regs + offset;
  } else {
    // upper env
    assert( regs[0].tt == MRBC_TT_PROC );

    mrbc_callinfo *callinfo = regs[0].proc->callinfo_self;
    blk = callinfo->cur_regs + callinfo->reg_offset + offset;
  }
  if( blk->tt != MRBC_TT_PROC ) {
    mrbc_printf("no block given (yield) (LocalJumpError)\n");
    return 1;	// raise?
  }

  mrbc_incref(blk);
  mrbc_decref(&regs[a]);
  regs[a] = *blk;

  return 0;
}


//================================================================
/*! OP_ADD

  R(a) = R(a)+R(a+1)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_add( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_B();

  if( regs[a].tt == MRBC_TT_INTEGER ) {
    if( regs[a+1].tt == MRBC_TT_INTEGER ) {     // in case of Integer, Integer
      regs[a].i += regs[a+1].i;
      return 0;
    }
#if MRBC_USE_FLOAT
    if( regs[a+1].tt == MRBC_TT_FLOAT ) {      // in case of Integer, Float
      regs[a].tt = MRBC_TT_FLOAT;
      regs[a].d = regs[a].i + regs[a+1].d;
      return 0;
    }
  }
  if( regs[a].tt == MRBC_TT_FLOAT ) {
    if( regs[a+1].tt == MRBC_TT_INTEGER ) {     // in case of Float, Integer
      regs[a].d += regs[a+1].i;
      return 0;
    }
    if( regs[a+1].tt == MRBC_TT_FLOAT ) {      // in case of Float, Float
      regs[a].d += regs[a+1].d;
      return 0;
    }
#endif
  }

  // other case
  send_by_name(vm, MRBC_SYM(PLUS), regs, a, 1, 0);

  return 0;
}


//================================================================
/*! OP_ADDI

  R(a) = R(a)+mrb_int(b)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_addi( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BB();

  if( regs[a].tt == MRBC_TT_INTEGER ) {
    regs[a].i += b;
    return 0;
  }

#if MRBC_USE_FLOAT
  if( regs[a].tt == MRBC_TT_FLOAT ) {
    regs[a].d += b;
    return 0;
  }
#endif

  not_supported();

  return 0;
}


//================================================================
/*! OP_SUB

  R(a) = R(a)-R(a+1)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_sub( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_B();

  if( regs[a].tt == MRBC_TT_INTEGER ) {
    if( regs[a+1].tt == MRBC_TT_INTEGER ) {     // in case of Integer, Integer
      regs[a].i -= regs[a+1].i;
      return 0;
    }
#if MRBC_USE_FLOAT
    if( regs[a+1].tt == MRBC_TT_FLOAT ) {      // in case of Integer, Float
      regs[a].tt = MRBC_TT_FLOAT;
      regs[a].d = regs[a].i - regs[a+1].d;
      return 0;
    }
  }
  if( regs[a].tt == MRBC_TT_FLOAT ) {
    if( regs[a+1].tt == MRBC_TT_INTEGER ) {     // in case of Float, Integer
      regs[a].d -= regs[a+1].i;
      return 0;
    }
    if( regs[a+1].tt == MRBC_TT_FLOAT ) {      // in case of Float, Float
      regs[a].d -= regs[a+1].d;
      return 0;
    }
#endif
  }

  // other case
  send_by_name(vm, MRBC_SYM(MINUS), regs, a, 1, 0);

  return 0;
}


//================================================================
/*! OP_SUBI

  R(a) = R(a)-mrb_int(b)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_subi( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BB();

  if( regs[a].tt == MRBC_TT_INTEGER ) {
    regs[a].i -= b;
    return 0;
  }

#if MRBC_USE_FLOAT
  if( regs[a].tt == MRBC_TT_FLOAT ) {
    regs[a].d -= b;
    return 0;
  }
#endif

  not_supported();

  return 0;
}


//================================================================
/*! OP_MUL

  R(a) = R(a)*R(a+1)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_mul( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_B();

  if( regs[a].tt == MRBC_TT_INTEGER ) {
    if( regs[a+1].tt == MRBC_TT_INTEGER ) {     // in case of Integer, Integer
      regs[a].i *= regs[a+1].i;
      return 0;
    }
#if MRBC_USE_FLOAT
    if( regs[a+1].tt == MRBC_TT_FLOAT ) {      // in case of Integer, Float
      regs[a].tt = MRBC_TT_FLOAT;
      regs[a].d = regs[a].i * regs[a+1].d;
      return 0;
    }
  }
  if( regs[a].tt == MRBC_TT_FLOAT ) {
    if( regs[a+1].tt == MRBC_TT_INTEGER ) {     // in case of Float, Integer
      regs[a].d *= regs[a+1].i;
      return 0;
    }
    if( regs[a+1].tt == MRBC_TT_FLOAT ) {      // in case of Float, Float
      regs[a].d *= regs[a+1].d;
      return 0;
    }
#endif
  }

  // other case
  send_by_name(vm, MRBC_SYM(MUL), regs, a, 1, 0);

  return 0;
}


//================================================================
/*! OP_DIV

  R(a) = R(a)/R(a+1)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_div( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_B();

  if( regs[a].tt == MRBC_TT_INTEGER ) {
    if( regs[a+1].tt == MRBC_TT_INTEGER ) {     // in case of Integer, Integer
      regs[a].i /= regs[a+1].i;
      return 0;
    }
#if MRBC_USE_FLOAT
    if( regs[a+1].tt == MRBC_TT_FLOAT ) {      // in case of Integer, Float
      regs[a].tt = MRBC_TT_FLOAT;
      regs[a].d = regs[a].i / regs[a+1].d;
      return 0;
    }
  }
  if( regs[a].tt == MRBC_TT_FLOAT ) {
    if( regs[a+1].tt == MRBC_TT_INTEGER ) {     // in case of Float, Integer
      regs[a].d /= regs[a+1].i;
      return 0;
    }
    if( regs[a+1].tt == MRBC_TT_FLOAT ) {      // in case of Float, Float
      regs[a].d /= regs[a+1].d;
      return 0;
    }
#endif
  }

  // other case
  send_by_name(vm, MRBC_SYM(DIV), regs, a, 1, 0);

  return 0;
}


//================================================================
/*! OP_EQ

  R(a) = R(a)==R(a+1)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_eq( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_B();

  // TODO: case OBJECT == OBJECT is not supported.
  int result = mrbc_compare(&regs[a], &regs[a+1]);

  mrbc_decref(&regs[a]);
  regs[a].tt = result ? MRBC_TT_FALSE : MRBC_TT_TRUE;

  return 0;
}


//================================================================
/*! OP_LT

  R(a) = R(a)<R(a+1)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_lt( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_B();

  // TODO: case OBJECT < OBJECT is not supported.
  int result = mrbc_compare(&regs[a], &regs[a+1]);

  mrbc_decref(&regs[a]);
  regs[a].tt = result < 0 ? MRBC_TT_TRUE : MRBC_TT_FALSE;

  return 0;
}


//================================================================
/*! OP_LE

  R(a) = R(a)<=R(a+1)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_le( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_B();

  // TODO: case OBJECT <= OBJECT is not supported.
  int result = mrbc_compare(&regs[a], &regs[a+1]);

  mrbc_decref(&regs[a]);
  regs[a].tt = result <= 0 ? MRBC_TT_TRUE : MRBC_TT_FALSE;

  return 0;
}


//================================================================
/*! OP_GT

  R(a) = R(a)>R(a+1)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_gt( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_B();

  // TODO: case OBJECT > OBJECT is not supported.
  int result = mrbc_compare(&regs[a], &regs[a+1]);

  mrbc_decref(&regs[a]);
  regs[a].tt = result > 0 ? MRBC_TT_TRUE : MRBC_TT_FALSE;

  return 0;
}


//================================================================
/*! OP_GE

  R(a) = R(a)>=R(a+1)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_ge( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_B();

  // TODO: case OBJECT >= OBJECT is not supported.
  int result = mrbc_compare(&regs[a], &regs[a+1]);

  mrbc_decref(&regs[a]);
  regs[a].tt = result >= 0 ? MRBC_TT_TRUE : MRBC_TT_FALSE;

  return 0;
}


//================================================================
/*! OP_ARRAY

  R(a) = ary_new(R(a),R(a+1)..R(a+b))

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_array( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BB();

  mrbc_value value = mrbc_array_new(vm, b);
  if( value.array == NULL ) return -1;  // ENOMEM

  memcpy( value.array->data, &regs[a], sizeof(mrbc_value) * b );
  memset( &regs[a], 0, sizeof(mrbc_value) * b );
  value.array->n_stored = b;

  mrbc_decref(&regs[a]);
  regs[a] = value;

  return 0;
}


//================================================================
/*! OP_ARRAY2

  R(a) = ary_new(R(b),R(b+1)..R(b+c))

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_array2( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BBB();

  mrbc_value value = mrbc_array_new(vm, c);
  if( value.array == NULL ) return -1;  // ENOMEM

  int i;
  for( i=0 ; i<c ; i++ ) {
    mrbc_incref( &regs[b+i] );
    value.array->data[i] = regs[b+i];
  }
  value.array->n_stored = c;

  mrbc_decref(&regs[a]);
  regs[a] = value;

  return 0;
}


//================================================================
/*! OP_ARYCAT

  ary_cat(R(a),R(a+1))

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_arycat( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_B();

  if( regs[a].tt == MRBC_TT_NIL ) {
    // arycat(nil, [...]) #=> [...]
    assert( regs[a+1].tt == MRBC_TT_ARRAY );
    regs[a] = regs[a+1];
    regs[a+1].tt = MRBC_TT_NIL;

    return 0;
  }

  assert( regs[a  ].tt == MRBC_TT_ARRAY );
  assert( regs[a+1].tt == MRBC_TT_ARRAY );

  int size_1 = regs[a  ].array->n_stored;
  int size_2 = regs[a+1].array->n_stored;
  int new_size = size_1 + regs[a+1].array->n_stored;

  // need resize?
  if( regs[a].array->data_size < new_size ) {
    mrbc_array_resize(&regs[a], new_size);
  }

  int i;
  for( i = 0; i < size_2; i++ ) {
    mrbc_incref( &regs[a+1].array->data[i] );
    regs[a].array->data[size_1+i] = regs[a+1].array->data[i];
  }
  regs[a].array->n_stored = new_size;

  return 0;
}


//================================================================
/*! OP_ARYDUP

  R(a) = ary_dup(R(a))

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_arydup( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_B();

  mrbc_value ret = mrbc_array_dup( vm, &regs[a] );
  mrbc_decref(&regs[a]);
  regs[a] = ret;

  return 0;
}


//================================================================
/*! OP_AREF

  R(a) = R(b)[c]

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_aref( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BBB();

  mrbc_value *src = &regs[b];
  mrbc_value *dst = &regs[a];

  mrbc_decref( dst );

  if( src->tt == MRBC_TT_ARRAY ) {
    // src is Array
    *dst = mrbc_array_get(src, c);
    mrbc_incref(dst);
  } else {
    // src is not Array
    if( c == 0 ) {
      mrbc_incref(src);
      *dst = *src;
    } else {
      dst->tt = MRBC_TT_NIL;
    }
  }

  return 0;
}


//================================================================
/*! OP_ASET

  R(a)[c] = R(b)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_aset( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BBB();

  mrbc_array_set(&regs[a], c, &regs[b]);

  return 0;
}


//================================================================
/*! OP_APOST

  *R(a),R(a+1)..R(a+c) = R(a)[b..]

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_apost( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BBB();

  mrbc_value src = regs[a];
  if( src.tt != MRBC_TT_ARRAY ) {
    src = mrbc_array_new(vm, 1);
    src.array->data[0] = regs[a];
    src.array->n_stored = 1;
  }

  int pre  = b;
  int post = c;
  int len = src.array->n_stored;

  if( len > pre + post ) {
    int ary_size = len-pre-post;
    regs[a] = mrbc_array_new(vm, ary_size);
    // copy elements
    int i;
    for( i = 0; i < ary_size; i++ ) {
      regs[a].array->data[i] = src.array->data[pre+i];
      mrbc_incref( &regs[a].array->data[i] );
    }
    regs[a].array->n_stored = ary_size;
  } else {
    // empty
    regs[a] = mrbc_array_new(vm, 0);
  }

  return 0;
}


//================================================================
/*! OP_INTERN

  R(a) = intern(R(a))

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_intern( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_B();

  assert( regs[a].tt == MRBC_TT_STRING );

  mrbc_value sym_id = mrbc_symbol_new(vm, (const char*)regs[a].string->data);

  mrbc_decref( &regs[a] );
  regs[a] = sym_id;

  return 0;
}


//================================================================
/*! OP_STRING

  R(a) = str_dup(Lit(b))

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_string( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BB();

  mrbc_decref(&regs[a]);
  regs[a] = mrbc_irep_pool_value(vm, b);

  return 0;
}


//================================================================
/*! OP_STRING16

  R(a) = str_dup(Lit(b))

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_string16( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BS();

  mrbc_decref(&regs[a]);
  regs[a] = mrbc_irep_pool_value(vm, b);

  return 0;
}


//================================================================
/*! OP_STRCAT

  str_cat(R(a),R(a+1))

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_strcat( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_B();

#if MRBC_USE_STRING
  // call "to_s"
  mrbc_method method;
  if( mrbc_find_method( &method, find_class_by_object(&regs[a+1]),
			MRBC_SYM(to_s)) == 0 ) return 0;
  if( !method.c_func ) return 0;	// TODO: Not support?

  method.func( vm, regs + a + 1, 0 );
  mrbc_string_append( &regs[a], &regs[a+1] );
  mrbc_decref_empty( &regs[a+1] );

#else
  not_supported();
#endif

  return 0;
}


//================================================================
/*! OP_HASH

  R(a) = hash_new(R(a),R(a+1)..R(a+b*2-1))

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_hash( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BB();

  mrbc_value value = mrbc_hash_new(vm, b);
  if( value.hash == NULL ) return -1;   // ENOMEM

  b *= 2;
  memcpy( value.hash->data, &regs[a], sizeof(mrbc_value) * b );
  memset( &regs[a], 0, sizeof(mrbc_value) * b );
  value.hash->n_stored = b;

  mrbc_decref(&regs[a]);
  regs[a] = value;

  return 0;
}


//================================================================
/*! OP_BLOCK, OP_METHOD

  R(a) = lambda(SEQ[b],L_METHOD)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_method( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BB();

  mrbc_value val = mrbc_proc_new(vm, mrbc_irep_child_irep(vm->cur_irep, b));
  if( !val.proc ) return -1;	// ENOMEM

  mrbc_decref(&regs[a]);
  regs[a] = val;

  return 0;
}


//================================================================
/*! OP_METHOD16

  R(a) = lambda(SEQ[b],L_METHOD)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_method16( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BS();

  mrbc_value val = mrbc_proc_new(vm, mrbc_irep_child_irep(vm->cur_irep, b));
  if( !val.proc ) return -1;	// ENOMEM

  mrbc_decref(&regs[a]);
  regs[a] = val;

  return 0;
}


//================================================================
/*! OP_RANGE_INC, OP_RANGE_EXC

  R(a) = range_new(R(a),R(a+1),FALSE)
  R(a) = range_new(R(a),R(a+1),TRUE)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_range( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_B();

  mrbc_value value = mrbc_range_new(vm, &regs[a], &regs[a+1],
				    (vm->inst[-2] == OP_RANGE_EXC));
  regs[a] = value;
  regs[a+1].tt = MRBC_TT_EMPTY;

  return 0;
}


//================================================================
/*! OP_CLASS

  R(a) = newclass(R(a),Syms(b),R(a+1))

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_class( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BB();

  const char *class_name = mrbc_irep_symbol_cstr(vm->cur_irep, b);
  mrbc_class *super = (regs[a+1].tt == MRBC_TT_CLASS) ? regs[a+1].cls : 0;
  mrbc_class *cls = mrbc_define_class(vm, class_name, super);
  if( !cls ) return -1;		// ENOMEM

  // (note)
  //  regs[a] was set to NIL by compiler. So, no need to release regs[a].
  regs[a].tt = MRBC_TT_CLASS;
  regs[a].cls = cls;

  return 0;
}


//================================================================
/*! OP_EXEC

  R(a) = blockexec(R(a),SEQ[b])

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_exec( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BB();
  assert( regs[a].tt == MRBC_TT_CLASS );

  // prepare callinfo
  mrbc_push_callinfo(vm, 0, 0, 0);

  // target irep
  vm->cur_irep = mrbc_irep_child_irep(vm->cur_irep, b);
  vm->inst = vm->cur_irep->inst;

  // new regs and class
  vm->cur_regs += a;
  vm->target_class = regs[a].cls;

  return 0;
}


//================================================================
/*! OP_EXEC16

  R(a) = blockexec(R(a),SEQ[b])

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_exec16( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BS();
  assert( regs[a].tt == MRBC_TT_CLASS );

  // prepare callinfo
  mrbc_push_callinfo(vm, 0, 0, 0);

  // target irep
  vm->cur_irep = mrbc_irep_child_irep(vm->cur_irep, b);
  vm->inst = vm->cur_irep->inst;

  // new regs and class
  vm->cur_regs += a;
  vm->target_class = regs[a].cls;

  return 0;
}


//================================================================
/*! OP_DEF

  R(a).newmethod(Syms(b),R(a+1))

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_def( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BB();

  assert( regs[a].tt == MRBC_TT_CLASS );
  assert( regs[a+1].tt == MRBC_TT_PROC );

  mrbc_class *cls = regs[a].cls;
  mrbc_sym sym_id = mrbc_irep_symbol_id(vm->cur_irep, b);
  mrbc_proc *proc = regs[a+1].proc;
  mrbc_method *method;

  if( vm->vm_id == 0 ) {
    method = mrbc_raw_alloc_no_free( sizeof(mrbc_method) );
  } else {
    method = mrbc_raw_alloc( sizeof(mrbc_method) );
  }
  if( !method ) return -1; // ENOMEM

  method->type = (vm->vm_id == 0) ? 'm' : 'M';
  method->c_func = 0;
  method->sym_id = sym_id;
  method->irep = proc->irep;
  method->next = cls->method_link;
  cls->method_link = method;

  // checking same method
  for( ;method->next != NULL; method = method->next ) {
    if( method->next->sym_id == sym_id ) {
      // Found it. Unchain it in linked list and remove.
      mrbc_method *del_method = method->next;

      method->next = del_method->next;
      if( del_method->type == 'M' ) mrbc_raw_free( del_method );

      break;
    }
  }

  return 0;
}


//================================================================
/*! OP_ALIAS

  alias_method(target_class,Syms(a),Syms(b))

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_alias( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BB();

  mrbc_sym sym_id_new = mrbc_irep_symbol_id(vm->cur_irep, a);
  mrbc_sym sym_id_org = mrbc_irep_symbol_id(vm->cur_irep, b);
  mrbc_class *cls = vm->target_class;
  mrbc_method *method = mrbc_raw_alloc( sizeof(mrbc_method) );
  if( !method ) return 0;	// ENOMEM

  if( mrbc_find_method( method, cls, sym_id_org ) == 0 ) {
    mrbc_printf("NameError: undefined method '%s'\n",
		symid_to_str(sym_id_org));
    mrbc_raw_free( method );
    return 0;
  }

  method->type = 'M';
  method->sym_id = sym_id_new;
  method->next = cls->method_link;
  cls->method_link = method;

  // checking same method
  //  see OP_DEF function. same it.
  for( ;method->next != NULL; method = method->next ) {
    if( method->next->sym_id == sym_id_new ) {
      mrbc_method *del_method = method->next;
      method->next = del_method->next;
      if( del_method->type == 'M' ) mrbc_raw_free( del_method );
      break;
    }
  }

  return 0;
}


//================================================================
/*! OP_SCLASS

  R(a) = R(a).singleton_class

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_sclass( mrbc_vm *vm, mrbc_value *regs )
{
  // currently, not supported
  FETCH_B();
  return 0;
}


//================================================================
/*! OP_TCLASS

  R(a) = target_class

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_tclass( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_B();

  mrbc_decref(&regs[a]);
  regs[a].tt = MRBC_TT_CLASS;
  regs[a].cls = vm->target_class;

  return 0;
}


//================================================================
/*! OP_STOP

  stop VM

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval -1  No error and exit from vm.
*/
static inline int op_stop( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_Z();

  vm->flag_preemption = 1;

  return -1;
}


//================================================================
/*! OP_ABORT

  stop VM

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval -1  No error and exit from vm.
*/
static inline int op_abort( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_Z();

  vm->flag_preemption = 1;

  return -1;
}


//================================================================
/*! Dummy function for unsupported opcode Z
*/
static inline int op_dummy_Z( mrbc_vm *vm, mrbc_value *regs )
{
  uint8_t op = *(vm->inst - 1);
  FETCH_Z();

  mrbc_printf("# Skip OP 0x%02x\n", op);
  return 0;
}


//================================================================
/*! Dummy function for unsupported opcode B
*/
static inline int op_dummy_B( mrbc_vm *vm, mrbc_value *regs )
{
  uint8_t op = *(vm->inst - 1);
  FETCH_B();

  mrbc_printf("# Skip OP 0x%02x\n", op);
  return 0;
}


//================================================================
/*! Dummy function for unsupported opcode BB
*/
static inline int op_dummy_BB( mrbc_vm *vm, mrbc_value *regs )
{
  uint8_t op = *(vm->inst - 1);
  FETCH_BB();

  mrbc_printf("# Skip OP 0x%02x\n", op);
  return 0;
}


//================================================================
/*! Dummy function for unsupported opcode BBB
*/
static inline int op_dummy_BBB( mrbc_vm *vm, mrbc_value *regs )
{
  uint8_t op = *(vm->inst - 1);
  FETCH_BBB();

  mrbc_printf("# Skip OP 0x%02x\n", op);
  return 0;
}


//================================================================
/*! Open the VM.

  @param vm_arg	Pointer to mrbc_vm or NULL.
  @return	Pointer to mrbc_vm.
  @retval NULL	error.
*/
mrbc_vm *mrbc_vm_open( struct VM *vm_arg )
{
  mrbc_vm *vm = vm_arg;

  if( vm == NULL ) {
    // allocate memory.
    vm = mrbc_raw_alloc( sizeof(mrbc_vm) );
    if( vm == NULL ) return NULL;
  }

  // allocate vm id.
  int vm_id;
  for( vm_id = 0; vm_id < MAX_VM_COUNT; vm_id++ ) {
    int idx = vm_id >> 4;
    int bit = 1 << (vm_id & 0x0f);
    if( (free_vm_bitmap[idx] & bit) == 0 ) {
      free_vm_bitmap[idx] |= bit;		// found
      break;
    }
  }

  if( vm_id == MAX_VM_COUNT ) {
    if( vm_arg == NULL ) mrbc_raw_free(vm);
    return NULL;
  }
  vm_id++;

  // initialize attributes.
  memset(vm, 0, sizeof(mrbc_vm));	// caution: assume NULL is zero.
#if defined(MRBC_DEBUG)
  memcpy(vm->type, "VM", 2);
#endif
  if( vm_arg == NULL ) vm->flag_need_memfree = 1;
  vm->vm_id = vm_id;

  return vm;
}


//================================================================
/*! Close the VM.

  @param  vm  Pointer to VM
*/
void mrbc_vm_close( struct VM *vm )
{
  // free vm id.
  int idx = (vm->vm_id-1) >> 4;
  int bit = 1 << ((vm->vm_id-1) & 0x0f);
  free_vm_bitmap[idx] &= ~bit;

  // free irep and vm
  if( vm->top_irep ) mrbc_irep_free( vm->top_irep );
  if( vm->flag_need_memfree ) mrbc_raw_free(vm);
}


//================================================================
/*! VM initializer.

  @param  vm  Pointer to VM
*/
void mrbc_vm_begin( struct VM *vm )
{
  vm->cur_irep = vm->top_irep;
  vm->inst = vm->cur_irep->inst;

  // set self to reg[0], others nil
  vm->regs[0] = mrbc_instance_new(vm, mrbc_class_object, 0);
  if( vm->regs[0].instance == NULL ) return;	// ENOMEM
  int i;
  for( i = 1; i < MAX_REGS_SIZE; i++ ) {
    vm->regs[i] = mrbc_nil_value();
  }

  vm->cur_regs = vm->regs;
  vm->callinfo_tail = NULL;
  vm->target_class = mrbc_class_object;
  vm->exception = mrbc_nil_value();
  vm->flag_preemption = 0;
}


//================================================================
/*! VM finalizer.

  @param  vm  Pointer to VM
*/
void mrbc_vm_end( struct VM *vm )
{
  if( mrbc_israised(vm) ) {
    mrbc_printf( "Exception : %s (%s)\n",
		 symid_to_str(vm->exception.exception->cls->sym_id),
		 vm->exception.exception->message ?
		   (const char *)vm->exception.exception->message :
		   symid_to_str(vm->exception.exception->cls->sym_id) );
    mrbc_decref(&vm->exception);
  }

  int n_used = 0;
  int i;
  for( i = 0; i < MAX_REGS_SIZE; i++ ) {
    //mrbc_printf("vm->regs[%d].tt = %d\n", i, mrbc_type(vm->regs[i]));
    if( mrbc_type(vm->regs[i]) != MRBC_TT_NIL ) n_used = i;
    mrbc_decref_empty(&vm->regs[i]);
  }
#if defined(MRBC_DEBUG_REGS)
  mrbc_printf("Finally number of registers used was %d in VM %d.\n",
	      n_used, vm->vm_id );
#endif

#if defined(MRBC_ALLOC_VMID)
  mrbc_global_clear_vm_id();
  mrbc_free_all(vm);
#endif
}


//================================================================
/*! Fetch a bytecode and execute

  @param  vm    A pointer of VM.
  @retval 0  No error.
*/
int mrbc_vm_run( struct VM *vm )
{
  int ret = 0;

  while( 1 ) {
    do {
      // regs
      mrbc_value *regs = vm->cur_regs;

      // Dispatch
      uint8_t op = *vm->inst++;

      switch( op ) {
      case OP_NOP:        ret = op_nop       (vm, regs); break;
      case OP_MOVE:       ret = op_move      (vm, regs); break;
      case OP_LOADL:      ret = op_loadl     (vm, regs); break;
      case OP_LOADL16:    ret = op_loadl16   (vm, regs); break;
      case OP_LOADI:      ret = op_loadi     (vm, regs); break;
      case OP_LOADINEG:   ret = op_loadineg  (vm, regs); break;
      case OP_LOADI__1:   // fall through
      case OP_LOADI_0:    // fall through
      case OP_LOADI_1:    // fall through
      case OP_LOADI_2:    // fall through
      case OP_LOADI_3:    // fall through
      case OP_LOADI_4:    // fall through
      case OP_LOADI_5:    // fall through
      case OP_LOADI_6:    // fall through
      case OP_LOADI_7:    ret = op_loadi_n   (vm, regs); break;
      case OP_LOADI16:    ret = op_loadi16   (vm, regs); break;
      case OP_LOADI32:    ret = op_loadi32   (vm, regs); break;
      case OP_LOADSYM:    ret = op_loadsym   (vm, regs); break;
      case OP_LOADSYM16:  ret = op_loadsym16 (vm, regs); break;
      case OP_LOADNIL:    ret = op_loadnil   (vm, regs); break;
      case OP_LOADSELF:   ret = op_loadself  (vm, regs); break;
      case OP_LOADT:      ret = op_loadt     (vm, regs); break;
      case OP_LOADF:      ret = op_loadf     (vm, regs); break;
      case OP_GETGV:      ret = op_getgv     (vm, regs); break;
      case OP_SETGV:      ret = op_setgv     (vm, regs); break;
      case OP_GETSV:      ret = op_dummy_BB  (vm, regs); break;
      case OP_SETSV:      ret = op_dummy_BB  (vm, regs); break;
      case OP_GETIV:      ret = op_getiv     (vm, regs); break;
      case OP_SETIV:      ret = op_setiv     (vm, regs); break;
      case OP_GETCV:      ret = op_dummy_BB  (vm, regs); break;
      case OP_SETCV:      ret = op_dummy_BB  (vm, regs); break;
      case OP_GETCONST:   ret = op_getconst  (vm, regs); break;
      case OP_SETCONST:   ret = op_setconst  (vm, regs); break;
      case OP_GETMCNST:   ret = op_getmcnst  (vm, regs); break;
      case OP_SETMCNST:   ret = op_dummy_BB  (vm, regs); break;
      case OP_GETUPVAR:   ret = op_getupvar  (vm, regs); break;
      case OP_SETUPVAR:   ret = op_setupvar  (vm, regs); break;
      case OP_JMP:        ret = op_jmp       (vm, regs); break;
      case OP_JMPIF:      ret = op_jmpif     (vm, regs); break;
      case OP_JMPNOT:     ret = op_jmpnot    (vm, regs); break;
      case OP_JMPNIL:     ret = op_jmpnil    (vm, regs); break;
      case OP_JMPUW:      ret = op_jmpuw     (vm, regs); break;
      case OP_EXCEPT:     ret = op_except    (vm, regs); break;
      case OP_RESCUE:     ret = op_rescue    (vm, regs); break;
      case OP_RAISEIF:    ret = op_raiseif   (vm, regs); break;
      case OP_SENDV:      ret = op_sendv     (vm, regs); break;
      case OP_SENDVB:     ret = op_sendvb    (vm, regs); break;
      case OP_SEND:       ret = op_send      (vm, regs); break;
      case OP_SENDB:      ret = op_sendb     (vm, regs); break;
	//case OP_SENDVK:     ret = op_sendvk    (vm, regs); break;
      case OP_CALL:       ret = op_dummy_Z   (vm, regs); break;
      case OP_SUPER:      ret = op_super     (vm, regs); break;
      case OP_ARGARY:     ret = op_argary    (vm, regs); break;
      case OP_ENTER:      ret = op_enter     (vm, regs); break;
      case OP_KEY_P:      ret = op_dummy_BB  (vm, regs); break;
      case OP_KEYEND:     ret = op_dummy_Z   (vm, regs); break;
      case OP_KARG:       ret = op_dummy_BB  (vm, regs); break;
      case OP_RETURN:     ret = op_return    (vm, regs); break;
      case OP_RETURN_BLK: ret = op_return_blk(vm, regs); break;
      case OP_BREAK:      ret = op_break     (vm, regs); break;
      case OP_BLKPUSH:    ret = op_blkpush   (vm, regs); break;
      case OP_ADD:        ret = op_add       (vm, regs); break;
      case OP_ADDI:       ret = op_addi      (vm, regs); break;
      case OP_SUB:        ret = op_sub       (vm, regs); break;
      case OP_SUBI:       ret = op_subi      (vm, regs); break;
      case OP_MUL:        ret = op_mul       (vm, regs); break;
      case OP_DIV:        ret = op_div       (vm, regs); break;
      case OP_EQ:         ret = op_eq        (vm, regs); break;
      case OP_LT:         ret = op_lt        (vm, regs); break;
      case OP_LE:         ret = op_le        (vm, regs); break;
      case OP_GT:         ret = op_gt        (vm, regs); break;
      case OP_GE:         ret = op_ge        (vm, regs); break;
      case OP_ARRAY:      ret = op_array     (vm, regs); break;
      case OP_ARRAY2:     ret = op_array2    (vm, regs); break;
      case OP_ARYCAT:     ret = op_arycat    (vm, regs); break;
      case OP_ARYPUSH:    ret = op_dummy_B   (vm, regs); break;
      case OP_ARYDUP:     ret = op_arydup    (vm, regs); break;
      case OP_AREF:       ret = op_aref      (vm, regs); break;
      case OP_ASET:       ret = op_aset      (vm, regs); break;
      case OP_APOST:      ret = op_apost     (vm, regs); break;
      case OP_INTERN:     ret = op_intern    (vm, regs); break;
      case OP_STRING:     ret = op_string    (vm, regs); break;
      case OP_STRING16:   ret = op_string16  (vm, regs); break;
      case OP_STRCAT:     ret = op_strcat    (vm, regs); break;
      case OP_HASH:       ret = op_hash      (vm, regs); break;
      case OP_HASHADD:    ret = op_dummy_BB  (vm, regs); break;
      case OP_HASHCAT:    ret = op_dummy_B   (vm, regs); break;
      case OP_LAMBDA:     ret = op_dummy_BB  (vm, regs); break;
      case OP_BLOCK:      // fall through
      case OP_METHOD:     ret = op_method    (vm, regs); break;
      case OP_BLOCK16:    // fall through, order of OP_METHOD and OP_BLOCK16 is changed.
      case OP_METHOD16:   ret = op_method16  (vm, regs); break;
      case OP_RANGE_INC:  // fall through
      case OP_RANGE_EXC:  ret = op_range     (vm, regs); break;
      case OP_OCLASS:     ret = op_dummy_B   (vm, regs); break;
      case OP_CLASS:      ret = op_class     (vm, regs); break;
      case OP_MODULE:     ret = op_dummy_BB  (vm, regs); break;
      case OP_EXEC:       ret = op_exec      (vm, regs); break;
      case OP_EXEC16:     ret = op_exec16    (vm, regs); break;
      case OP_DEF:        ret = op_def       (vm, regs); break;
      case OP_ALIAS:      ret = op_alias     (vm, regs); break;
      case OP_UNDEF:      ret = op_dummy_B   (vm, regs); break;
      case OP_SCLASS:     ret = op_sclass    (vm, regs); break;
      case OP_TCLASS:     ret = op_tclass    (vm, regs); break;
      case OP_DEBUG:      ret = op_dummy_BBB (vm, regs); break;
      case OP_ERR:        ret = op_dummy_B   (vm, regs); break;
      case OP_STOP:       ret = op_stop      (vm, regs); break;

      case OP_ABORT:      ret = op_abort     (vm, regs); break;
      default:
	mrbc_printf("Unknown OP 0x%02x\n", op);
	break;
      }
    } while( !vm->flag_preemption );

    if( !mrbc_israised(vm) ) return ret;	// normal return.

    // Handle exception
    vm->flag_preemption = 0;
    const mrbc_irep_catch_handler *handler =
      catch_handler_find(vm, 0, MRBC_CATCH_FILTER_ALL);
    if( !handler ) {
      const mrbc_callinfo *callinfo = vm->callinfo_tail;

      while( callinfo ) {
	handler = catch_handler_find(0, callinfo, MRBC_CATCH_FILTER_ALL);
	if( handler ) break;
	callinfo = callinfo->prev;
      }
      if( !callinfo ) return -2;	// to raise in top level.

      while( vm->callinfo_tail != callinfo ) {
	mrbc_pop_callinfo( vm );
      }
      mrbc_pop_callinfo( vm );
    }

    vm->inst = vm->cur_irep->inst + bin_to_uint32(handler->target);
  }
}
