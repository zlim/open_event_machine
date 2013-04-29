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
 * Event Machine performance test example
 *
 * Measures the average cycles consumed during an event send - sched - receive loop
 *
 */

#include "event_machine.h"
#include "environment.h"

#include <inttypes.h>
#include <string.h>
#include <stdio.h>




/** 
 * Number of test EOs and queues. Must be an even number.
 * Test has NUM_EO/2 EO pairs, that send ping-pong events.
 * Depending on test dynamics (e.g. single burst in atomic 
 * queue) only one EO of a pair might be active at a time.
 */
#define NUM_EO            128

/** Number of ping-pong events per EO pair. */
#define NUM_EVENT         8
//#define NUM_EVENT       256 // Try increasing the number of events to tune performance

/** Number of data bytes in the event */
#define DATA_SIZE         512

/** Max number of cores */
#define MAX_NBR_OF_CORES  256

/** The number of events to be received beore printing a result */
#define PRINT_EVENT_COUNT 0x400000

/** EM Queue type used */
#define QUEUE_TYPE        EM_QUEUE_TYPE_ATOMIC
//#define QUEUE_TYPE      EM_QUEUE_TYPE_PARALLEL
//#define QUEUE_TYPE      EM_QUEUE_TYPE_PARALLEL_ORDERED




/*
 * Per event processing options
 */

/** Alloc and free per event */
//#define ALLOC_FREE_PER_EVENT

/** memcpy per event */
//#define MEMCPY_PER_EVENT

/** Check sequence numbers, works only with atomic queues */
//#define CHECK_SEQ_PER_EVENT




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
 * Performance test statistics (per core)
 */
typedef union
{
  uint8_t u8[ENV_CACHE_LINE_SIZE] ENV_CACHE_LINE_ALIGNED;

  struct
  {
    uint64_t events;
    uint64_t begin_cycles;
    uint64_t end_cycles;
    uint64_t print_count;
  };

}perf_stat_t;


/** Array of core specific data accessed by a core using its core index. */
ENV_SHARED  static  perf_stat_t  core_stat[MAX_NBR_OF_CORES];

COMPILE_TIME_ASSERT( sizeof(perf_stat_t) == ENV_CACHE_LINE_SIZE,       PERF_STAT_T_SIZE_ERROR);
COMPILE_TIME_ASSERT((sizeof(core_stat)   %  ENV_CACHE_LINE_SIZE) == 0, CORE_STAT_ARR_SIZE_ERROR);




/**
 * Performance test EO context
 */
typedef struct 
{
  /** EO context id */
  em_eo_t id;
  
  /** Next sequence number (used with CHECK_SEQ_PER_EVENT) */
  int next_seq;
}eo_context_t;



/**
 * EO context padded to cache line size
 */
typedef union
{
  uint8_t u8[ENV_CACHE_LINE_SIZE] ENV_CACHE_LINE_ALIGNED;
  
  eo_context_t  eo_ctx;
  
} eo_context_array_elem_t;

COMPILE_TIME_ASSERT(sizeof(eo_context_array_elem_t) == ENV_CACHE_LINE_SIZE, PERF_EO_CONTEXT_SIZE_ERROR);


/** EO context array */
ENV_SHARED static  eo_context_array_elem_t  perf_eo_context[NUM_EO];





/**
 * Performance test event
 */
typedef struct
{
  /** Next destination queue */
  em_queue_t dest;

  /** Sequence number */
  int seq;

  /** Test data */
  uint8_t data[DATA_SIZE];

} perf_event_t;


/* 
 * Local function prototypes
 */
static void
print_result(perf_stat_t *const perf_stat);





/**
 * @private
 *
 * EO start function.
 *
 */
static em_status_t
start(void* eo_context, em_eo_t eo)
{
  eo_context_t* eo_ctx = eo_context;
  
  printf("EO %"PRI_EO" starting.\n", eo);

  eo_ctx->id = eo;

  return EM_OK;
}




/**
 * @private
 *
 * EO stop function.
 *
 */
static em_status_t
stop(void* eo_context, em_eo_t eo)
{
  printf("EO %"PRI_EO" stopping.\n", eo);

  return EM_OK;
}




/**
 * @private
 *
 * EO receive function for EO A.
 *
 * Loops back events and calculates the event rate.
 */
static void
receive_a(void* eo_context, em_event_t event, em_event_type_t type, em_queue_t queue, void* q_ctx)
{
  perf_event_t *perf;
  em_queue_t    dest_queue;
  int           core;
  uint64_t      events;

#ifdef CHECK_SEQ_PER_EVENT
  int           seq;
#endif
  
  core   = em_core_id();
  events = core_stat[core].events;
  perf   = em_event_pointer(event);
  
  dest_queue = perf->dest;


  /*
   * Update the cycle count and print results when necessary
   */
  if(ENV_UNLIKELY(events == 0))
  {
    core_stat[core].begin_cycles = env_get_cycle();
    events += 1;
  }
  else if(ENV_UNLIKELY(events > PRINT_EVENT_COUNT))
  {
    core_stat[core].end_cycles   = env_get_cycle();
    core_stat[core].print_count += 1;
    
    /* Print measurement result */
    print_result(&core_stat[core]);
    
    /* Restart the measurement */
    core_stat[core].begin_cycles = env_get_cycle();
    events = 0;
  }
  else {
    events += 1;
  }
  


#ifdef CHECK_SEQ_PER_EVENT
  seq = perf->seq;
  
  if(ENV_UNLIKELY(seq != eo_ctx->next_seq))
  {
    printf("Bad sequence number. EO(A) %"PRI_EO", queue %"PRI_QUEUE", expected seq %i, event seq %i \n",
           eo_ctx->id, queue, eo_ctx->next_seq, seq);
  }
  
  if(ENV_LIKELY(eo_ctx->next_seq < (NUM_EVENT - 1))) {
    eo_ctx->next_seq++;
  }
  else {
    eo_ctx->next_seq = 0;
  }
#endif



#ifdef MEMCPY_PER_EVENT
  {
    uint8_t* from = &perf->data[0];
    uint8_t* to   = &perf->data[DATA_SIZE/2];
    
    memcpy(to, from, DATA_SIZE/2);
  }
#endif



#ifdef ALLOC_FREE_PER_EVENT
  em_free(event);  
  event = em_alloc(sizeof(perf_event_t), EM_EVENT_TYPE_SW, EM_POOL_DEFAULT);
  perf  = em_event_pointer(event);

  #ifdef CHECK_SEQ_PER_EVENT
  perf->seq = seq;
  #endif

#endif
  
  
  perf->dest = queue;

  em_send(event, dest_queue);

  core_stat[core].events = events;
}




/**
 * @private
 *
 * EO receive function for EO B.
 *
 * Loops back events.
 */
static void
receive_b(void* eo_context, em_event_t event, em_event_type_t type, em_queue_t queue, void* q_ctx)
{
  perf_event_t *perf;
  em_queue_t    dest_queue;
  int           core;
  uint64_t      events;
  
#ifdef CHECK_SEQ_PER_EVENT
  int           seq;
  eo_context_t* eo_ctx = eo_context;
#endif


  perf = em_event_pointer(event);

  dest_queue = perf->dest;

  
#ifdef CHECK_SEQ_PER_EVENT
  seq = perf->seq;
  
  if(ENV_UNLIKELY(seq != eo_ctx->next_seq))
  {
    printf("Bad sequence number. EO(B) %"PRI_EO", queue %"PRI_QUEUE", expected seq %i, event seq %i \n",
           eo_ctx->id, queue, eo_ctx->next_seq, seq);
  }

  if(ENV_LIKELY(eo_ctx->next_seq < (NUM_EVENT - 1))) {
    eo_ctx->next_seq++;
  }
  else {
    eo_ctx->next_seq = 0;
  }
#endif



#ifdef MEMCPY_PER_EVENT
  {
    uint8_t* from = &perf->data[DATA_SIZE/2];
    uint8_t* to   = &perf->data[0];

    memcpy(to, from, DATA_SIZE/2);
  }
#endif



#ifdef ALLOC_FREE_PER_EVENT
  em_free(event);
  event = em_alloc(sizeof(perf_event_t), EM_EVENT_TYPE_SW, EM_POOL_DEFAULT);
  perf  = em_event_pointer(event);
  
  #ifdef CHECK_SEQ_PER_EVENT
  perf->seq = seq;
  #endif

#endif


  core   = em_core_id();
  events = core_stat[core].events + 1;


  perf->dest = queue;

  em_send(event, dest_queue);

  core_stat[core].events = events;
}




/**
 * Global init and startup of the performance test application.
 *
 */
void
test_appl_perf_start(void)
{
  em_eo_t          eo;
  eo_context_t*    eo_ctx;
  em_queue_t       queue_a, queue_b;
  em_event_t       event;
  perf_event_t*    perf;
  em_status_t      ret;
  int              i, j;
  
  
  (void) memset(core_stat, 0, sizeof(core_stat));

  /*
   * Create and start application pairs
   * Send initial test events to the queues
   */
  for(i = 0; i < (NUM_EO/2); i++)
  {
    /*
     * Create EO "A"
     */
    eo_ctx = &perf_eo_context[2*i].eo_ctx;

    eo      = em_eo_create("appl a", start, NULL, stop, NULL, receive_a, eo_ctx);
    queue_a = em_queue_create("queue A", QUEUE_TYPE, EM_QUEUE_PRIO_NORMAL, EM_QUEUE_GROUP_DEFAULT);

    eo_ctx->next_seq = 0;

    ret = em_eo_add_queue(eo, queue_a);
    IS_ERROR(ret != EM_OK, "EO or queue creation failed (%u). EO: %"PRI_EO", queue: %"PRI_QUEUE"\n", ret, eo, queue_a);

    ret = em_eo_start(eo, NULL, 0, NULL);
    IS_ERROR(ret != EM_OK, "EO start failed (%u). EO: %"PRI_EO"\n", ret, eo);

    ret = em_queue_enable(queue_a);
    IS_ERROR(ret != EM_OK, "Queue A enable failed (%u). EO: %"PRI_EO", queue: %"PRI_QUEUE"\n", ret, eo, queue_a);


    /*
     * Create EO "B"
     */
    eo_ctx = &perf_eo_context[2*i + 1].eo_ctx;

    eo      = em_eo_create("appl b", start, NULL, stop, NULL, receive_b, eo_ctx);
    queue_b = em_queue_create("queue B", QUEUE_TYPE, EM_QUEUE_PRIO_NORMAL, EM_QUEUE_GROUP_DEFAULT);

    eo_ctx->next_seq = NUM_EVENT/2;

    ret = em_eo_add_queue(eo, queue_b);
    IS_ERROR(ret != EM_OK, "EO or queue creation failed (%u). EO: %"PRI_EO", queue: %"PRI_QUEUE"\n", ret, eo, queue_b);

    ret = em_eo_start(eo, NULL, 0, NULL);
    IS_ERROR(ret != EM_OK, "EO start failed (%i). EO: %"PRI_EO"\n", ret, eo);
    
    ret = em_queue_enable(queue_b);                                                                              
    IS_ERROR(ret != EM_OK, "Queue B enable failed (%u). EO: %"PRI_EO", queue: %"PRI_QUEUE"\n", ret, eo, queue_b);
    
    
    /*
     * Alloc and send test events
     */
    for(j = 0; j < NUM_EVENT; j++)
    {
      em_queue_t first_q;
      
      event = em_alloc(sizeof(perf_event_t), EM_EVENT_TYPE_SW, EM_POOL_DEFAULT);
      IS_ERROR(event == EM_EVENT_UNDEF, "Event allocation failed (%i, %i)\n", i, j);
      
      perf = em_event_pointer(event);

      
      if(j & 0x1) 
      {
        // Odd: j = 1, 3, 5, ...
        perf->seq  = NUM_EVENT/2 + j/2;  // 4, 5, 6, 7, ...  (NUM_EVENT/2 ... NUM_EVENT - 1)
        first_q    = queue_b;
        perf->dest = queue_a;
      }
      else
      {
        // Even: j = 0, 2, 4, ...
        perf->seq  = j/2;                // 0, 1, 2, 3, ...  (0 ... NUM_EVENT/2 - 1)   
        first_q    = queue_a;
        perf->dest = queue_b;
      }


      ret = em_send(event, first_q);
      IS_ERROR(ret != EM_OK, "Event send failed (%u)! Queue: %"PRI_QUEUE" \n", ret, first_q);
    }

  }
  

  env_sync_mem();
}




/**
 * Prints test measurement result
 */
static void
print_result(perf_stat_t *const perf_stat)
{
    uint64_t diff;
    uint32_t hz;
    double   mhz;
    double   cycles_per_event;
    uint64_t print_count;


    diff             = perf_stat->end_cycles - perf_stat->begin_cycles;
    print_count      = perf_stat->print_count;
    cycles_per_event = ((double) diff) / ((double) perf_stat->events);

    hz  = env_core_hz();
    mhz = ((double) hz) / 1000000.0;

    printf("cycles per event %.2f  @%.2f MHz (core-%02i %"PRIu64")\n", cycles_per_event, mhz, em_core_id(), print_count);
}



