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

/*  
 *  Copyright (c) 2012 Intel Corporation. All rights reserved.
 *
 *  Changed atomic queue structure to allow lockless operation
 */
 
#ifndef EM_INTEL_H_
#define EM_INTEL_H_

#include "event_machine.h"
#include "event_machine_group.h"
#include "event_machine_helper.h"

#include "environment.h"

// Generic double linked list
#include "misc_list.h"

#include <rte_config.h>
#include <semaphore.h>

#ifdef EVENT_TIMER
  #include "event_timer.h"  
#endif  




#ifdef __cplusplus
extern "C" {
#endif

// Max number of EM cores supported
#define EM_MAX_CORES           (RTE_MAX_LCORE)
// Keep smaller than 64 to fit in uint64_t core_mask
COMPILE_TIME_ASSERT(EM_MAX_CORES <= 64, TOO_MANY_CORES);


// EO pools
#define FIRST_EO               (0)
#define EO_POOLS               (32)
#define EOS_PER_POOL           (EM_MAX_EOS / EO_POOLS)

COMPILE_TIME_ASSERT(EM_MAX_EOS == (EOS_PER_POOL * EO_POOLS), EOS_PER_POOL__TRUNKATE_ERROR);



/* Round up 'val' to next multiple of 'N' */
#define ROUND_UP(val, N)  ((((val)+((N)-1))/(N)) * (N))
/* True if x is a power of 2 */
#define POWEROF2(x)       ((((x)-1) & (x)) == 0)

#define MIN(a, b)  (((a) < (b)) ? (a) : (b))
#define MAX(a, b)  (((a) > (b)) ? (a) : (b))


// Prefetch cache line of the address
#define EM_PREFETCH(addr)            ENV_PREFETCH((addr))

// Prefetch next cache line of the address
#define EM_PREFETCH_NEXT_LINE(addr)  ENV_PREFETCH_NEXT_LINE((addr))

#define PREFETCH_RTE_RING(rte_ring) {ENV_PREFETCH(&((rte_ring)->prod)); ENV_PREFETCH(&((rte_ring)->cons)); /*ENV_PREFETCH(&(rte_ring)->ring);*/}

#define PREFETCH_Q_ELEM(q_elem)     {ENV_PREFETCH((q_elem)); ENV_PREFETCH(&(((em_queue_element_t*)(q_elem))->lock));}




//
// EM internal queue ids
//

// A queue per core and a shared queue
#define FIRST_INTERNAL_QUEUE           (EM_QUEUE_STATIC_MAX + 1)
#define INTERNAL_QUEUES                (EM_MAX_CORES + 1)
#define LAST_INTERNAL_QUEUE            (FIRST_INTERNAL_QUEUE + INTERNAL_QUEUES - 1)
#define SHARED_INTERNAL_QUEUE          (LAST_INTERNAL_QUEUE)
// Priority for the EM-internal queues
#define INTERNAL_QUEUE_PRIORITY        (EM_QUEUE_PRIO_LOWEST) // (EM_QUEUE_PRIO_HIGHEST)

COMPILE_TIME_ASSERT(EM_MAX_CORES <= (INTERNAL_QUEUES-1), TOO_FEW_INTERNAL_QUEUES_ERROR);

// Dynamic queue ids
#define FIRST_DYN_QUEUE        ROUND_UP(LAST_INTERNAL_QUEUE + 1, 32) // Keep divisible by 32
#define DYN_QUEUES             (EM_MAX_QUEUES - FIRST_DYN_QUEUE)
#define DYN_QUEUE_POOLS        (32)
#define DYN_QUEUES_PER_POOL    (DYN_QUEUES / DYN_QUEUE_POOLS)

COMPILE_TIME_ASSERT(FIRST_DYN_QUEUE > LAST_INTERNAL_QUEUE, FIRST_DYN_QUEUE_IDX_ERROR);
COMPILE_TIME_ASSERT(DYN_QUEUES == (DYN_QUEUES_PER_POOL * DYN_QUEUE_POOLS), DYN_QUEUES_PER_POOL__TRUNKATE_ERROR);


// Static queue ids
#define STATIC_QUEUE_LOCKS     (32)
COMPILE_TIME_ASSERT(POWEROF2(STATIC_QUEUE_LOCKS), STATIC_QUEUE_LOCKS__NOT_POWER_OF_TWO); // Must be power-of-two, used as mask (nbr-1)



/*
 * Macros
 */
#define invalid_queue(queue)   (ENV_UNLIKELY((queue) >= EM_MAX_QUEUES))

#define invalid_q_elem(q_elem) (ENV_UNLIKELY( (((uint64_t)(q_elem)) < ((uint64_t)&em.shm->em_queue_element_tbl[0])) \
                                           || (((uint64_t)(q_elem)) > ((uint64_t)&em.shm->em_queue_element_tbl[EM_MAX_QUEUES-1])) ))

#define invalid_eo(eo)         (ENV_UNLIKELY((eo) >= EM_MAX_EOS))



/**
 * EM internal shared run-time configuration
 *
 * @note one per EM-instance (i.e. shared by all EM processes and threads)
 */
typedef union
{
  struct
  {
    int em_instance_id; /**< Event Machine Instance Id */
    
    env_barrier_t barrier;
    
    /* Add further internal config */
  };
  
  uint8_t u8[ENV_CACHE_LINE_SIZE];
  
} em_shared_conf_t;



/**
 * EM internal run-time configuration options stored at startup
 *
 * @note one per process
 */
typedef union
{
  struct
  {
    em_shared_conf_t    *shared; /**< Ptr to the EM master conf - shared by all procs&threads in the EM instance */    
    
    em_conf_t            conf;        /**< Copy of config as given to em_init() */
    
    /* Add further internal config */
  };
  
  uint8_t u8[ENV_CACHE_LINE_SIZE];
  
} em_internal_conf_t;

COMPILE_TIME_ASSERT(sizeof(em_internal_conf_t) == ENV_CACHE_LINE_SIZE, EM_INTERNAL_CONF_T__SIZE_ERROR);



/*
 * EO state
 *
 * Note: Cache-line alignement statements in 'em_eo_element_t' 
 * force array of 'em_eo_element_t' to be a multiple of cache line size.
 */
typedef struct
{
  // eo list element
  m_list_head_t           list_head  ENV_CACHE_LINE_ALIGNED;

  char                    name[EM_EO_NAME_LEN];

  em_start_func_t         start_func;
  
  em_start_local_func_t   start_local_func;

  em_stop_func_t          stop_func;
  
  em_stop_local_func_t    stop_local_func;

  em_receive_func_t       receive_func; // Note: copy of this in q_elem for perf

  em_error_handler_t      error_handler_func;
  
  void                   *eo_ctx;       // Note: copy of this in q_elem for perf

  // head of queue list
  m_list_head_t           queue_list;

  em_eo_t                 id;    /**< eo table index */
  uint8_t                 pool;  /**< eo pool index */
  
} em_eo_element_t  ENV_CACHE_LINE_ALIGNED;




//
// Queue element status
// 
#define EM_QUEUE_STATUS_INVALID 0
#define EM_QUEUE_STATUS_INIT    1
#define EM_QUEUE_STATUS_BIND    2
#define EM_QUEUE_STATUS_READY   3

#define EM_QUEUE_ATOMIC_RTE_RING_SIZE        (4*1024) // Atomic: Event queue
#define EM_QUEUE_PARALLEL_ORD_RTE_RING_SIZE  (1024)// (512)    // Parallel-Ordered: Order queue

/**
 * Queue element
 */

typedef struct
{
  /* --------- CACHE LINE ----------- */

  // Actual Queue (ring buf) - Atomic: event-queue, Parallel-Ordered: order-queue, Parallel: not used
  struct rte_ring           *rte_ring  ENV_CACHE_LINE_ALIGNED;
  
  // Atomic, parallel or parallel-ordered queue
  em_queue_type_t            scheduler_type;
  
  // Queue priority
  em_queue_prio_t            priority;

  // The queue group idx for this queue
  em_queue_group_t           queue_group;
  
  // Queue status
  uint32_t                   status;

  // Queue-ID (queue table index)
  em_queue_t                 id;

  // Copy of receive the function and object pointer for better performance
  em_receive_func_t          receive_func;
  
  // User defined eo context (can be NULL)
  void                      *eo_ctx;        
  
  // User defined queue context (can be NULL)
  void                      *context;       
  

  /* --------- CACHE LINE ----------- */

  // Queue specific lock (needed by atomic and parallel-ordered queues)
  env_spinlock_t    lock  ENV_CACHE_LINE_ALIGNED;

  union
  {
    // for Atomic queues:
    union {
	
      struct {
        // Is this atomic queue already scheduled?
        volatile int32_t  sched_count;
        // The number of events for this queue?
        volatile int32_t  event_count;
      };
	  
      volatile  int64_t atomic_counts;
      volatile uint64_t atomic_counts_u64; // for funcs expecting uint64_t
	  
    } atomic;

    // for Parallel-Ordered queues:
    struct 
    {
      // pointer to the event that was received "first"
      void *volatile order_first;
	  
    } parallel_ord;
  } u;



  /* init/ctrl functions read/write */
  
  // Internal eo context
  em_eo_element_t           *eo_elem;           
  
  // Queue pool index
  uint8_t                    pool;              
  
  // Static queue id allocated or free
  uint8_t                    static_allocated;  

  // Is this queue set up to receive events from Packet-I/O
  uint8_t                    pkt_io_enabled;
  // If pkt_io_enabled set: contains the configured pkt-io params
  uint8_t                    pkt_io_proto;
  uint32_t                   pkt_io_ipv4_dst;
  uint16_t                   pkt_io_port_dst;

  // Linked-list of q_elems
  m_list_head_t              list_node;
  
  
  /* --------- CACHE LINE ----------- */
  
  // Linked-list of q_elems used by the em_queue_group_element_t to keep track of all queues in the queue group
  m_list_head_t              qgrp_node  ENV_CACHE_LINE_ALIGNED;
  
} em_queue_element_t;


COMPILE_TIME_ASSERT(sizeof(em_queue_element_t) <= (3*ENV_CACHE_LINE_SIZE),      EM_QUEUE_ELEMENT_T__SIZE_ERROR);
COMPILE_TIME_ASSERT(offsetof(em_queue_element_t, lock) == ENV_CACHE_LINE_SIZE, EM_QUEUE_ELEMENT_T__ALIGN_ERROR);
COMPILE_TIME_ASSERT(offsetof(em_queue_element_t, qgrp_node) == (2*ENV_CACHE_LINE_SIZE), EM_QUEUE_ELEMENT_T__ALIGN_ERROR2);


/**
 * Event header
 *
 * SW & I/O originated events.
 */

typedef union em_event_hdr_
{
  // 128 bytes reserved for event header
  uint8_t u8[RTE_PKTMBUF_HEADROOM];

  struct
  {
    em_queue_element_t  *q_elem;
    em_queue_type_t      src_q_type;
    em_event_type_t      event_type;
    em_event_group_t     event_group;
   
    // Parallel-ordered only
    env_spinlock_t      *volatile lock_p;
    em_queue_element_t  *volatile dst_q_elem;
    volatile int         processing_done;
    volatile int         operation;   
    
    // Packet-io only
    int                  io_port;
    
  #ifdef EVENT_TIMER
    struct rte_timer     event_timer  ENV_CACHE_LINE_ALIGNED; // Keep cache line aligned!
    em_queue_t           timer_dst_queue;
  #endif
  };

} em_event_hdr_t;


COMPILE_TIME_ASSERT(sizeof(em_event_hdr_t) == RTE_PKTMBUF_HEADROOM, EM_EVENT_HDR_SIZE_ERROR1);
#ifdef EVENT_TIMER
  // Note: 'timer_dst_queue' is assumed to be the LAST field in the struct!
  COMPILE_TIME_ASSERT((offsetof(em_event_hdr_t, timer_dst_queue) + sizeof(em_queue_t)) <= (2*ENV_CACHE_LINE_SIZE), EM_EVENT_HDR_SIZE_ERROR2);
  COMPILE_TIME_ASSERT(offsetof(em_event_hdr_t, event_timer) == ENV_CACHE_LINE_SIZE, EM_EVENT_HDR_SIZE_ERROR3);
#else
  // Note: 'io_port' is assumed to be the LAST field in the struct!
  COMPILE_TIME_ASSERT((offsetof(em_event_hdr_t, io_port) + sizeof(int)) <= ENV_CACHE_LINE_SIZE, EM_EVENT_HDR_SIZE_ERROR2);
#endif



#if 0
typedef union em_event_pool_u
{
  void*    pool;
  
  uint8_t  u8[ENV_CACHE_LINE_SIZE]; 
  
} em_event_pool_t;

COMPILE_TIME_ASSERT(sizeof(em_event_pool_t) == ENV_CACHE_LINE_SIZE, EM_EVENT_POOL_T__SIZE_ERROR);
#else
typedef void* em_event_pool_t;
#endif


typedef union
{
  uint8_t u8[ENV_CACHE_LINE_SIZE];

  struct
  {
    env_spinlock_t  lock;
    m_list_head_t   list_head;
  };

} em_pool_t  ENV_CACHE_LINE_ALIGNED;

COMPILE_TIME_ASSERT(sizeof(em_pool_t) == ENV_CACHE_LINE_SIZE, EM_POOL_T__SIZE_ERROR);



// Number of EM cores
typedef union
{

  struct
  {
    int count;

    // From physical cores ids to logical EM core ids
    uint8_t logic[EM_MAX_CORES];

    // From logical EM core ids to physical core ids
    uint8_t phys[EM_MAX_CORES];


    // Mask of logic core IDs
    em_core_mask_t logic_mask;

    // Mask of phys core IDs
    em_core_mask_t phys_mask;
  };


  uint8_t u8[2 * ENV_CACHE_LINE_SIZE];

} em_core_map_t;

COMPILE_TIME_ASSERT((sizeof(em_core_map_t) % ENV_CACHE_LINE_SIZE) == 0, EM_CORE_MAP_T__SIZE_ERROR);



/**
 *  EM core local variables
 */

typedef union
{
  // Core local variables:
  struct {
    
    // Points to the current queue element during a receive call
    em_queue_element_t    *current_q_elem;
    
    // Points to the current queue element during a receive call
    em_event_group_t       current_event_group;
    
    // Current group mask for this core
    uint64_t               current_group_mask;
    
    // The number of times queue create has been called
    uint64_t               queue_create_count;
    
    // The number of errors on a core
    uint64_t               error_count;
    
    // Error condition (true/false) used by the ERROR_IF() macro
    int                    error_cond;
    
  };
  
  // Guarantees that size is 1*cache-line-size
  uint8_t u8[ENV_CACHE_LINE_SIZE];

} em_core_local_t;


COMPILE_TIME_ASSERT((sizeof(em_core_local_t) % ENV_CACHE_LINE_SIZE) == 0, EM_CORE_LOCAL_DATA_SIZE_ERROR);  



/**
 * EM spinlock - Cache line sized & aligned
 */
typedef union
{
  uint8_t u8[ENV_CACHE_LINE_SIZE];

  struct
  {
    env_spinlock_t  lock;
  };

} em_spinlock_t  ENV_CACHE_LINE_ALIGNED;

COMPILE_TIME_ASSERT(sizeof(em_spinlock_t) == ENV_CACHE_LINE_SIZE, EM_STATIC_QUEUE_LOCK_T__SIZE_ERROR);



/**
 * Queues/rings containing free rte_rings for em_queue_create()/queue_init() to
 * use as q_elem->rte_rings for atomic and parallel-ordered EM queues.
 * Parallel EM queues do not require EM queue specific rings - all events are 
 * handled directly through the scheduling queues.
 */
typedef union
{
  struct
  {
    struct rte_ring *atomic_rings;
    struct rte_ring *parallel_ord_rings;
  };
  
  uint8_t u8[ENV_CACHE_LINE_SIZE];
  
} queue_init_rings_t;



/*
 * Externs
 */
extern ENV_LOCAL  em_core_local_t     em_core_local;
extern            em_internal_conf_t  em_internal_conf;  



/*
 * Functions
 */

/**
 * Global initialization of EM internals. Only one core calls this function once.
 *
 * @return status, EM_OK on success
 */
em_status_t
em_init_global(const em_internal_conf_t *const em_internal_conf);


/**
 * Local initialization of EM internals. All cores call this and it must be called
 * after em_init_global().
 * Implementation may be actually empty, but this might be needed later for some
 * core specific initializations.
 *
 * @return status, EM_OK on success
 */
em_status_t
em_init_local(const em_internal_conf_t *const em_internal_conf);


void
queue_init(const char*      name,
           em_queue_t       queue,
           em_queue_type_t  type,
           em_queue_prio_t  prio,
           em_queue_group_t group);



void em_print_info(void);
                

#ifdef __cplusplus
}
#endif

#endif  // EM_INTEL_H_
