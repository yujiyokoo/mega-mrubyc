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

/***** Feature test switches ************************************************/
/***** System headers *******************************************************/
#include <signal.h>
#include <sys/time.h>


/***** Local headers ********************************************************/
#include "hal.h"


/***** Constat values *******************************************************/
/***** Macros ***************************************************************/
/***** Typedefs *************************************************************/
/***** Function prototypes **************************************************/
/***** Local variables ******************************************************/
static sigset_t sigset_, sigset2_;
pthread_mutex_t mutex_critical_section_ = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutexattr_t mutex_attr_;


/***** Global variables *****************************************************/
/***** Signal catching functions ********************************************/
#if !defined(MRBC_NO_TIMER)
//================================================================
/*!@brief
  alarm signal handler
*/
static void sig_alarm(int dummy)
{
  mrbc_tick();
}
#endif


/***** Local functions ******************************************************/
/***** Global functions *****************************************************/
//================================================================
/*!@brief
  initialize

*/
void hal_init(void)
{
#if !defined(MRBC_NO_TIMER)
  sigemptyset(&sigset_);
  sigaddset(&sigset_, SIGALRM);

  // タイマー用シグナル準備
  struct sigaction sa;
  sa.sa_handler = sig_alarm;
  sa.sa_flags   = SA_RESTART;
  sa.sa_mask    = sigset_;
  sigaction(SIGALRM, &sa, 0);

  // タイマー設定
  struct itimerval tval;
  int sec  = 0;
  int usec = MRBC_TICK_UNIT * 1000;
  tval.it_interval.tv_sec  = sec;
  tval.it_interval.tv_usec = usec;
  tval.it_value.tv_sec     = sec;
  tval.it_value.tv_usec    = usec;
  setitimer(ITIMER_REAL, &tval, 0);
#endif // !defined(MRBC_NO_TIMER)

#if defined(MRBC_ENABLE_HAL_LOCK_DEBUG)
  // mutex準備
  pthread_mutexattr_init( &mutex_attr_ );
  pthread_mutexattr_settype( &mutex_attr_, PTHREAD_MUTEX_ERRORCHECK );
  pthread_mutex_init( &mutex_critical_section_, &mutex_attr_ );
#endif // defined(MRBC_ENABLE_HAL_LOCK_DEBUG)
}


#if !defined(MRBC_NO_TIMER)
//================================================================
/*!@brief
  enable interrupt

*/
void hal_enable_irq(void)
{
  sigprocmask(SIG_SETMASK, &sigset2_, 0);
}


//================================================================
/*!@brief
  disable interrupt

*/
void hal_disable_irq(void)
{
  sigprocmask(SIG_BLOCK, &sigset_, &sigset2_);
}
#endif // !defined(MRBC_NO_TIMER)
