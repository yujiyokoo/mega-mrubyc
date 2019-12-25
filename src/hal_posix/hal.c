/*! @file
  @brief
  Hardware abstraction layer
        for POSIX

  <pre>
  Copyright (C) 2016 Kyushu Institute of Technology.
  Copyright (C) 2016 Shimane IT Open-Innovation Center.

  This file is distributed under BSD 3-Clause License.
  </pre>
*/

/***** Feature test switches ************************************************/
/***** System headers *******************************************************/
#include <signal.h>
#include <sys/time.h>
#include <pthread.h>
#include <assert.h>


/***** Local headers ********************************************************/
#include "hal.h"


/***** Constat values *******************************************************/
/***** Macros ***************************************************************/
/***** Typedefs *************************************************************/
/***** Function prototypes **************************************************/
/***** Local variables ******************************************************/
#ifndef MRBC_NO_TIMER
static sigset_t sigset_, sigset2_;
#endif
static pthread_mutex_t mutex_critical_section_ = PTHREAD_MUTEX_INITIALIZER;
#if defined(MRBC_DEBUG)
static int flag_critical_section_;
#endif

/***** Global variables *****************************************************/
/***** Signal catching functions ********************************************/
#ifndef MRBC_NO_TIMER
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
#ifndef MRBC_NO_TIMER

//================================================================
/*!@brief
  initialize

*/
void hal_init(void)
{
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
}


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

#endif /* ifndef MRBC_NO_TIMER */


//================================================================
/*!@brief
  enter global critical section

*/
void hal_lock(void)
{
#if defined(MRBC_DEBUG)
  if( pthread_mutex_trylock( &mutex_critical_section_ ) != 0 ) {
    assert(!"hal double lock detected.");
  }
  flag_critical_section_ = 1;
#else
  pthread_mutex_lock( &mutex_critical_section_ );
#endif
}


//================================================================
/*!@brief
  leave global critical section

*/
void hal_unlock(void)
{
#if defined(MRBC_DEBUG)
  if( !flag_critical_section_ ) {
    assert(!"hal double unlock detected.");
  }
  flag_critical_section_ = 0;
#endif
  pthread_mutex_unlock( &mutex_critical_section_ );
}
