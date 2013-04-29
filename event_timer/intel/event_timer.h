/*
 *   Copyright (c) 2012, Nokia Siemens Networks
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions are met:
 *       * Redistributions of source code must retain the above copyright
 *         notice, this list of conditions and the following disclaimer.
 *       * Redistributions in binary form must reproduce the above copyright
 *         notice, this list of conditions and the following disclaimer in the
 *         documentation and/or other materials provided with the distribution.
 *       * Neither the name of Nokia Siemens Networks nor the
 *         names of its contributors may be used to endorse or promote products
 *         derived from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 *   ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 *   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY
 *   DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 *   (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *   LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 *   ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
 
#ifndef EVENT_TIMER_H_
#define EVENT_TIMER_H_

#include <inttypes.h>
#include <environment.h>
#include <event_machine.h>

#include "event_timer_conf.h"
#include <rte_timer.h>


#ifdef __cplusplus
extern "C" {
#endif


#define EVT_TIMER_INVALID  (NULL)  ///< invalid handle value (don't assume it's 0 always)
#define EVT_TIMER_OK       (0)     ///< success status


#define TIMER_RESOLUTION_us 500    ///< timer management interval per core, microseconds (us)


typedef void*        evt_timer_t;    ///< timer handle type
typedef uint64_t     evt_ticks_t;    ///< type for ticks
typedef evt_timer_t  evt_cancel_t;


typedef union
{
  struct {
    uint64_t    timer_manage_cycles; // event_timer_manage()
    uint64_t    prev_tsc;            // event_timer_manage()
    evt_ticks_t timer_resolution;    // Timer resolution in ticks, all timeouts are quantisized by this value
    double      core_hpet_hz;        // core Hz / hpet Hz (could be shared data but there's anyway spare room here)
  };
  
  uint8_t u8[ENV_CACHE_LINE_SIZE]; 
  
} event_timer_local_t;


extern ENV_LOCAL  event_timer_local_t  event_timer_local  ENV_CACHE_LINE_ALIGNED;



/**
 ****************************************************************************
 * @brief    Global initialization
 *
 *           Must be called _once_ before any other calls.
 *
 * @return   EVT_TIMER_OK on success
 *
 ***************************************************************************/
int evt_timer_init_global(void);


/**
 ****************************************************************************
 * @brief    Global kill
 *
 *           Shuts down the timer and tries to free all allocated
 *           memory (NOT pending application events)
 *
* @param    initPool:   EVT_TIMER_INIT_POOL or EVT_TIMER_NO_POOL_INIT
 *
 * @return   EVT_TIMER_OK on success
 ***************************************************************************/
int evt_timer_shutdown(void);


/**
 ****************************************************************************
 * @brief    Local initialization
 *
 *           Must be called once by each core participating AFTER
 *           evt_timer_init_global() returns.
 *           This function may initialize core local data.
 *
 * @return   EVT_TIMER_OK on success
 ***************************************************************************/
int evt_timer_init_local(void);


/**
 ****************************************************************************
 * @brief    Current tick
 *
 *           Returns current tick count. The speed at which tick is
 *           incremented is a system specific value and can be asked by
 *           evt_timer_ticks_per_sec(). It can NOT be assumed that tick
 *           is incremented by one, it may increment with other values
 *           as well (system/resolution).
 *
 * @return   current tick
 ***************************************************************************/
static inline evt_ticks_t
evt_timer_current_tick(void)
{
  return (evt_ticks_t) rte_get_hpet_cycles();
}



/**
 ****************************************************************************
 * @brief    Tick rate
 *
 *           Returns number of ticks per second. The rate is a system
 *           specific value and application needs to calculate needed
 *           tick value for timeouts based on evt_timer_ticks_per_sec()
 *
 * @return   ticks per second
 ***************************************************************************/
static inline evt_ticks_t
evt_timer_ticks_per_sec(void)
{
#ifdef RTE_LIBEAL_USE_HPET
  return (evt_ticks_t) rte_get_hpet_hz();
#else
  return env_core_hz();
#endif
}


/**
 ****************************************************************************
 * @brief    Timer resolution
 *
 *           Returns the number of ticks of timer resolution, i.e. all
 *           timeouts are quantized with this value.
 *
 * @return   number of ticks
 * @see
 ***************************************************************************/
static inline evt_ticks_t evt_timer_resolution(void)
{
  return event_timer_local.timer_resolution;
}



/**
 ****************************************************************************
 * @brief    
 *
 *
 * @return   Convert CPU cycles to Timer ticks
 ***************************************************************************/
static inline evt_ticks_t
evt_timer_to_ticks(uint64_t cpu_cycles)
{
  evt_ticks_t ticks;
  
  
  // ticks = (cpu_cycles * rte_get_hpet_hz()) / env_core_hz();

#ifdef RTE_LIBEAL_USE_HPET  
  ticks = (evt_ticks_t) (((double) cpu_cycles) / event_timer_local.core_hpet_hz);
#else
  ticks = (evt_ticks_t) cpu_cycles;
#endif
  
  return ticks;
}



/**
 ****************************************************************************
 * @brief    
 *
 *
 * @return   Convert Timer ticks to CPU cycles
 ***************************************************************************/
static inline uint64_t
evt_timer_to_cycles(evt_ticks_t timer_ticks)
{
  uint64_t cpu_cycles;
  
  
  // cpu_cycles = (timer_ticks * env_core_hz()) / rte_get_hpet_hz();

#ifdef RTE_LIBEAL_USE_HPET  
  cpu_cycles = (uint64_t) (((double)timer_ticks) * event_timer_local.core_hpet_hz);
#else
  cpu_cycles = (uint64_t) timer_ticks;
#endif
  
  return cpu_cycles;
}



/**
 ****************************************************************************
 *
 * @brief    Longest timeout
 *
 *           Returns the number of ticks for the longest supported timeout in
 *           system ticks.
 *
 * @return   new timer handle or EV_TIMER_INVALID on error
 * @see
 ***************************************************************************/
static inline evt_ticks_t evt_timer_max_timeout(void)
{
  return (evt_ticks_t) (evt_timer_ticks_per_sec() * 10); // 10s
}


/**
 ****************************************************************************
 *
 * @brief    Request new timeout
 *
 *           Requests given event to be posted after given timeout
 *           (single shot). Given event must be allocated from em_alloc()
 *           or received as an event.
 *           Implementation today does not guarantee, that a cancelled event
 *           is never received (event is already queued).
 *           Expired timers are invalidated automatically, i.e. should
 *           not be cancelled, but the optional cancel info is not released.
 *
 *           Tick values are quantized to the next time slot, i.e. quantization
 *           error should not cause the timer to expire earlier than the given
 *           time.
 *
 * @param    ticks:   timeout in ticks (system specific, often CPU or TimerHW cycles)
 * @param    event:   valid user event to be posted after the timeout
 * @param    queue:   where to send the event
 * @param    cancel:  if not NULL, points to user allocated area, that timer uses to store
 *                    information needed for cancel. Given pointer must point to accessible
 *                    memory, that at least has space for sizeof(evt_cancel_t).
 *                    The memory must also be accessible to all cores possibly canceling
 *                    the timer.
 *
 * @return   new timer handle or EVT_TIMER_INVALID on error
 * @see
 ***************************************************************************/
#ifdef EVT_TIMER_DEBUG
  // DEBUG version
  #define evt_request_timeout(...)  evt_request_timeout_func(__VA_ARGS__, __FILE__, __LINE__)
  evt_timer_t evt_request_timeout_func(evt_ticks_t ticks, em_event_t event, em_queue_t queue, evt_cancel_t* cancel, char *file, int line);

#else
  // Normal version
  #define evt_request_timeout(...)  evt_request_timeout_func(__VA_ARGS__)
  evt_timer_t evt_request_timeout_func(evt_ticks_t ticks, em_event_t event, em_queue_t queue, evt_cancel_t* cancel);

#endif


/// @todo periodic timer
// evt_timer_t evt_request_period_timeout(evt_ticks_t ticks, em_event_t* event, em_queue_t queue, evt_cancel_t* cancel);
// or
// evt_timer_t evt_request_period_timeout(evt_ticks_t initial, evt_ticks_t period, em_event_t* event, em_queue_t queue, evt_cancel_t* cancel);
// but how about the event itself?  copy sent or repeated same pointer?


/**
 ****************************************************************************
 *
 * @brief    Cancel timeout
 *
 *                Can be used to cancel a pending timer.
 *
 * @param    handle:    timeout handle as returned by request_timeout()
 * @param    cancel:    pointer to the memory, that was given during evt_request_timeout()
 *
 * @return   given timer handle or EVT_TIMER_INVALID on error (no more pending, too late to cancel)
 ***************************************************************************/
evt_timer_t evt_cancel_timeout(evt_timer_t handle, evt_cancel_t* cancel);




static inline void
evt_timer_manage(void)
{
#if 1
  uint64_t cur_tsc, diff_tsc;
  
  
  cur_tsc  = rte_rdtsc();
  diff_tsc = cur_tsc - event_timer_local.prev_tsc;
    
  if(diff_tsc > event_timer_local.timer_manage_cycles)
  {
    event_timer_local.prev_tsc = cur_tsc;    
    rte_timer_manage();
  }
#else
  rte_timer_manage();
#endif
}


#ifdef __cplusplus
}
#endif

#endif /* EVENT_TIMER_H_ */
