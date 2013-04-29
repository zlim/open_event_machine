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
#include <string.h>
#include <stdint.h>
#include <errno.h>
//#include <assert.h>

#include <rte_memory.h>
#include <rte_memzone.h>
#include <rte_launch.h>
#include <rte_eal.h>
#include <rte_per_lcore.h>
#include <rte_lcore.h>
#include <rte_debug.h>

/* For RTE-DEBUG_ if enabled in target-config */
#include <stdarg.h>
#include <rte_log.h>

#include "em_intel.h"
#include "environment.h"

#include "em_intel_sched.h"
#include "em_intel_event_group.h"
#include "em_intel_queue_group.h"
#include "em_internal_event.h"
#include "em_error.h"

#include "intel_hw_init.h"
#include "intel_alloc.h"

#ifdef EVENT_PACKET
  #include "em_intel_packet.h"
#endif

#ifdef EVENT_TIMER
  #include "event_timer.h"
#endif



/*
 * Defines
 */


// EO pools
#define FIRST_EO               (0)
#define EO_POOLS               (32)
#define EOS_PER_POOL           (EM_MAX_EOS / EO_POOLS)

COMPILE_TIME_ASSERT(EM_MAX_EOS == (EOS_PER_POOL * EO_POOLS), EOS_PER_POOL__TRUNKATE_ERROR);


#define EM_Q_BASENAME          "EM_Q_"



/*
 * Macros
 */



/*
 * Data Types
 */

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




/*
 * Global variables
 */

// EO table
ENV_SHARED  em_eo_element_t  em_eo_element_tbl[EM_MAX_EOS]  ENV_CACHE_LINE_ALIGNED;
COMPILE_TIME_ASSERT((sizeof(em_eo_element_tbl) % ENV_CACHE_LINE_SIZE) == 0, EM_EO_ELEMENT_TABLE_SIZE_ERROR);


// EM Pool table
ENV_SHARED  em_pool_t        em_eo_pool[EO_POOLS]           ENV_CACHE_LINE_ALIGNED;
COMPILE_TIME_ASSERT((sizeof(em_eo_pool) % ENV_CACHE_LINE_SIZE) == 0, EM_EO_POOL_TABLE_SIZE_ERROR);


// Event Pool (using mbuf-lib), force on own cache line
ENV_SHARED  em_event_pool_t  em_event_pool  ENV_CACHE_LINE_ALIGNED;



// Queue element table
ENV_SHARED  em_queue_element_t      em_queue_element_tbl[EM_MAX_QUEUES] ENV_CACHE_LINE_ALIGNED;       // Static queues first followed by dynamic queues
// EM dynamic queue pool
ENV_SHARED  em_pool_t               em_dyn_queue_pool[DYN_QUEUE_POOLS]  ENV_CACHE_LINE_ALIGNED;       // Dynamic queue ID FIFOs
// Spinlocks for the static numbered queues
ENV_SHARED  em_spinlock_t           em_static_queue_lock[STATIC_QUEUE_LOCKS] ENV_CACHE_LINE_ALIGNED;  // Static queue ID locks
// Queue name table
ENV_SHARED  char                    em_queue_name_tbl[EM_MAX_QUEUES][EM_QUEUE_NAME_LEN]  ENV_CACHE_LINE_ALIGNED;
// Lock used by queue_init() to serialize rte_ring_create() calls
ENV_SHARED  em_spinlock_t           queue_create_lock  ENV_CACHE_LINE_ALIGNED;



// Number of EM cores
typedef union
{

  struct
  {
    int count;

    // From physical cores ids to logical EM core ids
    uint8_t logic[MAX_CORES];

    // From logical EM core ids to physical core ids
    uint8_t phys[MAX_CORES];


    // Mask of logic core IDs
    em_core_mask_t logic_mask;

    // Mask of phys core IDs
    em_core_mask_t phys_mask;
  };


  uint8_t u8[2 * ENV_CACHE_LINE_SIZE];

} em_core_map_t;

ENV_SHARED  em_core_map_t  em_core_map  ENV_CACHE_LINE_ALIGNED;

COMPILE_TIME_ASSERT((sizeof(em_core_map) % ENV_CACHE_LINE_SIZE) == 0, EM_CORE_MAP_SIZE_ERROR);




/**
 * Core local variables collected into the same struct
 */
ENV_LOCAL  em_core_local_t  em_core_local  ENV_CACHE_LINE_ALIGNED
  = {
      {
      /* Set in local init() */
      .current_q_elem            = NULL,
      .current_event_group       = EM_EVENT_GROUP_UNDEF,
      .current_group_mask        = 0,
      .queue_create_count        = 0,
      .error_count               = 0,
      .error_cond                = 0
      }
    };



/**
 * EM core barrier
 */
ENV_SHARED  em_barrier_t  em_barrier  ENV_CACHE_LINE_ALIGNED;




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

/** Queues/rings of rte_rings for atomic and parallel-ordered EM queues (q_elem->rte_ring) */
ENV_SHARED  queue_init_rings_t  queue_init_rings  ENV_CACHE_LINE_ALIGNED;



/*
 * Local function prototypes
 */
static inline em_eo_t eo_alloc(void);
static inline void    eo_alloc_init(void);

static inline em_eo_element_t* get_current_eo_elem(void);

static inline void        queue_alloc_init(void);
static inline em_queue_t  queue_alloc(void);

static size_t str_copy(char* to, const char* from, size_t max);

static inline int get_static_queue_lock_idx(em_queue_t queue);

static void core_map_init(void);

static inline int phys_to_logic_core_id(const int phys_core);
static inline int logic_to_phys_core_id(const int logic_core);


static em_status_t em_eo_start_local(em_eo_element_t *const eo_elem, int num_notif, const em_notif_t* notif_tbl);
static em_status_t em_eo_stop_local(em_eo_element_t  *const eo_elem, int num_notif, const em_notif_t* notif_tbl);

static em_status_t
eo_local_func_call_req(em_eo_element_t *const eo_elem,
                       uint64_t               ev_id,
                       void (*f_done_callback)(void *arg_ptr),
                       void  *f_done_arg_ptr,
                       int                    num_notif,
                       const em_notif_t      *notif_tbl);

static void em_eo_start_local__done_callback(void *args);
static void em_eo_stop_local__done_callback(void *args);

static em_status_t      queue_init__rings_init(void);
static struct rte_ring *queue_init__ring_create(em_queue_type_e q_type);
static void             queue_init__ring_flush(struct rte_ring *ring_p);
static em_status_t      queue_delete__ring_free(struct rte_ring *ring_p, em_queue_type_e q_type);


/*
 * Execution object
 * ******************************************************
 */

/**
 * Allocate an EO
 */
static inline em_eo_t
eo_alloc(void)
{
  int              i;
  int              pool;
  em_eo_element_t *eo_elem;
  m_list_head_t   *node;


  // Pool selection tries to keep low competition on spinlocks.
  // Could be also a random number 0...EO_POOLS
  pool = em_core_id();


  for(i = 0; i < EO_POOLS; i++)
  {
    env_spinlock_lock(&em_eo_pool[pool].lock);

    node = m_list_rem_first(&em_eo_pool[pool].list_head);

    env_spinlock_unlock(&em_eo_pool[pool].lock);

    eo_elem = m_list_head_to_eo_elem(node);

    if(eo_elem != NULL)
    {
      // Alloc was OK
      // printf("eo_alloc: pool %i (%i)   eo %"PRI_EO" (0x%lx)\n", pool, eo_elem->pool, eo_elem->id, (uint64_t)eo_elem);
      return eo_elem->id;
    }

    // Pool was empty, try next pool
    pool++;

    if(pool == EO_POOLS)
    {
      pool = 0;
    }
  }

  // All pools were empty
  (void) EM_INTERNAL_ERROR(EM_ERR_ALLOC_FAILED, EM_ESCOPE_EO_ALLOC, "EO alloc failed! All EO-pools empty.");
  return EM_EO_UNDEF;
}



/**
 * Initialize EO allocations
 */
static inline void
eo_alloc_init(void)
{
  int i, j;
  em_eo_t eo;

  printf("eo alloc init\n");

  (void) memset(em_eo_element_tbl, 0, sizeof(em_eo_element_tbl));

  (void) memset(em_eo_pool, 0, sizeof(em_eo_pool));


  eo = FIRST_EO;

  //
  // Init and fill EO id pools
  //
  for(i = 0; i < EO_POOLS; i++)
  {
    env_spinlock_init(&em_eo_pool[i].lock);

    m_list_init(&em_eo_pool[i].list_head);


    for(j = 0; j < EOS_PER_POOL; j++)
    {
      em_eo_element_t* eo_elem;

      eo_elem = get_eo_element(eo);

      eo_elem->id   = eo;
      eo_elem->pool = i;

      m_list_add(&em_eo_pool[i].list_head, &eo_elem->list_head);
      eo++;
    }
  }

}



/**
 * Add a queue to an EO
 */
static inline void
eo_add_queue(em_eo_element_t    *eo_elem,
             em_queue_element_t *q_elem)
{
  q_elem->receive_func = eo_elem->receive_func;
  q_elem->eo_ctx       = eo_elem->eo_ctx;
  q_elem->eo_elem      = eo_elem;
  q_elem->status       = EM_QUEUE_STATUS_BIND;


  m_list_add(&eo_elem->queue_list, &q_elem->list_node);
}




/**
 * Remove a queue from an EO
 */
static inline void
eo_rem_queue(em_eo_element_t    *eo_elem,
             em_queue_element_t *q_elem)
{
  m_list_rem(&eo_elem->queue_list, &q_elem->list_node);
}




/*
 * Queues
 * ******************************************************
 */

static inline void
queue_alloc_init(void)
{
  int         i, j;
  em_queue_t  queue  = FIRST_DYN_QUEUE;


  printf("queue init\n");

  (void) memset(em_queue_element_tbl, 0, sizeof(em_queue_element_tbl));

  (void) memset(em_dyn_queue_pool,    0, sizeof(em_dyn_queue_pool));

  (void) memset(em_static_queue_lock, 0, sizeof(em_static_queue_lock));

  (void) memset(em_queue_name_tbl,    0, sizeof(em_queue_name_tbl));


  //
  // Init and fill dynamic queue id pools
  //
  for(i = 0; i < DYN_QUEUE_POOLS; i++)
  {
    env_spinlock_init(&em_dyn_queue_pool[i].lock);

    m_list_init(&em_dyn_queue_pool[i].list_head);


    for(j = 0; j < DYN_QUEUES_PER_POOL; j++)
    {
      em_queue_element_t* q_elem;

      q_elem = get_queue_element(queue);

      q_elem->id   = queue;
      q_elem->pool = i;

      m_list_add(&em_dyn_queue_pool[i].list_head, &q_elem->list_node);
      queue++;
    }
  }


  //
  // Init static queue locks
  //
  for(i = 0; i < STATIC_QUEUE_LOCKS; i++)
  {
    env_spinlock_init(&em_static_queue_lock[i].lock);
  }

}



/**
 * Dynamic queue id allocation
 */
static inline em_queue_t
queue_alloc(void)
{
  int                 i;
  int                 pool;
  em_queue_element_t *q_elem;
  m_list_head_t      *node;


  // Pool selection tries to keep low competition on spinlocks.
  // Could be also a random number 0...DYN_QUEUE_POOLS
  pool = em_core_id();


  for(i = 0; i < DYN_QUEUE_POOLS; i++)
  {
    env_spinlock_lock(&em_dyn_queue_pool[pool].lock);

    node = m_list_rem_first(&em_dyn_queue_pool[pool].list_head);

    env_spinlock_unlock(&em_dyn_queue_pool[pool].lock);


    q_elem = m_list_node_to_queue_elem(node);

    if(q_elem != NULL)
    {
      // Alloc was OK
      //printf("queue_alloc: pool %i (%i)   queue %"PRI_QUEUE" (0x%lx)\n", pool, q_elem->pool, q_elem->id, (uint64_t)q_elem);
      return q_elem->id;
    }

    // Pool was empty, try next pool
    pool++;

    if(pool == DYN_QUEUE_POOLS)
    {
      pool = 0;
    }
  }

  // All pools were empty
  (void) EM_INTERNAL_ERROR(EM_ERR_ALLOC_FAILED, EM_ESCOPE_QUEUE_ALLOC, "Queue alloc failed! All queue-pools empty.");
  return EM_QUEUE_UNDEF;
}



/**
 * Initialize an allocated/created queue before use.
 */
void
queue_init(const char*      name,
           em_queue_t       queue,
           em_queue_type_t  type,
           em_queue_prio_t  prio,
           em_queue_group_t group)
{
  em_queue_element_t *q_elem;
  char               *queue_name;

  q_elem     =  get_queue_element(queue);
  queue_name = &em_queue_name_tbl[queue][0];


  q_elem->context        = NULL;
  q_elem->priority       = prio;
  q_elem->scheduler_type = type;
  q_elem->queue_group    = group;
  q_elem->id             = queue;
  q_elem->status         = EM_QUEUE_STATUS_INIT;
  q_elem->eo_elem        = NULL;
  
  q_elem->pkt_io_enabled  = 0;
  q_elem->pkt_io_proto    = 0;
  q_elem->pkt_io_ipv4_dst = 0;
  q_elem->pkt_io_port_dst = 0;



  if(name) {
    (void) str_copy(queue_name, name, EM_QUEUE_NAME_LEN);
  }
  else {
    // No name, use default as base with additional info to create a unique name
    // "EM_Q_" + Q-id = e.g. EM_Q_1234
    (void) snprintf(queue_name, EM_QUEUE_NAME_LEN, "%s%"PRI_QUEUE"", EM_Q_BASENAME, queue);
  }
  // ensure '\0'-termination:
  queue_name[EM_QUEUE_NAME_LEN-1] = '\0';



  /*
   * ATOMIC and PARALLEL_ORDERED EM-queues have dedicated queues (=q_elem->rte_ring) in addition
   * to the shared atomic- and parallel-ordered- scheduling queues.
   * PARALLEL queues only use the shared parallel-scheduling-queues.
   */

  if(type == EM_QUEUE_TYPE_ATOMIC)
  {
    env_spinlock_init(&q_elem->lock);
    q_elem->u.atomic.event_count = 0;
    q_elem->u.atomic.sched_count = 0;
    
    q_elem->rte_ring = queue_init__ring_create(EM_QUEUE_TYPE_ATOMIC);
    
    ERROR_IF(q_elem->rte_ring == NULL, EM_ERR_ALLOC_FAILED, EM_ESCOPE_QUEUE_INIT,
             "rte_ring_create() failed for atomic Q:%"PRI_QUEUE" (%s)\n", queue, queue_name);
  }
  else if(type == EM_QUEUE_TYPE_PARALLEL_ORDERED)
  {
    env_spinlock_init(&q_elem->lock);
    q_elem->u.parallel_ord.order_first = NULL;
    
    q_elem->rte_ring = queue_init__ring_create(EM_QUEUE_TYPE_PARALLEL_ORDERED);

    ERROR_IF(q_elem->rte_ring == NULL, EM_ERR_ALLOC_FAILED, EM_ESCOPE_QUEUE_INIT,
             "rte_ring_create() failed for parallel-ordered Q:%"PRI_QUEUE" (%s)\n", queue, queue_name);
  }

  env_sync_mem();

  // q_elem->static_allocated already set (for static queues)
}



/**
 * Create the rte_ring needed by atomic and parallel-ordered EM-queues (q_elem->rte_ring)
 */
static struct rte_ring *
queue_init__ring_create(em_queue_type_t q_type)
{
  struct rte_ring *rte_ring = NULL;
  char ring_name[RTE_RING_NAMESIZE];
  int ret;
  
  
  if(q_type == EM_QUEUE_TYPE_ATOMIC)
  {
    // Event Queue. multi-producer, single-consumer
    // Only single core can dequeue an event,
    // synchronized by scheduling queue dequeue - there's max one event queue pointer in sched queue.
    // Create the RTE-ring only the first time a queue is created, a queue delete will not destoy the rte-ring.
    
    ret = rte_ring_dequeue(queue_init_rings.atomic_rings, (void **) &rte_ring);
    
    if(!ret) {
      queue_init__ring_flush(rte_ring);
    }
    else // rte_ring == NULL
    { 
      // Intel RTE-lib requires unique names for the rte-rings:
      // "RingAtom" + core-id(where created) + running count => e.g. "RingAtom_4_012a"
      (void) snprintf(&ring_name[0], RTE_RING_NAMESIZE, "RingAtom_%i_%"PRIx64"", em_core_id(), em_core_local.queue_create_count++);
      ring_name[RTE_RING_NAMESIZE-1] = '\0';
      
      // Calls to rte_ring_create() needs to be serialized, use lock
      env_spinlock_lock(&queue_create_lock.lock);
      
      rte_ring = rte_ring_create(&ring_name[0], EM_QUEUE_ATOMIC_RTE_RING_SIZE, DEVICE_SOCKET, RING_F_SC_DEQ);
      
      env_spinlock_unlock(&queue_create_lock.lock);
    }
    
  }
  else if(q_type == EM_QUEUE_TYPE_PARALLEL_ORDERED)
  {
    // Order Queue (preserves order), access serialized by spinlocks (single-producer, single-consumer)
    // Create the RTE-ring only the first time a queue is created, a queue delete will not destoy the rte-ring.

    ret = rte_ring_dequeue(queue_init_rings.parallel_ord_rings, (void **) &rte_ring);

    
    if(!ret) {
      queue_init__ring_flush(rte_ring);
    }
    else // rte_ring == NULL
    { 
      // Intel RTE-lib requires unique names for the rte-rings:
      // "RingPO" + core-id(where created) + running count => e.g. "RingPO_5_012b"
      (void) snprintf(ring_name, RTE_RING_NAMESIZE, "RingPO_%i_%"PRIx64"", em_core_id(), em_core_local.queue_create_count++);
      ring_name[RTE_RING_NAMESIZE-1] = '\0';
      
      // Calls to rte_ring_create needs to be serialized, use lock      
      env_spinlock_lock(&queue_create_lock.lock);
      
      rte_ring = rte_ring_create(ring_name, EM_QUEUE_PARALLEL_ORD_RTE_RING_SIZE, DEVICE_SOCKET, (RING_F_SC_DEQ | RING_F_SP_ENQ));
      
      env_spinlock_unlock(&queue_create_lock.lock);
    }
  }
  
  
  return rte_ring;
}


/**
 * Flush the rte_ring used by atomic and parallel-ordered EM-queues (q_elem->rte_ring)
 */
static void
queue_init__ring_flush(struct rte_ring *ring_p)
{
#define FLUSH_EVENT_COUNT (10)
  
  unsigned ring_count = 0;
  em_event_t flush_events[FLUSH_EVENT_COUNT];
  unsigned i; 
  int err;
  
  
  while((ring_count = rte_ring_count(ring_p)))
  {
    if(ring_count > FLUSH_EVENT_COUNT) {
      ring_count = FLUSH_EVENT_COUNT;
    }
    
    err = rte_ring_dequeue_bulk(ring_p, (void **) &flush_events, ring_count);
    IF_UNLIKELY(err != 0) {
      return; // Could not dequeue, somethings wrong...
    }
    
    for(i = 0; i < ring_count; i++)
    {
      em_free(flush_events[i]);
    }
    
  }
  
}


/**
 * Free the rte_ring used by atomic and parallel-ordered EM-queues (q_elem->rte_ring)
 */
static em_status_t
queue_delete__ring_free(struct rte_ring *ring_p, em_queue_type_t q_type)
{
  int ret;
  
  
  if(q_type == EM_QUEUE_TYPE_ATOMIC)
  {
    if(ring_p == NULL) {
      return EM_ERR_BAD_POINTER;
    }
    
    ret = rte_ring_enqueue(queue_init_rings.atomic_rings, ring_p);
    
    IF_UNLIKELY(ret == (-ENOBUFS)) {
      return EM_ERR_LIB_FAILED;
    }
  }
  else if(q_type == EM_QUEUE_TYPE_PARALLEL_ORDERED)
  {
    if(ring_p == NULL) {
      return EM_ERR_BAD_POINTER;
    }
    
    ret = rte_ring_enqueue(queue_init_rings.parallel_ord_rings, ring_p);
      
    IF_UNLIKELY(ret == (-ENOBUFS)) {
      return EM_ERR_LIB_FAILED;
    }
  }
  
  return EM_OK;
}


/**
 * Initialize the queues of free rte_rings (i.e. a queue of queues). The free rte_rings are taken into 
 * use (q_elem->rte_ring) in em_queue_create()/queue_init() and freed back in em_queue_delete()
 */
static em_status_t
queue_init__rings_init(void)
{ 
  
  queue_init_rings.atomic_rings = rte_ring_create("ATOMIC q_elem rings",  EM_MAX_QUEUES, DEVICE_SOCKET, 0);  
  if(queue_init_rings.atomic_rings == NULL) {
    return EM_ERR_BAD_POINTER;
  }
  
  queue_init_rings.parallel_ord_rings = rte_ring_create("PAR-ORD q_elem rings", EM_MAX_QUEUES, DEVICE_SOCKET, 0);
  if(queue_init_rings.parallel_ord_rings == NULL) {
    return EM_ERR_BAD_POINTER;
  }  
  
  return EM_OK;
}



/**
 * Helper func for queue_state_change()
 */
static inline em_status_t
queue_state_change__sched_masks_change(const uint8_t          new_state,
                                       const em_queue_t       queue,
                                       const em_queue_type_t  type,
                                       const em_queue_group_t group)

{
  em_status_t    err;
  em_core_mask_t mask;


  if(new_state == EM_QUEUE_STATUS_READY)
  {
    /* em_queue_enable() / em_queue_enable_all() */
    
    err = em_queue_group_mask(group, &mask);
    RETURN_ERROR_IF(err != EM_OK, err, EM_ESCOPE_QUEUE_STATE_CHANGE,
                    "Cannot enable scheduling for queue %"PRI_QUEUE": Getting queue group %"PRI_QGRP" mask failed!", queue, group);
    
    RETURN_ERROR_IF(em_core_mask_iszero(&mask), EM_ERR_BAD_ID, EM_ESCOPE_QUEUE_STATE_CHANGE,
                    "Cannot enable scheduling for queue %"PRI_QUEUE": Queue Group %"PRI_QGRP" mask is zero (0x0)!", queue, group);
    
    err = sched_masks_add_queue(queue, type, group);
    
    RETURN_ERROR_IF(err != EM_OK, err, EM_ESCOPE_QUEUE_STATE_CHANGE,
                    "Call to sched_masks_add_queue(queue=%"PRI_QUEUE", type=%u, group=%"PRI_QGRP") failed!",
                    queue, type, group);
  }
  else if(new_state == EM_QUEUE_STATUS_BIND)
  {
    /* em_queue_disable() / em_queue_disable_all() */
    err = sched_masks_rem_queue(queue, type, group);

    RETURN_ERROR_IF(err != EM_OK, err, EM_ESCOPE_QUEUE_STATE_CHANGE,
                    "Call to sched_masks_add_queue(queue=%"PRI_QUEUE", type=%u, group=%"PRI_QGRP") failed!",
                    queue, type, group);
  }
  else
  {
    /* Illegal new state */
    return EM_INTERNAL_ERROR(EM_FATAL(EM_ERR_BAD_STATE), EM_ESCOPE_QUEUE_STATE_CHANGE, "Illegal new state %u", new_state);
  }

  return EM_OK;
}



/**
 * Enable/disable event reception on one or all the queues of an EO
 */
inline static em_status_t
queue_state_change(em_queue_t queue, int all_queues, uint8_t new_state, em_eo_t eo)
{
  m_list_head_t      *pos;
  m_list_head_t      *list_node;
  em_queue_element_t *q_elem;
  em_eo_element_t    *eo_elem;
  em_status_t         err;


  // Sync all previous operations before
  // adding/removing queues to/from scheduling
  env_sync_mem();



  if(all_queues) // Only used with em_eo_start() -> em_queue_enable_all()
  {
    //
    // Enable/disable ALL queues belonging to the EO
    //
    
    eo_elem = get_eo_element(eo); 
    
    RETURN_ERROR_IF(eo_elem == NULL, EM_FATAL(EM_ERR_BAD_POINTER), EM_ESCOPE_QUEUE_STATE_CHANGE,
                    "EO %"PRI_EO" is invalid!", eo);


    m_list_for_each(&eo_elem->queue_list, pos, list_node)
    {
      q_elem = m_list_node_to_queue_elem(list_node);
      em_queue_t cur_queue = q_elem->id;

      RETURN_ERROR_IF((q_elem->status != EM_QUEUE_STATUS_BIND) && (q_elem->status != EM_QUEUE_STATUS_READY),
                      EM_ERR_BAD_STATE, EM_ESCOPE_QUEUE_STATE_CHANGE,
                      "EM queue %"PRI_QUEUE" not yet initialized (status %i)", cur_queue, q_elem->status);
      
      q_elem->status = new_state;

      err = queue_state_change__sched_masks_change(new_state, q_elem->id, q_elem->scheduler_type, q_elem->queue_group);
      if(err != EM_OK) {
        return err; // Error already logged in subfunc, just return
      }

    } // end loop

  }
  else
  {
    //
    // Enable/disable one queue
    //

    // queue-id verified by caller
    q_elem = get_queue_element(queue);
    
    RETURN_ERROR_IF(q_elem == NULL, EM_FATAL(EM_ERR_BAD_POINTER), EM_ESCOPE_QUEUE_STATE_CHANGE,
                    "EM queue %"PRI_QUEUE" is invalid!", queue);

    RETURN_ERROR_IF((q_elem->status != EM_QUEUE_STATUS_BIND) && (q_elem->status != EM_QUEUE_STATUS_READY),
                    EM_ERR_BAD_STATE, EM_ESCOPE_QUEUE_STATE_CHANGE,
                    "EM queue %"PRI_QUEUE" not yet initialized (status %i)", queue, q_elem->status);

    q_elem->status = new_state;

    err = queue_state_change__sched_masks_change(new_state, q_elem->id, q_elem->scheduler_type, q_elem->queue_group);
    if(err != EM_OK) {
      return err; // Error already logged in subfunc, just return
    }

  }


  env_sync_mem();

  return EM_OK;
}




/*
 * From application to Event Machine interface
 * -------------------------------------------
 */


/**
 * Create a new queue with a dynamic queue id.
 *
 * The given name string is copied to EM internal data structure. The maximum
 * string length is EM_QUEUE_NAME_LEN. 
 *
 * @param name          Queue name for debugging purposes (optional, NULL ok)
 * @param type          Queue scheduling type
 * @param prio          Queue priority
 * @param group         Queue group for this queue
 *
 * @return New queue id or EM_QUEUE_UNDEF on an error
 *
 * @see em_queue_group_create(), em_queue_delete()
 */
em_queue_t
em_queue_create(const char* name, em_queue_type_t type, em_queue_prio_t prio, em_queue_group_t group)
{
  em_queue_t queue;

  queue = queue_alloc(); // queue_alloc() logged errors.
  
  IF_LIKELY(queue != EM_QUEUE_UNDEF)
  {
    queue_init(name, queue, type, prio, group);
    
    queue_group_add_queue_list(group, queue);    
  }

  return queue;
}



/**
 * Create a new queue with a static queue id.
 *
 * Note, that system may have limited amount of static identifiers,
 * so unless really needed use dynamic queues instead.
 * The range of static identifiers is system dependent, but macros
 * EM_QUEUE_STATIC_MIN and EM_QUEUE_STATIC_MAX can be used to abstract,
 * i.e. use EM_QUEUE_STATIC_MIN+x for the application.
 *
 * The given name string is copied to EM internal data structure. The maximum
 * string length is EM_QUEUE_NAME_LEN.
 *
 * @param name          Queue name for debugging purposes (optional, NULL ok)
 * @param type          Queue scheduling type
 * @param prio          Queue priority
 * @param group         Queue group for this queue
 * @param queue         Requested queue id from the static range
 *
 * @return EM_OK if successful.
 *
 * @see em_queue_group_create(), em_queue_delete()
 */
em_status_t
em_queue_create_static(const char* name, em_queue_type_t type, em_queue_prio_t prio, em_queue_group_t group, em_queue_t queue)
{
  int                 idx;
  env_spinlock_t     *lock;
  em_queue_element_t *q_elem;


  RETURN_ERROR_IF(queue > EM_QUEUE_STATIC_MAX /*|| queue < EM_QUEUE_STATIC_MIN */,
                  EM_ERR_BAD_ID, EM_ESCOPE_QUEUE_CREATE_STATIC,
                  "Invalid static EM queue id %"PRI_QUEUE"", queue);


  idx    =  get_static_queue_lock_idx(queue);
  lock   = &em_static_queue_lock[idx].lock;
  q_elem =  get_queue_element(queue);


  env_spinlock_lock(lock);

  IF_UNLIKELY(q_elem->static_allocated)
  {
    env_spinlock_unlock(lock);
    return EM_INTERNAL_ERROR(EM_ERR_NOT_FREE, EM_ESCOPE_QUEUE_CREATE_STATIC,
                             "Static EM queue %"PRI_QUEUE" already in use!", queue);
  }
  
  q_elem->static_allocated = 1;

  env_spinlock_unlock(lock);


  queue_init(name, queue, type, prio, group);

  return EM_OK;
}



/**
 * Delete a queue.
 *
 * Unallocates the queue id.
 * NOTE: this is an immediate deletion and can *only*
 * be done after the queue has been removed from scheduling
 * using em_eo_remove_queue() !
 *
 * @param queue         Queue id to delete
 *
 * @return EM_OK if successful.
 *
 * @see em_eo_remove_queue(), em_queue_create(), em_queue_create_static()
 */
em_status_t
em_queue_delete(em_queue_t queue)
{
  em_queue_element_t *q_elem;
  int                 pool;
  em_status_t         ret;


  RETURN_ERROR_IF(invalid_queue(queue), EM_ERR_BAD_ID, EM_ESCOPE_QUEUE_DELETE, 
                  "Invalid EM queue id %"PRI_QUEUE"", queue);
  
  q_elem = get_queue_element(queue);
  
  
  // Remove the queue from the queue group list
  queue_group_rem_queue_list(q_elem->queue_group, queue);
  
  
  ret = queue_delete__ring_free(q_elem->rte_ring, q_elem->scheduler_type);
  RETURN_ERROR_IF(ret != EM_OK, EM_FATAL(ret), EM_ESCOPE_QUEUE_DELETE, 
                  "queue_delete__ring_free() failed (%i)", ret);
  
  
  // Zero queue name.
  em_queue_name_tbl[queue][0] = '\0';
  
  
  if(queue <= EM_QUEUE_STATIC_MAX)
  {
    // No synchronisation needed. Queue should be freed from single core.
    q_elem->status = EM_QUEUE_STATUS_INVALID;
    env_sync_mem();

    q_elem->static_allocated = 0;
    env_sync_mem();
  }
  else
  {
    // Free queue back to the pool set in init
    pool = q_elem->pool;

    env_spinlock_lock(&em_dyn_queue_pool[pool].lock);
    
    q_elem->status = EM_QUEUE_STATUS_INVALID;
    m_list_add(&em_dyn_queue_pool[pool].list_head, &q_elem->list_node);

    env_spinlock_unlock(&em_dyn_queue_pool[pool].lock);
  }


  return EM_OK;
}



/**
 * Enable event scheduling for the queue.
 *
 * All events sent to a non-enabled queue may get discarded or held
 * depending on the system. Queue enable/disable is not meant to be used
 * for additional scheduling nor used frequently. Main purpose is to
 * synchronize startup or recovery actions.
 *
 * @param queue         Queue to enable
 *
 * @return EM_OK if successful.
 *
 * @see em_eo_start(), em_queue_enable_all(), em_queue_disable()
 */
em_status_t
em_queue_enable(em_queue_t queue)
{ 
  em_status_t ret;
  

  RETURN_ERROR_IF(invalid_queue(queue), EM_ERR_BAD_ID, EM_ESCOPE_QUEUE_ENABLE,
                  "Invalid queue-id: (%"PRI_QUEUE")\n", queue);
  
  
  ret = queue_state_change(queue, 0, EM_QUEUE_STATUS_READY, EM_EO_UNDEF);
  
  
  RETURN_ERROR_IF(ret != EM_OK, ret, EM_ESCOPE_QUEUE_ENABLE,
                  "queue_state_change() to READY for EM queue %"PRI_QUEUE" failed", queue);
  
  return EM_OK; 
}



/**
 * Enable event scheduling for all the EO's queues.
 * 
 * Otherwise identical to em_queue_enable().
 *
 * @param eo            EO id 
 *
 * @return EM_OK if successful.
 *
 * @see em_queue_enable(), em_queue_disable_all()
 */
em_status_t
em_queue_enable_all(em_eo_t eo)
{
  em_status_t ret;
  
  
  // TODO: now taking eo from current start context
  ret = queue_state_change(EM_QUEUE_UNDEF, 1, EM_QUEUE_STATUS_READY, eo);
  
  RETURN_ERROR_IF(ret != EM_OK, ret, EM_ESCOPE_QUEUE_ENABLE_ALL,
                  "queue_state_change_all() to READY for EO %"PRI_EO" failed", eo);  
  
  return EM_OK;
}



/**
 * Disable scheduling for the queue.
 *
 * Note, that this might be an asynchronous operation and actually complete later as
 * other cores may still be handling existing events. If application needs to
 * know exactly when all processing is completed, it can use the notification
 * arguments - the given notification(s) are sent after all cores have completed.
 *
 * Implicit disable is done for all queues, that are mapped to an EO when
 * it's stop-function is called (via em_eo_stop()).
 *
 * All events sent to a non-enabled queue may get discarded or held
 * depending on the system. Queue enable/disable is not meant to be used
 * for additional scheduling nor used frequently. Main purpose is to
 * synchronize startup or recovery actions.
 *
 * @param queue         Queue to disable
 * @param num_notif     Number of entries in notif_tbl, use 0 for no notification
 * @param notif_tbl     Notification events to send
 *
 * @return EM_OK if successful.
 *
 * @see em_eo_stop(), em_queue_disable_all(), em_queue_enable()
 */
em_status_t
em_queue_disable(em_queue_t queue, int num_notif, const em_notif_t* notif_tbl)
{
  em_status_t          ret;
  em_core_mask_t       core_mask;
  em_queue_element_t  *q_elem;
  em_event_t           event;
  em_internal_event_t *i_event;
  int                  send_notifs;

  
  
  RETURN_ERROR_IF(invalid_queue(queue), EM_ERR_BAD_ID, EM_ESCOPE_QUEUE_DISABLE,
                  "Invalid queue-id: (%"PRI_QUEUE")\n", queue);
  
  
  send_notifs = (num_notif > 0) && (num_notif < EM_EVENT_GROUP_MAX_NOTIF);
  
  
  // Store the core_mask before any changes
  if(send_notifs)
  {
    q_elem = get_queue_element(queue);
    
    ret = em_queue_group_mask(q_elem->queue_group, &core_mask);
    
    RETURN_ERROR_IF(ret != EM_OK, ret, EM_ESCOPE_QUEUE_DISABLE,
                    "em_queue_group_mask() failed for group %"PRI_QGRP"", q_elem->queue_group);
  }
  
  
  // Change the state of the queue and modify scheduling masks
  ret = queue_state_change(queue, 0, EM_QUEUE_STATUS_BIND, EM_EO_UNDEF);
  
  RETURN_ERROR_IF(ret != EM_OK, ret, EM_ESCOPE_QUEUE_DISABLE,
                  "queue_state_change() to BIND for EM queue %"PRI_QUEUE" failed", queue);
  
  
  if(send_notifs)
  {
    event = em_alloc(sizeof(em_internal_event_t), EM_EVENT_TYPE_SW, EM_POOL_DEFAULT);
    
    RETURN_ERROR_IF(event == EM_EVENT_UNDEF, EM_ERR_ALLOC_FAILED, EM_ESCOPE_QUEUE_DISABLE,
                    "Internal event 'EM_QUEUE_DISABLE' alloc failed");
    
    i_event = em_event_pointer(event);
    i_event->id = EM_QUEUE_DISABLE;
    
    ret = em_internal_notif(&core_mask, event, NULL, NULL, num_notif, notif_tbl);

    RETURN_ERROR_IF(ret != EM_OK, ret, EM_ESCOPE_QUEUE_DISABLE,
                    "em_internal_notif(core_mask=%"PRIX64") failed", core_mask.u64);
  }
  
  
  return EM_OK;
}



/**
 * Disable scheduling for all the EO's queues.
 *
 * Otherwise identical to em_queue_disable().
 *
 * @param eo            EO id
 * @param num_notif     Number of entries in notif_tbl, use 0 for no notification
 * @param notif_tbl     Notification events to send
 *
 * @return EM_OK if successful.
 *
 * @see em_eo_stop(), em_queue_disable(), em_queue_enable_all()
 */
em_status_t
em_queue_disable_all(em_eo_t eo, int num_notif, const em_notif_t* notif_tbl)
{
  em_status_t         ret;
    
  
  // TODO: now taking eo from current start context
  ret = queue_state_change(EM_QUEUE_UNDEF, 1, EM_QUEUE_STATUS_BIND, eo);
  
  RETURN_ERROR_IF(ret != EM_OK, ret, EM_ESCOPE_QUEUE_DISABLE_ALL,
                  "queue_state_change() all Qs to BIND for EO %"PRI_EO" failed", eo);
  
  
  if((num_notif > 0) && (num_notif < EM_EVENT_GROUP_MAX_NOTIF))
  {
    em_core_mask_t      core_mask;
    em_event_t          event;
    em_internal_event_t *i_event;
    
    
    event = em_alloc(sizeof(em_internal_event_t), EM_EVENT_TYPE_SW, EM_POOL_DEFAULT);
    
    RETURN_ERROR_IF(event == EM_EVENT_UNDEF, EM_ERR_ALLOC_FAILED, EM_ESCOPE_QUEUE_DISABLE_ALL,
                    "Internal event 'EM_QUEUE_DISABLE' alloc failed");
    
    i_event = em_event_pointer(event);
    i_event->id = EM_QUEUE_DISABLE;
    
    // set all cores in mask since we do not want to lookup all queues belonging to this EO and check all queue groups
    em_core_mask_zero(&core_mask);
    em_core_mask_set_count(em_core_count(), &core_mask);
    
    ret = em_internal_notif(&core_mask, event, NULL, NULL, num_notif, notif_tbl);
    
    RETURN_ERROR_IF(ret != EM_OK, ret, EM_ESCOPE_QUEUE_DISABLE_ALL,
                    "em_internal_notif() failed");
  }
  
  
  return EM_OK;
}



/**
 * Set queue specific (application) context.
 * 
 * This is just a single pointer associated with a queue. Application can use it
 * to access some context data quickly (without a lookup). The context is given
 * as argument for the receive function. EM does not use the value, it just
 * passes it.
 *                        
 * @param queue         Queue to which associate the context
 * @param context       Context pointer
 *
 * @return EM_OK if successful.
 *
 * @see em_receive_func_t(), em_queue_get_context()
 */
em_status_t
em_queue_set_context(em_queue_t queue, const void* context)
{
  em_queue_element_t* q_elem;


  RETURN_ERROR_IF(invalid_queue(queue), EM_ERR_BAD_ID, EM_ESCOPE_QUEUE_SET_CONTEXT,
                  "Invalid queue-id: (%"PRI_QUEUE")\n", queue);

  q_elem = get_queue_element(queue);

  q_elem->context = (void *) context;

  return EM_OK;
}



/**
 * Get queue specific (application) context.
 *
 * Returns the value application has earlier set with em_queue_set_context().
 *
 * @param queue         Queue which context is requested
 *
 * @return Queue specific context pointer or NULL on an error
 *
 * @see em_queue_set_context()
 */
void*
em_queue_get_context(em_queue_t queue)
{
  em_queue_element_t* q_elem;


  ERROR_IF(invalid_queue(queue), EM_ERR_BAD_ID, EM_ESCOPE_QUEUE_GET_CONTEXT,
                  "Invalid queue-id: (%"PRI_QUEUE")\n", queue) {
    return NULL;
  }


  q_elem = get_queue_element(queue);

  return q_elem->context;
}



/**
 * Get queue name.
 *
 * Returns the name given to a queue when it was created.
 * A copy of the queue name string (up to 'maxlen' characters) is
 * written to the user given buffer.
 * String is always null terminated even if the given buffer length
 * is less than the name length.
 *
 * If the queue has no name, function returns 0 and writes empty string.
 *
 * This is only for debugging purposes.
 *
 * @param queue         Queue id
 * @param name          Destination buffer
 * @param maxlen        Maximum length (including the terminating '0')
 *
 * @return Number of characters written (excludes the terminating '0').
 * 
 * @see em_queue_create()
 */
size_t
em_queue_get_name(em_queue_t queue, char* name, size_t maxlen)
{
  char* queue_name;


  ERROR_IF(invalid_queue(queue), EM_ERR_BAD_ID, EM_ESCOPE_QUEUE_GET_NAME,
                  "Invalid queue-id: (%"PRI_QUEUE")\n", queue) {
    return 0;
  }

  queue_name = &em_queue_name_tbl[queue][0];

  return str_copy(name, queue_name, maxlen);
}



/**
 * Get queue priority.
 *
 * @param  queue        Queue identifier
 *
 * @return Priority class or EM_QUEUE_PRIO_UNDEF on an error
 *
 * @see em_queue_create()
 */
em_queue_prio_t
em_queue_get_priority(em_queue_t queue)
{
  em_queue_element_t *q_elem;

  
  ERROR_IF(invalid_queue(queue), EM_ERR_BAD_ID, EM_ESCOPE_QUEUE_GET_PRIORITY,
                  "Invalid queue-id: (%"PRI_QUEUE")\n", queue) {
    return EM_QUEUE_PRIO_UNDEF;
  }

  q_elem = get_queue_element(queue);

  return q_elem->priority;
}



/**
 * Get queue type (scheduling mode).
 *
 * @param  queue        Queue identifier
 *
 * @return Queue type or EM_QUEUE_TYPE_UNDEF on an error
 *
 * @see em_queue_create()
 */
em_queue_type_t
em_queue_get_type(em_queue_t queue)
{
  em_queue_element_t *q_elem;


  ERROR_IF(invalid_queue(queue), EM_ERR_BAD_ID, EM_ESCOPE_QUEUE_GET_TYPE,
           "Invalid queue-id: (%"PRI_QUEUE")\n", queue) {
    return EM_QUEUE_TYPE_UNDEF;
  }
  
  q_elem = get_queue_element(queue);

  return q_elem->scheduler_type;
}



/**
 * Get queue's queue group
 *
 * @param  queue        Queue identifier 
 *
 * @return Queue group or EM_QUEUE_GROUP_UNDEF on an error
 * 
 * @see em_queue_create(), em_queue_group_create(), em_queue_group_modify()
 */
em_queue_group_t
em_queue_get_group(em_queue_t queue)
{
   em_queue_element_t *q_elem;
   
    
   ERROR_IF(invalid_queue(queue), EM_ERR_BAD_ID, EM_ESCOPE_QUEUE_GET_GROUP,
            "Invalid queue-id: (%"PRI_QUEUE")\n", queue) {
     return EM_QUEUE_GROUP_UNDEF;
   }
  
  q_elem = get_queue_element(queue);
  
  IF_UNLIKELY(q_elem->status == EM_QUEUE_STATUS_INVALID) {
    return EM_QUEUE_GROUP_UNDEF;
  }
  else {
    return q_elem->queue_group;  
  }
}




/**
 * Create Execution Object (EO).
 * 
 * This will allocate identifier and initialize internal data for a new EO.
 * It is left in a non-active state, i.e. no events are dispatched before
 * em_eo_start() has called. Start, stop and receive callback function
 * pointers are mandatory parameters.
 *
 * The name given is copied to EO internal data and can be used e.g. for debugging.
 * The maximum length stored is EM_EO_NAME_LEN.
 *
 * @param name          Name of the EO (NULL if no name)
 * @param start         Start function
 * @param local_start   Core local start function (NULL if no local start)
 * @param stop          Stop function
 * @param local_stop    Core local stop function (NULL if no local stop)
 * @param receive       Receive function
 * @param eo_ctx        User defined EO context data, EM just passes the pointer (NULL if not context)
 *
 * @return New EO id if successful, otherwise EM_EO_UNDEF
 *
 * @see em_eo_start(), em_eo_delete(), em_queue_create(), em_eo_add_queue()
 * @see em_start_func_t(), em_stop_func_t(), em_receive_func_t()
 */
em_eo_t
em_eo_create(const char*            name,
             em_start_func_t        start,
             em_start_local_func_t  local_start,
             em_stop_func_t         stop,
             em_stop_local_func_t   local_stop,
             em_receive_func_t      receive,
             const void            *eo_ctx)
{
  em_eo_t          eo;
  em_eo_element_t* eo_elem;
  size_t           nlen;


  ERROR_IF((start == NULL) || (stop == NULL) || (receive == NULL),
           EM_ERR_BAD_POINTER, EM_ESCOPE_EO_CREATE,
           "Mandatory function pointer(s) NULL!")
  {
    return EM_EO_UNDEF;
  }
  

  eo = eo_alloc();

  ERROR_IF(invalid_eo(eo), EM_ERR_ALLOC_FAILED, EM_ESCOPE_EO_CREATE,
           "EO alloc failed")
  {
     return EM_EO_UNDEF;
  }


  eo_elem = get_eo_element(eo);


  // Store the name
  nlen = str_copy(eo_elem->name, name, EM_EO_NAME_LEN);
  nlen = nlen > 0 ? nlen+1 : 0;
  eo_elem->name[nlen] = '\0'; // Be sure to null-terminate the string


  // queue list init
  m_list_init(&eo_elem->queue_list);

  eo_elem->start_func       = start;
  eo_elem->start_local_func = local_start;
  eo_elem->stop_func        = stop;
  eo_elem->stop_local_func  = local_stop;
  eo_elem->receive_func     = receive;
  eo_elem->eo_ctx           = (void *) eo_ctx;
  eo_elem->id               = eo;

  env_sync_mem();

  return eo;
}



/**
 * Delete Execution Object (EO).
 *
 * This will immediately delete the given EO and free the identifier.
 *
 * NOTE, that EO can only be deleted after it has been stopped using
 * em_eo_stop(), otherwise another core might still access the EO data!
 * Deletion will fail, if the EO is not stopped.
 *
 * This will delete all possibly remaining queues.
 *
 * @param eo            EO id to delete
 *
 * @return EM_OK if successful.
 *
 * @see em_eo_stop(), em_eo_create()
 */
em_status_t
em_eo_delete(em_eo_t eo)
{
  em_eo_element_t *eo_elem;
  int              pool;

  
  RETURN_ERROR_IF(invalid_eo(eo), EM_ERR_BAD_ID, EM_ESCOPE_EO_DELETE, "Invalid EO id %"PRI_EO"", eo);


  eo_elem = get_eo_element(eo);

  // Free EO back to the pool set in init
  pool = eo_elem->pool;

  env_spinlock_lock(&em_eo_pool[pool].lock);

  m_list_add(&em_eo_pool[pool].list_head, &eo_elem->list_head);

  env_spinlock_unlock(&em_eo_pool[pool].lock);


  return EM_OK;
}



/**
 * Returns the name given to the EO when it was created.
 *
 * A copy of the name string (up to 'maxlen' characters) is
 * written to the user buffer 'name'.
 * String is always null terminated even if the given buffer length
 * is less than the name length.
 *
 * If the EO has no name, function returns 0 and writes empty string. 
 *
 * This is only for debugging purposes.
 *
 * @param eo            EO id
 * @param name          Destination buffer
 * @param maxlen        Maximum length (including the terminating '0')
 *
 * @return Number of characters written (excludes the terminating '0')
 *
 * @see em_eo_create() 
 */
size_t
em_eo_get_name(em_eo_t eo, char* name, size_t maxlen)
{
  em_eo_element_t* eo_elem;

  ERROR_IF(invalid_eo(eo), EM_ERR_BAD_ID, EM_ESCOPE_EO_GET_NAME, "Invalid EO id %"PRI_EO"", eo)
  {
    return 0;
  }

  eo_elem = get_eo_element(eo);

  return str_copy(name, eo_elem->name, maxlen);
}



/**
 * Add a queue to an EO.
 *
 * Note, that this does not enable the queue. Although queues added in
 * (or before) the start function will be enabled automatically.
 *
 * @param eo            EO id
 * @param queue         Queue id
 *
 * @return EM_OK if successful.
 *
 * @see em_queue_create(), em_eo_create(), em_queue_enable(), em_eo_remove_queue()
 */
em_status_t
em_eo_add_queue(em_eo_t eo, em_queue_t queue)
{
  em_eo_element_t    *eo_elem;
  em_queue_element_t *q_elem;


  RETURN_ERROR_IF(invalid_eo(eo) || invalid_queue(queue), EM_ERR_BAD_ID, EM_ESCOPE_EO_ADD_QUEUE,
                  "Invalid arguments given: eo=%"PRI_EO" queue=%"PRI_QUEUE"", eo, queue);

  eo_elem = get_eo_element(eo);
  q_elem  = get_queue_element(queue);

  eo_add_queue(eo_elem, q_elem);

  return EM_OK;
}



/**
 * Removes a queue from an EO.
 * 
 * Function disables scheduling of the queue and removes the queue from the
 * EO. The operation is asynchronous, to quarantee that all cores have completed
 * processing of events from the queue (i.e. there's no cores in middle of the
 * receive function) before removing it.
 *
 * If the caller needs to know when the context deletion actually occurred,
 * the num_notif and notif_tbl can be used. The given notification event(s)
 * will be sent to given queue(s), when the removal has completed.
 * If such notification is not needed, use 0 as num_notif.
 *
 * If the queue to be removed is still enabled, it will first be disabled.
 *
 * @param eo            EO id
 * @param queue         Queue id to remove
 * @param num_notif     How many notification events given, 0 for no notification
 * @param notif_tbl     Array of pairs of event and queue identifiers
 *
 * @return EM_OK if successful.
 *
 * @see em_eo_add_queue(), em_queue_disable(), em_queue_delete()
 */
em_status_t
em_eo_remove_queue(em_eo_t eo, em_queue_t queue, int num_notif, const em_notif_t* notif_tbl)
{
  em_eo_element_t    *eo_elem;
  em_queue_element_t *q_elem;
  em_status_t         ret;
  em_core_mask_t      core_mask;
  int                 send_notifs;

  
  RETURN_ERROR_IF((eo == EM_EO_UNDEF) || invalid_queue(queue), EM_ERR_BAD_ID, EM_ESCOPE_EO_REMOVE_QUEUE,
                  "Invalid arguments given: eo=%"PRI_EO" queue=%"PRI_QUEUE"", eo, queue);


  eo_elem = get_eo_element(eo);
  q_elem  = get_queue_element(queue);


  // Remove the queue from packet-I/O if not already done
  if(q_elem->pkt_io_enabled) { 
    em_packet_rem_io_queue(q_elem->pkt_io_proto, q_elem->pkt_io_ipv4_dst, q_elem->pkt_io_port_dst, queue);
  }


  send_notifs = (num_notif > 0) && (num_notif < EM_EVENT_GROUP_MAX_NOTIF);
  
  if(send_notifs)
  {
    // Store queue related info before it possibly is changed
    ret = em_queue_group_mask(q_elem->queue_group, &core_mask);
    
    RETURN_ERROR_IF(ret != EM_OK, ret, EM_ESCOPE_EO_REMOVE_QUEUE,
                    "em_queue_group_mask(qgrp:%"PRI_QGRP") failed", q_elem->queue_group);
  }


  // Disable the queue if not already done
  if(q_elem->status == EM_QUEUE_STATUS_READY) {
    ret = em_queue_disable(queue, 0, NULL);
    
    RETURN_ERROR_IF(ret != EM_OK, ret, EM_ESCOPE_EO_REMOVE_QUEUE,
                    "em_queue_disable(q=%"PRI_QUEUE") failed", queue);
  }
  
  // Remove the queue from the EO
  eo_rem_queue(eo_elem, q_elem);
  
  
  if(send_notifs)
  {
    em_event_t event;
    em_internal_event_t *i_event;
    
    event = em_alloc(sizeof(em_internal_event_t), EM_EVENT_TYPE_SW, EM_POOL_DEFAULT);
    RETURN_ERROR_IF(event == EM_EVENT_UNDEF, EM_ERR_ALLOC_FAILED, EM_ESCOPE_EO_REMOVE_QUEUE,
                    "Internal event EM_EO_REMOVE_QUEUE alloc failed");
    
    i_event = em_event_pointer(event);
    i_event->id = EM_EO_REMOVE_QUEUE;
    
    ret = em_internal_notif(&core_mask, event, NULL, NULL, num_notif, notif_tbl);
    
    RETURN_ERROR_IF(ret != EM_OK, ret, EM_ESCOPE_EO_REMOVE_QUEUE,
                    "em_internal_notif(core_mask=%"PRIX64") failed", core_mask.u64);
  }

  return EM_OK;
}









/**
 * Start Execution Object (EO).
 *
 * Calls global EO start function. If that returns EM_OK,
 * an internal event to trigger local start is sent to all cores belonging to
 * the queue group of this EO.
 * If the global start function does not return EM_OK the local start is
 * not called and event dispatching is not enabled for this EO.
 *
 * If the caller needs to know when the EO start was actually completed
 * on all cores, the num_notif and notif_tbl can be used. The given
 * notification event(s) will be sent to given queue(s), when the
 * start is completed on all cores.
 * If local start does not exist the notification(s) are sent as the global
 * start returns.
 * If such notification is not needed, use 0 as num_notif.
 *
 * @param eo            EO id
 * @param result        Optional pointer to em_status_t, which gets updated to the
 *                      return value of the actual EO global start function
 *
 * @param num_notif     If not 0, defines the number of events to send as all cores
 *                      have returned from the start function (in notif_tbl).
 *
 * @param notif_tbl     Array of em_notif_t, the optional notification events (data copied)
 *
 * @return EM_OK if successful.
 *
 * @see em_start_func_t(), em_start_local_func_t(), em_eo_stop()
 *
 * @todo Way to read core local start value or status?
 */
em_status_t
em_eo_start(em_eo_t eo, em_status_t *result, int num_notif, const em_notif_t* notif_tbl)
{
  em_eo_element_t *eo_elem;
  void            *eo_ctx;
  em_status_t      ret;

  
  RETURN_ERROR_IF(invalid_eo(eo), EM_ERR_BAD_ID, EM_ESCOPE_EO_START,
                  "Invalid EO id %"PRI_EO"", eo);
  
  eo_elem = get_eo_element(eo);

  // Call eo start before changing queue status
  eo_ctx = eo_elem->eo_ctx;

  
  //
  // Call the global EO start function
  // 
  ret = eo_elem->start_func(eo_ctx, eo);
  
  // Store the return value of the actual EO global start function
  if(result != NULL) {
    *result = ret;
  }
  
  IF_UNLIKELY(ret != EM_OK) {
    return EM_INTERNAL_ERROR(ret, EM_ESCOPE_EO_START, "EO:%"PRI_EO" start func failed", eo);
  }
  
  
  if(eo_elem->start_local_func != NULL)
  {
    // Notifications sent when the local start functions have completed
    ret = em_eo_start_local(eo_elem, num_notif, notif_tbl);

    IF_UNLIKELY(ret != EM_OK) {
      return EM_INTERNAL_ERROR(ret, EM_ESCOPE_EO_START, "EO:%"PRI_EO" local start func failed", eo);
    }
  }
  else
  {
    // Send notification if requested
    if((num_notif > 0) && (num_notif < EM_EVENT_GROUP_MAX_NOTIF))
    {
      em_core_mask_t      core_mask;
      em_event_t          event;
      em_internal_event_t *i_event;
      
      
      event = em_alloc(sizeof(em_internal_event_t), EM_EVENT_TYPE_SW, EM_POOL_DEFAULT);
      
      IF_UNLIKELY(event == EM_EVENT_UNDEF) {
        return EM_INTERNAL_ERROR(EM_ERR_ALLOC_FAILED, EM_ESCOPE_EO_START, "Internal event EO_START_REQ alloc failed");
      }
      
      i_event = em_event_pointer(event);
      i_event->id = EO_START_REQ;
      
      // set all cores in mask since we do not want to lookup all queues belonging to this EO and check all queue groups
      em_core_mask_zero(&core_mask);
      em_core_mask_set_count(em_core_count(), &core_mask);
      
      ret = em_internal_notif(&core_mask, event, NULL, NULL, num_notif, notif_tbl);
      
      IF_UNLIKELY(ret != EM_OK) {
        return EM_INTERNAL_ERROR(ret, EM_ESCOPE_EO_START, "em_internal_notif(core_mask=%"PRIX64") failed", core_mask.u64); 
      }
    }
  }
  

  return EM_OK;
}



/**
 * Callback function run when all start_local functions are finished
 */
static void
em_eo_start_local__done_callback(void *args)
{
  em_eo_element_t *const eo_elem = (em_eo_element_t *) args; 
  
  
  ERROR_IF(eo_elem == NULL, EM_FATAL(EM_ERR_BAD_POINTER),
           EM_ESCOPE_EO_START_LOCAL__DONE_CALLBACK, "eo_elem is NULL!")
  {
    return;
  }
  
    
  // Currently do nothing...
      
}



/**
 * Stop Execution Object (EO).
 *
 * Disables event dispatch from all related queues, calls core local stop
 * on all cores and finally calls the global stop function of the EO,
 * when all cores have returned from the (optional) core local stop.
 * Call to the global EO stop is asynchronous and only done, when all cores
 * have completed processing of the receive function and/or core local stop.
 * This guarantees no core is accessing EO data during EO global stop function.
 *
 * This function returns immediately.
 *
 * If the caller needs to know when the EO stop was actually completed,
 * the num_notif and notif_tbl can be used. The given notification event(s)
 * will be sent to given queue(s), when the stop actually completes.
 * If such notification is not needed, use 0 as num_notif.
 *
 * @param eo            EO id
 * @param num_notif     How many notification events given, 0 for no notification
 * @param notif_tbl     Array of pairs of event and queue identifiers
 *
 * @return EM_OK if successful.
 *
 * @see em_stop_func_t(), em_stop_local_func_t(), em_eo_start()
 *
 * @todo Method for the application to get the final stop status
 */
em_status_t
em_eo_stop(em_eo_t eo, int num_notif, const em_notif_t* notif_tbl)
{
  em_eo_element_t *eo_elem;
  void            *eo_ctx;
  em_status_t      ret;


  RETURN_ERROR_IF(invalid_eo(eo), EM_ERR_BAD_ID, EM_ESCOPE_EO_STOP,
                  "Invalid EO id %"PRI_EO"", eo);

  eo_elem = get_eo_element(eo);


  // Disable all queues. It doesn't matter if some of the queues are already disabled.
  em_queue_disable_all(eo, 0, NULL);


  if(eo_elem->stop_local_func != NULL)
  {
    // Note: callback function used to call global EO stop once all stop_local:s have run on all cores (with internal 'done' event)
    // em_eo_stop_local__done_callback((void *) eo_elem);
    // Note2: Also send notifications if requested.    
    ret = em_eo_stop_local(eo_elem, num_notif, notif_tbl);

    IF_UNLIKELY(ret != EM_OK)
    {
      return EM_INTERNAL_ERROR(ret, EM_ESCOPE_EO_STOP, "EO:%"PRI_EO" local stop func failed", eo);
    }
  }
  else
  {
    eo_ctx = eo_elem->eo_ctx;
   
    //
    // Call eo stop after changing and verifying queue status
    // 
    ret = eo_elem->stop_func(eo_ctx, eo);
    
    IF_UNLIKELY(ret != EM_OK) {
      return EM_INTERNAL_ERROR(ret, EM_ESCOPE_EO_STOP, "EO:%"PRI_EO" stop-func failed", eo);
    }
    
    
    // Send notification if requested
    if((num_notif > 0) && (num_notif < EM_EVENT_GROUP_MAX_NOTIF))
    {
      em_core_mask_t      core_mask;
      em_event_t          event;
      em_internal_event_t *i_event;
      
      
      event = em_alloc(sizeof(em_internal_event_t), EM_EVENT_TYPE_SW, EM_POOL_DEFAULT);

      IF_UNLIKELY(event == EM_EVENT_UNDEF) {
        return EM_INTERNAL_ERROR(EM_ERR_ALLOC_FAILED, EM_ESCOPE_EO_STOP, "Internal event EO_STOP_REQ alloc failed");
      }
      
      i_event = em_event_pointer(event);
      i_event->id = EO_STOP_REQ;
      
      // set all cores in mask since we do not want to lookup all queues belonging to this EO and check all queue groups
      em_core_mask_zero(&core_mask);
      em_core_mask_set_count(em_core_count(), &core_mask);
      
      ret = em_internal_notif(&core_mask, event, NULL, NULL, num_notif, notif_tbl);

      IF_UNLIKELY(ret != EM_OK) {
        return EM_INTERNAL_ERROR(ret, EM_ESCOPE_EO_STOP, "em_internal_notif(core_mask=%"PRIX64") failed", core_mask.u64); 
      }
    }
  }
  

  return EM_OK;
}



/**
 * Callback function run when all start_local functions are finished
 */
static void
em_eo_stop_local__done_callback(void *args)
{
  em_eo_element_t *const eo_elem = (em_eo_element_t *) args;
  void            *const eo_ctx  = eo_elem->eo_ctx;
  em_status_t            ret;
  

  ERROR_IF(eo_elem == NULL, EM_FATAL(EM_ERR_BAD_POINTER),
           EM_ESCOPE_EO_STOP_LOCAL__DONE_CALLBACK, "eo_elem is NULL!")
  {
    return;          
  }
  
  
  // Call the Global EO stop function now that all EO local stop functions are done
  ret = eo_elem->stop_func(eo_ctx, eo_elem->id);
  
  ERROR_IF(ret != EM_OK, ret, EM_ESCOPE_EO_STOP_LOCAL__DONE_CALLBACK,
           "EO:%"PRI_EO" stop-func failed", eo_elem->id);
           
}



/**
 * Logical core id.
 *
 * Returns the logical id of the current core.
 * EM enumerates cores (or HW threads) to always start from 0 and be contiguous,
 * i.e. valid core identifiers are 0...em_core_count()-1
 *
 * @return Current logical core id
 *
 * @see em_core_count()
 */
int
em_core_id(void)
{
  return phys_to_logic_core_id(env_get_core_num());
}



/**
 * The number of cores running within the same EM instance (sharing the EM state).
 *
 * @return Number of EM cores (or HW threads)
 *
 * @see em_core_id()
 *
 * @todo CPU hot plugging support
 */
int
em_core_count(void)
{
  return em_core_map.count;
}



/**
 * Converts a logical core id to a physical core id
 *
 * Mainly needed when interfacing HW specific APIs
 *
 * @param core     logical (Event Machine) core id
 *
 * @return Physical core id
 *
 * @todo Needs better spec and review
 */
int
em_core_id_get_physical(int logic_core)
{
  return logic_to_phys_core_id(logic_core);
}



/**
 * Helper func - is this the first EM-core?
 * 
 * @return  'true' if the caller is running on the first EM-core
 */
int
em_is_first_core(void)
{
  return (em_core_id() == 0);
}



/**
 * Converts a logical core mask to a physical core mask
 *
 * Mainly needed when interfacing HW specific APIs
 *
 * @param phys     Core mask of physical core ids
 * @param logic    Core mask of logical (Event Machine) core ids
 *
 * @todo Needs better spec and review
 */
void
em_core_mask_get_physical(em_core_mask_t       *phys,
                          const em_core_mask_t *logic)
{

  if(em_core_mask_equal(logic, &em_core_map.logic_mask))
  {
    em_core_mask_copy(phys, &em_core_map.phys_mask);
  }
  else
  {
    int i;

    em_core_mask_zero(phys);


    for(i = 0; i < MAX_CORES; i++)
    {
      int phys_core;

      if(em_core_mask_isset(i, logic))
      {
        phys_core = logic_to_phys_core_id(i);
        em_core_mask_set(phys_core, phys);
      }
    }

  }

  return;  
}




/**
 * Allocate an event.
 *
 * Memory address of the allocated event is system specific and
 * can depend on given pool id, event size and type. Returned 
 * event (handle) may refer to a memory buffer or a HW specific 
 * descriptor, i.e. the event structure is system specific. 
 *
 * Use em_event_pointer() to convert an event (handle) to a pointer to 
 * the event structure. 
 *
 * EM_EVENT_TYPE_SW with minor type 0 is reserved for direct portability.
 * It is always guaranteed to return a 64-bit aligned contiguous 
 * data buffer, that can directly be used by the application up to
 * the given size (no HW specific descriptors etc are visible).
 *
 * EM_POOL_DEFAULT can be used as pool id if there's no need to 
 * use any specific memory pool.
 *
 * Additionally it is guaranteed, that two separate buffers
 * never share a cache line to avoid false sharing.
 *
 * @param size          Event size in octets
 * @param type          Event type to allocate
 * @param pool_id       Event pool id 
 *
 * @return the allocated event or EM_EVENT_UNDEF on an error
 *
 * @see em_free(), em_send(), em_event_pointer(), em_receive_func_t()
 */
em_event_t
em_alloc(size_t size, em_event_type_t type, em_pool_id_t pool_id)
{
  /* Return:
   * The pointer to the new mbuf on success.
   *   - NULL if allocation failed
   */

  IF_UNLIKELY(size > MBUF_SIZE)
  {
    (void) EM_INTERNAL_ERROR(EM_ERR_TOO_LARGE, EM_ESCOPE_ALLOC, "size(%u) > MBUF_SIZE(%u)", size, MBUF_SIZE);
    return EM_EVENT_UNDEF;
  }
  else
  {
    struct rte_mbuf *const m = rte_pktmbuf_alloc((struct rte_mempool *) em_event_pool.pool);

    IF_UNLIKELY(m == NULL)
    {
      (void) EM_INTERNAL_ERROR(EM_ERR_ALLOC_FAILED, EM_ESCOPE_ALLOC, "rte_pktmbuf_alloc() failed");
      return EM_EVENT_UNDEF;
    }
    else {
      /* Need to init the src_q_type so that em_send() does not misinterpret.
       * The event hdr starts after the 'struct mbuf' (RTE_PKTMBUF_HEADROOM)
       */
      em_event_hdr_t *const ev_hdr = mbuf_to_event_hdr(m);

      ev_hdr->q_elem      = em_core_local.current_q_elem; // Direct Tx after alloc expects q_elem set
      ev_hdr->src_q_type  = EM_QUEUE_TYPE_UNDEF;
      ev_hdr->event_type  = type;
      ev_hdr->event_group = EM_EVENT_GROUP_UNDEF;

      // ev_hdr->lock_p          = NULL;
      // ev_hdr->dst_q_elem      = NULL;
      // ev_hdr->processing_done = 0;
      // ev_hdr->operation       = 0;
      // ev_hdr->io_port         = 0;
      
    
    #ifdef EVENT_TIMER
      if(em_internal_conf.conf.evt_timer) {
        rte_timer_init(&ev_hdr->event_timer);
      }
    #endif  
    }

    return mbuf_to_event(m);
  }
}



/**
 * Free an event.
 *
 * It is assumed the implementation can detect from which
 * memory area/pool the event was originally allocated from.
 *
 * Free transfers the ownership of the event to the system and
 * application must not touch the event (or related memory buffers)
 * after calling it.
 *
 * Application must only free events it owns. For example, sender must
 * not free an event after sending it. 
 * 
 * @param event         Event to be freed
 *
 * @see em_alloc(), em_receive_func_t()
 *
 * @todo OK or not to free EM_EVENT_UNDEF?
 */
void
em_free(em_event_t event)
{
  em_event_hdr_t *ev_hdr;


  IF_UNLIKELY(event == NULL)
  {
    (void) EM_INTERNAL_ERROR(EM_ERR_BAD_POINTER, EM_ESCOPE_FREE, "event ptr NULL!");
    return;
  }


  ev_hdr = event_to_event_hdr(event);


  if(ev_hdr->src_q_type == EM_QUEUE_TYPE_PARALLEL_ORDERED)
  {
    /*
     * Special handling for parallel-ordered _originated_ events.
     * Handling is similar to em_send() (instead of the last send we do a free)
     */
    em_send_from_parallel_ord_q(ev_hdr, NULL, EM_QUEUE_UNDEF, OPERATION_MARK_FREE);
  }
  else
  {
    intel_free(ev_hdr);
  }

}




/*
 * EM initialisation
 * ******************************************************
 */


/*
 * Global event machine initialization. Run only once.
 */
em_status_t
em_init_global(const em_internal_conf_t *const em_internal_conf)
{
  em_status_t ret;


  // Initialize the lcore <-> em-core mappings (em-core ids always start from 0)
  // Keep first.
  core_map_init();

  printf("em_init_global() on EM-core %u (lcore %u)\n", em_core_id(), rte_lcore_id());

  #ifdef EM_64_BIT
  printf("EM API version: v%i.%i, 64 bit \n", EM_API_VERSION_MAJOR, EM_API_VERSION_MINOR);
  #else
  printf("EM API version: v%i.%i, 32 bit \n", EM_API_VERSION_MAJOR, EM_API_VERSION_MINOR);
  #endif

  /* Initialize the error handling */
  em_error_init();


  /* Initialize the EM barrier */
  env_barrier_init(&em_barrier.barrier, env_core_mask_count(em_core_count()));


  /* Initialise the event pool */
  em_event_pool.pool = intel_pool_init();

  // Init EM data structures
  queue_alloc_init();
  eo_alloc_init();

  // staged init: need to memset & init vars that are used in e.g. queue_group_init_global()
  sched_init_global_1(); 
 
  env_spinlock_init(&queue_create_lock.lock);

  ret = queue_init__rings_init();
  RETURN_ERROR_IF(ret != EM_OK, ret, EM_ESCOPE_INIT_GLOBAL, "queue_init__rings_init() returned error");
  
  
  event_group_alloc_init();
  queue_group_init_global();

  // 2nd stage sched init
  ret = sched_init_global_2();
  RETURN_ERROR_IF(ret != EM_OK, ret, EM_ESCOPE_INIT_GLOBAL, "sched_init_global() returned error");


#ifdef EVENT_TIMER
  if(em_internal_conf->conf.evt_timer)
  {
    int err = evt_timer_init_global();
    
    RETURN_ERROR_IF(err != EVT_TIMER_OK, EM_ERR_LIB_FAILED, EM_ESCOPE_INIT_GLOBAL,
                    "evt_timer_init_global() returned error %i", err);
  }
#endif
  
  
#ifdef EVENT_PACKET
  if(em_internal_conf->conf.pkt_io)
  {
    intel_eth_init();
    
    intel_init_packet_q_hash_global();
  }
#endif


  env_sync_mem();

  return EM_OK;
}



/*
 * Local event machine initialization. Run on each core.
 */
em_status_t
em_init_local(const em_internal_conf_t *const em_internal_conf)
{
  unsigned     core_id;
  em_status_t  stat;


  core_id = em_core_id();

  printf("em_init_local() on em-core %u\n", core_id);

  // Don't memset em_core_local anymore, use static initialization at declaration,
  // because EM-core 0 might use these vars during global startup - thus avoid dual initialization.
  //(void) memset(&em_core_local, 0, sizeof(em_core_local));


  stat = queue_group_init_local();
  RETURN_ERROR_IF(stat != EM_OK, stat, EM_ESCOPE_EM_INIT_LOCAL,
                  "queue_group_init_local() fails!");


  sched_init_local();
  

#ifdef EVENT_TIMER
  if(em_internal_conf->conf.evt_timer)
  {
    int err = evt_timer_init_local();
    RETURN_ERROR_IF(err != EVT_TIMER_OK, EM_ERR_LIB_FAILED, EM_ESCOPE_EM_INIT_LOCAL,
                    "evt_timer_init_local() returned error %i", err);
  }
#endif



#ifdef EVENT_PACKET
  if(em_internal_conf->conf.pkt_io)
  {
    intel_eth_init_local();
    
    intel_init_packet_q_hash_local();
  }
#endif


  env_sync_mem();

  return EM_OK;
}




static size_t
str_copy(char* to, const char* from, size_t max)
{
  size_t i;

  IF_UNLIKELY(max == 0)
  {
    return 0;
  }

  // max >= 1
  for(i = 0; (i < (max-1)) && (from[i] != 0); i++)
  {
    to[i] = from[i];
  }

  // string termination
  to[i] = '\0';

  // return the number of chars copied, excl. '\0'
  return (i > 0) ? (i - 1) : 0;
}




/**
 * Print information about EM & the environment
 */
void
em_print_info(void)
{
  int logic_core;


  printf(                             "\n"
         "==========================" "\n"
         "EM Info on Intel:"          "\n"
         "Cache Line size         = %u B"  "\n"
         "em_queue_element_t      = %lu B" "\n"
         "em_queue_element_t.lock = %lu B" "\n"
         "==========================" "\n"
                                      "\n",
         ENV_CACHE_LINE_SIZE,
         sizeof(em_queue_element_t),
         offsetof(em_queue_element_t, lock)
        );


  printf("Core mapping logic EM core -> phys core (lcore)\n");

  for(logic_core = 0; logic_core < em_core_count(); logic_core++)
  {
    printf("                        %2i           %2i\n", logic_core, logic_to_phys_core_id(logic_core));
  }

  print_queue_groups();

  printf("\n"); fflush(NULL);
}



static inline int
get_static_queue_lock_idx(em_queue_t queue)
{
  return  queue & (STATIC_QUEUE_LOCKS - 1);
}




static void
core_map_init(void)
{
  int i;
  int count;


  (void) memset(&em_core_map, 0, sizeof(em_core_map));

  //
  // Loop through all running cores
  //

  RTE_LCORE_FOREACH(i)
  {
    count = em_core_map.count;

    em_core_map.logic[i]    = count;
    em_core_map.phys[count] = i;

    em_core_mask_set(count, &em_core_map.logic_mask);
    em_core_mask_set(i,     &em_core_map.phys_mask);

    em_core_map.count++;
  }
  
}



static inline int
logic_to_phys_core_id(const int logic_core)
{
  return em_core_map.phys[logic_core];
}



static inline int
phys_to_logic_core_id(const int phys_core)
{
  return em_core_map.logic[phys_core];
}



static em_status_t
em_eo_start_local(em_eo_element_t *const eo_elem, int num_notif, const em_notif_t* notif_tbl)
{
  return eo_local_func_call_req(eo_elem, EO_LOCAL_START_REQ, em_eo_start_local__done_callback,
                                (void *)eo_elem, num_notif, notif_tbl);
}



static em_status_t
em_eo_stop_local(em_eo_element_t *const eo_elem, int num_notif, const em_notif_t* notif_tbl)
{
  return eo_local_func_call_req(eo_elem, EO_LOCAL_STOP_REQ, em_eo_stop_local__done_callback,
                                (void *)eo_elem, num_notif, notif_tbl);
}



static em_status_t
eo_local_func_call_req(em_eo_element_t *const eo_elem,
                       uint64_t               ev_id,
                       void (*f_done_callback)(void *arg_ptr),
                       void  *f_done_arg_ptr,
                       int                    num_notif,
                       const em_notif_t      *notif_tbl)
{
  em_status_t         err;
  em_event_t          event;
  em_internal_event_t *i_event;
  int                 core_count;
  em_core_mask_t      core_mask;

  
  event = em_alloc(sizeof(em_internal_event_t), EM_EVENT_TYPE_SW, EM_POOL_DEFAULT);
  
  RETURN_ERROR_IF(event == EM_EVENT_UNDEF, EM_ERR_ALLOC_FAILED, EM_ESCOPE_EO_LOCAL_FUNC_CALL_REQ,
                  "Internal event (%u) allocation failed", ev_id);
  
  i_event = em_event_pointer(event);
  i_event->id               = ev_id;
  i_event->loc_func.eo_elem = eo_elem;

  core_count = em_core_count();
  
  em_core_mask_zero(&core_mask);
  em_core_mask_set_count(core_count, &core_mask);
  
  
  err = em_internal_notif(&core_mask, event, f_done_callback, f_done_arg_ptr, num_notif, notif_tbl);
  
  RETURN_ERROR_IF(err != EM_OK, err, EM_ESCOPE_EO_LOCAL_FUNC_CALL_REQ,
                  "em_internal_notif(core_mask=%"PRIX64") failed", core_mask.u64);
  
  return EM_OK;
}






