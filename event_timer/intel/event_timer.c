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

#include <stdio.h>
#include <inttypes.h>

#include <event_timer.h>
#include "environment.h"

#include <rte_timer.h>

#include "em_intel.h"

#include "em_intel_inline.h"


/*
 * Defines
 */


/*
 * Data Types
 */


/*
 * Variables
 */
ENV_LOCAL  event_timer_local_t  event_timer_local  ENV_CACHE_LINE_ALIGNED;

COMPILE_TIME_ASSERT(sizeof(event_timer_local) == ENV_CACHE_LINE_SIZE, EVENT_TIMER_LOCAL_T_SIZE_ERROR);


/*
 * Local Function Prototypes
 */
static void event_timer_callback(struct rte_timer *tim, void *arg);
  


/*********************************************
 * Initialize once per process at startup
 */
int
evt_timer_init_global(void)
{
  EVT_INFO_PRINTF("Event Timer: Global Init\n");
  
  /* Init the RTE timer library */
  rte_timer_subsystem_init();
  
  return EVT_TIMER_OK;
}



/*********************************************
 * Each EM-core runs once at startup after global init
 */
int
evt_timer_init_local(void)
{ 
  (void) memset(&event_timer_local, 0, sizeof(event_timer_local));

#ifdef RTE_LIBEAL_USE_HPET  
  event_timer_local.core_hpet_hz = ((double) env_core_hz()) / ((double) rte_get_hpet_hz());
#else
  event_timer_local.core_hpet_hz = 1.0;
#endif
  
  event_timer_local.timer_manage_cycles = (uint64_t) (((double)(TIMER_RESOLUTION_us * env_core_hz())) / ((double)1000000));
  event_timer_local.timer_resolution    = evt_timer_to_ticks(event_timer_local.timer_manage_cycles);
  
  if(em_core_id() == 0) // print once
  {
    printf("\n"
           "Event Timer: Local Init\n"
           "Core Hz:%"PRIu64"\n"
         #ifdef RTE_LIBEAL_USE_HPET
           "HPET Hz:%"PRIu64"\n" 
           "Core Hz/HPET Hz:%.10f\n"
         #else
           "Core Hz/HPET Hz:%.10f (HPET not used!)\n"
         #endif
           "Timer manage resolution:%ius (cycles:%"PRIu64")\n"
           "Timer tick resolution  :%"PRIu64"\n"
           ,
           env_core_hz(),
         #ifdef RTE_LIBEAL_USE_HPET
           rte_get_hpet_hz(), 
         #endif
           event_timer_local.core_hpet_hz,
           TIMER_RESOLUTION_us, event_timer_local.timer_manage_cycles,
           event_timer_local.timer_resolution
           );
  }
  
  
  return EVT_TIMER_OK;
}



/*********************************************
 * Not implemented.
 */
int
evt_timer_shutdown(void)
{ 
  return EVT_TIMER_OK;
}



/*********************************************
 * Request a one-shot timeout
 */
#ifdef EVT_TIMER_DEBUG
evt_timer_t
evt_request_timeout_func(evt_ticks_t ticks, em_event_t event, em_queue_t queue, evt_cancel_t* cancel, char *file, int line)
#else
evt_timer_t
evt_request_timeout_func(evt_ticks_t ticks, em_event_t event, em_queue_t queue, evt_cancel_t* cancel)
#endif
{
  int ret;
  em_event_hdr_t   *const ev_hdr = event_to_event_hdr(event);
  struct rte_timer *const tim    = &ev_hdr->event_timer;
 
  // ENV_PREFETCH(tim);

  ev_hdr->timer_dst_queue = queue;
  
  ret = rte_timer_reset(tim, ticks, SINGLE, rte_lcore_id(),
  		                  event_timer_callback, (void *) ev_hdr);
  
  IF_UNLIKELY(ret != 0)
  {
  #ifdef EVT_TIMER_DEBUG
    fprintf(stderr, "%s() called from %s:L%i: Error ret = %i\n", __func__, file, line, ret);
  #else
    fprintf(stderr, "%s(): Error ret = %i\n", __func__, ret);
  #endif
    
    // abort();
    
    return EVT_TIMER_INVALID;
  } 
  
  *cancel = tim;
  
  return (evt_timer_t) tim;
}




/*********************************************
 *  Temporary implementation, re-think the cancel data so user don't need to carry it around!
 *  (implies API change)
 */
evt_timer_t
evt_cancel_timeout(evt_timer_t handle, evt_cancel_t* cancel)
{
  rte_timer_stop_sync((struct rte_timer *)cancel);
  
  return handle;
}




static void
event_timer_callback(struct rte_timer *tim, void *arg)
{
 em_event_hdr_t *const ev_hdr = (em_event_hdr_t *) arg;
 em_event_t            event  = event_hdr_to_event(ev_hdr);
 em_queue_t      const queue  = ev_hdr->timer_dst_queue;
 em_status_t           status;
 
  
  ev_hdr->q_elem = NULL; // No source queue
  
  status = em_send(event, queue);
  
  IF_UNLIKELY(status != EM_OK) {
    EVT_INFO_PRINTF("%s(): em_send() returned status=%"PRIu32" on EM-core %i\n", __func__, status, em_core_id());
  }
}

