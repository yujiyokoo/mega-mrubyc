/*! @file
  @brief
  mruby bytecode loader.

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
#include "vm.h"
#include "load.h"
#include "value.h"
#include "alloc.h"
#include "symbol.h"
#include "c_string.h"


/***** Constat values *******************************************************/
// for mrb file structure.
static const char IDENT[8] = "RITE0200";
static const int SIZE_RITE_BINARY_HEADER = 20;
static const int SIZE_RITE_SECTION_HEADER = 12;
static const char IREP[4] = "IREP";
static const char END[4] = "END\0";


/*! IREP TT */
enum irep_pool_type {
  IREP_TT_STR   = 0,	// string (need free)
  IREP_TT_SSTR  = 2,	// string (static)
  IREP_TT_INT32 = 1,	// 32bit integer
  IREP_TT_INT64 = 3,	// 64bit integer
  IREP_TT_FLOAT = 5,	// float (double/float)
};


/***** Macros ***************************************************************/
/***** Typedefs *************************************************************/
/***** Function prototypes **************************************************/
/***** Local variables ******************************************************/
/***** Global variables *****************************************************/
/***** Signal catching functions ********************************************/
/***** Local functions ******************************************************/

//================================================================
/*! Get double (64bit) value from byte array.

  @param  p	Pointer to byte array.
  @return	double value.
*/
static inline double bin_to_double64( const void *p )
{
  double value;

#if defined(MRBC_LITTLE_ENDIAN) && !defined(MRBC_REQUIRE_32BIT_ALIGNMENT)
  // Little endian, no alignment.
  //  e.g. ARM Cortex-M3, Intel x86
  memcpy(&value, p, 8);

#elif defined(MRBC_BIG_ENDIAN) && !defined(MRBC_REQUIRE_32BIT_ALIGNMENT)
  // Big endian, no alignment.
  //  e.g. IBM PPC405
  int i;
  uint8_t *p2 = (uint8_t*)&value;
  for( i = 7; i >= 0; i-- ) {
    p2[i] = *(const char *)p++;
  }

#elif defined(MRBC_LITTLE_ENDIAN) && defined(MRBC_REQUIRE_32BIT_ALIGNMENT)
  // Little endian, 32bit alignment required.
  //  e.g. ARM Cortex-M0
  int i;
  uint8_t *p2 = (uint8_t*)&value;
  for( i = 7; i >= 0; i-- ) {
    *p2++ = *(const char *)p++;
  }

#elif defined(MRBC_BIG_ENDIAN) && defined(MRBC_REQUIRE_32BIT_ALIGNMENT)
  // Big endian, 32bit alignment required.
  //  e.g. OpenRISC
  int i;
  uint8_t *p2 = (uint8_t*)&value;
  for( i = 7; i >= 0; i-- ) {
    p2[i] = *(const char *)p++;
  }

#else
  #error "Specify MRBC_BIG_ENDIAN or MRBC_LITTLE_ENDIAN"
#endif

  return value;
}


#if defined(MRBC_INT64)
//================================================================
/*! Get 64bit int value from byte array.

  @param  p	Pointer to byte array.
  @return	double value.
*/
static inline int64_t bin_to_int64( const void *p )
{
  int64_t value;

#if defined(MRBC_LITTLE_ENDIAN) && !defined(MRBC_REQUIRE_32BIT_ALIGNMENT)
  // Little endian, no alignment.
  //  e.g. ARM Cortex-M3, Intel x86
  int i;
  uint8_t *p2 = (uint8_t*)&value;
  for( i = 7; i >= 0; i-- ) {
    p2[i] = *(const char *)p++;
  }

#elif defined(MRBC_BIG_ENDIAN) && !defined(MRBC_REQUIRE_32BIT_ALIGNMENT)
  // Big endian, no alignment.
  //  e.g. IBM PPC405
  memcpy(&value, p, 8);

#elif defined(MRBC_LITTLE_ENDIAN) && defined(MRBC_REQUIRE_32BIT_ALIGNMENT)
  // Little endian, 32bit alignment required.
  //  e.g. ARM Cortex-M0
  int i;
  uint8_t *p2 = (uint8_t*)&value;
  for( i = 7; i >= 0; i-- ) {
    p2[i] = *(const char *)p++;
  }

#elif defined(MRBC_BIG_ENDIAN) && defined(MRBC_REQUIRE_32BIT_ALIGNMENT)
  // Big endian, 32bit alignment required.
  //  e.g. OpenRISC
  int i;
  uint8_t *p2 = (uint8_t*)&value;
  for( i = 7; i >= 0; i-- ) {
    *p2++ = *(const char *)p++;
  }

#else
  #error "Specify MRBC_BIG_ENDIAN or MRBC_LITTLE_ENDIAN"
#endif

  return value;
}
#endif


//================================================================
/*! Parse header section.

  @param  vm    A pointer to VM.
  @param  bin	A pointer to RITE header.
  @return int	zero if no error.

  <pre>
  Structure
   "RITE"     identifier
   "01"       major version
   "00"       minor version
   0000_0000  total size
   "MATZ"     compiler name
   "0000"     compiler version
  </pre>
*/
static int load_header(struct VM *vm, const uint8_t *bin)
{
  if( memcmp(bin, IDENT, sizeof(IDENT)) != 0 ) return -1;

  /* Ignore others. */

  return 0;
}


//================================================================
/*! read one irep section.

  @param  vm	A pointer to VM.
  @param  bin	A pointer to RITE ISEQ.
  @param  len	Returns the parsed length.
  @param  flag_top	is irep top level?
  @return	Pointer to allocated mrbc_irep or NULL

  <pre>
   (loop n of child irep bellow)
   0000_0000	record size
   0000		n of local variable
   0000		n of register
   0000		n of child irep
   0000		n of catch handler
   0000		n of byte code  (ISEQ BLOCK)
   ...		byte codes

   0000		n of pool	(POOL BLOCK)
   (loop n of pool)
     00		type
     ...	pool data

   0000		n of symbol	(SYMS BLOCK)
   (loop n of symbol)
     0000	length
     ...	symbol data
  </pre>
*/
static mrbc_irep * load_irep_1(struct VM *vm, const uint8_t *bin, int *len, int flag_top)
{
  mrbc_irep irep;
  const uint8_t *p = bin + 4;	// 4 = skip record size.
  int i;

#if defined(MRBC_DEBUG)
  irep.type[0] = 'R';	// set "RP"
  irep.type[1] = 'P';
#endif

  irep.nlocals = bin_to_uint16(p);	p += 2;
  irep.nregs = bin_to_uint16(p);	p += 2;
  irep.rlen = bin_to_uint16(p);		p += 2;
  irep.clen = bin_to_uint16(p);		p += 2;
  irep.ilen = bin_to_uint16(p);		p += 2;
  irep.inst = (uint8_t *)p;
  assert( sizeof(mrbc_irep_catch_handler) == 13 );

  // POOL block
  p += irep.ilen + sizeof(mrbc_irep_catch_handler) * irep.clen;
  irep.pool = p;
  irep.plen = bin_to_uint16(p);		p += 2;

  // skip pool
  for( i = 0; i < irep.plen; i++ ) {
    int siz = 0;
    switch( *p++ ) {
    case IREP_TT_STR:
    case IREP_TT_SSTR:	siz = bin_to_uint16(p) + 3;	break;
    case IREP_TT_INT32:	siz = 4;	break;
    case IREP_TT_INT64:
    case IREP_TT_FLOAT:	siz = 8;	break;
    default:
      assert(!"Loader unknown TT found.");
      return NULL;
    }
    p += siz;
  }

  // # of symbols, offset of tbl_ireps.
  irep.slen = bin_to_uint16(p);		p += 2;
  int siz = sizeof(mrbc_sym) * irep.slen + sizeof(uint16_t) * irep.plen;
  siz += (-siz & 0x03);	// padding. 32bit align.
  irep.ofs_ireps = siz >> 2;

  // allocate new irep
  mrbc_irep *p_irep;
  siz = sizeof(mrbc_irep) + siz + sizeof(mrbc_irep*) * irep.rlen;
  if( vm->vm_id == 0 && !flag_top ) {
    p_irep = mrbc_raw_alloc_no_free( siz );
  } else {
    p_irep = mrbc_raw_alloc( siz );
  }
  if( !p_irep ) return NULL;

  *p_irep = irep;

  // make a sym_id table.
  mrbc_sym *tbl_syms = mrbc_irep_tbl_syms(p_irep);
  for( i = 0; i < irep.slen; i++ ) {
    int siz = bin_to_uint16(p); p += 2;
    *tbl_syms++ = mrbc_str_to_symid( (char*)p );
    p += (siz+1);
  }

  // make a pool data's offset table.
  uint16_t *ofs_pools = mrbc_irep_tbl_pools(p_irep);
  p = p_irep->pool + 2;
  for( i = 0; i < irep.plen; i++ ) {
    int siz;
    *ofs_pools++ = (uint16_t)(p - irep.pool);
    switch( *p++ ) {
    case IREP_TT_STR:
    case IREP_TT_SSTR:	siz = bin_to_uint16(p) + 3;	break;
    case IREP_TT_INT32:	siz = 4;	break;
    case IREP_TT_INT64:
    case IREP_TT_FLOAT:	siz = 8;	break;
    }
    p += siz;
  }

  // return length
  *len = bin_to_uint32(bin);
  return p_irep;
}


//================================================================
/*! Load IREP section.

  @param  vm	A pointer to VM.
  @param  bin	A pointer to RITE ISEQ.
  @param  len	Returns the parsed length.
  @return	Pointer to allocated mrbc_irep or NULL
*/
static mrbc_irep *load_irep(struct VM *vm, const uint8_t *bin, int *len)
{
  int len1;
  mrbc_irep *irep = load_irep_1(vm, bin, &len1, len == 0);
  if( !irep ) return NULL;
  int total_len = len1;

  mrbc_irep **tbl_ireps = mrbc_irep_tbl_ireps(irep);
  int i;
  for( i = 0; i < irep->rlen; i++ ) {
    tbl_ireps[i] = load_irep(vm, bin + total_len, &len1);
    if( ! tbl_ireps[i] ) return NULL;
    total_len += len1;
  }

  if( len ) *len = total_len;
  return irep;
}


/***** Global functions *****************************************************/

//================================================================
/*! Load the VM bytecode.

  @param  vm    Pointer to VM.
  @param  bin	Pointer to bytecode.
  @return int	zero if no error.
*/
int mrbc_load_mrb(struct VM *vm, const uint8_t *bin)
{
  if( load_header(vm, bin) != 0 ) return -1;

  bin += SIZE_RITE_BINARY_HEADER;

  while( 1 ) {
    uint32_t section_size = bin_to_uint32(bin+4);

    if( memcmp(bin, IREP, sizeof(IREP)) == 0 ) {
      vm->top_irep = load_irep(vm, bin + SIZE_RITE_SECTION_HEADER, 0);
      if( vm->top_irep == NULL ) return -1;

    } else if( memcmp(bin, END, sizeof(END)) == 0 ) {
      break;
    }

    bin += section_size;
  }

  return 0;
}


//================================================================
/*! release mrbc_irep holds memory

  @param  irep	Pointer to allocated mrbc_irep.
*/
void mrbc_irep_free(struct IREP *irep)
{
  // release child ireps.
  mrbc_irep **tbl_ireps = mrbc_irep_tbl_ireps(irep);
  int i;
  for( i = 0; i < irep->rlen; i++ ) {
    mrbc_irep_free( *tbl_ireps++ );
  }

  mrbc_raw_free( irep );
}


//================================================================
/*! get a mrbc_value in irep pool.

  @param  vm		Pointer to VM.
  @param  n		n'th
  @return mrbc_value	value
*/
mrbc_value mrbc_irep_pool_value(struct VM *vm, int n)
{
  assert( vm->cur_irep->plen > n );
  const uint8_t *p = mrbc_irep_pool_ptr(vm->cur_irep, n);
  mrbc_value obj;

  int tt = *p++;
  switch( tt ) {
#if MRBC_USE_STRING
  case IREP_TT_STR:
  case IREP_TT_SSTR: {
    int len = bin_to_uint16(p);
    return mrbc_string_new( vm, p+2, len );
  }
#endif

  case IREP_TT_INT32:
    mrbc_set_integer(&obj, bin_to_uint32(p));
    break;

#if MRBC_USE_FLOAT
  case IREP_TT_FLOAT:
    mrbc_set_float(&obj, bin_to_double64(p));
    break;
#endif

#ifdef MRBC_INT64
  case IREP_TT_INT64:
    mrbc_set_integer(&obj, obj.i = bin_to_int64(p));
    break;
#endif
  }

  return obj;
}
