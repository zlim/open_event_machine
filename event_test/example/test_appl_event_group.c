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


/**
 * @file
 *
 * Event Machine event group example.
 * 
 * Test and measure the event group feature for fork-join type of operations using events.
 * See the event_machine_group.h file for the event group API calls.
 * 
 * Allocates and sends a number of data events to itself (using an event group)
 * to trigger a notification event to be sent when the configured event count
 * has been received. The cycles consumed until the notification is received is measured
 * and printed.
 *
 * Note: To keep things simple this testcase uses only a single queue into which to receive
 * all events, including the notification events. The event group fork-join mechanism does not
 * care about the used queues however, it's basically a counter of events sent using a certain 
 * event group id. In a more complex example each data event could be send from different
 * EO:s to different queues and the final notification event sent yet to another queue.
 *
 */

#include "event_machine.h"
#include "event_machine_group.h"
#include "environment.h"

#include <string.h>
#include <stdio.h>

#include "example.h"



/*
 * Test configuration
 */

/** The number of data events to allocate and send */
#define DATA_EVENTS           128

/** The amount of data per event to work on to create some dummy load */
#define DATA_PER_EVENT        1024

/** The number of times to call em_event_group_increment() per received data event */
#define EVENT_GROUP_INCREMENT 1

/** Max number of cores */
#define MAX_NBR_OF_CORES      256

/** Spin value before restarting the test */
#define DELAY_SPIN_COUNT      50000000


/**
 * Macros
 */

/*
 * Note: The error macros below are NOT proper event machine error handling mechanisms.
 * For better error handling see the API funcs: em_error(), em_register_error_handler(),  
 * em_eo_register_error_handler() etc.
 */
#define ERROR_PRINT(...)     {printf("ERROR: file: %s line: %d\n", __FILE__, __LINE__); \
                              printf(__VA_ARGS__); fflush(NULL); abort();}

#define IS_ERROR(cond, ...)    \
  if(ENV_UNLIKELY( (cond) )) { \
    ERROR_PRINT(__VA_ARGS__);  \
  }




/**
 * The event for this test case.
 */
typedef struct
{
  #define MSG_START 1
  #define MSG_DATA  2
  #define MSG_DONE  3
  /** Event/msg number */
  uint64_t  msg;
  
  /** Start cycles stored at the beginning of each round */
  uint64_t  start_cycles;
  
  /** The number of times to increment (per event) the event group count
   *  by calling em_event_group_increment(1).
   */
  uint64_t  increment;
  
  /** Pointer to data area to use for dummy data processing */
  uint8_t  *data_ptr;

} event_group_test_t;


/**
 * EO context used by the event group test
 */
typedef struct
{
  /** Event Group Id used by the EO */
  em_event_group_t event_group;
  
  /** The number of events to send using an event group before triggering a notification event */
  int              event_count;

  /** Accumulator for a dummy sum */
  uint64_t         acc;

  /** Total test case running time in cycles updated when receiving a notification event */
  uint64_t         total_cycles;

  /** Number of rounds, i.e. received notifications, during this test case */  
  uint64_t         total_rounds;
}eo_context_t;



/**
 * EO context padded to cache line size.
 */
typedef union 
{
  /** The EO context data */
  eo_context_t eo_ctx;
  
  /** Pad EO context to cache line size */
  uint8_t u8[ENV_CACHE_LINE_SIZE];
  
} eo_context_pad_t;




/** EO context */
ENV_SHARED  static  eo_context_pad_t  event_group_test_eo_context  ENV_CACHE_LINE_ALIGNED;

COMPILE_TIME_ASSERT(sizeof(eo_context_pad_t) == ENV_CACHE_LINE_SIZE, EVENT_GROUP_TEST_EO_CONTEXT_SIZE_ERROR);




/**
 * Core-specific data
 */
typedef union 
{
  /** Counter of the received data events on a core */
  uint64_t rcv_ev_cnt;
  
  /** Pad to cache line size to avoid cache line sharing */
  uint8_t u8[ENV_CACHE_LINE_SIZE];
  
} core_stat_t;


/** 
 * Array of core specific data accessed by a core using its core index.
 * No serialization mechanisms needed to protect the data even when using parallel queues.
 */
ENV_SHARED  static  core_stat_t  core_stats[MAX_NBR_OF_CORES]  ENV_CACHE_LINE_ALIGNED;

COMPILE_TIME_ASSERT( sizeof(core_stat_t) == ENV_CACHE_LINE_SIZE,       CORE_STAT_T_SIZE_ERROR);
COMPILE_TIME_ASSERT((sizeof(core_stats)  %  ENV_CACHE_LINE_SIZE) == 0, CORE_RCVD_EV_CNT_ARR_SIZE_ERROR);




/** Array containing dummy test data */
ENV_SHARED  static  uint8_t  event_group_test_data[DATA_EVENTS * DATA_PER_EVENT]  ENV_CACHE_LINE_ALIGNED;



/*
 * Local function prototypes
 */
static em_status_t
egroup_start(void* eo_context, em_eo_t eo);

static em_status_t
egroup_stop(void* eo_context, em_eo_t eo);

static void
egroup_receive(void* eo_context, em_event_t event, em_event_type_t type, em_queue_t queue, void* q_ctx);

static void
delay_spin(const uint64_t spin_count);



/**
 * Init and startup of the Event Group test application.
 *
 * @see main() and example_start() for setup and dispatch.
 */
void
test_init(example_conf_t *const example_conf)
{
  em_eo_t             eo;
  eo_context_t*       eo_ctx;
  em_queue_t          queue;
  em_event_t          event;
  event_group_test_t* egroup_test;
  em_status_t         ret;
  em_status_t         eo_start_ret; // return value from the EO's start function 'group_start'
  
  
  /*
   * Initializations only on one EM-core, return on all others.
   */  
  if(em_core_id() != 0)
  {
    return;
  }


  printf("\n**********************************************************************\n"
         "EM APPLICATION: '%s' initializing: \n"
         "  %s: %s() - EM-core:%i \n"
         "  Application running on %d EM-cores (procs:%d, threads:%d)."
         "\n**********************************************************************\n"
         "\n"
         ,
         example_conf->name,
         NO_PATH(__FILE__), __func__,
         em_core_id(),
         em_core_count(),
         example_conf->num_procs,
         example_conf->num_threads);
         
  
  /*
   * Create the event group test EO and a parallel queue, add the queue to the EO
   */
  eo_ctx = &event_group_test_eo_context.eo_ctx;
  
  eo    = em_eo_create("group test appl", egroup_start, NULL, egroup_stop, NULL, egroup_receive, eo_ctx);  
  queue = em_queue_create("group test parallelQ", EM_QUEUE_TYPE_PARALLEL, EM_QUEUE_PRIO_NORMAL, EM_QUEUE_GROUP_DEFAULT);

  ret = em_eo_add_queue(eo, queue);
  IS_ERROR(ret != EM_OK, "EO or queue creation failed (%u). EO: %"PRI_EO", queue: %"PRI_QUEUE"\n", ret, eo, queue);
  
  ret = em_queue_enable(queue);
  IS_ERROR(ret != EM_OK, "Queue enable failed (%u). EO: %"PRI_EO", queue: %"PRI_QUEUE"\n", ret, eo, queue);
  
  // Start the EO (triggers the EO's start function 'group_start')
  ret = em_eo_start(eo, &eo_start_ret, 0, NULL);
  
  IS_ERROR((ret != EM_OK) || (eo_start_ret != EM_OK),
           "em_eo_start() failed! EO: %"PRI_EO", ret: %u, EO-start-ret: %u \n", eo, ret, eo_start_ret);


  event = em_alloc(sizeof(event_group_test_t), EM_EVENT_TYPE_SW, EM_POOL_DEFAULT);
  IS_ERROR(event == EM_EVENT_UNDEF, "Event allocation failed! \n");
  

  egroup_test = em_event_pointer(event);
  
  egroup_test->msg = MSG_START;

  ret = em_send(event, queue);
  IS_ERROR(ret != EM_OK, "Event send failed (%u)! queue: %"PRI_QUEUE" \n", ret, queue);
}  



/**
 * @private
 * 
 * EO start function.
 *
 * Creates the event group used in this test case.
 *
 */
static em_status_t
egroup_start(void* eo_context, em_eo_t eo)
{
  eo_context_t* eo_ctx = eo_context;
  
  (void) memset(eo_ctx, 0, sizeof(eo_context_t));

  eo_ctx->event_group = em_event_group_create();
  
  if(eo_ctx->event_group == EM_EVENT_GROUP_UNDEF) {
    return EM_ERR_ALLOC_FAILED;
  }
  
  (void) memset(core_stats, 0, sizeof(core_stats));
  
  printf("EO:%"PRI_EO" - event group %"PRI_EGRP" created\n", eo, eo_ctx->event_group);
  
  // Require 'DATA_EVENTS' sent using the event_group before a notification, see em_event_group_apply() later
  eo_ctx->event_count = DATA_EVENTS;

  return EM_OK;
}



/**
 * @private
 * 
 * EO stop function.
 *
 */
static em_status_t
egroup_stop(void* eo_context, em_eo_t eo)
{
  return EM_OK;
}



/**
 * @private
 * 
 * EO receive function.
 *
 */
static void
egroup_receive(void* eo_context, em_event_t event, em_event_type_t type, em_queue_t queue, void* q_ctx)
{
  event_group_test_t* egroup_test;
  em_notif_t          notif_tbl[1];
  em_status_t         ret;
  uint64_t            diff;
  uint64_t            sum;
  int                 i;
  eo_context_t*       eo_ctx = eo_context;

  egroup_test = em_event_pointer(event);

  switch(egroup_test->msg)
  {
    case MSG_START:    
      {
        /*
         * (Re)start the test by configuring the event group, allocating all data events and
         * then sending (with event gourp) all events back to itself.
         */
         
        printf("\n" "Start." "\n");
        
        eo_ctx->acc = 0;
        
        
        // Re-use the start event as the notification event
        egroup_test->msg          = MSG_DONE;
        egroup_test->data_ptr     = NULL;
        egroup_test->start_cycles = env_get_cycle();
        egroup_test->increment    = 0;
        
        // The notification 'event' should be sent to 'queue' when done
        notif_tbl[0].event = event;
        notif_tbl[0].queue = queue;
        
        /*
         * Request one notification event when 'eo_ctx->event_count' events from 'eo_ctx->event_group' have been received.
         * Note that the em_event_group_increment() functionality is used in the event:MSG_DATA processing so
         * the total number of events received will be larger than 'eo_ctx->event_count' before the notification
         * 'MSG_DONE' is received.
         */
        ret = em_event_group_apply(eo_ctx->event_group, eo_ctx->event_count, 1, notif_tbl);
        IS_ERROR(ret != EM_OK, "em_event_group_apply() failed (%u)! \n", ret);
        
        
        
        // Allocate 'eo_ctx->event_count' number of DATA events and send using the event group
        // to trigger the notification event configured above with em_event_group_apply()
        for(i = 0; i < eo_ctx->event_count; i++)
        {
          em_event_t          data;
          event_group_test_t* group_test_2;
        
          data = em_alloc(sizeof(event_group_test_t), EM_EVENT_TYPE_SW, EM_POOL_DEFAULT);
          IS_ERROR(data == EM_EVENT_UNDEF, "Event allocation failed! \n");
          
          group_test_2 = em_event_pointer(data);
          
          group_test_2->msg          = MSG_DATA;
          group_test_2->data_ptr     = &event_group_test_data[i*DATA_PER_EVENT];
          group_test_2->start_cycles = 0;
          group_test_2->increment    = EVENT_GROUP_INCREMENT; // how many times to increment and re-send
          
          // Send events using the event group.
          em_send_group(data, queue, eo_ctx->event_group);
        }
      }
      break;



    case MSG_DATA:
      {
        /*
         * Do some dummy data processing:
         * Calculate a sum over the event and increment a shared variable.
         * For a correct result the shared variable 'eo_ctx->acc' should be updated
         * using an atomic-add because the used queue type is parallel.
         * Don't care about the result however, only prevent the optimizer 
         * from removing the data processing loop.
         */
        sum = 0;
        
        for(i = 0; i < DATA_PER_EVENT; i++)
        {
          sum += egroup_test->data_ptr[i];
        }
        
        eo_ctx->acc += sum;
        
        
        // update the count of data events received on this core
        core_stats[em_core_id()].rcv_ev_cnt += 1;
        
        
        /*
         * Test the em_event_group_increment() functionality.
         * Note that the total number of events received will become larger than
         * 'eo_ctx->event_count' before the notification 'MSG_DONE' is received.
         */
        if(egroup_test->increment)
        {
          em_event_group_t event_group;
        
          egroup_test->increment--;
        
          // increment event count in group
          (void) em_event_group_increment(1);
        
          // get the current event group
          event_group = em_event_group_current();
        
          // re-send event using the event group
          (void) em_send_group(event, queue, event_group);
        }
        else
        {
          // free event
          em_free(event);
        }
      }
      break;



    case MSG_DONE:
      {
        /*
         * Notification event received!
         * 
         * Calulate the number of cycles it took and restart the test.
         */
        uint64_t     rcv_ev_cnt = 0;
        uint64_t     cfg_ev_cnt;
        
         
        // Calculate results. Ignore first round because of cold caches.
        diff = env_get_cycle() - egroup_test->start_cycles;
      
        if(eo_ctx->total_rounds == 0) {
          /* Ignore */
        }
        else if(eo_ctx->total_rounds == 1) {
          eo_ctx->total_cycles += 2*diff;
        }
        else {
          eo_ctx->total_cycles += diff;
        }
      
        eo_ctx->total_rounds++;
      
        
        // Sum up the amount of data events processed on each core 
        for(i = 0; i < em_core_count(); i++) {
          rcv_ev_cnt += core_stats[i].rcv_ev_cnt;
        }
        
        // The expected number of data events processed to trigger a notification event
        cfg_ev_cnt = (DATA_EVENTS * (1 + EVENT_GROUP_INCREMENT));
      
        // Verify that the amount of received data events prior to this notification event is correct
        IS_ERROR(rcv_ev_cnt != cfg_ev_cnt, 
                 "Incorrect amount of data events prior to notification: %"PRIu64" != %"PRIu64"! \n",
                 rcv_ev_cnt, cfg_ev_cnt);
        
        
        // OK, print results
        printf("Done. Notification event received after %"PRIu64" data events. Cycles curr:%"PRIu64", ave:%"PRIu64" \n",
               rcv_ev_cnt, diff, eo_ctx->total_cycles / eo_ctx->total_rounds);        
      
         
        // Restart the test after "some cycles" of delay
        delay_spin(DELAY_SPIN_COUNT);
        
        (void) memset(core_stats, 0, sizeof(core_stats));
        
        egroup_test->msg = MSG_START;
      
        ret = em_send(event, queue);
        IS_ERROR(ret != EM_OK, "Event send failed (%u)! queue: %"PRI_QUEUE" \n", ret, queue);
      }
      break;


    default:
      IS_ERROR(1, "Bad msg (%"PRIu64")! \n", egroup_test->msg);
  };

}



static void
delay_spin(const uint64_t spin_count)
{
  volatile uint64_t dummy = 0;
  uint64_t          i;
  
  
  for(i = 0; i < spin_count; i++) {
    dummy++;
  }
}

