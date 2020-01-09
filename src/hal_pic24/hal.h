/*! @file
  @brief
  Hardware abstraction layer
        for Microchip PIC24

  <pre>
  Copyright (C) 2018 Kyushu Institute of Technology.
  Copyright (C) 2018 Shimane IT Open-Innovation Center.

  This file is distributed under BSD 3-Clause License.
  </pre>
*/

#ifndef MRBC_SRC_HAL_H_
#define MRBC_SRC_HAL_H_

#ifdef __cplusplus
extern "C" {
#endif

/***** Feature test switches ************************************************/
/***** System headers *******************************************************/
#ifndef FCY
#define FCY (_XTAL_FREQ/2)
#endif
#include <xc.h>
#include <libpic30.h>

/***** Local headers ********************************************************/
#include "../mcc_generated_files/clock.h"

/***** Constant values ******************************************************/
#if !defined(MRBC_TICK_UNIT)
#define MRBC_TICK_UNIT_1_MS   1
#define MRBC_TICK_UNIT_2_MS   2
#define MRBC_TICK_UNIT_4_MS   4
#define MRBC_TICK_UNIT_10_MS 10
// You may be able to reduce power consumption if you configure
// MRBC_TICK_UNIT_2_MS or larger.
#define MRBC_TICK_UNIT MRBC_TICK_UNIT_1_MS
// Substantial timeslice value (millisecond) will be
// MRBC_TICK_UNIT * MRBC_TIMESLICE_TICK_COUNT (+ Jitter).
// MRBC_TIMESLICE_TICK_COUNT must be natural number
// (recommended value is from 1 to 10).
#define MRBC_TIMESLICE_TICK_COUNT 10
#endif

#if !defined(MRBC_NO_TIMER)	// use hardware timer.
# define hal_init()        ((void)0)
# define hal_enable_irq()  __builtin_disi(0x0000)
# define hal_disable_irq() __builtin_disi(0x3fff)
# define hal_idle_cpu()    Idle()

#else // MRBC_NO_TIMER
# define hal_init()        ((void)0)
# define hal_enable_irq()  ((void)0)
# define hal_disable_irq() ((void)0)
# define hal_idle_cpu()    ((__delay_ms(MRBC_TICK_UNIT)), mrbc_tick())

#endif


/***** Typedefs *************************************************************/
/***** Global variables *****************************************************/
/***** Function prototypes **************************************************/
int hal_write(int fd, const void *buf, int nbytes);
int hal_flush(int fd);


/***** Macros ***************************************************************/
#define hal_lock()   ((void)0)	// dummy yet.
#define hal_unlock() ((void)0)


/***** Inline functions *****************************************************/


#ifdef __cplusplus
}
#endif
#endif // ifndef MRBC_SRC_HAL_H_
