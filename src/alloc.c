/*! @file
  @brief
  mrubyc memory management.

  <pre>
  Copyright (C) 2015-2019 Kyushu Institute of Technology.
  Copyright (C) 2015-2019 Shimane IT Open-Innovation Center.

  This file is distributed under BSD 3-Clause License.

  Memory management for objects in mruby/c.

  </pre>
*/


/***** Feature test switches ************************************************/
/***** System headers *******************************************************/
#include "vm_config.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>

/***** Local headers ********************************************************/
#include "vm.h"
#include "alloc.h"
#include "hal/hal.h"

/***** Constant values ******************************************************/
/*
  Layer 1st(f) and 2nd(s) model
  last 4bit is ignored

 FLI range      SLI0  1     2     3     4     5     6     7
  0  0000-007f  0000- 0010- 0020- 0030- 0040- 0050- 0060- 0070-007f
  1  0080-00ff  0080- 0090- 00a0- 00b0- 00c0- 00d0- 00e0- 00f0-00ff
  2  0100-01ff  0100- 0120- 0140- 0160- 0180- 01a0- 01c0- 01e0-01ff
  3  0200-03ff  0200- 0240- 0280- 02c0- 0300- 0340- 0380- 03c0-03ff
  4  0400-07ff  0400- 0480- 0500- 0580- 0600- 0680- 0700- 0780-07ff
  5  0800-0fff  0800- 0900- 0a00- 0b00- 0c00- 0d00- 0e00- 0f00-0fff
  6  1000-1fff  1000- 1200- 1400- 1600- 1800- 1a00- 1c00- 1e00-1fff
  7  2000-3fff  2000- 2400- 2800- 2c00- 3000- 3400- 3800- 3c00-3fff
  8  4000-7fff  4000- 4800- 5000- 5800- 6000- 6800- 7000- 7800-7fff
  9  8000-ffff  8000- 9000- a000- b000- c000- d000- e000- f000-ffff
*/

#ifndef MRBC_ALLOC_FLI_BIT_WIDTH	// 0000 0000 0000 0000
# define MRBC_ALLOC_FLI_BIT_WIDTH 9	// ~~~~~~~~~~~
#endif
#ifndef MRBC_ALLOC_SLI_BIT_WIDTH	// 0000 0000 0000 0000
# define MRBC_ALLOC_SLI_BIT_WIDTH 3	//            ~~~
#endif
#ifndef MRBC_ALLOC_IGNORE_LSBS		// 0000 0000 0000 0000
# define MRBC_ALLOC_IGNORE_LSBS	  4	//                ~~~~
#endif


/***** Macros ***************************************************************/
#define FLI(x) ((x) >> MRBC_ALLOC_SLI_BIT_WIDTH)
#define SLI(x) ((x) & ((1 << MRBC_ALLOC_SLI_BIT_WIDTH) - 1))

/*
   Minimum memory block size parameter.
   Choose large one From sizeof(FREE_BLOCK) or (1 << MRBC_ALLOC_IGNORE_LSBS)
*/
#if !defined(MRBC_MIN_MEMORY_BLOCK_SIZE)
#define MRBC_MIN_MEMORY_BLOCK_SIZE sizeof(FREE_BLOCK)
// #define MRBC_MIN_MEMORY_BLOCK_SIZE (1 << MRBC_ALLOC_IGNORE_LSBS)
#endif


/***** Typedefs *************************************************************/
// memory block header
#if defined(MRBC_ALLOC_16BIT)
#define MRBC_ALLOC_MEMSIZE_T	uint16_t
typedef struct USED_BLOCK {
  unsigned int         flag_free : 1;	//!< Free block or used block flag.
  uint8_t	       vm_id;		//!< mruby/c VM ID
  MRBC_ALLOC_MEMSIZE_T size;		//!< block size, header included
  MRBC_ALLOC_MEMSIZE_T prev_offset;	//!< offset of previous physical block
} USED_BLOCK;

typedef struct FREE_BLOCK {
  unsigned int         flag_free : 1;	//!< Free block or used block flag.
  uint8_t	       vm_id;		//!< dummy
  MRBC_ALLOC_MEMSIZE_T size;		//!< block size, header included
  MRBC_ALLOC_MEMSIZE_T prev_offset;	//!< offset of previous physical block

  struct FREE_BLOCK *next_free;
  struct FREE_BLOCK *prev_free;
} FREE_BLOCK;

#elif defined(MRBC_ALLOC_24BIT)
#define MRBC_ALLOC_MEMSIZE_T	uint32_t
typedef struct USED_BLOCK {
  MRBC_ALLOC_MEMSIZE_T size;		//!< block size, header included
  unsigned int         flag_free : 1;	//!< Free block or used block flag.
  uint8_t	       vm_id : 7;	//!< mruby/c VM ID
  MRBC_ALLOC_MEMSIZE_T prev_offset : 24;//!< offset of previous physical block
} USED_BLOCK;

typedef struct FREE_BLOCK {
  MRBC_ALLOC_MEMSIZE_T size;		//!< block size, header included
  unsigned int         flag_free : 1;	//!< Free block or used block flag.
  uint8_t	       vm_id : 7;	//!< dummy
  MRBC_ALLOC_MEMSIZE_T prev_offset : 24;//!< offset of previous physical block

  struct FREE_BLOCK *next_free;
  struct FREE_BLOCK *prev_free;
} FREE_BLOCK;

#elif defined(MRBC_ALLOC_LIBC)
// TODO

#elif defined(MRBC_ALLOC_INCLUDE)
#include "alloc_block.c"

#else
# error 'define MRBC_ALLOC_*' required.
#endif

// and operation macro
#define SET_FREE_BLOCK(p) ((p)->flag_free = 1)
#define SET_USED_BLOCK(p) ((p)->flag_free = 0)
#define IS_FREE_BLOCK(p) ( (p)->flag_free)
#define IS_USED_BLOCK(p) (!(p)->flag_free)
#define PHYS_NEXT(p) ((uint8_t *)(p) + (p)->size)
#define PHYS_PREV(p) ((uint8_t *)(p) - (p)->prev_offset)
#define SET_PHYS_PREV(p1,p2) \
  ((p2)->prev_offset = (uint8_t *)(p2)-(uint8_t *)(p1))

#define SET_VM_ID(p,id) \
  (((USED_BLOCK *)((uint8_t *)(p) - sizeof(USED_BLOCK)))->vm_id = (id))
#define GET_VM_ID(p) \
  (((USED_BLOCK *)((uint8_t *)(p) - sizeof(USED_BLOCK)))->vm_id)



/***** Function prototypes **************************************************/
/***** Local variables ******************************************************/
// memory pool
static uint8_t *memory_pool;
static MRBC_ALLOC_MEMSIZE_T memory_pool_size;

// free memory block index
#define SIZE_FREE_BLOCKS \
  ((MRBC_ALLOC_FLI_BIT_WIDTH + 1) * (1 << MRBC_ALLOC_SLI_BIT_WIDTH))
static FREE_BLOCK *free_blocks[SIZE_FREE_BLOCKS + 1];

// free memory bitmap
static uint16_t free_fli_bitmap;
static uint8_t  free_sli_bitmap[MRBC_ALLOC_FLI_BIT_WIDTH +1+1]; // + sentinel
#define MSB_BIT1_FLI 0x8000
#define MSB_BIT1_SLI 0x80
#define NLZ_FLI(x) nlz16(x)
#define NLZ_SLI(x) nlz8(x)


/***** Global variables *****************************************************/
/***** Signal catching functions ********************************************/
/***** Local functions ******************************************************/

//================================================================
/*! Number of leading zeros. 16bit version.

  @param  x	target (16bit unsigned)
  @retval int	nlz value
*/
static inline int nlz16(uint16_t x)
{
  if( x == 0 ) return 16;

  int n = 1;
  if((x >>  8) == 0 ) { n += 8; x <<= 8; }
  if((x >> 12) == 0 ) { n += 4; x <<= 4; }
  if((x >> 14) == 0 ) { n += 2; x <<= 2; }
  return n - (x >> 15);
}


//================================================================
/*! Number of leading zeros. 8bit version.

  @param  x	target (8bit unsigned)
  @retval int	nlz value
*/
static inline int nlz8(uint8_t x)
{
  if( x == 0 ) return 8;

  int n = 1;
  if((x >> 4) == 0 ) { n += 4; x <<= 4; }
  if((x >> 6) == 0 ) { n += 2; x <<= 2; }
  return n - (x >> 7);
}


//================================================================
/*! calc f and s, and returns fli,sli of free_blocks

  @param  alloc_size	alloc size
  @retval unsigned int	index of free_blocks
*/
static unsigned int calc_index(unsigned int alloc_size)
{
  // check overflow
  if( (alloc_size >> (MRBC_ALLOC_FLI_BIT_WIDTH
                      + MRBC_ALLOC_SLI_BIT_WIDTH
                      + MRBC_ALLOC_IGNORE_LSBS)) != 0) {
    return SIZE_FREE_BLOCKS;
  }

  // calculate First Level Index.
  int fli = 16 -
    nlz16( alloc_size >> (MRBC_ALLOC_SLI_BIT_WIDTH + MRBC_ALLOC_IGNORE_LSBS) );

  // calculate Second Level Index.
  int shift = (fli == 0) ? (fli + MRBC_ALLOC_IGNORE_LSBS) :
			   (fli + MRBC_ALLOC_IGNORE_LSBS - 1);

  int sli   = (alloc_size >> shift) & ((1 << MRBC_ALLOC_SLI_BIT_WIDTH) - 1);
  int index = (fli << MRBC_ALLOC_SLI_BIT_WIDTH) + sli;

  assert(fli >= 0);
  assert(fli <= MRBC_ALLOC_FLI_BIT_WIDTH);
  assert(sli >= 0);
  assert(sli <= (1 << MRBC_ALLOC_SLI_BIT_WIDTH) - 1);

  return index;
}


//================================================================
/*! Mark that block free and register it in the free index table.

  @param  target	Pointer to target block.
*/
static void add_free_block(FREE_BLOCK *target)
{
  SET_FREE_BLOCK(target);

  unsigned int index = calc_index(target->size) - 1;
  int fli = FLI(index);
  int sli = SLI(index);
  assert( index < SIZE_FREE_BLOCKS );

  free_fli_bitmap      |= (MSB_BIT1_FLI >> fli);
  free_sli_bitmap[fli] |= (MSB_BIT1_SLI >> sli);

  target->prev_free = NULL;
  target->next_free = free_blocks[index];
  if( target->next_free != NULL ) {
    target->next_free->prev_free = target;
  }
  free_blocks[index] = target;

#ifdef MRBC_DEBUG
  target->vm_id = -1;
  memset( (uint8_t *)target + sizeof(FREE_BLOCK), 0xff,
          target->size - sizeof(FREE_BLOCK) );
#endif
}


//================================================================
/*! just remove the free_block *target from index

  @param  target	pointer to target block.
*/
static void remove_index(FREE_BLOCK *target)
{
  // top of linked list?
  if( target->prev_free == NULL ) {
    unsigned int index = calc_index(target->size) - 1;
    free_blocks[index] = target->next_free;

    if( free_blocks[index] == NULL ) {
      int fli = FLI(index);
      int sli = SLI(index);
      free_sli_bitmap[fli] &= ~(MSB_BIT1_SLI >> sli);
      if( free_sli_bitmap[fli] == 0 ) free_fli_bitmap &= ~(MSB_BIT1_FLI >> fli);
    }
  }
  else {
    target->prev_free->next_free = target->next_free;
  }

  if( target->next_free != NULL ) {
    target->next_free->prev_free = target->prev_free;
  }
}


//================================================================
/*! Split block by size

  @param  target	pointer to target block
  @param  size	size
  @retval NULL	no split.
  @retval FREE_BLOCK *	pointer to splitted free block.
*/
static inline FREE_BLOCK* split_block(FREE_BLOCK *target, unsigned int size)
{
  if( target->size < (size + sizeof(FREE_BLOCK)
                      + (1 << MRBC_ALLOC_IGNORE_LSBS)) ) return NULL;

  // split block, free
  FREE_BLOCK *split = (FREE_BLOCK *)((uint8_t *)target + size);
  USED_BLOCK *next  = (USED_BLOCK *)PHYS_NEXT(target);

  split->size  = target->size - size;
  target->size = size;
  SET_PHYS_PREV(target, split);
  SET_PHYS_PREV(split, next);

  return split;
}


//================================================================
/*! merge ptr1 and ptr2 block.
    ptr2 will disappear

  @param  ptr1	pointer to free block 1
  @param  ptr2	pointer to free block 2
*/
static void merge_block(FREE_BLOCK *ptr1, FREE_BLOCK *ptr2)
{
  assert(ptr1 < ptr2);

  // merge ptr1 and ptr2
  ptr1->size += ptr2->size;

  // update block info
  USED_BLOCK *next = (USED_BLOCK *)PHYS_NEXT(ptr1);
  SET_PHYS_PREV(ptr1, next);
}


/***** Global functions *****************************************************/
//================================================================
/*! initialize

  @param  ptr	pointer to free memory block.
  @param  size	size. (max 64KB. see MRBC_ALLOC_MEMSIZE_T)
*/
void mrbc_init_alloc(void *ptr, unsigned int size)
{
  assert( MRBC_MIN_MEMORY_BLOCK_SIZE >= (1 << MRBC_ALLOC_IGNORE_LSBS) );
  assert( size != 0 );
  assert( size <= (MRBC_ALLOC_MEMSIZE_T)(~0) );
  if( memory_pool != NULL ) return;

  memory_pool      = ptr;
  memory_pool_size = size;

  // initialize memory pool
  //  large free block + zero size used block (sentinel).
  unsigned int free_size = size - sizeof(USED_BLOCK);

  FREE_BLOCK *free  = (FREE_BLOCK *)memory_pool;
  SET_FREE_BLOCK(free);
  free->size        = free_size;
  free->prev_offset = 0;

  USED_BLOCK *used  = (USED_BLOCK *)((uint8_t*)ptr + free_size);
  SET_USED_BLOCK(used);
  used->size        = sizeof(USED_BLOCK);
  used->prev_offset = free_size;

  add_free_block(free);
}


//================================================================
/*! cleanup memory pool
*/
void mrbc_cleanup_alloc(void)
{
  memory_pool = NULL;
  memset( free_blocks, 0, sizeof(free_blocks) );
  free_fli_bitmap = 0;
  memset( free_sli_bitmap, 0, sizeof(free_sli_bitmap) );
}


//================================================================
/*! allocate memory sub function.
*/
static inline void * mrbc_raw_alloc_ff_sub(unsigned int alloc_size, unsigned int index)
{
  FREE_BLOCK *target = free_blocks[--index];

  while(1) {
    if( target == NULL ) return NULL;
    if( target->size >= alloc_size ) break;
    target = target->next_free;
  }

  SET_USED_BLOCK(target);

  // remove free_blocks index
  remove_index( target );

  // split a block
  FREE_BLOCK *release = split_block(target, alloc_size);
  if( release != NULL ) {
    add_free_block(release);
  }

#ifdef MRBC_DEBUG
  memset( (uint8_t *)target + sizeof(USED_BLOCK), 0xaa,
          target->size - sizeof(USED_BLOCK) );
#endif
  target->vm_id = 0;

  return (uint8_t *)target + sizeof(USED_BLOCK);
}


//================================================================
/*! allocate memory

  @param  size	request size.
  @return void * pointer to allocated memory.
  @retval NULL	error.
*/
void * mrbc_raw_alloc(unsigned int size)
{
  unsigned int alloc_size = size + sizeof(USED_BLOCK);

  // align 4 byte
  alloc_size += (-alloc_size & 3);

  // check minimum alloc size.
  if( alloc_size < MRBC_MIN_MEMORY_BLOCK_SIZE ) alloc_size = MRBC_MIN_MEMORY_BLOCK_SIZE;

  // find free memory block.
  unsigned int index = calc_index(alloc_size);
  int fli = FLI(index);
  int sli = SLI(index);

  FREE_BLOCK *target = free_blocks[index];
  if( target != NULL ) goto FOUND_TARGET_BLOCK;

  // check in SLI bitmap table.
  uint16_t masked = free_sli_bitmap[fli] & ((MSB_BIT1_SLI >> sli) - 1);
  if( masked != 0 ) {
    sli = NLZ_SLI( masked );
    goto FOUND_FLI_SLI;
  }

  // check in FLI bitmap table.
  masked = free_fli_bitmap & ((MSB_BIT1_FLI >> fli) - 1);
  if( masked != 0 ) {
    fli = NLZ_FLI( masked );
    sli = NLZ_SLI( free_sli_bitmap[fli] );
    goto FOUND_FLI_SLI;
  }

  // Change strategy to First-fit.
  void *ret = mrbc_raw_alloc_ff_sub( alloc_size, index );
  if( ret ) return ret;

  // else out of memory
  static const char msg[] = "Fatal error: Out of memory.\n";
  hal_write(1, msg, sizeof(msg)-1);
  return NULL;  // ENOMEM


 FOUND_FLI_SLI:
  assert(fli >= 0);
  assert(fli <= MRBC_ALLOC_FLI_BIT_WIDTH);
  assert(sli >= 0);
  assert(sli <= (1 << MRBC_ALLOC_SLI_BIT_WIDTH) - 1);

  index = (fli << MRBC_ALLOC_SLI_BIT_WIDTH) + sli;
  target = free_blocks[index];
  assert( target != NULL );

 FOUND_TARGET_BLOCK:
  assert(target->size >= alloc_size);
  SET_USED_BLOCK(target);

  // remove free_blocks index
  free_blocks[index] = target->next_free;

  if( target->next_free == NULL ) {
    free_sli_bitmap[fli] &= ~(MSB_BIT1_SLI >> sli);
    if( free_sli_bitmap[fli] == 0 ) free_fli_bitmap &= ~(MSB_BIT1_FLI >> fli);
  }
  else {
    target->next_free->prev_free = NULL;
  }

  // split a block
  FREE_BLOCK *release = split_block(target, alloc_size);
  if( release != NULL ) {
    add_free_block(release);
  }

#ifdef MRBC_DEBUG
  memset( (uint8_t *)target + sizeof(USED_BLOCK), 0xaa,
          target->size - sizeof(USED_BLOCK) );
#endif
  target->vm_id = 0;

  return (uint8_t *)target + sizeof(USED_BLOCK);
}


//================================================================
/*! release memory

  @param  ptr	Return value of mrbc_raw_alloc()
*/
void mrbc_raw_free(void *ptr)
{
  // get target block
  FREE_BLOCK *target = (FREE_BLOCK *)((uint8_t *)ptr - sizeof(USED_BLOCK));

  // check next block, merge?
  FREE_BLOCK *next = (FREE_BLOCK *)PHYS_NEXT(target);

  if( IS_FREE_BLOCK(next) ) {
    remove_index(next);
    merge_block(target, next);
  }

  // check previous block, merge?
  FREE_BLOCK *prev = (FREE_BLOCK *)PHYS_PREV(target);

  if( (prev != NULL) && IS_FREE_BLOCK(prev) ) {
    remove_index(prev);
    merge_block(prev, target);
    target = prev;
  }

  // target, add to index
  add_free_block(target);
}


//================================================================
/*! re-allocate memory

  @param  ptr	Return value of mrbc_raw_alloc()
  @param  size	request size
  @return void * pointer to allocated memory.
  @retval NULL	error.
*/
void * mrbc_raw_realloc(void *ptr, unsigned int size)
{
  USED_BLOCK  *target = (USED_BLOCK *)((uint8_t *)ptr - sizeof(USED_BLOCK));
  unsigned int alloc_size = size + sizeof(USED_BLOCK);

  // align 4 byte
  alloc_size += (-alloc_size & 3);

  // check minimum alloc size.
  if( alloc_size < MRBC_MIN_MEMORY_BLOCK_SIZE ) alloc_size = MRBC_MIN_MEMORY_BLOCK_SIZE;

  // expand? part1.
  // next phys block is free and enough size?
  if( alloc_size > target->size ) {
    FREE_BLOCK *next = (FREE_BLOCK *)PHYS_NEXT(target);
    if( IS_FREE_BLOCK(next) &&
	((target->size + next->size) >= alloc_size)) {
      remove_index(next);
      merge_block((FREE_BLOCK *)target, next);

      // and fall through.
    }
  }

  // same size?
  if( alloc_size == target->size ) {
    return (uint8_t *)ptr;
  }

  // shrink?
  if( alloc_size < target->size ) {
    FREE_BLOCK *release = split_block((FREE_BLOCK *)target, alloc_size);
    if( release != NULL ) {
      // check next block, merge?
      FREE_BLOCK *next = (FREE_BLOCK *)PHYS_NEXT(release);
      if( IS_FREE_BLOCK(next) ) {
        remove_index(next);
        merge_block(release, next);
      }
      add_free_block(release);
    }

    return (uint8_t *)ptr;
  }

  // expand part2.
  // new alloc and copy
  uint8_t *new_ptr = mrbc_raw_alloc(size);
  if( new_ptr == NULL ) return NULL;  // ENOMEM

  memcpy(new_ptr, ptr, target->size - sizeof(USED_BLOCK));
  SET_VM_ID(new_ptr, target->vm_id);

  mrbc_raw_free(ptr);

  return new_ptr;
}


//================================================================
/*! Check if the pointer points allocated memory.

  @param  tgt	Pointer to check.
  @retval int	result in boolean.
*/
int is_allocated_memory(void *tgt)
{
  // check simply.
  return ((void *)memory_pool < tgt) &&
    (tgt < (void *)(memory_pool + memory_pool_size));
}



//// for mruby/c

//================================================================
/*! allocate memory

  @param  vm	pointer to VM.
  @param  size	request size.
  @return void * pointer to allocated memory.
  @retval NULL	error.
*/
void * mrbc_alloc(const struct VM *vm, unsigned int size)
{
  uint8_t *ptr = mrbc_raw_alloc(size);
  if( ptr == NULL ) return NULL;	// ENOMEM
  if( vm ) SET_VM_ID(ptr, vm->vm_id);

  return ptr;
}


//================================================================
/*! release memory, vm used.

  @param  vm	pointer to VM.
*/
void mrbc_free_all(const struct VM *vm)
{
  USED_BLOCK *ptr = (USED_BLOCK *)memory_pool;
  void *free_target = NULL;
  int vm_id = vm->vm_id;

  while( (uint8_t *)ptr < (memory_pool + memory_pool_size) ) {
    if( IS_USED_BLOCK(ptr) && (ptr->vm_id == vm_id) ) {
      if( free_target ) {
	mrbc_raw_free(free_target);
      }
      free_target = (char *)ptr + sizeof(USED_BLOCK);
    }
    ptr = (USED_BLOCK *)PHYS_NEXT(ptr);
  }
  if( free_target ) {
    mrbc_raw_free(free_target);
  }
}


//================================================================
/*! set vm id

  @param  ptr	Return value of mrbc_alloc()
  @param  vm_id	vm id
*/
void mrbc_set_vm_id(void *ptr, int vm_id)
{
  SET_VM_ID(ptr, vm_id);
}


//================================================================
/*! get vm id

  @param  ptr	Return value of mrbc_alloc()
  @return int	vm id
*/
int mrbc_get_vm_id(void *ptr)
{
  return GET_VM_ID(ptr);
}



#ifdef MRBC_DEBUG
#include "console.h"
//================================================================
/*! statistics

  @param  *total	returns total memory.
  @param  *used		returns used memory.
  @param  *free		returns free memory.
  @param  *fragment	returns memory fragmentation
*/
void mrbc_alloc_statistics(int *total, int *used, int *free, int *fragmentation)
{
  *total = memory_pool_size;
  *used = 0;
  *free = 0;
  *fragmentation = 0;

  USED_BLOCK *block = (USED_BLOCK *)memory_pool;
  int flag_used_free = IS_USED_BLOCK(block);
  while( (uint8_t *)block < (memory_pool + memory_pool_size) ) {
    if( IS_FREE_BLOCK(block) ) {
      *free += block->size;
    } else {
      *used += block->size;
    }
    if( flag_used_free != IS_USED_BLOCK(block) ) {
      (*fragmentation)++;
      flag_used_free = IS_USED_BLOCK(block);
    }
    block = (USED_BLOCK *)PHYS_NEXT(block);
  }
}



//================================================================
/*! get used memory size

  @param  vm_id		vm_id
  @return int		total used memory size
*/
int mrbc_alloc_vm_used( int vm_id )
{
  USED_BLOCK *block = (USED_BLOCK *)memory_pool;
  int total = 0;

  while( (uint8_t *)block < (memory_pool + memory_pool_size) ) {
    if( block->vm_id == vm_id && IS_USED_BLOCK(block) ) {
      total += block->size;
    }
    block = (USED_BLOCK *)PHYS_NEXT(block);
  }

  return total;
}



//================================================================
/*! print memory block for debug.

*/
void mrbc_alloc_print_memory_pool( void )
{
  FREE_BLOCK *block = (FREE_BLOCK *)memory_pool;

  while( block < (FREE_BLOCK *)(memory_pool + memory_pool_size) ) {
    console_printf("%08x f:%d id:%-2d size:%4d(%4x)",
		   (uint32_t)block, block->flag_free, (int8_t)block->vm_id,
		   block->size, block->size );

    if( IS_FREE_BLOCK(block) ) {
      unsigned int index = calc_index(block->size) - 1;
      console_printf(" fli:%d sli:%d block:%p nf:%p",
		     FLI(index), SLI(index),
		     block->prev_free, block->next_free);
    }
    console_printf( "\n" );
    block = (FREE_BLOCK *)PHYS_NEXT(block);
  }
}

#endif
