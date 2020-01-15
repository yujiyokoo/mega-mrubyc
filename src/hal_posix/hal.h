/*! @file
  @brief
  Hardware abstraction layer
        for POSIX

  <pre>
  Copyright (C) 2016-2020 Kyushu Institute of Technology.
  Copyright (C) 2016-2020 Shimane IT Open-Innovation Center.

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
#include <unistd.h>
#include <pthread.h>


/***** Local headers ********************************************************/
/***** Constant values ******************************************************/
#ifndef MRBC_SCHEDULER_EXIT
#define MRBC_SCHEDULER_EXIT 1
#endif

#if !defined(MRBC_TICK_UNIT)
#define MRBC_TICK_UNIT_1_MS   1
#define MRBC_TICK_UNIT_2_MS   2
#define MRBC_TICK_UNIT_4_MS   4
#define MRBC_TICK_UNIT_10_MS 10
// Congiguring small value for MRBC_TICK_UNIT may cause a decline of timer
// accracy depending on kernel constant HZ and USER_HZ.
// For more information about it on `man 7 time`.
#define MRBC_TICK_UNIT MRBC_TICK_UNIT_4_MS
// Substantial timeslice value (millisecond) will be
// MRBC_TICK_UNIT * MRBC_TIMESLICE_TICK_COUNT (+ Jitter).
// MRBC_TIMESLICE_TICK_COUNT must be natural number
// (recommended value is from 1 to 10).
#define MRBC_TIMESLICE_TICK_COUNT 3
#endif


/***** Typedefs *************************************************************/
/***** Global variables *****************************************************/
extern pthread_mutex_t mutex_critical_section_;


/***** Function prototypes **************************************************/
void mrbc_tick(void);

#ifndef MRBC_NO_TIMER
void hal_init(void);
void hal_enable_irq(void);
void hal_disable_irq(void);
# define hal_idle_cpu()    sleep(1) // maybe interrupt by SIGINT

#else // MRBC_NO_TIMER
void hal_init(void);
# define hal_enable_irq()  ((void)0)
# define hal_disable_irq() ((void)0)
# define hal_idle_cpu()    (usleep(MRBC_TICK_UNIT * 1000), mrbc_tick())

#endif


/***** Macros ***************************************************************/
#if defined(MRBC_ENABLE_HAL_LOCK)
# define hal_lock()   pthread_mutex_lock( &mutex_critical_section_ )
# define hal_unlock() pthread_mutex_unlock( &mutex_critical_section_ )

#elif defined(MRBC_ENABLE_HAL_LOCK_DEBUG)
# define hal_lock() do {					\
    int r = pthread_mutex_lock( &mutex_critical_section_ );	\
    if( r ) {							\
      static const char msg[] = "HAL LOCK ERROR: ";		\
      hal_write(1, msg, sizeof(msg)-1);				\
      hal_write(1, strerror(r), strlen(strerror(r)));		\
      hal_write(1, "\n", 1);					\
    }								\
  } while(0)

# define hal_unlock() do {					\
    int r = pthread_mutex_unlock( &mutex_critical_section_ );	\
    if( r ) {							\
      static const char msg[] = "HAL UnLOCK ERROR: ";		\
      hal_write(1, msg, sizeof(msg)-1);				\
      hal_write(1, strerror(r), strlen(strerror(r)));		\
      hal_write(1, "\n", 1);					\
    }								\
  } while(0)

#else
# define hal_lock()   ((void)0)
# define hal_unlock() ((void)0)
#endif


/***** Inline functions *****************************************************/
//================================================================
/*!@brief
  Write

  @param  fd    dummy, but 1.
  @param  buf   pointer of buffer.
  @param  nbytes        output byte length.
*/
inline static int hal_write(int fd, const void *buf, int nbytes)
{
  return write(1, buf, nbytes);
}


//================================================================
/*!@brief
  Flush write baffer

  @param  fd    dummy, but 1.
*/
inline static int hal_flush(int fd)
{
  return fsync(1);
}


#ifdef __cplusplus
}
#endif
#endif // ifndef MRBC_HAL_H_
