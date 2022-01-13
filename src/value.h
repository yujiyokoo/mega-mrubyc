/*! @file
  @brief
  mruby/c value definitions

  <pre>
  Copyright (C) 2015-2021 Kyushu Institute of Technology.
  Copyright (C) 2015-2021 Shimane IT Open-Innovation Center.

  This file is distributed under BSD 3-Clause License.


  </pre>
*/

#ifndef MRBC_SRC_VALUE_H_
#define MRBC_SRC_VALUE_H_

/***** Feature test switches ************************************************/
/***** System headers *******************************************************/
#include <stdint.h>
#include <assert.h>
#include "vm_config.h"

/***** Local headers ********************************************************/

#ifdef __cplusplus
extern "C" {
#endif
/***** Constant values ******************************************************/
/***** Typedefs *************************************************************/
// pre define of some struct
struct VM;
struct RClass;
struct RObject;
struct IREP;

// mrbc types
#if defined(MRBC_INT16)
typedef int16_t mrbc_int;
#elif defined(MRBC_INT64)
typedef int64_t mrbc_int;
#else
typedef int32_t mrbc_int;
#endif
typedef mrbc_int mrb_int;

#if MRBC_USE_FLOAT == 1
typedef float mrbc_float;
typedef float mrb_float;
#elif MRBC_USE_FLOAT == 2
typedef double mrbc_float;
typedef double mrb_float;
#endif
typedef int16_t mrbc_sym;
typedef void (*mrbc_func_t)(struct VM *vm, struct RObject *v, int argc);


//================================================================
/*! define the value type.

  @note  Must be same order as mrbc_class_tbl[], mrbc_delfunc[].
*/
typedef enum {
  /* internal use */
  MRBC_TT_JMPUW      = -3,  // use in OP_JMPUW
  MRBC_TT_RETBLK     = -2,  // use in OP_RETURN, OP_RETURN_BLK, OP_BREAK
  MRBC_TT_HANDLE     = -1,

  /* primitive */
  MRBC_TT_EMPTY	  = 0,
  MRBC_TT_NIL	  = 1,
  MRBC_TT_FALSE	  = 2,	// (note) true/false threshold. see op_jmpif

  MRBC_TT_TRUE	  = 3,
  MRBC_TT_INTEGER = 4,
  MRBC_TT_FIXNUM  = 4,
  MRBC_TT_FLOAT	  = 5,
  MRBC_TT_SYMBOL  = 6,
  MRBC_TT_CLASS	  = 7,

  /* non-primitive */
  MRBC_TT_OBJECT    = 8,	// (note) inc/dec ref threshold.
  MRBC_TT_PROC	    = 9,
  MRBC_TT_ARRAY	    = 10,
  MRBC_TT_STRING    = 11,
  MRBC_TT_RANGE	    = 12,
  MRBC_TT_HASH	    = 13,
  MRBC_TT_EXCEPTION = 14,
} mrbc_vtype;
#define	MRBC_TT_INC_DEC_THRESHOLD MRBC_TT_OBJECT
#define	MRBC_TT_MAXVAL MRBC_TT_EXCEPTION


//================================================================
/*! define the error code. (BETA TEST)
*/
typedef enum {
  E_NOMEMORY_ERROR = 1,
  E_RUNTIME_ERROR,
  E_TYPE_ERROR,
  E_ARGUMENT_ERROR,
  E_INDEX_ERROR,
  E_RANGE_ERROR,
  E_NAME_ERROR,
  E_NOMETHOD_ERROR,
  E_SCRIPT_ERROR,
  E_SYNTAX_ERROR,
  E_LOCALJUMP_ERROR,
  E_REGEXP_ERROR,
  E_NOTIMP_ERROR,
  E_FLOATDOMAIN_ERROR,
  E_KEY_ERROR,

  // Internal Error
  E_BYTECODE_ERROR,
} mrbc_error_code;


//================================================================
/*! Define the object structure having reference counter.
*/
#if defined(MRBC_DEBUG)
#define MRBC_OBJECT_HEADER  uint8_t type[2]; uint16_t ref_count;
#else
#define MRBC_OBJECT_HEADER  uint16_t ref_count;
#endif

struct RBasic {
  MRBC_OBJECT_HEADER;
};


//================================================================
/*! mruby/c value object.
*/
struct RObject {
  mrbc_vtype tt : 8;
  union {
    mrbc_int i;			// MRBC_TT_INTEGER, SYMBOL
#if MRBC_USE_FLOAT
    mrbc_float d;		// MRBC_TT_FLOAT
#endif
    struct RBasic *obj;		// use inc/dec ref only.
    struct RClass *cls;		// MRBC_TT_CLASS
    struct RInstance *instance;	// MRBC_TT_OBJECT
    struct RProc *proc;		// MRBC_TT_PROC
    struct RArray *array;	// MRBC_TT_ARRAY
    struct RString *string;	// MRBC_TT_STRING
    struct RRange *range;	// MRBC_TT_RANGE
    struct RHash *hash;		// MRBC_TT_HASH
    struct RException *exception; // MRBC_TT_EXCEPTION
    void *handle;		// internal use only.
    const uint8_t *jmpuw;       // jump point from break
  };
};
typedef struct RObject mrb_object;	// not recommended.
typedef struct RObject mrb_value;	// not recommended.
typedef struct RObject mrbc_object;
typedef struct RObject mrbc_value;


/***** Macros ***************************************************************/

// getters
#define mrbc_type(o)		((o).tt)
#define mrbc_integer(o)		((o).i)
#define mrbc_float(o)		((o).d)
#define mrbc_symbol(o)		((o).i)

// setters
#define mrbc_set_integer(p,n)	(p)->tt = MRBC_TT_INTEGER; (p)->i = (n)
#define mrbc_set_float(p,n)	(p)->tt = MRBC_TT_FLOAT; (p)->d = (n)
#define mrbc_set_nil(p)		(p)->tt = MRBC_TT_NIL
#define mrbc_set_true(p)	(p)->tt = MRBC_TT_TRUE
#define mrbc_set_false(p)	(p)->tt = MRBC_TT_FALSE
#define mrbc_set_bool(p,n)	(p)->tt = (n)? MRBC_TT_TRUE: MRBC_TT_FALSE
#define mrbc_set_symbol(p,n)	(p)->tt = MRBC_TT_SYMBOL; (p)->i = (n)

// make immediate values.
#define mrbc_integer_value(n)	((mrbc_value){.tt = MRBC_TT_INTEGER, .i=(n)})
#define mrbc_float_value(vm,n)	((mrbc_value){.tt = MRBC_TT_FLOAT, .d=(n)})
#define mrbc_nil_value()	((mrbc_value){.tt = MRBC_TT_NIL})
#define mrbc_true_value()	((mrbc_value){.tt = MRBC_TT_TRUE})
#define mrbc_false_value()	((mrbc_value){.tt = MRBC_TT_FALSE})
#define mrbc_bool_value(n)	((mrbc_value){.tt = (n)?MRBC_TT_TRUE:MRBC_TT_FALSE})
#define mrbc_symbol_value(n)	((mrbc_value){.tt = MRBC_TT_SYMBOL, .i=(n)})

// (for mruby compatible)
#define mrb_type(o)		mrbc_type(o)
#define mrb_integer(o)		mrbc_integer(o)
#define mrb_float(o)		mrbc_float(o)
#define mrb_symbol(o)		mrbc_symbol(o)
#define mrb_integer_value(n)	mrbc_integer_value(n)
#define mrb_float_value(vm,n)	mrbc_float_value(vm,n)
#define mrb_nil_value()		mrbc_nil_value()
#define mrb_true_value()	mrbc_true_value()
#define mrb_false_value()	mrbc_false_value()
#define mrb_bool_value(n)	mrbc_bool_value(n)
#define mrb_symbol_value(n)	mrbc_symbol_value(n)

// (for mruby/c 2 compatibility)
#define mrbc_fixnum(o)		mrbc_integer(o)
#define mrbc_set_fixnum(p,n)	mrbc_set_integer(p,n)
#define mrbc_fixnum_value(n)	mrbc_integer_value(n)
#define mrb_fixnum(o)		mrbc_integer(o)
#define mrb_fixnum_value(n)	mrbc_integer_value(n)



// for C call
#define SET_RETURN(n) do {	\
    mrbc_value nnn = (n);	\
    mrbc_decref(v);		\
    v[0] = nnn;			\
  } while(0)
#define SET_NIL_RETURN() do {	\
    mrbc_decref(v);		\
    v[0].tt = MRBC_TT_NIL;	\
  } while(0)
#define SET_FALSE_RETURN() do { \
    mrbc_decref(v);		\
    v[0].tt = MRBC_TT_FALSE;	\
  } while(0)
#define SET_TRUE_RETURN() do {	\
    mrbc_decref(v);		\
    v[0].tt = MRBC_TT_TRUE;	\
  } while(0)
#define SET_BOOL_RETURN(n) do {			 \
    int tt = (n) ? MRBC_TT_TRUE : MRBC_TT_FALSE; \
    mrbc_decref(v);				 \
    v[0].tt = tt;				 \
  } while(0)
#define SET_INT_RETURN(n) do {	\
    mrbc_int nnn = (n);		\
    mrbc_decref(v);		\
    v[0].tt = MRBC_TT_INTEGER;	\
    v[0].i = nnn;		\
  } while(0)
#define SET_FLOAT_RETURN(n) do {\
    mrbc_float nnn = (n);	\
    mrbc_decref(v);		\
    v[0].tt = MRBC_TT_FLOAT;	\
    v[0].d = nnn;		\
} while(0)

#define GET_TT_ARG(n)		(v[(n)].tt)
#define GET_INT_ARG(n)		(v[(n)].i)
#define GET_ARY_ARG(n)		(v[(n)])
#define GET_ARG(n)		(v[(n)])
#define GET_FLOAT_ARG(n)	(v[(n)].d)
#define GET_STRING_ARG(n)	(v[(n)].string->data)


#if defined(MRBC_DEBUG)
#define MRBC_INIT_OBJECT_HEADER(p, t)  (p)->ref_count = 1; (p)->type[0] = (t)[0]; (p)->type[1] = (t)[1]
#else
#define MRBC_INIT_OBJECT_HEADER(p, t)  (p)->ref_count = 1
#endif


/***** Global variables *****************************************************/
extern void (* const mrbc_delfunc[])(mrbc_value *);


/***** Function prototypes **************************************************/
int mrbc_compare(const mrbc_value *v1, const mrbc_value *v2);
void mrbc_clear_vm_id(mrbc_value *v);
mrbc_int mrbc_atoi(const char *s, int base);


/***** Inline functions *****************************************************/

//================================================================
/*! Increment reference counter

  @param   v     Pointer to mrbc_value
*/
static inline void mrbc_incref(mrbc_value *v)
{
  if( v->tt < MRBC_TT_INC_DEC_THRESHOLD ) return;

  assert( v->obj->ref_count != 0 );
  assert( v->obj->ref_count != 0xff );	// check max value.
  v->obj->ref_count++;
}


//================================================================
/*! Decrement reference counter

  @param   v     Pointer to target mrbc_value
*/
static inline void mrbc_decref(mrbc_value *v)
{
  if( v->tt < MRBC_TT_INC_DEC_THRESHOLD ) return;

  assert( v->obj->ref_count != 0 );
  assert( v->obj->ref_count != 0xffff );	// check broken data.

  if( --v->obj->ref_count != 0 ) return;

  (*mrbc_delfunc[ v->tt - MRBC_TT_INC_DEC_THRESHOLD ])(v);
}


//================================================================
/*! Decrement reference counter with set TT_EMPTY.

  @param   v     Pointer to target mrbc_value
*/
static inline void mrbc_decref_empty(mrbc_value *v)
{
  mrbc_decref(v);
  v->tt = MRBC_TT_EMPTY;
}


#ifdef __cplusplus
}
#endif
#endif
