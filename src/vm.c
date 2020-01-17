/*! @file
  @brief
  mruby bytecode executor.

  <pre>
  Copyright (C) 2015-2020 Kyushu Institute of Technology.
  Copyright (C) 2015-2020 Shimane IT Open-Innovation Center.

  This file is distributed under BSD 3-Clause License.

  Fetch mruby VM bytecodes, decode and execute.

  </pre>
*/

#include "vm_config.h"
#include "hal/hal.h"
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include "vm.h"
#include "alloc.h"
#include "load.h"
#include "static.h"
#include "global.h"
#include "opcode.h"
#include "class.h"
#include "symbol.h"
#include "console.h"

#include "c_string.h"
#include "c_range.h"
#include "c_array.h"
#include "c_hash.h"


static uint16_t free_vm_bitmap[MAX_VM_COUNT / 16 + 1];
#define Num(n) (sizeof(n)/sizeof((n)[0]))


//================================================================
/*! cleanup
*/
void mrbc_cleanup_vm(void)
{
  memset(free_vm_bitmap, 0, sizeof(free_vm_bitmap));
}


//================================================================
/*! get sym[n] from symbol table in irep

  @param  p	Pointer to IREP SYMS section.
  @param  n	n th
  @return	symbol name string
*/
const char * mrbc_get_irep_symbol( const uint8_t *p, int n )
{
  int cnt = bin_to_uint32(p);
  if( n >= cnt ) return 0;
  p += 4;
  while( n > 0 ) {
    uint16_t s = bin_to_uint16(p);
    p += 2+s+1;   // size(2 bytes) + symbol len + '\0'
    n--;
  }
  return (char *)p+2;  // skip size(2 bytes)
}


//================================================================
/*! get callee name

  @param  vm	Pointer to VM
  @return	string
*/
const char *mrbc_get_callee_name( struct VM *vm )
{
  uint8_t rb = vm->inst[-2];
  return mrbc_get_irep_symbol(vm->pc_irep->ptr_to_sym, rb);
}


//================================================================
/*!@brief

*/
static void not_supported(void)
{
  console_printf("Not supported!\n");
}


//================================================================
/*! mrbc_irep allocator

  @param  vm	Pointer to VM.
  @return	Pointer to allocated memory or NULL.
*/
mrbc_irep *mrbc_irep_alloc(struct VM *vm)
{
  mrbc_irep *p = (mrbc_irep *)mrbc_alloc(vm, sizeof(mrbc_irep));
  if( p )
    memset(p, 0, sizeof(mrbc_irep));	// caution: assume NULL is zero.
  return p;
}


//================================================================
/*! release mrbc_irep holds memory

  @param  irep	Pointer to allocated mrbc_irep.
*/
void mrbc_irep_free(mrbc_irep *irep)
{
  int i;

  // release pools.
  for( i = 0; i < irep->plen; i++ ) {
    mrbc_raw_free( irep->pools[i] );
  }
  if( irep->plen ) mrbc_raw_free( irep->pools );

  // release child ireps.
  for( i = 0; i < irep->rlen; i++ ) {
    mrbc_irep_free( irep->reps[i] );
  }
  if( irep->rlen ) mrbc_raw_free( irep->reps );

  mrbc_raw_free( irep );
}


//================================================================
/*! Push current status to callinfo stack

*/
void mrbc_push_callinfo( struct VM *vm, mrbc_sym mid, int n_args )
{
  mrbc_callinfo *callinfo = mrbc_alloc(vm, sizeof(mrbc_callinfo));
  if( !callinfo ) return;

  callinfo->current_regs = vm->current_regs;
  callinfo->pc_irep = vm->pc_irep;
  callinfo->pc = vm->pc;
  callinfo->inst = vm->inst;
  callinfo->mid = mid;
  callinfo->n_args = n_args;
  callinfo->target_class = vm->target_class;
  callinfo->prev = vm->callinfo_tail;
  vm->callinfo_tail = callinfo;
}


//================================================================
/*! Pop current status to callinfo stack

*/
void mrbc_pop_callinfo( struct VM *vm )
{
  mrbc_callinfo *callinfo = vm->callinfo_tail;
  if( !callinfo ) return;
  vm->callinfo_tail = callinfo->prev;
  vm->current_regs = callinfo->current_regs;
  vm->pc_irep = callinfo->pc_irep;
  vm->pc = callinfo->pc;
  vm->inst = callinfo->inst;
  vm->target_class = callinfo->target_class;

  mrbc_free(vm, callinfo);
}





//================================================================
/*!@brief
  Execute OP_NOP

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
/*!@brief
  Execute OP_MOVE

  R(a) = R(b)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_move( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BB();

  if( a != b ){
    mrbc_release(&regs[a]);
    mrbc_dup(&regs[b]);
    regs[a] = regs[b];
  }
  return 0;
}




//================================================================
/*!@brief
  Execute OP_LOADL

  R(a) = Pool(b)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_loadl( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BB();

  mrbc_release(&regs[a]);

  mrbc_object *pool_obj = vm->pc_irep->pools[b];
  regs[a] = *pool_obj;

  return 0;
}




//================================================================
/*!@brief
  Execute OP_LOADI

  R(a) = mrb_int(b)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_loadi( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BB();

  mrbc_release(&regs[a]);
  regs[a] = mrbc_fixnum_value(b);
  return 0;
}




//================================================================
/*!@brief
  Execute OP_LOADINEG

  R(a) = mrb_int(-b)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_loadineg( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BB();

  mrbc_release(&regs[a]);
  regs[a] = mrbc_fixnum_value(-b);
  return 0;
}




//================================================================
/*!@brief
  Execute OP_LOADI_n (n=-1,0,1..7)

  R(a) = R(a)+mrb_int(n)

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

  mrbc_release(&regs[a]);
  regs[a] = mrbc_fixnum_value(n);

  return 0;
}



//================================================================
/*!@brief
  Execute OP_LOADSYM

  R(a) = Syms(b)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_loadsym( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BB();

  const char *sym_name = mrbc_get_irep_symbol(vm->pc_irep->ptr_to_sym, b);
  mrbc_sym sym_id = str_to_symid(sym_name);

  mrbc_release(&regs[a]);
  regs[a].tt = MRBC_TT_SYMBOL;
  regs[a].i = sym_id;

  return 0;
}



//================================================================
/*!@brief
  Execute OP_LOADNIL

  R(a) = nil

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_loadnil( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_B();

  mrbc_release(&regs[a]);
  regs[a].tt = MRBC_TT_NIL;

  return 0;
}



//================================================================
/*!@brief
  Execute OP_LOADSELF

  R(a) = self

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_loadself( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_B();

  mrbc_release(&regs[a]);
  mrbc_dup(&regs[0]);
  regs[a] = regs[0];
  return 0;
}



//================================================================
/*!@brief
  Execute OP_LOADF

  R(a) = false

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_loadt( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_B();

  mrbc_release(&regs[a]);
  regs[a].tt = MRBC_TT_TRUE;

  return 0;
}



//================================================================
/*!@brief
  Execute OP_LOADF

  R(a) = false

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_loadf( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_B();

  mrbc_release(&regs[a]);
  regs[a].tt = MRBC_TT_FALSE;

  return 0;
}



//================================================================
/*!@brief
  Execute OP_GETGV

  R(a) = getglobal(Syms(b))

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_getgv( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BB();

  const char *sym_name = mrbc_get_irep_symbol(vm->pc_irep->ptr_to_sym, b);
  mrbc_sym sym_id = str_to_symid(sym_name);

  mrbc_release(&regs[a]);

  hal_lock();

  mrbc_value *v = mrbc_get_global(sym_id);
  if( v == NULL ) {
    regs[a] = mrbc_nil_value();
  } else {
    mrbc_dup(v);
    regs[a] = *v;
  }

  hal_unlock();
  return 0;
}



//================================================================
/*!@brief
  Execute OP_SETGV

  setglobal(Syms(b), R(a))

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_setgv( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BB();

  const char *sym_name = mrbc_get_irep_symbol(vm->pc_irep->ptr_to_sym, b);
  mrbc_sym sym_id = str_to_symid(sym_name);
  mrbc_dup(&regs[a]);
  hal_lock();
  mrbc_set_global(sym_id, &regs[a]);
  hal_unlock();

  return 0;
}



//================================================================
/*!@brief
  Execute OP_GETIV

  R(a) = ivget(Syms(b))

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_getiv( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BB();

  const char *sym_name = mrbc_get_irep_symbol(vm->pc_irep->ptr_to_sym, b);
  mrbc_sym sym_id = str_to_symid(sym_name+1);   // skip '@'

  mrbc_value val = mrbc_instance_getiv(&regs[0], sym_id);

  mrbc_release(&regs[a]);
  regs[a] = val;

  return 0;
}




//================================================================
/*!@brief
  Execute OP_SETIV

  ivset(Syms(b),R(a))

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_setiv( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BB();

  const char *sym_name = mrbc_get_irep_symbol(vm->pc_irep->ptr_to_sym, b);
  mrbc_sym sym_id = str_to_symid(sym_name+1);   // skip '@'

  mrbc_instance_setiv(&regs[0], sym_id, &regs[a]);

  return 0;
}



//================================================================
/*!@brief
  Execute OP_GETCONST

  R(a) = constget(Syms(b))

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_getconst( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BB();

  const char *sym_name = mrbc_get_irep_symbol(vm->pc_irep->ptr_to_sym, b);
  mrbc_sym sym_id = str_to_symid(sym_name);

  mrbc_release(&regs[a]);

  hal_lock();

  mrbc_value *v = mrbc_get_const(sym_id);
  if( v != NULL ) {
    mrbc_dup(v);
    regs[a] = *v;
  } else {
    // raise?
    console_printf( "NameError: uninitialized constant %s\n", sym_name );
  }

  hal_unlock();
  return 0;
}



//================================================================
/*!@brief
  Execute OP_SETCONST

  constset(Syms(b),R(a))

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_setconst( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BB();

  const char *sym_name = mrbc_get_irep_symbol(vm->pc_irep->ptr_to_sym, b);
  mrbc_sym sym_id = str_to_symid(sym_name);
  mrbc_class *cls = find_class_by_object( vm, &regs[0] );

  // supports class constants up to 1 level.
  if( cls != mrbc_class_object ) {
    mrbc_sym id = cls->sym_id;
    char buf[10];
    int i;
    for( i = 3; i >= 0; i-- ) {
      buf[i] = '0' + (id & 0x0f);
      id >>= 4;
    }
    id = sym_id;
    for( i = 7; i >= 4; i-- ) {
      buf[i] = '0' + (id & 0x0f);
      id >>= 4;
    }
    buf[8] = 0;

    sym_id = mrbc_symbol_new( vm, buf ).i;
  }

  mrbc_dup(&regs[a]);
  hal_lock();
  mrbc_set_const(sym_id, &regs[a]);
  hal_unlock();

  return 0;
}



//================================================================
/*!@brief
  Execute OP_GETMCNST

  R(a) = R(a)::Syms(b)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_getmcnst( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BB();

  const char *sym_name = mrbc_get_irep_symbol(vm->pc_irep->ptr_to_sym, b);
  mrbc_class *cls = regs[a].cls;
  mrbc_sym id = cls->sym_id;
  char buf[10];
  int i;
  for( i = 3; i >= 0; i-- ) {
    buf[i] = '0' + (id & 0x0f);
    id >>= 4;
  }
  id = str_to_symid(sym_name);
  for( i = 7; i >= 4; i-- ) {
    buf[i] = '0' + (id & 0x0f);
    id >>= 4;
  }
  buf[8] = 0;

  mrbc_sym sym_id = str_to_symid(buf);
  mrbc_release(&regs[a]);

  hal_lock();

  mrbc_value *v = mrbc_get_const(sym_id);
  if( v != NULL ) {
    mrbc_dup(v);
    regs[a] = *v;
  } else {
    // raise?
    console_printf( "NameError: uninitialized constant %s::%s\n",
		    symid_to_str( cls->sym_id ), sym_name );
  }

  hal_unlock();
  return 0;
}



//================================================================
/*!@brief
  Execute OP_GETUPVAR

  R(a) = uvget(b,c)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_getupvar( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BBB();

  mrbc_callinfo *callinfo = vm->callinfo_tail;

  // find callinfo
  int n = c * 2 + 1;
  while( n > 0 ){
    callinfo = callinfo->prev;
    n--;
  }

  mrbc_value *up_regs = callinfo->current_regs;

  mrbc_release( &regs[a] );
  mrbc_dup( &up_regs[b] );
  regs[a] = up_regs[b];

  return 0;
}



//================================================================
/*!@brief
  Execute OP_SETUPVAR

  uvset(b,c,R(a))

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_setupvar( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BBB();

  mrbc_callinfo *callinfo = vm->callinfo_tail;

  // find callinfo
  int n = c * 2 + 1;
  while( n > 0 ){
    callinfo = callinfo->prev;
    n--;
  }

  mrbc_value *up_regs = callinfo->current_regs;

  mrbc_release( &up_regs[b] );
  mrbc_dup( &regs[a] );
  up_regs[b] = regs[a];

  return 0;
}



//================================================================
/*!@brief
  Execute OP_JMP

  pc=a

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_jmp( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_S();

  vm->inst = vm->pc_irep->code + a;

  return 0;
}



//================================================================
/*!@brief
  Execute OP_JMPIF

  if R(b) pc=a

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_jmpif( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BS();

  if( regs[a].tt > MRBC_TT_FALSE ) {
    vm->inst = vm->pc_irep->code + b;
  }

  return 0;
}



//================================================================
/*!@brief
  Execute OP_JMPNOT

  if !R(b) pc=a

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_jmpnot( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BS();

  if( regs[a].tt <= MRBC_TT_FALSE ) {
    vm->inst = vm->pc_irep->code + b;
  }

  return 0;
}



//================================================================
/*!@brief
  Execute OP_JMPNIL

  if R(b)==nil pc=a

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_jmpnil( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BS();

  if( regs[a].tt == MRBC_TT_NIL ) {
    vm->inst = vm->pc_irep->code + b;
  }

  return 0;
}



//================================================================
/*!@brief
  Execute OP_ONERR

  rescue_push(a)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_onerr( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_S();

  int idx = vm->exception_idx++;
  vm->exceptions[idx] = a;
  vm->exc_callinfo[idx] = vm->callinfo_tail;
  return 0;
}



//================================================================
/*!@brief
  Execute OP_EXCEPT

  R(a) = exc

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_except( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_B();

  // currently support raise only ( not yet raise "string", raise Exception )
  mrbc_release( &regs[a] );
  regs[a].tt = MRBC_TT_CLASS;
  regs[a].cls = vm->exc;

  return 0;
}



//================================================================
/*!@brief
  Execute OP_RESCUE

  R(b) = R(a).isa?(R(b))

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_rescue( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BB();

  assert( regs[a].tt == MRBC_TT_CLASS );
  assert( regs[b].tt == MRBC_TT_CLASS );
  mrbc_class *cls = regs[a].cls;
  while( cls != NULL ){
    if( regs[b].cls == cls ){
      mrbc_release( &regs[b] );
      regs[b] = mrbc_true_value();
      return 0;
    }
    cls = cls->super;
  }

  mrbc_release( &regs[b] );
  regs[b] = mrbc_false_value();

  return 0;
}



//================================================================
/*!@brief
  Execute OP_POPERR

  a.times{rescue_pop()}

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_poperr( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_B();

  vm->exception_idx -= a;

  return 0;
}



//================================================================
/*!@brief
  Execute OP_RAISE

  raise(R(a))

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_raise( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_B();

  vm->exc = regs[a].cls;
  uint16_t line = vm->exceptions[--vm->exception_idx];
  vm->inst = vm->pc_irep->code + line;

  return 0;
}



//================================================================
/*!@brief
  Execute OP_EPUSH

  ensure_push(SEQ[a])

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_epush( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_B();

  vm->ensures[vm->ensure_idx++] = vm->pc_irep->reps[a];

  return 0;
}



//================================================================
/*!@brief
  Execute OP_EPOP

  A.times{ensure_pop().call}

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_epop( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_B();

  (void)a;   // EPOP <a>

  mrb_irep *block = vm->ensures[--vm->ensure_idx];

  // same as OP_EXEC
  mrbc_push_callinfo(vm, 0, 0);

  // target irep
  vm->pc = 0;
  vm->pc_irep = block;
  vm->inst = block->code;

  // new regs
  //  vm->current_regs += a;

  vm->target_class = find_class_by_object(vm, &regs[0]);

  return 0;
}



//================================================================
/*!@brief
  Method call by method name

  @param  vm    pointer of VM.
  @param  method_name  method name
  @param  regs  pointer to regs
  @param  a     operand a
  @param  b     operand b
  @param  c     operand c
  @param  is_sendb  Is called from OP_SENDB?
  @retval 0  No error.
*/
static inline int op_send_by_name( mrbc_vm *vm, const char *method_name, mrbc_value *regs, uint8_t a, uint8_t b, uint8_t c, int is_sendb )
{
  mrbc_value recv = regs[a];

  // if not OP_SENDB, blcok does not exist
  int bidx = a + c + 1;
  if( !is_sendb ){
    mrbc_release( &regs[bidx] );
    regs[bidx].tt = MRBC_TT_NIL;
  }

  mrbc_sym sym_id = str_to_symid(method_name);
  mrbc_proc *m = find_method(vm, &recv, sym_id);

  if( m == 0 ) {
    mrb_class *cls = find_class_by_object( vm, &recv );
    console_printf("No method. Class:%s Method:%s\n",
		   symid_to_str(cls->sym_id), method_name );
    return 0;
  }

  // m is C func
  if( m->c_func ) {
    m->func(vm, regs + a, c);
    if( m->func == c_proc_call ) return 0;

    int release_reg = a+1;
    while( release_reg <= bidx ) {
      mrbc_release(&regs[release_reg]);
      release_reg++;
    }
    return 0;
  }

  // m is Ruby method.
  // callinfo
  mrbc_push_callinfo(vm, sym_id, c);

  // target irep
  vm->pc = 0;
  vm->pc_irep = m->irep;
  vm->inst = m->irep->code;

  // new regs
  vm->current_regs += a;

  return 0;
}





//================================================================
/*!@brief
  Execute OP_SENDV

  R(a) = call(R(a),Syms(b),*R(a+1))

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
// static inline int op_sendv( mrbc_vm *vm, mrbc_value *regs )
// {
//   FETCH_BB();

//   a = a;
//   b = b;

//   //  const char *sym_name = mrbc_get_irep_symbol(vm->pc_irep->ptr_to_sym, b);

//   return 0;
// }



//================================================================
/*!@brief
  Execute OP_SEND

  R(a) = call(R(a),Syms(b),R(a+1),...,R(a+c))

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_send( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BBB();

  const char *sym_name = mrbc_get_irep_symbol(vm->pc_irep->ptr_to_sym, b);

  return op_send_by_name( vm, sym_name, regs, a, b, c, (vm->inst[-4] == OP_SENDB) );
}



//================================================================
/*!@brief
  Execute OP_SUPER

  R(a) = super(R(a+1),... ,R(a+b+1))

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_super( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BB();

  mrbc_callinfo *callinfo = vm->callinfo_tail;

  int id = callinfo->mid;
  const char *sym_name = symid_to_str(id);

  mrbc_dup( &regs[0] );
  mrbc_release( &regs[a] );
  regs[a] = regs[0];

  // fing super class
  mrbc_class *orig_class = regs[a].instance->cls;
  regs[a].instance->cls = regs[a].instance->cls->super;

  if( b == 127 ){
    // expand array
    assert( regs[a+1].tt == MRBC_TT_ARRAY );

    mrbc_value value = regs[a+1];
    mrbc_dup( &value );
    int argc = value.array->n_stored;
    int i;
    for( i = 0; i < argc; i++ ) {
      mrbc_release( &regs[a+1+i] );
      regs[a+1+i] = value.array->data[i];
    }
    b = argc;
  }
  op_send_by_name(vm, sym_name, regs, a, 0, b, 0);
  regs[a].instance->cls = orig_class;

  return 0;
}





//================================================================
/*!@brief
  Execute OP_ARGARY

  R(a) = argument array (16=m5:r1:m5:d1:lv4)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_argary( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BS();

  int m1 = (b>>11)&0x3f;
  int r = (b >> 10) & 0x01;

  if( r == 0 ){
    int array_size = m1;
    mrbc_value value = mrbc_array_new(vm, array_size);
    memcpy( value.array->data, &regs[1], sizeof(mrbc_value) * array_size );
    memset( &regs[1], 0, sizeof(mrbc_value) * array_size );
    value.array->n_stored = array_size;

    mrbc_release(&regs[a]);
    regs[a] = value;

  }

  return 0;
}



//================================================================
/*!@brief
  Execute OP_ENTER

  arg setup according to flags (23=m5:o5:r1:m5:k5:d1:b1)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
#define MRB_ASPEC_REQ(a)          (((a) >> 18) & 0x1f)
#define MRB_ASPEC_OPT(a)          (((a) >> 13) & 0x1f)
#define MRB_ASPEC_REST(a)         (((a) >> 12) & 0x1)
#define MRB_ASPEC_POST(a)         (((a) >> 7) & 0x1f)
#define MRB_ASPEC_KEY(a)          (((a) >> 2) & 0x1f)
#define MRB_ASPEC_KDICT(a)        ((a) & (1<<1))
#define MRB_ASPEC_BLOCK(a)        ((a) & 1)
static inline int op_enter( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_W();

  int m1 = MRB_ASPEC_REQ(a);   // # of required parameters
  int o  = MRB_ASPEC_OPT(a);   // # of optional parameters
  int r  = MRB_ASPEC_REST(a);  // rest is exists?

  int argc = vm->callinfo_tail->n_args;

  // arg check
  if( argc < m1 ){
    console_printf("ArgumentError\n");  // raise
    return 0;
  }

  // default args, skip bytecode
  if( o > 0 && argc > m1 ){
    vm->inst += (argc - m1) * 3;
  }

  // rest param exists?
  if( r ){
    int rest_size = argc - m1 - o;
    if( rest_size < 0 ) rest_size = 0;
    mrb_value rest = mrbc_array_new(vm, rest_size);
    int i;
    for( i = 0; i < rest_size; i++ ) {
      rest.array->data[i] = regs[1+m1+o+i];
    }
    rest.array->n_stored = rest_size;
    regs[m1+o+1] = rest;
  }

  return 0;
}



//================================================================
/*!@brief
  Execute OP_RETURN

  return R(a) (normal)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_return( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_B();

  mrbc_release(&regs[0]);
  regs[0] = regs[a];
  regs[a].tt = MRBC_TT_EMPTY;

  // nregs to release
  int nregs = vm->pc_irep->nregs;

  // restore irep,pc,regs
  mrbc_pop_callinfo(vm);

  // clear stacked arguments
  int i;
  for( i = 1; i < nregs; i++ ) {
    mrbc_release( &regs[i] );
  }

  return 0;
}




//================================================================
/*!@brief
  Execute OP_RETURN_BLK

  return R(a) (normal)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_return_blk( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_B();

  int nregs = vm->pc_irep->nregs;
  mrbc_irep *caller = vm->irep;

  // trace back to caller
  while( vm->callinfo_tail->pc_irep != caller ){
    nregs += vm->callinfo_tail->n_args;
    mrbc_pop_callinfo(vm);
  }
  mrbc_release(&vm->current_regs[0]);

  // ret value
  vm->current_regs[0] = regs[a];
  regs[a].tt = MRBC_TT_EMPTY;

  mrbc_pop_callinfo(vm);

  // clear stacked arguments
  int i;
  for( i = 1; i < nregs; i++ ) {
    mrbc_release( &regs[i] );
  }

  return 0;
}



//================================================================
/*!@brief
  Execute OP_BREAK

  break R(a)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_break( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_B();

  (void)a;

  // pop until bytecode is OP_SENDB
  mrbc_callinfo *callinfo = vm->callinfo_tail;
  while( callinfo ){
    if( callinfo->inst[-4-callinfo->n_args] == OP_SENDB ){
      // found then return to callinfo
      vm->callinfo_tail = callinfo->prev;
      vm->current_regs = callinfo->current_regs;
      vm->pc_irep = callinfo->pc_irep;
      vm->pc = callinfo->pc;
      vm->inst = callinfo->inst;
      vm->target_class = callinfo->target_class;
      break;
    }
    callinfo = callinfo->prev;
  }

  return 0;
}



//================================================================
/*!@brief
  Execute OP_BLKPUSH

  R(a) = block (16=m5:r1:m5:d1:lv4)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_blkpush( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BS();

  // get m5 where m5:r1:m5:d1:lv4
  int m1 = (b >> 11) & 0x3f;
  int r  = (b >> 10) & 0x01;
  int m2 = (b >>  5) & 0x1f;
  int kd = (b >>  4) & 0x01;
  int lv = (b      ) & 0x0f;

  mrbc_release(&regs[a]);

  int offset = m1+r+m2+kd+1;
  mrbc_value *stack;
  if( lv== 0 ){
    // current env
    stack = regs + offset;
  } else {
    // upper env
    --lv;
    mrbc_callinfo *callinfo = vm->callinfo_tail;
    while( lv > 0 ){
      callinfo = callinfo->prev;
      --lv;
    }
    stack = callinfo->current_regs + 1 - offset;
  }
  regs[a] = *stack;

  return 0;
}



//================================================================
/*!@brief
  Execute OP_ADD

  R(a) = R(a)+R(a+1)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_add( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_B();

  if( regs[a].tt == MRBC_TT_FIXNUM ) {
    if( regs[a+1].tt == MRBC_TT_FIXNUM ) {     // in case of Fixnum, Fixnum
      regs[a].i += regs[a+1].i;
      return 0;
    }
#if MRBC_USE_FLOAT
    if( regs[a+1].tt == MRBC_TT_FLOAT ) {      // in case of Fixnum, Float
      regs[a].tt = MRBC_TT_FLOAT;
      regs[a].d = regs[a].i + regs[a+1].d;
      return 0;
    }
  }
  if( regs[a].tt == MRBC_TT_FLOAT ) {
    if( regs[a+1].tt == MRBC_TT_FIXNUM ) {     // in case of Float, Fixnum
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
  op_send_by_name(vm, "+", regs, a, 0, 1, 0);
  return 0;
}



//================================================================
/*!@brief
  Execute OP_ADDI

  R(a) = R(a)+mrb_int(b)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_addi( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BB();

  if( regs[a].tt == MRBC_TT_FIXNUM ) {
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
/*!@brief
  Execute OP_SUB

  R(a) = R(a)-R(a+1)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_sub( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_B();

  if( regs[a].tt == MRBC_TT_FIXNUM ) {
    if( regs[a+1].tt == MRBC_TT_FIXNUM ) {     // in case of Fixnum, Fixnum
      regs[a].i -= regs[a+1].i;
      return 0;
    }
#if MRBC_USE_FLOAT
    if( regs[a+1].tt == MRBC_TT_FLOAT ) {      // in case of Fixnum, Float
      regs[a].tt = MRBC_TT_FLOAT;
      regs[a].d = regs[a].i - regs[a+1].d;
      return 0;
    }
  }
  if( regs[a].tt == MRBC_TT_FLOAT ) {
    if( regs[a+1].tt == MRBC_TT_FIXNUM ) {     // in case of Float, Fixnum
      regs[a].d -= regs[a+1].i;
      return 0;
    }
    if( regs[a+1].tt == MRBC_TT_FLOAT ) {      // in case of Float, Float
      regs[a].d -= regs[a+1].d;
      return 0;
    }
#endif
  }

  not_supported();

  return 0;
}



//================================================================
/*!@brief
  Execute OP_SUBI

  R(a) = R(a)-mrb_int(b)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_subi( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BB();

  if( regs[a].tt == MRBC_TT_FIXNUM ) {
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
/*!@brief
  Execute OP_MUL

  R(a) = R(a)*R(a+1)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_mul( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_B();

  if( regs[a].tt == MRBC_TT_FIXNUM ) {
    if( regs[a+1].tt == MRBC_TT_FIXNUM ) {     // in case of Fixnum, Fixnum
      regs[a].i *= regs[a+1].i;
      return 0;
    }
    #if MRBC_USE_FLOAT
    if( regs[a+1].tt == MRBC_TT_FLOAT ) {      // in case of Fixnum, Float
      regs[a].tt = MRBC_TT_FLOAT;
      regs[a].d = regs[a].i * regs[a+1].d;
      return 0;
    }
  }
  if( regs[a].tt == MRBC_TT_FLOAT ) {
    if( regs[a+1].tt == MRBC_TT_FIXNUM ) {     // in case of Float, Fixnum
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
  op_send_by_name(vm, "*", regs, a, 0, 1, 0);

  return 0;
}





//================================================================
/*!@brief
  Execute OP_DIV

  R(a) = R(a)/R(a+1)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_div( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_B();

  if( regs[a].tt == MRBC_TT_FIXNUM ) {
    if( regs[a+1].tt == MRBC_TT_FIXNUM ) {     // in case of Fixnum, Fixnum
      regs[a].i /= regs[a+1].i;
      return 0;
    }
    #if MRBC_USE_FLOAT
    if( regs[a+1].tt == MRBC_TT_FLOAT ) {      // in case of Fixnum, Float
      regs[a].tt = MRBC_TT_FLOAT;
      regs[a].d = regs[a].i / regs[a+1].d;
      return 0;
    }
  }
  if( regs[a].tt == MRBC_TT_FLOAT ) {
    if( regs[a+1].tt == MRBC_TT_FIXNUM ) {     // in case of Float, Fixnum
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
  //op_send(vm, code, regs);
  mrbc_release(&regs[a+1]);

  return 0;
}





//================================================================
/*!@brief
  Execute OP_EQ

  R(a) = R(a)==R(a+1)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_eq( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_B();

  int result = mrbc_compare(&regs[a], &regs[a+1]);

  mrbc_release(&regs[a+1]);
  mrbc_release(&regs[a]);
  regs[a].tt = result ? MRBC_TT_FALSE : MRBC_TT_TRUE;

  return 0;
}



//================================================================
/*!@brief
  Execute OP_LT

  R(a) = R(a)<R(a+1)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_lt( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_B();

  int result = 0;

  if( regs[a].tt == MRBC_TT_FIXNUM ) {
    if( regs[a+1].tt == MRBC_TT_FIXNUM ) {
      result = regs[a].i < regs[a+1].i;      // in case of Fixnum, Fixnum
      goto DONE;
    }
#if MRBC_USE_FLOAT
    if( regs[a+1].tt == MRBC_TT_FLOAT ) {
      result = regs[a].i < regs[a+1].d;      // in case of Fixnum, Float
      goto DONE;
    }
  }
  if( regs[a].tt == MRBC_TT_FLOAT ) {
    if( regs[a+1].tt == MRBC_TT_FIXNUM ) {
      result = regs[a].d < regs[a+1].i;      // in case of Float, Fixnum
      goto DONE;
    }
    if( regs[a+1].tt == MRBC_TT_FLOAT ) {
      result = regs[a].d < regs[a+1].d;      // in case of Float, Float
      goto DONE;
    }
#endif
  }

  // TODO: other cases
  //

 DONE:
  regs[a].tt = result ? MRBC_TT_TRUE : MRBC_TT_FALSE;

  return 0;
}



//================================================================
/*!@brief
  Execute OP_LE

  R(a) = R(a)<=R(a+1)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_le( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_B();

  int result = 0;

  if( regs[a].tt == MRBC_TT_FIXNUM ) {
    if( regs[a+1].tt == MRBC_TT_FIXNUM ) {
      result = regs[a].i <= regs[a+1].i;      // in case of Fixnum, Fixnum
      goto DONE;
    }
#if MRBC_USE_FLOAT
    if( regs[a+1].tt == MRBC_TT_FLOAT ) {
      result = regs[a].i <= regs[a+1].d;      // in case of Fixnum, Float
      goto DONE;
    }
  }
  if( regs[a].tt == MRBC_TT_FLOAT ) {
    if( regs[a+1].tt == MRBC_TT_FIXNUM ) {
      result = regs[a].d <= regs[a+1].i;      // in case of Float, Fixnum
      goto DONE;
    }
    if( regs[a+1].tt == MRBC_TT_FLOAT ) {
      result = regs[a].d <= regs[a+1].d;      // in case of Float, Float
      goto DONE;
    }
#endif
  }

  // TODO: other cases
  //

 DONE:
  regs[a].tt = result ? MRBC_TT_TRUE : MRBC_TT_FALSE;

  return 0;
}



//================================================================
/*!@brief
  Execute OP_GT

  R(a) = R(a)>R(a+1)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_gt( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_B();

  int result = 0;

  if( regs[a].tt == MRBC_TT_FIXNUM ) {
    if( regs[a+1].tt == MRBC_TT_FIXNUM ) {
      result = regs[a].i > regs[a+1].i;      // in case of Fixnum, Fixnum
      goto DONE;
    }
#if MRBC_USE_FLOAT
    if( regs[a+1].tt == MRBC_TT_FLOAT ) {
      result = regs[a].i > regs[a+1].d;      // in case of Fixnum, Float
      goto DONE;
    }
  }
  if( regs[a].tt == MRBC_TT_FLOAT ) {
    if( regs[a+1].tt == MRBC_TT_FIXNUM ) {
      result = regs[a].d > regs[a+1].i;      // in case of Float, Fixnum
      goto DONE;
    }
    if( regs[a+1].tt == MRBC_TT_FLOAT ) {
      result = regs[a].d > regs[a+1].d;      // in case of Float, Float
      goto DONE;
    }
#endif
  }

  // TODO: other cases
  //

 DONE:
  regs[a].tt = result ? MRBC_TT_TRUE : MRBC_TT_FALSE;

  return 0;
}



//================================================================
/*!@brief
  Execute OP_GE

  R(a) = R(a)>=R(a+1)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_ge( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_B();

  int result = 0;

  if( regs[a].tt == MRBC_TT_FIXNUM ) {
    if( regs[a+1].tt == MRBC_TT_FIXNUM ) {
      result = regs[a].i >= regs[a+1].i;      // in case of Fixnum, Fixnum
      goto DONE;
    }
#if MRBC_USE_FLOAT
    if( regs[a+1].tt == MRBC_TT_FLOAT ) {
      result = regs[a].i >= regs[a+1].d;      // in case of Fixnum, Float
      goto DONE;
    }
  }
  if( regs[a].tt == MRBC_TT_FLOAT ) {
    if( regs[a+1].tt == MRBC_TT_FIXNUM ) {
      result = regs[a].d >= regs[a+1].i;      // in case of Float, Fixnum
      goto DONE;
    }
    if( regs[a+1].tt == MRBC_TT_FLOAT ) {
      result = regs[a].d >= regs[a+1].d;      // in case of Float, Float
      goto DONE;
    }
#endif
  }

  // TODO: other cases
  //

 DONE:
  regs[a].tt = result ? MRBC_TT_TRUE : MRBC_TT_FALSE;

  return 0;
}



//================================================================
/*!@brief
  Execute OP_ARRAY

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

  mrbc_release(&regs[a]);
  regs[a] = value;

  return 0;
}



//================================================================
/*!@brief
  Execute OP_ARRAY2

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
  for( i=0 ; i<c ; i++ ){
    mrbc_dup( &regs[b+i] );
    value.array->data[i] = regs[b+i];
  }
  value.array->n_stored = c;

  mrbc_release(&regs[a]);
  regs[a] = value;

  return 0;
}



//================================================================
/*!@brief
  Execute OP_ARYCAT

  ary_cat(R(a),R(a+1))

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_arycat( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_B();

  assert( regs[a  ].tt == MRBC_TT_ARRAY );
  assert( regs[a+1].tt == MRBC_TT_ARRAY );

  int size_1 = regs[a  ].array->n_stored;
  int size_2 = regs[a+1].array->n_stored;
  int new_size = size_1 + regs[a+1].array->n_stored;

  // need resize?
  if( regs[a].array->data_size < new_size ){
    mrbc_array_resize(&regs[a], new_size);
  }

  int i;
  for( i = 0; i < size_2; i++ ) {
    mrbc_dup( &regs[a+1].array->data[i] );
    regs[a].array->data[size_1+i] = regs[a+1].array->data[i];
  }
  regs[a].array->n_stored = new_size;

  return 0;
}




//================================================================
/*!@brief
  Execute OP_ARYDUP

  R(a) = ary_dup(R(a))

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_arydup( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_B();

  mrbc_dup( &regs[a] );

  return 0;
}



//================================================================
/*!@brief
  Execute OP_AREF

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

  mrbc_release( dst );

  if( src->tt == MRBC_TT_ARRAY ){
    // src is Array
    *dst = mrbc_array_get(src, c);
    mrbc_dup(dst);
  } else {
    // src is not Array
    if( c == 0 ){
      mrbc_dup(src);
      *dst = *src;
    } else {
      dst->tt = MRBC_TT_NIL;
    }
  }

  return 0;
}



//================================================================
/*!@brief
  Execute OP_APOST

  *R(a),R(a+1)..R(a+c) = R(a)[b..]

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_apost( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BBB();

  mrbc_value src = regs[a];
  if( src.tt != MRBC_TT_ARRAY ){
    src = mrbc_array_new(vm, 1);
    src.array->data[0] = regs[a];
    src.array->n_stored = 1;
  }

  int pre  = b;
  int post = c;
  int len = src.array->n_stored;

  if( len > pre + post ){
    int ary_size = len-pre-post;
    regs[a] = mrbc_array_new(vm, ary_size);
    // copy elements
    int i;
    for( i = 0; i < ary_size; i++ ) {
      regs[a].array->data[i] = src.array->data[pre+i];
      mrbc_dup( &regs[a].array->data[i] );
    }
    regs[a].array->n_stored = ary_size;
  } else {
    // empty
    regs[a] = mrbc_array_new(vm, 0);
  }

  return 0;
}



//================================================================
/*!@brief
  Execute OP_INTERN

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

  mrbc_release( &regs[a] );
  regs[a] = sym_id;

  return 0;
}


//================================================================
/*!@brief
  Execute OP_STRING

  R(a) = str_dup(Lit(b))

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_string( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BB();

#if MRBC_USE_STRING
  mrbc_object *pool_obj = vm->pc_irep->pools[b];

  /* CAUTION: pool_obj->str - 2. see IREP POOL structure. */
  int len = bin_to_uint16(pool_obj->str - 2);
  mrbc_value value = mrbc_string_new(vm, pool_obj->str, len);
  if( value.string == NULL ) return -1;         // ENOMEM

  mrbc_release(&regs[a]);
  regs[a] = value;

#else
  not_supported();
#endif

  return 0;
}



//================================================================
/*!@brief
  Execute OP_STRCAT

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
  mrbc_sym sym_id = str_to_symid("to_s");
  mrbc_proc *m = find_method(vm, &regs[a+1], sym_id);
  if( m && m->c_func ){
    m->func(vm, regs+a+1, 0);
  }

  mrbc_value v = mrbc_string_add(vm, &regs[a], &regs[a+1]);
  mrbc_release(&regs[a]);
  regs[a] = v;

#else
  not_supported();
#endif

  return 0;
}



//================================================================
/*!@brief
  Execute OP_HASH

  R(a) = hash_new(R(a),R(a+1)..R(a+b))

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

  mrbc_release(&regs[a]);
  regs[a] = value;

  return 0;
}



//================================================================
/*!@brief
  Execute OP_BLOCK

  R(a) = lambda(SEQ[b],L_BLOCK)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
// static inline int op_block( mrbc_vm *vm, mrbc_value *regs )
// {
//   FETCH_BB();

//   mrbc_release(&regs[a]);

//   // new proc
//   mrbc_proc *proc = mrbc_rproc_alloc(vm, "");
//   if( !proc ) return 0;	// ENOMEM
//   proc->c_func = 0;
//   proc->sym_id = -1;
//   proc->next = NULL;
//   proc->irep = vm->pc_irep->reps[b];

//   regs[a].tt = MRBC_TT_PROC;
//   regs[a].proc = proc;

//   return 0;
// }




//================================================================
/*!@brief
  Execute OP_METHOD

  R(a) = lambda(SEQ[b],L_METHOD)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_method( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BB();

  mrbc_release(&regs[a]);

  // new proc
  mrbc_proc *proc = mrbc_rproc_alloc(vm, "");
  if( !proc ) return 0;	// ENOMEM
  proc->c_func = 0;
  proc->sym_id = -1;
  proc->next = NULL;
  proc->irep = vm->pc_irep->reps[b];

  regs[a].tt = MRBC_TT_PROC;
  regs[a].proc = proc;

  return 0;
}



//================================================================
/*!@brief
  Execute OP_RANGE_INC, OP_RANGE_EXC

  R(a) = range_new(R(a),R(a+1),FALSE)
  R(a) = range_new(R(a),R(a+1),TRUE)

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_range( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_B();

  mrbc_value value;
  if( vm->inst[-2] == OP_RANGE_INC ){
    value = mrbc_range_new(vm, &regs[a], &regs[a+1], 0);
  } else {
    value = mrbc_range_new(vm, &regs[a], &regs[a+1], 1);
  }

  mrbc_release( &regs[a] );
  regs[a] = value;

  return 0;
}



//================================================================
/*!@brief
  Execute OP_CLASS

  R(a) = newclass(R(a),Syms(b),R(a+1))

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_class( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BB();

  mrbc_irep *cur_irep = vm->pc_irep;
  const char *sym_name = mrbc_get_irep_symbol(cur_irep->ptr_to_sym, b);
  mrbc_class *super = (regs[a+1].tt == MRBC_TT_CLASS) ? regs[a+1].cls : mrbc_class_object;

  mrbc_class *cls = mrbc_define_class(vm, sym_name, super);

  mrbc_value ret = {.tt = MRBC_TT_CLASS};
  ret.cls = cls;

  regs[a] = ret;

  return 0;
}



//================================================================
/*!@brief
  Execute OP_EXEC

  R(a) = blockexec(R(a),SEQ[b])

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_exec( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BB();

  mrbc_value recv = regs[a];

  // prepare callinfo
  mrbc_push_callinfo(vm, 0, 0);

  // target irep
  vm->pc = 0;
  vm->pc_irep = vm->irep->reps[b];
  vm->inst = vm->pc_irep->code;

  // new regs
  vm->current_regs += a;

  vm->target_class = find_class_by_object(vm, &recv);

  return 0;
}




//================================================================
/*!@brief
  Execute OP_DEF

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
  const char *sym_name = mrbc_get_irep_symbol(vm->pc_irep->ptr_to_sym, b);
  mrbc_sym sym_id = str_to_symid(sym_name);
  mrbc_proc *proc = regs[a+1].proc;

  mrbc_set_vm_id(proc, 0);
  proc->sym_id = sym_id;
#ifdef MRBC_DEBUG
  proc->names = sym_name;
#endif

  // add to class
  proc->next = cls->procs;
  cls->procs = proc;

  // checking same method
  for( ;proc->next != NULL; proc = proc->next ) {
    if( proc->next->sym_id == sym_id ) {
      // Found it. Unchain it in linked list and remove.
      mrbc_proc *del_proc = proc->next;
      proc->next = proc->next->next;
      mrbc_raw_free( del_proc );
      break;
    }
  }

  regs[a+1].tt = MRBC_TT_EMPTY;
  return 0;
}



//================================================================
/*!@brief
  Execute OP_ALIAS

  alias_method(target_class,Syms(a),Syms(b))

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_alias( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_BB();

  const char *sym_name_a = mrbc_get_irep_symbol(vm->pc_irep->ptr_to_sym, a);
  mrbc_sym sym_id_a = str_to_symid(sym_name_a);
  const char *sym_name_b = mrbc_get_irep_symbol(vm->pc_irep->ptr_to_sym, b);
  mrbc_sym sym_id_b = str_to_symid(sym_name_b);

  // find method only in this class.
  mrb_proc *proc = vm->target_class->procs;
  while( proc != NULL ) {
    if( proc->sym_id == sym_id_b ) break;
    proc = proc->next;
  }
  if( !proc ) {
    console_printf("NameError: undefined_method '%s'\n", sym_name_b);
    return 0;
  }

  // copy the Proc object
  mrbc_proc *proc_alias = mrbc_alloc(0, sizeof(mrbc_proc));
  if( !proc_alias ) return 0;		// ENOMEM
  memcpy( proc_alias, proc, sizeof(mrbc_proc) );

  // register procs link.
  proc_alias->sym_id = sym_id_a;
#if defined(MRBC_DEBUG)
  proc_alias->names = sym_name_a;
#endif
  proc_alias->next = vm->target_class->procs;
  vm->target_class->procs = proc_alias;

  return 0;
}


//================================================================
/*!@brief
  Execute OP_SCLASS

  R(A) := R(B).singleton_class

  @param  vm    A pointer of VM.
  @param  code  bytecode
  @param  regs  vm->regs + vm->reg_top
  @retval 0  No error.
*/
static inline int op_sclass( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_B();
  // currently, not supported
  (void)a;

  return 0;
}



//================================================================
/*!@brief
  Execute OP_TCLASS

  R(a) = target_class

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval 0  No error.
*/
static inline int op_tclass( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_B();

  mrbc_release(&regs[a]);
  regs[a].tt = MRBC_TT_CLASS;
  regs[a].cls = vm->target_class;

  return 0;
}



//================================================================
/*!@brief
  Execute OP_EXT1, OP_EXT2, OP_EXT3

  if OP_EXT1, make 1st operand 16bit
  if OP_EXT2, make 2nd operand 16bit
  if OP_EXT3, make 1st and 2nd operand 16bit

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval -1  No error and exit from vm.
*/
static inline int op_ext( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_Z();

  vm->ext_flag = vm->inst[-1] - OP_EXT1 + 1;

  return 0;
}


//================================================================
/*!@brief
  Execute OP_STOP

  stop VM

  @param  vm    pointer of VM.
  @param  regs  pointer to regs
  @retval -1  No error and exit from vm.
*/
static inline int op_stop( mrbc_vm *vm, mrbc_value *regs )
{
  FETCH_Z();

  if( vm->inst[-1] == OP_STOP ) {
    int i;
    for( i = 0; i < MAX_REGS_SIZE; i++ ) {
      mrbc_release(&vm->regs[i]);
    }
  }

  vm->flag_preemption = 1;

  return -1;
}


//================================================================
/*!@brief
  Execute OP_ABORT

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
/*!@brief
  Open the VM.

  @param vm     Pointer to mrbc_vm or NULL.
  @return	Pointer to mrbc_vm.
  @retval NULL	error.
*/
mrbc_vm *mrbc_vm_open( struct VM *vm_arg )
{
  mrbc_vm *vm = vm_arg;

  if( vm == NULL ) {
    // allocate memory.
    vm = (mrbc_vm *)mrbc_raw_alloc( sizeof(mrbc_vm) );
    if( vm == NULL ) return NULL;
  }

  // allocate vm id.
  int vm_id;
  hal_lock();
  for( vm_id = 0; vm_id < MAX_VM_COUNT; vm_id++ ) {
    int idx = vm_id >> 4;
    int bit = 1 << (vm_id & 0x0f);
    if( (free_vm_bitmap[idx] & bit) == 0 ) {
      free_vm_bitmap[idx] |= bit;		// found
      break;
    }
  }
  hal_unlock();
  if( vm_id == MAX_VM_COUNT ) {
    mrbc_raw_free(vm);
    return NULL;
  }
  vm_id++;

  // initialize attributes.
  memset(vm, 0, sizeof(mrbc_vm));	// caution: assume NULL is zero.
  if( vm_arg == NULL ) vm->flag_need_memfree = 1;
  vm->vm_id = vm_id;

#ifdef MRBC_DEBUG
  vm->flag_debug_mode = 1;
#endif

  return vm;
}



//================================================================
/*!@brief
  Close the VM.

  @param  vm  Pointer to VM
*/
void mrbc_vm_close( struct VM *vm )
{
  // free vm id.
  int idx = (vm->vm_id-1) >> 4;
  int bit = 1 << ((vm->vm_id-1) & 0x0f);
  hal_lock();
  free_vm_bitmap[idx] &= ~bit;
  hal_unlock();

  // free irep and vm
  if( vm->irep ) mrbc_irep_free( vm->irep );
  if( vm->flag_need_memfree ) mrbc_raw_free(vm);
}



//================================================================
/*!@brief
  VM initializer.

  @param  vm  Pointer to VM
*/
void mrbc_vm_begin( struct VM *vm )
{
  vm->pc_irep = vm->irep;
  vm->inst = vm->pc_irep->code;
  vm->current_regs = vm->regs;
  memset(vm->regs, 0, sizeof(vm->regs));

  // clear regs
  int i;
  for( i = 1; i < MAX_REGS_SIZE; i++ ) {
    vm->regs[i].tt = MRBC_TT_NIL;
  }

  // set self to reg[0]
  // create instance of Object
  mrbc_value v;
  v.tt = MRBC_TT_OBJECT;
  v.instance = (mrbc_instance *)mrbc_alloc(vm, sizeof(mrbc_instance));
  if( v.instance == NULL ) return;	// ENOMEM

  if( mrbc_kv_init_handle(vm, &v.instance->ivar, 0) != 0 ) {
    mrbc_raw_free(v.instance);
    v.instance = NULL;
    return;
  }

  v.instance->ref_count = 1;
  v.instance->tt = MRBC_TT_OBJECT;	// for debug only.
  v.instance->cls = mrbc_class_object;
  vm->regs[0] = v;

  // Empty callinfo
  vm->callinfo_tail = NULL;

  // target_class
  vm->target_class = mrbc_class_object;

  vm->error_code = 0;
  vm->flag_preemption = 0;
}


//================================================================
/*!@brief
  VM finalizer.

  @param  vm  Pointer to VM
*/
void mrbc_vm_end( struct VM *vm )
{
  mrbc_global_clear_vm_id();
  mrbc_free_all(vm);
}





//================================================================
/*!@brief
  output op for debug

  @param  opcode   opcode
*/
#ifdef MRBC_DEBUG
void output_opcode( uint8_t opcode )
{
  const char *n[] = {
    // 0x00
    "NOP",     "MOVE",    "LOADL",   "LOADI",
    "LOADINEG","LOADI__1","LOADI_0", "LOADI_1",
    "LOADI_2", "LOADI_3", "LOADI_4", "LOADI_5",
    "LOADI_6", "LOADI_7", "LOADSYM", "LOADNIL",
    // 0x10
    "LOADSELF","LOADT",   "LOADF",   "GETGV",
    "SETGV",   0,         0,         "GETIV",
    "SETIV",   0,         0,         "GETCONST",
    "SETCONST",0,         0,         "GETUPVAR",
    // 0x20
    "SETUPVAR","JMP",     "JMPIF",   "JMPNOT",
    "JMPNIL",  "ONERR",   0,         0,
    0,         0,         0,         0,
    "SENDV",   0,         "SEND",    "SENDB",
    // 0x30
    0,         "SUPER",   "ARGARY",  "ENTER",
    0,         0,         0,         "RETURN",
    "RETRUN_BLK","BREAK", "BLKPUSH", "ADD",
    "ADDI",    "SUB",     "SUBI",    "MUL",
    // 0x40
    "DIV",     "EQ",      "LT",      "LE",
    "GT",      "GE",      "ARRAY",   "ARRAY2",
    "ARYCAT",  "",        "ARYDUP",  "AREF",
    0,         "APOST",   0,         "STRING",
    // 0x50
    "STRCAT",  "HASH",    0,         0,
    0,         "BLOCK",   "METHOD",  "RANGE_INC",
    "RANGE_EXC", 0,       "CLASS",   0,
    "EXEC",    "DEF",     0,         0,
    // 0x60
    "",        "TCLASS",  "",        "",
    "EXT1",    "EXT2",    "EXT3",    "STOP",
    "ABORT",
  };

  if( opcode < sizeof(n)/sizeof(char *) ){
    if( n[opcode] ){
      console_printf("(OP_%s)\n", n[opcode]);
    } else {
      console_printf("(OP=%02x)\n", opcode);
    }
  } else {
    console_printf("(ERROR=%02x)\n", opcode);
  }
}
#endif



//================================================================
/*!@brief
  Fetch a bytecode and execute

  @param  vm    A pointer of VM.
  @retval 0  No error.
*/
int mrbc_vm_run( struct VM *vm )
{
  int ret = 0;

  do {
    // regs
    mrbc_value *regs = vm->current_regs;

    // Dispatch
    uint8_t op = *vm->inst++;

    // output OP_XXX for debug
    //if( vm->flag_debug_mode )output_opcode( op );

    switch( op ) {
    case OP_NOP:        ret = op_nop       (vm, regs); break;
    case OP_MOVE:       ret = op_move      (vm, regs); break;
    case OP_LOADL:      ret = op_loadl     (vm, regs); break;
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
    case OP_LOADSYM:    ret = op_loadsym   (vm, regs); break;
    case OP_LOADNIL:    ret = op_loadnil   (vm, regs); break;
    case OP_LOADSELF:   ret = op_loadself  (vm, regs); break;
    case OP_LOADT:      ret = op_loadt     (vm, regs); break;
    case OP_LOADF:      ret = op_loadf     (vm, regs); break;
    case OP_GETGV:      ret = op_getgv     (vm, regs); break;
    case OP_SETGV:      ret = op_setgv     (vm, regs); break;

    case OP_GETIV:      ret = op_getiv     (vm, regs); break;
    case OP_SETIV:      ret = op_setiv     (vm, regs); break;

    case OP_GETCONST:   ret = op_getconst  (vm, regs); break;
    case OP_SETCONST:   ret = op_setconst  (vm, regs); break;
    case OP_GETMCNST:   ret = op_getmcnst  (vm, regs); break;

    case OP_GETUPVAR:   ret = op_getupvar  (vm, regs); break;
    case OP_SETUPVAR:   ret = op_setupvar  (vm, regs); break;
    case OP_JMP:        ret = op_jmp       (vm, regs); break;
    case OP_JMPIF:      ret = op_jmpif     (vm, regs); break;
    case OP_JMPNOT:     ret = op_jmpnot    (vm, regs); break;
    case OP_JMPNIL:     ret = op_jmpnil    (vm, regs); break;
    case OP_ONERR:      ret = op_onerr     (vm, regs); break;
    case OP_EXCEPT:     ret = op_except    (vm, regs); break;
    case OP_RESCUE:     ret = op_rescue    (vm, regs); break;
    case OP_POPERR:     ret = op_poperr    (vm, regs); break;
    case OP_RAISE:      ret = op_raise     (vm, regs); break;
    case OP_EPUSH:      ret = op_epush     (vm, regs); break;
    case OP_EPOP:       ret = op_epop      (vm, regs); break;

      //    case OP_SENDV:      ret = op_sendv     (vm, regs); break;

    case OP_SEND:       // fall through
    case OP_SENDB:      ret = op_send      (vm, regs); break;

    case OP_SUPER:      ret = op_super     (vm, regs); break;
    case OP_ARGARY:     ret = op_argary    (vm, regs); break;
    case OP_ENTER:      ret = op_enter     (vm, regs); break;

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

    case OP_ARYDUP:     ret = op_arydup    (vm, regs); break;
    case OP_AREF:       ret = op_aref      (vm, regs); break;

    case OP_APOST:      ret = op_apost     (vm, regs); break;
    case OP_INTERN:     ret = op_intern    (vm, regs); break;
    case OP_STRING:     ret = op_string    (vm, regs); break;
    case OP_STRCAT:     ret = op_strcat    (vm, regs); break;
    case OP_HASH:       ret = op_hash      (vm, regs); break;

    case OP_BLOCK:      // fall through
    case OP_METHOD:     ret = op_method    (vm, regs); break;
    case OP_RANGE_INC:  // fall through
    case OP_RANGE_EXC:  ret = op_range     (vm, regs); break;

    case OP_CLASS:      ret = op_class     (vm, regs); break;

    case OP_EXEC:       ret = op_exec      (vm, regs); break;
    case OP_DEF:        ret = op_def       (vm, regs); break;
    case OP_ALIAS:      ret = op_alias     (vm, regs); break;

    case OP_SCLASS:     ret = op_sclass    (vm, regs); break;
    case OP_TCLASS:     ret = op_tclass    (vm, regs); break;

    case OP_EXT1:       // fall through
    case OP_EXT2:       // fall through
    case OP_EXT3:       ret = op_ext       (vm, regs); break;

    case OP_STOP:       ret = op_stop      (vm, regs); break;
    case OP_ABORT:      ret = op_abort     (vm, regs); break;
    default:
      console_printf("Skip OP=%02x\n", op);
      break;
    }
  } while( !vm->flag_preemption );

  vm->flag_preemption = 0;

  return ret;
}
