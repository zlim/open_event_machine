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
 *  Changed atomic queues to operate in a lockless manner. Integrated
 *  the multiqueue data type in place of priority arrays.
 */
 

/**
 * @file
 *
 * EM Intel Scheduler 
 *
 */


#include "em_intel_sched.h"
#include "em_intel_event_group.h"
#include "em_intel_queue_group.h"
#include "em_internal_event.h"
#include "em_error.h"


#ifdef EVENT_PACKET
  #include "em_intel_packet.h"
#endif

#include "intel_hw_init.h"

#include "multiring.h"

#include "em_shared_data.h"
#include "em_intel_inline.h"



#define  SCHED_QS_MASK                       (SCHED_QS - 1)
COMPILE_TIME_ASSERT(POWEROF2(SCHED_QS), SCHED_QS__NOT_POWER_OF_TWO);

#define  SCHED_Q_QUEUE_MASK                  (SCHED_Q_MAX_QUEUES - 1)
COMPILE_TIME_ASSERT(POWEROF2(SCHED_Q_MAX_QUEUES), SCHED_QS_MAX_QUEUES__NOT_POWER_OF_TWO);

#define  SCHED_Q_ATOMIC_SELECT(q_elem)       (&em.shm->sched_qs_prio.sched_q_atomic[(q_elem)->queue_group])
#define  SCHED_Q_PARALLEL_SELECT(q_elem)     (&em.shm->sched_qs_prio.sched_q_parallel[(q_elem)->queue_group])
#define  SCHED_Q_PARALLEL_ORD_SELECT(q_elem) (&em.shm->sched_qs_prio.sched_q_parallel_ord[(q_elem)->queue_group])



#define PARALLEL_ORDERED__USE_SCHED_Q_LOCKS  (0) // 0=default=use Q-locks to maintain order, 1=use sched-Q locks to maintain order



/*
 * Bulk dequeue buffers used in the schedule_...() functions.
 */
 
// Multi-Ring
#define MAX_Q_BULK_ATOMIC        (8)  // Max nbr of q_elems to bulk dequeue
#define MAX_E_BULK_PARALLEL      (16) // Max nbr of event-hdrs to bulk dequeue
#define MAX_E_BULK_PARALLEL_ORD  (16) // Max nbr of event-hdrs to bulk dequeue

// RTE-Ring
#define MAX_E_BULK_ATOMIC        (16)  // Max nbr of events  to bulk dequeue

#define BULK_DEQUEUE_BUF1_SIZE   (MAX_Q_BULK_ATOMIC)
#define BULK_DEQUEUE_BUF2_SIZE   (MAX_E_BULK_ATOMIC)
#define BULK_DEQUEUE_BUF_SIZE    (MAX(MAX_E_BULK_PARALLEL, MAX_E_BULK_PARALLEL_ORD))


typedef union
{
  struct {
    
    void* buf1[BULK_DEQUEUE_BUF1_SIZE];

    void* buf2[BULK_DEQUEUE_BUF2_SIZE];
  };
  
  void*   buf[BULK_DEQUEUE_BUF_SIZE];

} bulk_dequeue_bufs_t ENV_CACHE_LINE_ALIGNED;


/**
 * Bulk dequeue buffers
 */
static ENV_LOCAL  bulk_dequeue_bufs_t  bulk_dequeue_bufs  ENV_CACHE_LINE_ALIGNED;

COMPILE_TIME_ASSERT((sizeof(bulk_dequeue_bufs) % ENV_CACHE_LINE_SIZE) == 0, BULK_DEQUEUE_BUFS_SIZE_ERROR);




typedef union
{ 
  struct
  {
    int64_t                  events_enqueued;  /**< Core local number of succesful event enqueues (for sched-rounds approx.) */
    
    core_sched_masks_t      *sched_masks;      /**< Core local pointer to &em.shm->core_sched_masks[core] */

    core_sched_add_counts_t *sched_add_counts; /**< Core local pointer to &em.shm->core_sched_add_counts[core] */

    sched_qs_info_local_t    sched_qs_info;    /**< Core local sched queue info (indexes, sched-counts etc.) */
  };
  
  uint8_t u8[4 * ENV_CACHE_LINE_SIZE];
  
} sched_core_local_t;


static ENV_LOCAL  sched_core_local_t  sched_core_local  ENV_CACHE_LINE_ALIGNED;

COMPILE_TIME_ASSERT((sizeof(sched_core_local) % ENV_CACHE_LINE_SIZE) == 0, EM_SCHED_CORE_LOCAL_SIZE_ERROR);



/*
 * Local function prototypes
 */
 
static inline int
em_schedule_queues(void);

static inline int
em_schedule_atomic(sched_q_atomic_t              sched_q_atomic[],
                   sched_qs_info_local_t *const  sched_qs_info,
                   sched_type_mask_t     *const  sched_masks);

static inline int
em_schedule_parallel(sched_q_parallel_t           sched_q_parallel[],
                     sched_qs_info_local_t *const sched_qs_info,
                     sched_type_mask_t     *const sched_masks);

static inline int
em_schedule_parallel_ordered(sched_q_parallel_ord_t       sched_q_parallel_ord[],
                             sched_qs_info_local_t *const sched_qs_info,
                             sched_type_mask_t     *const sched_masks);

static inline em_status_t
parallel_ordered_maintain_order(em_event_hdr_t *const ev_hdr, em_queue_element_t *const q_elem, env_spinlock_t *const lock);                             




static inline em_status_t
em_send_atomic(em_event_hdr_t *const ev_hdr, em_queue_element_t *const q_elem, const em_queue_t queue);

static inline em_status_t
em_send_parallel(em_event_hdr_t *const ev_hdr, em_queue_element_t *const q_elem, const em_queue_t queue);

static inline em_status_t
em_send_parallel_ordered(em_event_hdr_t *const ev_hdr, em_queue_element_t *const q_elem, const em_queue_t queue);




static inline void
dispatch_event(em_queue_element_t *const q_elem,
               em_event_t                event,
               const em_event_type_t     event_type);


#if RX_DIRECT_DISPATCH == 1

static inline void
em_direct_dispatch__atomic(em_queue_element_t *const q_elem,
                           em_event_t                event,
                           em_event_hdr_t     *const ev_hdr);

static inline void
em_direct_dispatch__parallel_ordered(em_queue_element_t *const q_elem,
                                     em_event_t                event,
                                     em_event_hdr_t     *const ev_hdr);
#endif




static inline uint32_t
sched_q_get_next_idx(const uint32_t curr_idx, const uint64_t group_mask);

static inline uint16_t
sched_q_get_next_qidx(const uint16_t curr_idx, const uint16_t mask);




/*
 * Functions
 */


/**
 * EM event dispatch.
 * 
 * Called by each EM-core to dispatch events for EM processing.
 *
 * @param rounds  Dispatch rounds before returning, 0 means 'never return from dispatch'
 */
void
em_dispatch(uint32_t rounds)
{
  uint32_t i;
  
  
  IF_LIKELY(rounds > 0)
  {
    for(i = 0; i < rounds; i++)
    {
      /*
       * Schedule events to the core from queues
       */
      em_schedule();
    }
  }
  else // rounds == 0 (== FOREVER)
  {
    for(;/*ever*/;)
    {
      em_schedule();
    }
  }
  
  return;
}



/**
 * Global init of the scheduling queues
 */
void
sched_init_global_1(void) 
{
  (void) memset(&em.shm->sched_qs_prio,         0, sizeof(em.shm->sched_qs_prio));
  (void) memset( em.shm->core_sched_masks,      0, sizeof(em.shm->core_sched_masks));     
  (void) memset( em.shm->core_sched_add_counts, 0, sizeof(em.shm->core_sched_add_counts));
  
  env_spinlock_init(&em.shm->sched_add_counts_lock.lock);
}



em_status_t
sched_init_global_2(void)
{
  char     sched_q_name[32];
  int      num_cores;
  int      i, j;

  sched_q_atomic_t        *sched_q_atomic;
  sched_q_parallel_t      *sched_q_parallel;
  sched_q_parallel_ord_t  *sched_q_parallel_ord;


  printf("%s():\n", __func__);

  num_cores = em_core_count();


  sched_q_atomic       = em.shm->sched_qs_prio.sched_q_atomic;
  sched_q_parallel     = em.shm->sched_qs_prio.sched_q_parallel;
  sched_q_parallel_ord = em.shm->sched_qs_prio.sched_q_parallel_ord;

  printf("  Initialize SchedQs:\n");

  /* Set the default sched-q nbr and mask... */
  for(i = 0; i < SCHED_QS; i++)
  {
    sched_q_atomic[i].nbr_queues       = SCHED_Q_MAX_QUEUES;
    sched_q_atomic[i].queue_mask       = SCHED_Q_QUEUE_MASK;

    sched_q_parallel[i].nbr_queues     = SCHED_Q_MAX_QUEUES;
    sched_q_parallel[i].queue_mask     = SCHED_Q_QUEUE_MASK;

    sched_q_parallel_ord[i].nbr_queues = SCHED_Q_MAX_QUEUES;
    sched_q_parallel_ord[i].queue_mask = SCHED_Q_QUEUE_MASK;
  }


  /* ...and override the ones used by the internal messaging queue-groups */
  for(i = 0; i < num_cores; i++)
  {
    em_core_mask_t    mask;
    em_queue_group_t  local_group;
    char              q_grp_name[] = EM_QUEUE_GROUP_CORE_LOCAL_BASE_NAME;

    em_core_mask_zero(&mask);
    em_core_mask_set(i, &mask);

    core_queue_grp_name(q_grp_name, i);
    local_group = em_queue_group_find(q_grp_name);
    
    RETURN_ERROR_IF(local_group == EM_QUEUE_GROUP_UNDEF, EM_FATAL(EM_ERR_NOT_FOUND), EM_ESCOPE_SCHED_QUEUE_INIT,
                    "Did not find a queue group for EM-core %i", i);
                    
    sched_q_atomic[local_group].nbr_queues       = 1;
    sched_q_atomic[local_group].queue_mask       = 0x0;

    sched_q_parallel[local_group].nbr_queues     = 1;
    sched_q_parallel[local_group].queue_mask     = 0x0;

    sched_q_parallel_ord[local_group].nbr_queues = 1;
    sched_q_parallel_ord[local_group].queue_mask = 0x0;
  }

  
  printf("    Atomic SchedQs...          ");
  /* Initialize the atomic scheduling queues */
  for(i = 0; i < SCHED_QS; i++)
  {
    /*
     * Atomic scheduling queues - multi-producer, multi-consumer
     * Multiple different atomic event queues can be enqeueud/dequeued simultaneously
     */
    for(j = 0; j < sched_q_atomic[i].nbr_queues; j++)
    {
      (void) snprintf(sched_q_name, sizeof(sched_q_name), "Atomic-ShedQ-%i-%i", i, j);
      sched_q_name[31] = '\0';

      sched_q_atomic[i].sched_q[j] = mring_create(sched_q_name, DEVICE_SOCKET, 0);
      
      RETURN_ERROR_IF(sched_q_atomic[i].sched_q[j] == NULL, EM_FATAL(EM_ERR_ALLOC_FAILED), EM_ESCOPE_SCHED_QUEUE_INIT,
                      "Atomic sched-queue-%i-%i alloc failed!", i, j);
    }

  }
  printf(" done.\n");

  
  printf("    Parallel SchedQs...        ");
  /* Initialize the parallel scheduling queues */
  for(i = 0; i < SCHED_QS; i++)
  {
    for(j = 0; j < sched_q_parallel[i].nbr_queues; j++)
    {
      (void) snprintf(sched_q_name, sizeof(sched_q_name), "Parallel-ShedQ-%i-%i", i, j);
      sched_q_name[31] = '\0';

      sched_q_parallel[i].sched_q[j] = mring_create(sched_q_name, DEVICE_SOCKET, 0);

      RETURN_ERROR_IF(sched_q_parallel[i].sched_q[j] == NULL, EM_FATAL(EM_ERR_ALLOC_FAILED), EM_ESCOPE_SCHED_QUEUE_INIT,
                      "Parallel sched-queue-%i-%i alloc failed!", i, j);
    }

  }
  printf(" done. \n");

  
  printf("    Parallel-Ordered SchedQs...");
  /* Initialize the parallel ordered scheduling queues */
  for(i = 0; i < SCHED_QS; i++)
  {
    for(j = 0; j < sched_q_parallel_ord[i].nbr_queues; j++)
    {
      (void) snprintf(sched_q_name, sizeof(sched_q_name), "ParalOrd-ShedQ-%i-%i", i, j);
      sched_q_name[31] = '\0';

      /* Parallel-Ordered scheduling queues - multi-producer, single-consumer (spinlocks used to serialize access) */
      sched_q_parallel_ord[i].sched_q[j] = mring_create(sched_q_name, DEVICE_SOCKET, RING_F_SC_DEQ);

      RETURN_ERROR_IF(sched_q_parallel_ord[i].sched_q[j] == NULL, EM_FATAL(EM_ERR_ALLOC_FAILED), EM_ESCOPE_SCHED_QUEUE_INIT,
                      "Parallel-Ordered sched-queue-%i-%i alloc failed!", i, j);

      env_spinlock_init(&sched_q_parallel_ord[i].locks[j].lock);
    }

  }
  printf(" done. \n");

  return EM_OK;
}




/**
 * Local init of the scheduling queues
 */
void
sched_init_local(void)
{
  int core = em_core_id();
  
  
  (void) memset(&sched_core_local, 0, sizeof(sched_core_local));
  
  // Set core-local pointer to the sched masks & counts.
  sched_core_local.sched_masks      = &em.shm->core_sched_masks[core];
  sched_core_local.sched_add_counts = &em.shm->core_sched_add_counts[core];
  

  sched_core_local.sched_qs_info.sched_q_atomic_idx 
    = sched_q_get_next_idx(SCHED_QS - 1, sched_core_local.sched_masks->sched_masks_prio.atomic_masks.q_grp_mask);
    
  sched_core_local.sched_qs_info.sched_q_parallel_idx 
    = sched_q_get_next_idx(SCHED_QS - 1, sched_core_local.sched_masks->sched_masks_prio.parallel_masks.q_grp_mask);
    
  sched_core_local.sched_qs_info.sched_q_parallel_ord_idx
    = sched_q_get_next_idx(SCHED_QS - 1, sched_core_local.sched_masks->sched_masks_prio.parallel_ord_masks.q_grp_mask);
}




void
em_schedule(void)
{
  int events_dispatched;
  
  
  #ifdef EVENT_PACKET
  if(em_internal_conf.conf.pkt_io)
  {
    /*
     * Poll for Eth frames and enqueue.
     */
    em_eth_rx_packets();
  }
  #endif
  

  do {
    
    #ifdef EVENT_TIMER
    if(em_internal_conf.conf.evt_timer)
    {
      /*
       * Manage the event timer.
       */
      evt_timer_manage();
    }
    #endif
    
    
    /*
     * Schedule & dispatch.
     */
    events_dispatched = em_schedule_queues();


    #ifdef EVENT_PACKET
    if(em_internal_conf.conf.pkt_io)
    {
      /*
       * Transmit Eth frames that have been in the output buffers for "too long".
       */
      em_eth_tx_packets_timed();
    }
    #endif        
    
    
    /* 
     * Approximate for the need for additional schduling rounds before new Eth-Rx events.
     * Note: the approx is core-local and does not take into account what the other cores do.
     */
    if(events_dispatched == 0) {
      break;
    }
    else {
      sched_core_local.events_enqueued -= events_dispatched;
    }
    
  } while(sched_core_local.events_enqueued > 0);
  
  
  /* Reset count for the next round */
  sched_core_local.events_enqueued = 0;
}




static inline int
em_schedule_queues(void)
{
  int ev_a = 0, ev_p = 0, ev_po = 0;

  sched_qs_t            *const sched_qs_ptr  = &em.shm->sched_qs_prio;
  sched_qs_info_local_t *const sched_qs_info = &sched_core_local.sched_qs_info;
  sched_masks_t         *const sched_masks   = &sched_core_local.sched_masks->sched_masks_prio;


  if(sched_masks->atomic_masks.q_grp_mask)
  {
    ev_a = em_schedule_atomic(sched_qs_ptr->sched_q_atomic,
                              sched_qs_info,
                             &sched_masks->atomic_masks);
  }


  if(sched_masks->parallel_masks.q_grp_mask)
  {
    ev_p = em_schedule_parallel(sched_qs_ptr->sched_q_parallel,
                                sched_qs_info,
                               &sched_masks->parallel_masks);
  }


  if(sched_masks->parallel_ord_masks.q_grp_mask)
  {
    ev_po = em_schedule_parallel_ordered(sched_qs_ptr->sched_q_parallel_ord,
                                         sched_qs_info,
                                        &sched_masks->parallel_ord_masks);
  }


  return (ev_a + ev_p + ev_po);
}




/**
 * Select and schedule events from an atomic scheduling queue.
 *
 * The scheduling queues contain q_elems (not events!). Once a schedulable q_elem is dequeued
 * then an event associated with it is dequeued from q_elem->rte_ring.
 */
static inline int
em_schedule_atomic(sched_q_atomic_t              sched_q_atomic[],
                   sched_qs_info_local_t *const  sched_qs_info,
                   sched_type_mask_t     *const  sched_masks)
{
  struct multiring* sched_q;

  uint32_t         sched_idx;
  uint16_t         qidx, next_qidx;

  uint64_t         sched_mask;
  uint16_t         qidx_mask;  

  int              ret, j;
  int              q_count;
  void* *const     q_ptr = bulk_dequeue_bufs.buf1;
  void* *const     e_ptr = bulk_dequeue_bufs.buf2;
  int              events_dispatched = 0; // Return value

  

  sched_idx  = sched_qs_info->sched_q_atomic_idx;
  
  sched_mask = sched_masks->q_grp_mask;
  qidx_mask  = sched_masks->qidx_mask[sched_idx];
  
  qidx = sched_qs_info->atomic_qidx[sched_idx];
  
  sched_q = sched_q_atomic[sched_idx].sched_q[qidx];



  /*
   * Dequeue q_elems from the sched-queue. Use the same sched-q up to
   * 'SCHED_Q_ATOMIC_CNT_MAX' times if there's a lot of events in this sched-q.
   */
  q_count = mring_dequeue_mp_burst(sched_q, q_ptr, MAX_Q_BULK_ATOMIC);
  

  if((q_count != MAX_Q_BULK_ATOMIC) ||
     (++(sched_qs_info->sched_q_atomic_cnt)) == SCHED_Q_ATOMIC_CNT_MAX) // 'expr2' evaluated only if 'expr1' is false
  {
      next_qidx = sched_q_get_next_qidx(qidx, qidx_mask);

      IF_UNLIKELY(next_qidx <= qidx) // Wrap
      {
        sched_qs_info->sched_q_atomic_idx = sched_q_get_next_idx(sched_idx, sched_mask); // Increment core/thread local var
      }

      sched_qs_info->atomic_qidx[sched_idx] = next_qidx;
      sched_qs_info->sched_q_atomic_cnt     = 0;
  }


  if(q_count > 0)
  {
    PREFETCH_Q_ELEM(q_ptr[0]);    


    for(j = 0; j < q_count; j++)
    {
      em_queue_element_t *const q_elem = q_ptr[j];
      unsigned                  e_count;


      //e_count = rte_ring_count(q_elem->rte_ring);
      e_count = q_elem->u.atomic.event_count;
      
      if(e_count > MAX_E_BULK_ATOMIC) {
         e_count = MAX_E_BULK_ATOMIC;
      }


      ret = rte_ring_dequeue_bulk(q_elem->rte_ring, e_ptr, e_count);

      IF_LIKELY(ret == 0)
      {
        unsigned i;

        //
        // Dispatch events
        //
        for(i = 0; i < e_count; i++)
        {
          em_event_t            event  = e_ptr[i];
          ENV_PREFETCH(event);
          em_event_hdr_t *const ev_hdr = event_to_event_hdr(event);

          // Mark that this event was received from an atomic queue
          ev_hdr->src_q_type = EM_QUEUE_TYPE_ATOMIC;

          dispatch_event(q_elem, event, ev_hdr->event_type);
        }
        
        events_dispatched += e_count;
        
        
        // Prefetch next q_elem
        if((j+1) < q_count) {
          PREFETCH_Q_ELEM(q_ptr[j+1])
        }
        

        //
        // Update counters
        //
        
#if LOCKLESS_ATOMIC_QUEUES == 1

        int32_t new_count = __sync_sub_and_fetch(&q_elem->u.atomic.event_count, e_count);
        
        ret = 1;
        
        if(new_count > 0)
        {
          ret = mring_enqueue(sched_q, q_elem->priority, q_elem);
        }
        else
        {
          // Nothing currently to be scheduled, lets clear the sched_count using atomic cmpset
          union {
            struct {
              int32_t sched_count, event_count;
            };
            uint64_t atomic_counts;
          } expected_value = {{.sched_count = 1, .event_count = 0}};

          // If cmpset fails, then someone has modified event_count (upwards) so reschedule the queue
          if(!rte_atomic64_cmpset(&q_elem->u.atomic.atomic_counts_u64, expected_value.atomic_counts, 0))
          {
            ret = mring_enqueue(sched_q, q_elem->priority, q_elem);
          }
        }

        IF_UNLIKELY(ret != 1) {
          // Should never happen
          (void) EM_INTERNAL_ERROR(EM_FATAL(EM_ERR_LIB_FAILED), EM_ESCOPE_SCHEDULE_ATOMIC,
                                   "Atomic sched queue enqueue failed, ret=%i!", ret);
        }          
        
#else // LOCKLESS_ATOMIC_QUEUES == 0

        env_spinlock_lock(&q_elem->lock);

        if(q_elem->u.atomic.event_count > e_count)
        {
          // Continue scheduling.
          q_elem->u.atomic.event_count -= e_count;

          // MULTI PRODUCER
          // NOTE: Atomic context lost after this. Another core can schedule/dispatch.
          ret = mring_enqueue(sched_q, q_elem->priority, q_elem);

          IF_UNLIKELY(ret != 1) {
            // Should never happen
            (void) EM_INTERNAL_ERROR(EM_FATAL(EM_ERR_LIB_FAILED), EM_ESCOPE_SCHEDULE_ATOMIC,
                                     "Atomic sched queue enqueue failed, ret=%i!", ret);
          }
        }
        else
        {
          // Last event. Don't schedule any more.
          q_elem->u.atomic.sched_count = 0;          
          q_elem->u.atomic.event_count = 0;
        }

        env_spinlock_unlock(&q_elem->lock);
        
#endif // #if LOCKLESS_ATOMIC_QUEUES == 1

      }
      else {
        // Should never happen
       (void) EM_INTERNAL_ERROR(EM_FATAL(EM_ERR_LIB_FAILED), EM_ESCOPE_SCHEDULE_ATOMIC,
                                "Schedulable ATOMIC queue found but NO EVENTS in queue, ret=%i", ret);
      }
    }
  }


  return events_dispatched;
}



/**
 * Select and schedule events from a parallel scheduling queue.
 *
 * The scheduling queues contain events in this case (cmp to atomic or parallel-ordered sched queues).
 */
static inline int
em_schedule_parallel(sched_q_parallel_t           sched_q_parallel[],
                     sched_qs_info_local_t *const sched_qs_info,
                     sched_type_mask_t     *const sched_masks)
{
  struct multiring   *sched_q;

  uint32_t            sched_idx;
  uint16_t            qidx, next_qidx;

  uint64_t            sched_mask;
  uint16_t            qidx_mask;

  int                 ev_hdr_count;
  void* *const        ev_hdr_ptr        = bulk_dequeue_bufs.buf;
  int                 events_dispatched = 0; // Return value



  sched_idx = sched_qs_info->sched_q_parallel_idx;

  sched_mask = sched_masks->q_grp_mask;
  qidx_mask  = sched_masks->qidx_mask[sched_idx];
  
  qidx = sched_qs_info->parallel_qidx[sched_idx];
  
  sched_q = sched_q_parallel[sched_idx].sched_q[qidx];


  /*
   * Get the number of events in the scheduling queue. Use the same sched-q up to
   * 'SCHED_Q_PARALLEL_CNT_MAX' times if there's a lot of events in this sched-q.
   */

  ev_hdr_count = mring_dequeue_mp_burst(sched_q, ev_hdr_ptr, MAX_E_BULK_PARALLEL);

  if((ev_hdr_count != MAX_E_BULK_PARALLEL) ||
     (++(sched_qs_info->sched_q_parallel_cnt)) == SCHED_Q_PARALLEL_CNT_MAX) // 'expr2' evaluated only if 'expr1' is false
  {
      next_qidx = sched_q_get_next_qidx(qidx, qidx_mask);

      IF_UNLIKELY(next_qidx <= qidx) // Wrap
      {
        sched_qs_info->sched_q_parallel_idx = sched_q_get_next_idx(sched_idx, sched_mask);
      }

      sched_qs_info->parallel_qidx[sched_idx] = next_qidx;
      sched_qs_info->sched_q_parallel_cnt     = 0;
  }



  if(ev_hdr_count > 0) // Event hdrs dequeued:
  {
    em_event_hdr_t     *ev_hdr;
    em_queue_element_t *q_elem;
    em_event_t          event;
    int                 i;


    for(i = 0; i < ev_hdr_count; i++)
    {
      ev_hdr = ev_hdr_ptr[i];
      q_elem = ev_hdr->q_elem;

      PREFETCH_Q_ELEM(q_elem);
    }


    for(i = 0; i < ev_hdr_count; i++)
    {
      ev_hdr = ev_hdr_ptr[i];
      q_elem = ev_hdr->q_elem;
      event  = event_hdr_to_event(ev_hdr);

      ev_hdr->src_q_type = EM_QUEUE_TYPE_PARALLEL;

      dispatch_event(q_elem, event, ev_hdr->event_type);
    }

    events_dispatched += ev_hdr_count;
  }


  return events_dispatched;
}



/**
 * Select and schedule events from a parallel-ordered scheduling queue.
 *
 * The scheduling queues contain event-headers (not directly events).
 * Event headers are stored in Order-Queues before dispatch to EO processing.
 * The order-queues maintain event input order for the output.
 */
static inline int
em_schedule_parallel_ordered(sched_q_parallel_ord_t       sched_q_parallel_ord[],
                             sched_qs_info_local_t *const sched_qs_info,
                             sched_type_mask_t     *const sched_masks)
{
  struct multiring *sched_q;
  
  uint32_t          sched_idx, next_sched_idx;
  uint16_t          qidx, next_qidx, save_qidx;
  uint16_t          count;

  uint64_t          sched_mask;
  uint16_t          qidx_mask;  
  
  env_spinlock_t   *lock;
  int               ev_hdr_count;
  void* *const      ev_hdr_ptr        = bulk_dequeue_bufs.buf;
  int               events_dispatched = 0; // Return value



  sched_mask = sched_masks->q_grp_mask;

  /*
   * Instead of busy-waiting for a lock, try-locks instead.
   * Return on empty queue to be fair to atomic and parallel queue scheduling.
   */

  next_sched_idx = sched_qs_info->sched_q_parallel_ord_idx;
  
  next_qidx      = sched_qs_info->parallel_ord_qidx[next_sched_idx];
  count          = sched_qs_info->sched_q_parallel_ord_cnt;
  
  
  do {
    do {
      sched_idx =  next_sched_idx;
      qidx      =  next_qidx;

      lock      = &sched_q_parallel_ord[sched_idx].locks[qidx].lock;
      ENV_PREFETCH(lock);            
      sched_q   =  sched_q_parallel_ord[sched_idx].sched_q[qidx];

      sched_qs_info->sched_q_parallel_ord_cnt = count;
      qidx_mask =  sched_masks->qidx_mask[sched_idx];

      
      next_qidx = sched_q_get_next_qidx(qidx, qidx_mask);
      save_qidx = next_qidx;
      count     = 0;

      IF_UNLIKELY(next_qidx <= qidx) // Wrap
      {
        next_sched_idx = sched_q_get_next_idx(sched_idx, sched_mask);
        next_qidx      = sched_qs_info->parallel_ord_qidx[next_sched_idx];
      }
      

      /* Avoid accessing the spinlock if there are no events */
      IF_UNLIKELY(mring_empty(sched_q))
      {
        sched_qs_info->sched_q_parallel_ord_idx          = next_sched_idx;
        sched_qs_info->parallel_ord_qidx[next_sched_idx] = next_qidx;
        sched_qs_info->parallel_ord_qidx[sched_idx]      = save_qidx;
        sched_qs_info->sched_q_parallel_ord_cnt          = 0;
        return (0);
      }

    } while(env_spinlock_is_locked(lock));

  } while(!env_spinlock_trylock(lock));

  /* Lock Taken - continue */


  ev_hdr_count = mring_dequeue_mp_burst(sched_q, ev_hdr_ptr, MAX_E_BULK_PARALLEL_ORD);
  
  if((ev_hdr_count < MAX_E_BULK_PARALLEL_ORD) ||
     ((++(sched_qs_info->sched_q_parallel_ord_cnt)) == SCHED_Q_PARALLEL_ORD_CNT_MAX)) // 'expr2' evaluated only if 'expr1' is false
  {
      sched_qs_info->sched_q_parallel_ord_idx          = next_sched_idx;
      sched_qs_info->parallel_ord_qidx[next_sched_idx] = next_qidx;
      sched_qs_info->sched_q_parallel_ord_cnt          = 0;

      IF_UNLIKELY(ev_hdr_count == 0)
      {
        // No event-hdrs - return
        env_spinlock_unlock(lock);
        return (0);
      }
  }



  if(ev_hdr_count > 0)
  {
#if PARALLEL_ORDERED__USE_SCHED_Q_LOCKS == 1 // Optional
    int  i;

    /*
     * Record/store event order before dispatch to be able to
     * restore order after parallel processing
     */
    for(i = 0; i < ev_hdr_count; i++)
    {
      em_event_hdr_t     *const ev_hdr = ev_hdr_ptr[i];
      em_queue_element_t *const q_elem = ev_hdr->q_elem;

      (void) parallel_ordered_maintain_order(ev_hdr, q_elem, lock);
    }
#else // Default
    /*
     * Maintain queue order.
     * Try to minimize the spinlock lock/unlock calls for events belonging to the same queue.
     */
    em_event_hdr_t     *ev_hdr   =  ev_hdr_ptr[0];
    em_queue_element_t *q_elem   =  ev_hdr->q_elem;
    env_spinlock_t     *q_lock   = &q_elem->lock;
    env_spinlock_t     *tmp_lock =  q_lock;
    int                 i        =  0;

    env_spinlock_lock(q_lock);

    do
    {
      ev_hdr = ev_hdr_ptr[i];
      q_elem = ev_hdr->q_elem;
      q_lock = &q_elem->lock;


      if(q_lock != tmp_lock) {
        env_spinlock_unlock(tmp_lock);
        env_spinlock_lock(q_lock);
        tmp_lock = q_lock;
      }

      (void) parallel_ordered_maintain_order(ev_hdr, q_elem, q_lock);

      i++;
    } while(i < ev_hdr_count);

    env_spinlock_unlock(q_lock);

    /********/
#endif


    /* Unlock the Scheduling-Queue lock */
    env_spinlock_unlock(lock);


    /*
     * Dispatch - parallel processing
     */
    for(i = 0; i < ev_hdr_count; i++)
    {
      em_event_hdr_t     *const ev_hdr = ev_hdr_ptr[i];
      em_queue_element_t *const q_elem = ev_hdr->q_elem;
      // PREFETCH_Q_ELEM(q_elem);
      em_event_t                event  = event_hdr_to_event(ev_hdr);

      dispatch_event(q_elem, event, ev_hdr->event_type);
    }

    events_dispatched += ev_hdr_count;

  }
  else {
    env_spinlock_unlock(lock);
  }


  return events_dispatched;
}



/**
 * Helper function to maintain order in parallel-ordered queues.
 * Call must be serialized by spinlock 'lock'.
 */
static inline em_status_t
parallel_ordered_maintain_order(em_event_hdr_t     *const ev_hdr,
                                em_queue_element_t *const q_elem,
                                env_spinlock_t     *const lock)
{
  int ret;


  PREFETCH_RTE_RING(q_elem->rte_ring);

  // Needed in em_send for re-ordering
  ev_hdr->src_q_type      = EM_QUEUE_TYPE_PARALLEL_ORDERED;
  ev_hdr->lock_p          = lock;
  ev_hdr->processing_done = 0;
  ev_hdr->dst_q_elem      = NULL;

  if(q_elem->u.parallel_ord.order_first == NULL) {
    q_elem->u.parallel_ord.order_first = ev_hdr;
  }
  else {
    // Order-Queue
    ret = rte_ring_enqueue(q_elem->rte_ring, ev_hdr);

    IF_UNLIKELY(ret) {
      /* On enqueue-error: clear the src_q_type=EM_QUEUE_TYPE_PARALLEL_ORDERED
       * to avoid never being sent if em_send() is called.
       * Order is lost for this event however...
       */
      if(ret == (-ENOBUFS))
      {
        ev_hdr->src_q_type = EM_QUEUE_TYPE_UNDEF;
        
        return EM_ERR_LIB_FAILED;
        //(void) EM_INTERNAL_ERROR(EM_ERR_LIB_FAILED, EM_ESCOPE_PARALLEL_ORDERED_MAINTAIN_ORDER,
        //                         "Order-queue enequeue failed, ret=%i", ret);
      }
    }
  }
  
  return EM_OK;
}




/**
 * Return the index of the next set bit in group_mask.
 *
 * @param curr_idx    Index of the last used sched_q-obj
 * @param group_mask  Mask bits represent sched_q-objs that should be polled
 */
static inline uint32_t
sched_q_get_next_idx(const uint32_t curr_idx, const uint64_t group_mask)
{
  uint32_t next_idx, tmp_idx;
  uint64_t tmp_mask1, tmp_mask2;


  // Example: curr_idx=5 => bit(5+1) => 01000000 => -1 => 00111111 => ~ => 11000000
  tmp_mask1 = ~(((uint64_t)(1 << ((curr_idx + 1) & SCHED_QS_MASK))) - 1);
  // clear bit for current index and lower
  tmp_mask2 = group_mask & tmp_mask1;

  if(tmp_mask2 == 0) {
    tmp_mask2 = group_mask;
  }

  // Returns one plus the index of the least significant 1-bit of x, or if x is zero, returns zero.
  tmp_idx  = (uint32_t) __builtin_ffsl(tmp_mask2);

  next_idx = likely(tmp_idx > 0) ? (tmp_idx - 1) : 0;

  return next_idx;
}




/**
 * Return the index of the next set bit in the mask
 * 
 * @param curr_idx  Index of the last used sched-queue inside a sched_q-obj
 * @param mask      Mask bits represent sched-qs in a specific sched_q-obj that should be polled
 */
static inline uint16_t
sched_q_get_next_qidx(const uint16_t curr_idx, const uint16_t mask)
{
  uint16_t next_idx, tmp_idx;
  uint16_t tmp_mask1, tmp_mask2;


  // Example: curr_idx=5 => bit(5+1) => 01000000 => -1 => 00111111 => ~ => 11000000
  tmp_mask1 = ~(((uint16_t)(1 << ((curr_idx + 1) & 0xF))) - 1);
  // clear bit for current index and lower
  tmp_mask2 = mask & tmp_mask1;

  if(tmp_mask2 == 0) {
    tmp_mask2 = mask;
  }

  // Returns one plus the index of the least significant 1-bit of x, or if x is zero, returns zero.
  tmp_idx  = (uint16_t) __builtin_ffsl(tmp_mask2);

  next_idx = likely(tmp_idx > 0) ? (tmp_idx - 1) : 0;

  return next_idx;
}





/**
 * Set bits in the masks used by the scheduler.
 * Triggered by a call to em_queue_enable(_all).
 */
em_status_t
sched_masks_add_queue(const em_queue_t       queue,
                      const em_queue_type_t  type,
                      const em_queue_group_t group)
{
  em_status_t        err;
  em_core_mask_t     core_mask;
  int                core_count;
  int                core;
  uint16_t           count, qidx_count;
  uint64_t           qidx;
  uint16_t           qidx_mask_bit;


  err = em_queue_group_mask(group, &core_mask);
  
  RETURN_ERROR_IF(err != EM_OK, err, EM_ESCOPE_SCHED_MASKS_ADD,
                  "em_queue_group_mask(group=%"PRI_QGRP", core_mask=%"PRIX64") returns %u",
                   group, core_mask.u64, err);


  RETURN_ERROR_IF(em_core_mask_iszero(&core_mask), EM_FATAL(EM_ERR_BAD_ID), EM_ESCOPE_SCHED_MASKS_ADD,
                  "em_queue_group_mask(group=%"PRI_QGRP", core_mask=%"PRIX64") is zero!",
                   group, core_mask.u64);

  core_count = em_core_count();


  env_spinlock_lock(&em.shm->sched_add_counts_lock.lock);
  
  // printf("%s(): Core:%02i QGrp:%"PRI_QGRP" CoreMask:%"PRIX64"\n", __func__, em_core_id(), group, core_mask.u64[0]); fflush(NULL);

  for(core = 0; core < core_count; core++)
  {
    if(em_core_mask_isset(core, &core_mask))
    {
      if(type == EM_QUEUE_TYPE_ATOMIC)
      {
        sched_q_atomic_t *const sched_q_obj = &em.shm->sched_qs_prio.sched_q_atomic[group];
          
        qidx = queue & (sched_q_obj->queue_mask);        
        
        qidx_count = em.shm->core_sched_add_counts[core].add_count.atomic_add_cnt[group].qidx_cnt[qidx];
        em.shm->core_sched_add_counts[core].add_count.atomic_add_cnt[group].qidx_cnt[qidx] = qidx_count + 1;
        
        if(qidx_count == 0)
        {
          qidx_mask_bit = (uint16_t) (1 << qidx);
          
          // Set the bit QIDX bit, i.e. enable scheduling for the FIFO that the EM-queue is scheduled through
          em.shm->core_sched_masks[core].sched_masks_prio.atomic_masks.qidx_mask[group] |= qidx_mask_bit;
        }
        
        // Increase the queue count for this sched object
        count = em.shm->core_sched_add_counts[core].add_count.atomic_add_cnt[group].q_grp_cnt;
        em.shm->core_sched_add_counts[core].add_count.atomic_add_cnt[group].q_grp_cnt = count + 1;        
        
        if(count == 0)
        {
          // Set the mask bit for this sched object, i.e. enable scheduling for this type,group
          group_mask_set(&em.shm->core_sched_masks[core].sched_masks_prio.atomic_masks.q_grp_mask, group);
        }
        
      }
      else if(type == EM_QUEUE_TYPE_PARALLEL)
      {
        sched_q_parallel_t *const sched_q_obj = &em.shm->sched_qs_prio.sched_q_parallel[group];
          
        qidx = queue & (sched_q_obj->queue_mask);        
        
        qidx_count = em.shm->core_sched_add_counts[core].add_count.parallel_add_cnt[group].qidx_cnt[qidx];
        em.shm->core_sched_add_counts[core].add_count.parallel_add_cnt[group].qidx_cnt[qidx] = qidx_count + 1;
        
        if(qidx_count == 0)
        {        
          qidx_mask_bit = (uint16_t) (1 << qidx);
          
          // Set the bit QIDX bit, i.e. enable scheduling for the FIFO that the EM-queue is scheduled through
          em.shm->core_sched_masks[core].sched_masks_prio.parallel_masks.qidx_mask[group] |= qidx_mask_bit;
        }
        
        // Increase the queue count for this sched object
        count = em.shm->core_sched_add_counts[core].add_count.parallel_add_cnt[group].q_grp_cnt;
        em.shm->core_sched_add_counts[core].add_count.parallel_add_cnt[group].q_grp_cnt = count + 1;

        if(count == 0) 
        {
          // Set the mask bit for this sched object, i.e. enable scheduling for this type,group
          group_mask_set(&em.shm->core_sched_masks[core].sched_masks_prio.parallel_masks.q_grp_mask, group);
        }

      }
      else if(type == EM_QUEUE_TYPE_PARALLEL_ORDERED)
      {
        sched_q_parallel_ord_t *const sched_q_obj = &em.shm->sched_qs_prio.sched_q_parallel_ord[group];

        qidx = queue & (sched_q_obj->queue_mask);
        
        qidx_count = em.shm->core_sched_add_counts[core].add_count.parallel_ord_add_cnt[group].qidx_cnt[qidx];
        em.shm->core_sched_add_counts[core].add_count.parallel_ord_add_cnt[group].qidx_cnt[qidx] = qidx_count + 1;
        
        if(qidx_count == 0)
        {                
          qidx_mask_bit = (uint16_t) (1 << qidx);

          // Set the bit QIDX bit, i.e. enable scheduling for the FIFO that the EM-queue is scheduled through
          em.shm->core_sched_masks[core].sched_masks_prio.parallel_ord_masks.qidx_mask[group] |= qidx_mask_bit;
        }

        // Increase the queue count for this sched object
        count = em.shm->core_sched_add_counts[core].add_count.parallel_ord_add_cnt[group].q_grp_cnt;
        em.shm->core_sched_add_counts[core].add_count.parallel_ord_add_cnt[group].q_grp_cnt = count + 1;

        if(count == 0)
        {
          // Set the mask bit for this sched object, i.e. enable scheduling for this type,group
          group_mask_set(&em.shm->core_sched_masks[core].sched_masks_prio.parallel_ord_masks.q_grp_mask, group);
        }

      }
      else
      {
        env_spinlock_unlock(&em.shm->sched_add_counts_lock.lock);
        
        return EM_INTERNAL_ERROR(EM_ERR_NOT_FOUND, EM_ESCOPE_SCHED_MASKS_ADD,
                                 "Unknown EM-queue type (%u)", type);
      }

    }
  } // for(;;)


  env_spinlock_unlock(&em.shm->sched_add_counts_lock.lock);

  return EM_OK;
}



/**
 * Clear bits in the masks used by the scheduler.
 * Triggered by a call to em_queue_disable(_all)
 */
em_status_t
sched_masks_rem_queue(const em_queue_t       queue,
                      const em_queue_type_t  type,
                      const em_queue_group_t group)
{
  em_status_t        err;
  em_core_mask_t     core_mask;
  int                core_count;
  int                core;
  uint16_t           count, qidx_count;
  uint64_t           qidx;
  uint16_t           qidx_mask_bit;


  err = em_queue_group_mask(group, &core_mask);

  RETURN_ERROR_IF(err != EM_OK, err, EM_ESCOPE_SCHED_MASKS_REM,
                  "em_queue_group_mask(group=%"PRI_QGRP", core_mask=%"PRIX64") returns %u",
                   group, core_mask.u64, err);


  core_count = em_core_count();
  
  
  if(em_core_mask_iszero(&core_mask))
  {
    // The queue group is newly created or has been modified and removed from all cores.
    // Set the core_mask to include all running cores and remove the queue from all. 
    em_core_mask_set_count(core_count, &core_mask);
  }


  env_spinlock_lock(&em.shm->sched_add_counts_lock.lock);
  

  for(core = 0; core < core_count; core++)
  {
    if(em_core_mask_isset(core, &core_mask))
    {
      if(type == EM_QUEUE_TYPE_ATOMIC)
      {
        sched_q_atomic_t *const sched_q_obj = &em.shm->sched_qs_prio.sched_q_atomic[group];
        
        qidx = queue & (sched_q_obj->queue_mask);

        count      = em.shm->core_sched_add_counts[core].add_count.atomic_add_cnt[group].q_grp_cnt;
        qidx_count = em.shm->core_sched_add_counts[core].add_count.atomic_add_cnt[group].qidx_cnt[qidx];

        // Decrease qidx count and if needed clear associated bit to remove from scheduling
        if(qidx_count > 1)
        {
          em.shm->core_sched_add_counts[core].add_count.atomic_add_cnt[group].qidx_cnt[qidx] = qidx_count - 1;
        }
        else if(qidx_count == 1)
        {
          em.shm->core_sched_add_counts[core].add_count.atomic_add_cnt[group].qidx_cnt[qidx] = 0;
          
          qidx_mask_bit = (uint16_t) (1 << qidx);
          em.shm->core_sched_masks[core].sched_masks_prio.atomic_masks.qidx_mask[group] &= (~qidx_mask_bit);
        }
        
        // Decrease sched obj count and if needed disable scheduling for this group by clearing the bit
        if(count > 1)
        {
          em.shm->core_sched_add_counts[core].add_count.atomic_add_cnt[group].q_grp_cnt = count - 1;
        }
        else if(count == 1)
        {
          em.shm->core_sched_add_counts[core].add_count.atomic_add_cnt[group].q_grp_cnt = 0;
          group_mask_clr(&em.shm->core_sched_masks[core].sched_masks_prio.atomic_masks.q_grp_mask, group);
        }
        
      }
      else if(type == EM_QUEUE_TYPE_PARALLEL)
      {
        sched_q_parallel_t *const sched_q_obj = &em.shm->sched_qs_prio.sched_q_parallel[group];
        
        qidx = queue & (sched_q_obj->queue_mask);

        count      = em.shm->core_sched_add_counts[core].add_count.parallel_add_cnt[group].q_grp_cnt;
        qidx_count = em.shm->core_sched_add_counts[core].add_count.parallel_add_cnt[group].qidx_cnt[qidx];

        // Decrease qidx count and if needed clear associated bit to remove from scheduling
        if(qidx_count > 1)
        {
          em.shm->core_sched_add_counts[core].add_count.parallel_add_cnt[group].qidx_cnt[qidx] = qidx_count - 1;
        }
        else if(qidx_count == 1)
        {
          em.shm->core_sched_add_counts[core].add_count.parallel_add_cnt[group].qidx_cnt[qidx] = 0;
          
          qidx_mask_bit = (uint16_t) (1 << qidx);
          em.shm->core_sched_masks[core].sched_masks_prio.parallel_masks.qidx_mask[group] &= (~qidx_mask_bit);
        }
        
        // Decrease sched obj count and if needed disable scheduling for this group by clearing the bit
        if(count > 1)
        {
          em.shm->core_sched_add_counts[core].add_count.parallel_add_cnt[group].q_grp_cnt = count - 1;
        }
        else if(count == 1)
        {
          em.shm->core_sched_add_counts[core].add_count.parallel_add_cnt[group].q_grp_cnt = 0;
          group_mask_clr(&em.shm->core_sched_masks[core].sched_masks_prio.parallel_masks.q_grp_mask, group);
        }
        
      }
      else if(type == EM_QUEUE_TYPE_PARALLEL_ORDERED)
      {
        sched_q_parallel_ord_t *const sched_q_obj = &em.shm->sched_qs_prio.sched_q_parallel_ord[group];
        
        qidx = queue & (sched_q_obj->queue_mask);

        count      = em.shm->core_sched_add_counts[core].add_count.parallel_ord_add_cnt[group].q_grp_cnt;
        qidx_count = em.shm->core_sched_add_counts[core].add_count.parallel_ord_add_cnt[group].qidx_cnt[qidx];

        // Decrease qidx count and if needed clear associated bit to remove from scheduling
        if(qidx_count > 1)
        {
          em.shm->core_sched_add_counts[core].add_count.parallel_ord_add_cnt[group].qidx_cnt[qidx] = qidx_count - 1;
        }
        else if(qidx_count == 1)
        {
          em.shm->core_sched_add_counts[core].add_count.parallel_ord_add_cnt[group].qidx_cnt[qidx] = 0;
          
          qidx_mask_bit = (uint16_t) (1 << qidx);
          em.shm->core_sched_masks[core].sched_masks_prio.parallel_ord_masks.qidx_mask[group] &= (~qidx_mask_bit);
        }
        
        // Decrease sched obj count and if needed disable scheduling for this group by clearing the bit
        if(count > 1)
        {
          em.shm->core_sched_add_counts[core].add_count.parallel_ord_add_cnt[group].q_grp_cnt = count - 1;
        }
        else if(count == 1)
        {
          em.shm->core_sched_add_counts[core].add_count.parallel_ord_add_cnt[group].q_grp_cnt = 0;
          group_mask_clr(&em.shm->core_sched_masks[core].sched_masks_prio.parallel_ord_masks.q_grp_mask, group);
        }
        
      }
      else{
        env_spinlock_unlock(&em.shm->sched_add_counts_lock.lock);
        
        return EM_INTERNAL_ERROR(EM_ERR_NOT_FOUND, EM_ESCOPE_SCHED_MASKS_REM,
                                 "Unknown EM-queue type (%u)", type);
      }
    }
  } // for(;;)

  
  env_spinlock_unlock(&em.shm->sched_add_counts_lock.lock);

  return EM_OK;
}




/**
 * Set bits in the scheduling masks associated with the queue group 'grp'.
 * Triggered locally on a core by em_queue_group_modify() or em_queue_group_create()
 */
void
sched_masks_add_queue_group__local(const em_queue_group_t group)
{
  m_list_head_t      *pos;
  m_list_head_t      *qgrp_node;
  em_queue_element_t *q_elem;
  uint16_t            count, qidx_count;
  uint64_t            qidx;
  uint16_t            qidx_mask_bit;


  env_spinlock_lock(&em.shm->em_queue_group_lock.lock);
  env_spinlock_lock(&em.shm->sched_add_counts_lock.lock);

  
  /*
   * Loop through the queues that belong to this queue group
   * and modify scheduling information locally on this core.
   */
  m_list_for_each(&em.shm->em_queue_group[group].list_head, pos, qgrp_node)
  {
    q_elem = m_list_qgrp_node_to_queue_elem(qgrp_node);
   
    if(q_elem->status == EM_QUEUE_STATUS_READY)
    {
      em_queue_type_t type  = q_elem->scheduler_type;
      em_queue_t      queue = q_elem->id;

      
      if(type == EM_QUEUE_TYPE_ATOMIC)
      {
        sched_q_atomic_t *const sched_q_obj = &em.shm->sched_qs_prio.sched_q_atomic[group];
          
        qidx = queue & (sched_q_obj->queue_mask);        
        
        qidx_mask_bit = (uint16_t) (1 << qidx);

        qidx_count = sched_core_local.sched_add_counts->add_count.atomic_add_cnt[group].qidx_cnt[qidx];
        sched_core_local.sched_add_counts->add_count.atomic_add_cnt[group].qidx_cnt[qidx] = qidx_count + 1;
          
        // Set the bit QIDX bit, i.e. enable scheduling for the FIFO that the EM-queue is scheduled through
        sched_core_local.sched_masks->sched_masks_prio.atomic_masks.qidx_mask[group] |= qidx_mask_bit;
        
        
        // Increase the queue count for this sched object
        count = sched_core_local.sched_add_counts->add_count.atomic_add_cnt[group].q_grp_cnt;
        sched_core_local.sched_add_counts->add_count.atomic_add_cnt[group].q_grp_cnt = count + 1;         
        
        // Set the mask bit for this sched object, i.e. enable scheduling for this type,group
        group_mask_set(&sched_core_local.sched_masks->sched_masks_prio.atomic_masks.q_grp_mask, group);
      }
      else if(type == EM_QUEUE_TYPE_PARALLEL)
      {
        sched_q_parallel_t *const sched_q_obj = &em.shm->sched_qs_prio.sched_q_parallel[group];
          
        qidx = queue & (sched_q_obj->queue_mask);        
        
        qidx_mask_bit = (uint16_t) (1 << qidx);

        qidx_count = sched_core_local.sched_add_counts->add_count.parallel_add_cnt[group].qidx_cnt[qidx];
        sched_core_local.sched_add_counts->add_count.parallel_add_cnt[group].qidx_cnt[qidx] = qidx_count + 1;
          
        // Set the bit QIDX bit, i.e. enable scheduling for the FIFO that the EM-queue is scheduled through
        sched_core_local.sched_masks->sched_masks_prio.parallel_masks.qidx_mask[group] |= qidx_mask_bit;


        // Increase the queue count for this sched object
        count = sched_core_local.sched_add_counts->add_count.parallel_add_cnt[group].q_grp_cnt;
        sched_core_local.sched_add_counts->add_count.parallel_add_cnt[group].q_grp_cnt = count + 1;         
        
        // Set the mask bit for this sched object, i.e. enable scheduling for this type,group
        group_mask_set(&sched_core_local.sched_masks->sched_masks_prio.parallel_masks.q_grp_mask, group);
      }
      else if(type == EM_QUEUE_TYPE_PARALLEL_ORDERED)
      {
        sched_q_parallel_ord_t *const sched_q_obj = &em.shm->sched_qs_prio.sched_q_parallel_ord[group];
          
        qidx = queue & (sched_q_obj->queue_mask);        
        
        qidx_mask_bit = (uint16_t) (1 << qidx);

        qidx_count = sched_core_local.sched_add_counts->add_count.parallel_ord_add_cnt[group].qidx_cnt[qidx];
        sched_core_local.sched_add_counts->add_count.parallel_ord_add_cnt[group].qidx_cnt[qidx] = qidx_count + 1;
          
        // Set the bit QIDX bit, i.e. enable scheduling for the FIFO that the EM-queue is scheduled through
        sched_core_local.sched_masks->sched_masks_prio.parallel_ord_masks.qidx_mask[group] |= qidx_mask_bit;


        // Increase the queue count for this sched object
        count = sched_core_local.sched_add_counts->add_count.parallel_ord_add_cnt[group].q_grp_cnt;
        sched_core_local.sched_add_counts->add_count.parallel_ord_add_cnt[group].q_grp_cnt = count + 1;         
        
        // Set the mask bit for this sched object, i.e. enable scheduling for this type,group
        group_mask_set(&sched_core_local.sched_masks->sched_masks_prio.parallel_ord_masks.q_grp_mask, group);
      }
      else
      {
        env_spinlock_unlock(&em.shm->sched_add_counts_lock.lock);
        env_spinlock_unlock(&em.shm->em_queue_group_lock.lock);  
        
        EM_INTERNAL_ERROR(EM_ERR_NOT_FOUND, EM_ESCOPE_SCHED_MASKS_ADD,
                          "Unknown EM-queue type (%u)", type);
        return;
      }
      
    }
  }
  
  env_spinlock_unlock(&em.shm->sched_add_counts_lock.lock);
  env_spinlock_unlock(&em.shm->em_queue_group_lock.lock);
}




/**
 * Clear all set bits in the scheduling masks associated with the queue group 'grp'
 * Triggered locally on a core by em_queue_group_modify() or em_queue_group_delete()
 */
void
sched_masks_rem_queue_group__local(const em_queue_group_t grp)
{
  int qidx;
  
  
  env_spinlock_lock(&em.shm->sched_add_counts_lock.lock);
  
  group_mask_clr(&sched_core_local.sched_masks->sched_masks_prio.atomic_masks.q_grp_mask,       grp);
  group_mask_clr(&sched_core_local.sched_masks->sched_masks_prio.parallel_masks.q_grp_mask,     grp);
  group_mask_clr(&sched_core_local.sched_masks->sched_masks_prio.parallel_ord_masks.q_grp_mask, grp);
  
  sched_core_local.sched_add_counts->add_count.atomic_add_cnt[grp].q_grp_cnt       = 0;
  sched_core_local.sched_add_counts->add_count.parallel_add_cnt[grp].q_grp_cnt     = 0;
  sched_core_local.sched_add_counts->add_count.parallel_ord_add_cnt[grp].q_grp_cnt = 0;
  
  
  sched_core_local.sched_masks->sched_masks_prio.atomic_masks.qidx_mask[grp]       = 0x0;
  sched_core_local.sched_masks->sched_masks_prio.parallel_masks.qidx_mask[grp]     = 0x0;
  sched_core_local.sched_masks->sched_masks_prio.parallel_ord_masks.qidx_mask[grp] = 0x0;
  
  for(qidx = 0; qidx < SCHED_Q_MAX_QUEUES; qidx++)
  {
    sched_core_local.sched_add_counts->add_count.atomic_add_cnt[grp].qidx_cnt[qidx]       = 0;
    sched_core_local.sched_add_counts->add_count.parallel_add_cnt[grp].qidx_cnt[qidx]     = 0;
    sched_core_local.sched_add_counts->add_count.parallel_ord_add_cnt[grp].qidx_cnt[qidx] = 0;
  }
  
  env_spinlock_unlock(&em.shm->sched_add_counts_lock.lock);
}




/**
 * Send event with group number.
 *
 * Any valid event and destination queue parameters can be used. Event group id
 * indicates which event group the event belongs to. Event group has to be first
 * created and applied.
 *
 * @param event    Event to send
 * @param queue    Destination queue
 * @param group    Event group
 *
 * @return EM_OK if successful.
 *
 * @see em_send(), em_event_group_create(), em_event_group_apply(), em_event_group_increment()
 */
em_status_t
em_send_group(em_event_t event, em_queue_t queue, em_event_group_t group)
{
  em_queue_element_t *const q_elem = get_queue_element(queue);
  PREFETCH_Q_ELEM(q_elem);
  em_event_hdr_t     *const ev_hdr = event_to_event_hdr(event);
  em_status_t               em_status;


  // env_sync_mem();

  ev_hdr->event_group = group;


  if(ev_hdr->src_q_type == EM_QUEUE_TYPE_PARALLEL_ORDERED)
  {
    em_status = em_send_from_parallel_ord_q(ev_hdr, q_elem, queue, OPERATION_SEND);
  }
  else
  {
    /* Send to queue based on the destination queue type. */
    em_status = em_send_switch(ev_hdr, q_elem, queue);
  }


  return em_status;
}



/**
 * Sends events ORIGINATING from a parallel-ordered queue. Alternatively handles free requests.
 *
 * Events are sent to the destination queue (of any type) in the order that they arrived in the
 * source parallel-ordered queue. If an event is sent/freed before a previous event has completed
 * then it is left to wait in he order-queue. Once the previous event(s) are done then ALL completed
 * events are sent to the destinations queues.
 *
 * @param ev_hdr    the event header
 * @param q_elem    destination queue element (or NULL   if operation==OPERATION_MARK_FREE)
 * @param queue     destination queue (or EM_QUEUE_UNDEF if operation==OPERATION_MARK_FREE)
 * @param operation OPERATION_SEND      - Normal send FROM parallel-ordered queue
                    OPERATION_MARK_FREE - em_free() called for event origination from parl-ord. queue
 *
 * @return status EM_OK on success (OK does not guarantee
 *                 message delivery, it only suggest the event was
 *                 posted successfully)
 */
em_status_t
em_send_from_parallel_ord_q(em_event_hdr_t     *const ev_hdr,
                            em_queue_element_t *const q_elem,
                            const em_queue_t          queue,
                            const int                 operation)
{
  env_spinlock_t     *const lock = ev_hdr->lock_p;
  em_queue_element_t *src_q_elem;  // Stored by previous em_send_xxx()

  union {
    em_event_hdr_t  *ev_hdr;
    void            *ev_hdr_void;
  } de_q;


  /* Take the lock early - another core might also be checking the order-queue */
  env_spinlock_lock(lock);


  // Clear src_q_type - set again in em_schedule_parallell_ordered() if 'queue' is of that type.
  ev_hdr->src_q_type      = EM_QUEUE_TYPE_UNDEF;
  // Mark event as 'processed' and ready for sending.
  ev_hdr->processing_done = 1;
  // Save the intended operation for the event: send, free or packet-output
  ev_hdr->operation       = operation;
  // Store the destination q_elem (can be NULL for packet-output)
  ev_hdr->dst_q_elem      = q_elem;


  // Store ptr to the source parallel-ordered queue
  src_q_elem = ev_hdr->q_elem;


  if(ev_hdr != src_q_elem->u.parallel_ord.order_first)
  {
    /*
     * NOT the first ev_hdr in the order-queue, return to wait for the first to complete.
     */
    env_spinlock_unlock(lock);

    return EM_OK;
  }
  else
  {
    em_status_t     em_status  = EM_OK;
    em_status_t     ret_status = EM_OK;
    // helper vars:
    em_event_hdr_t *tmp_hdr;
    int             processing_done;
    int             operation;
    int             ret;


    /* FIRST in order - Clear ptr to first ev_hdr */
    src_q_elem->u.parallel_ord.order_first = NULL;


    /*
     * Send HEAD EVENT - see also if other later events have completed processing,
     * and if so, dequeue from order-queue and send all to their destination queues.
     */

    de_q.ev_hdr = ev_hdr; // initialize loop

    do {
      tmp_hdr         = de_q.ev_hdr;
      processing_done = tmp_hdr->processing_done;

      ret = 1; // 1 breaks out of loop if not changed
      
      if(processing_done)
      {
        operation = tmp_hdr->operation;

        switch(operation)
        {
          case OPERATION_ETH_TX:
          {
            struct rte_mbuf *const m = (struct rte_mbuf *) (((size_t) tmp_hdr) - sizeof(struct rte_mbuf));
          
            eth_tx_packet__ordered(m, tmp_hdr->io_port, EM_QUEUE_TO_MBUF_TBL(src_q_elem->id));
          }
          break;
          
          case OPERATION_SEND:
          {
            /* Send to queue based on the destination queue type.
             * On error: Break & return to caller, next event will retry to send remaining events ready in the order-queue.
             */
            em_status = em_send_switch(tmp_hdr, tmp_hdr->dst_q_elem, tmp_hdr->dst_q_elem->id);
            
            IF_UNLIKELY(em_status != EM_OK)
            {
              if(tmp_hdr == ev_hdr) // Input event to em_send() and first in order
              {
                /* Save error status from input event and eventually return this value,
                 * but for now continue processing loop dropping subsequent events on further errors.
                 * Note that the packet order for the input event is lost.
                 */
                ret_status = em_status;
              }
              else {
                intel_free(tmp_hdr);
              }
            }
          }
          break;
          
          default:
          {
            // OPERATION_MARK_FREE
            intel_free(tmp_hdr);
          }
          break;
          
        } // switch(operation)

        // Dequeue next ev_hdr in order-list (if any)
        ret = rte_ring_dequeue(src_q_elem->rte_ring, &de_q.ev_hdr_void);
      }
    } while(!ret);


    if(!processing_done)
    {
      // Set new head ev_hdr instead of NULL
      src_q_elem->u.parallel_ord.order_first = tmp_hdr;
    }


    env_spinlock_unlock(lock);
    
    return ret_status;
  }
}




/**
 * Send / Enqueue the given event (header) based on the EM-queue type
 */
em_status_t
em_send_switch(em_event_hdr_t     *const ev_hdr,
               em_queue_element_t *const q_elem,
               const em_queue_t          queue)
{
  em_status_t em_status = EM_ERR_LIB_FAILED;


  switch(q_elem->scheduler_type)
  {
    case EM_QUEUE_TYPE_ATOMIC:
      {
        PREFETCH_RTE_RING(q_elem->rte_ring);

        em_status = em_send_atomic(ev_hdr, q_elem, queue);
      }
      break;


    case EM_QUEUE_TYPE_PARALLEL:
      {
        em_status = em_send_parallel(ev_hdr, q_elem, queue);
      }
      break;


    case EM_QUEUE_TYPE_PARALLEL_ORDERED:
      {
        em_status = em_send_parallel_ordered(ev_hdr, q_elem, queue);
      }
      break;
  }
  
  IF_LIKELY(em_status == EM_OK)
  {
    const uint64_t q_elem_mask = ((uint64_t)1) << q_elem->queue_group;
    
    if(q_elem_mask & (em_core_local.current_group_mask)) {
      sched_core_local.events_enqueued++;
    }
  }

  return em_status;
}




/**
 * Send the event (header) to an atomic EM-queue
 */
static inline em_status_t
em_send_atomic(em_event_hdr_t     *const ev_hdr,
               em_queue_element_t *const q_elem,
               const em_queue_t          queue)
{
  struct rte_ring    *const event_q     = q_elem->rte_ring;
  const em_event_t          event       = event_hdr_to_event(ev_hdr);  
  em_queue_element_t *const src_q_elem  = ev_hdr->q_elem;  // Stored by previous em_send_xxx(), NOTE: can be NULL (packet-IO)!!!

  sched_q_atomic_t   *const sched_q_obj = SCHED_Q_ATOMIC_SELECT(q_elem);
  const uint64_t            qidx        = queue & (sched_q_obj->queue_mask);
  struct multiring   *const sched_q     = sched_q_obj->sched_q[qidx];

  int                       ret;


  ev_hdr->q_elem = q_elem;

  // MULTI PRODUCER
  ret = rte_ring_enqueue(event_q, event);

  IF_UNLIKELY(ret != 0)
  {
    return EM_ERR_LIB_FAILED;
  }
  // RETURN_ERROR_IF(ret != 0, EM_ERR_LIB_FAILED, EM_ESCOPE_SEND_ATOMIC,
  //                 "Atomic: event queue enqueue failed, ret=%i", ret);


#if LOCKLESS_ATOMIC_QUEUES == 1

  // If the atomic count was previously zero, we must see if we need to schedule this atomic queue
  if((__sync_fetch_and_add(&q_elem->u.atomic.event_count, 1) == 0) && (q_elem != src_q_elem))
  {
    // Set the sched_count to 1, retrieving old value. If it was 0, we must enqueue to schedule
    if(__sync_lock_test_and_set(&q_elem->u.atomic.sched_count,1) == 0)
    {
      ret = mring_enqueue(sched_q, q_elem->priority, q_elem);
      
      RETURN_ERROR_IF(ret != 1, EM_FATAL(EM_ERR_LIB_FAILED), EM_ESCOPE_SEND_ATOMIC,
                      "Atomic: sched queue enqueue failed, ret=%i", ret);      
    }
  }
              
#else // LOCKLESS_ATOMIC_QUEUES == 0

  ret = 1; // Set for later error check
  
  env_spinlock_lock(&q_elem->lock);

  q_elem->u.atomic.event_count++;

  if(q_elem->u.atomic.sched_count == 0)
  {
    // Event queue not currently in sched queue, enqueue it...

    // ... except if dst-queue is the atomic src-queue,
    // i.e. can't release atomic context yet, reschedule queue when returing from dispatch instead
    IF_LIKELY(q_elem != src_q_elem)
    {
      // MULTI PRODUCER
      ret = mring_enqueue(sched_q, q_elem->priority, q_elem);

      IF_LIKELY(ret == 1) {
        q_elem->u.atomic.sched_count = 1;
      }
    }
  }

  env_spinlock_unlock(&q_elem->lock);
  
  RETURN_ERROR_IF(ret != 1, EM_FATAL(EM_ERR_LIB_FAILED), EM_ESCOPE_SEND_ATOMIC,
                  "Atomic: sched queue enqueue failed, ret=%i", ret);
                  
#endif // #if LOCKLESS_ATOMIC_QUEUES == 1

  
  return EM_OK;
}




/**
 * Send the event (header) to a parallel EM-queue
 */
static inline em_status_t
em_send_parallel(em_event_hdr_t     *const ev_hdr,
                 em_queue_element_t *const q_elem,
                 const em_queue_t          queue)
{
  sched_q_parallel_t *sched_q_obj;
  struct multiring   *sched_q;
  uint64_t            qidx;
  int                 ret;


  ev_hdr->q_elem = q_elem;

  sched_q_obj = SCHED_Q_PARALLEL_SELECT(q_elem);
  qidx        = queue & (sched_q_obj->queue_mask);
  sched_q     = sched_q_obj->sched_q[qidx]; // Spread into scheduling queues

  ret = mring_enqueue(sched_q, q_elem->priority, ev_hdr);
  
  IF_UNLIKELY(ret != 1)
  {
    return EM_ERR_LIB_FAILED;
  }
  // RETURN_ERROR_IF(ret != 1, EM_ERR_LIB_FAILED, EM_ESCOPE_SEND_PARALLEL,
  //                 "Parallel: sched queue enqueue failed, ret=%i", ret); 
  
  return EM_OK;
}




/**
 * Send the event (header) to a parallel-ordered EM-queue
 */
static inline em_status_t
em_send_parallel_ordered(em_event_hdr_t     *const ev_hdr,
                         em_queue_element_t *const q_elem,
                         const em_queue_t          queue)
{
  sched_q_parallel_ord_t *sched_q_obj;
  struct multiring       *sched_q;
  uint64_t                qidx;
  int                     ret;


  ev_hdr->q_elem = q_elem;

  sched_q_obj = SCHED_Q_PARALLEL_ORD_SELECT(q_elem);
  qidx        = queue & (sched_q_obj->queue_mask);
  sched_q     = sched_q_obj->sched_q[qidx]; // Spread into scheduling queues

  ret = mring_enqueue(sched_q, q_elem->priority, ev_hdr);
      
  IF_UNLIKELY(ret != 1)
  {
    return EM_ERR_LIB_FAILED;
  }
  // RETURN_ERROR_IF(ret != 1, EM_ERR_LIB_FAILED, EM_ESCOPE_SEND_PARALLEL_ORD,
  //                 "Parallel-Ordered: sched queue enqueue failed, ret=%i", ret); 
  
  return EM_OK;
}



/**
 * Release atomic processing context.
 *
 * When an event was received from an atomic queue, the function can be used to 
 * release the atomic context before receive function return. After the call,
 * scheduler is allowed to schedule another event from the same queue
 * to another core. This increases parallelism and may improve performance - 
 * however the exclusive processing and ordering (!) might be lost after the call.
 *
 * Can only be called from within the event receive function!
 *
 * The call is ignored, if current event was not received from an atomic queue.
 *
 * Pseudo-code example:
 * @code
 *  receive_func(void* eo_ctx, em_event_t event, em_event_type_t type, em_queue_t queue, void* q_ctx);
 *  {
 *      if(is_my_atomic_queue(q_ctx))
 *      {
 *          update_sequence_number(event);  // this needs to be done atomically
 *          em_atomic_processing_end();
 *          ...                             // do other processing (potentially) in parallel
 *      }
 *   }
 * @endcode
 *
 * @see em_receive_func_t()
 */
void
em_atomic_processing_end(void)
{
  return;
 
  // @TODO: Implementation below is not working, disable it and re-implement 
#if 0
  em_queue_element_t *const q_elem = em_core_local.current_q_elem;
  sched_q_atomic_t   *sched_q_obj;
  struct rte_ring    *sched_q;
  uint64_t            qidx;
  int                 ret = 0;


  IF_LIKELY((q_elem != NULL) && (q_elem->scheduler_type == EM_QUEUE_TYPE_ATOMIC))
  {
    sched_q_obj = SCHED_Q_ATOMIC_SELECT(q_elem);
    qidx        = (q_elem->id) & (sched_q_obj->queue_mask);
    sched_q     = sched_q_obj->sched_q[qidx];

    env_spinlock_lock(&q_elem->lock);

    IF_LIKELY(q_elem->u.atomic.sched_count == 0)
    {
      // Event queue not currently in sched queue, enqueue it... (also means no events for this queue)

      // MULTI PRODUCER
      ret = rte_ring_enqueue(sched_q, q_elem);

      IF_LIKELY(ret == 0) {
        q_elem->u.atomic.sched_count = 1;
      }
    }

    env_spinlock_unlock(&q_elem->lock);
    
    
    IF_UNLIKELY(ret != 0)
    {
      (void) EM_INTERNAL_ERROR(EM_ERR_LIB_FAILED, EM_ESCOPE_ATOMIC_PROCESSING_END,
                               "Atomic: sched queue enqueue failed, ret=%i", ret);
    }
  } 
#endif
}




/**
 *  This is the EM dispatcher.
 */
static inline void
dispatch_event(em_queue_element_t *const q_elem,
               em_event_t                event,
               const em_event_type_t     event_type)
{
  em_queue_t           queue;
  em_event_hdr_t      *event_hdr;
  em_event_group_t     event_group;
  em_receive_func_t    receive_func;
  void                *eo_ctx;
  void                *q_ctx;


  // Check if queue status ready, drop event if not
  IF_UNLIKELY(q_elem->status != EM_QUEUE_STATUS_READY)
  {
    // Consider removing the error report if dropping is accepted
    (void) EM_INTERNAL_ERROR(EM_ERR_BAD_STATE, EM_ESCOPE_DISPATCH, 
                             "Queue %"PRI_QUEUE" not ready (state=%u -> drop event\n",
                             q_elem->id, q_elem->status);
    em_free(event);
    return;
  }


  if(event_type == EM_EVENT_TYPE_SW)
  {
    event_hdr   = event_to_event_hdr(event);
    event_group = event_hdr->event_group;
  }
  else
  {
    event_group = EM_EVENT_GROUP_UNDEF;
  }



  // Update core-local globals
  // Note: current EO elem = q_elem->eo_elem
  em_core_local.current_q_elem      = q_elem;
  em_core_local.current_event_group = event_group;


  queue        = q_elem->id;
  receive_func = q_elem->receive_func;
  eo_ctx       = q_elem->eo_ctx;
  q_ctx        = q_elem->context;



#if 0
  IF_UNLIKELY((receive_func == NULL) || (event == NULL))
  {
    if(event_type == EM_EVENT_TYPE_SW) {
      em_free(event);
    }
    else {// EM_EVENT_TYPE_PACKET
      em_packet_drop((struct qm_fd *) event);
    }

    return;
  }
#endif



  //
  // Call execution object receive function. Atomic context may have been lost after this...
  //
  receive_func(eo_ctx, event, event_type, queue, q_ctx);

  em_core_local.current_q_elem = NULL;



  //
  // Event belongs to an event_group, update the count and if requested send notifications
  //
  IF_UNLIKELY(event_group != EM_EVENT_GROUP_UNDEF)
  {
    // Atomically decrease event group count. If new count is zero, send notification events.
    event_group_count_update(event_group);
  }


  // env_sync_mem();
}




#if RX_DIRECT_DISPATCH == 1
void
em_direct_dispatch(em_event_t                event,
                   em_event_hdr_t     *const ev_hdr,
                   em_queue_element_t *const q_elem,
                   const em_queue_t          queue)
{
  const uint64_t q_elem_mask = ((uint64_t)1) << q_elem->queue_group;


  // Only dispatch if the queue belongs to a queue group enabled on this core, otherwise enqueue for another core to handle
  if(q_elem_mask & (em_core_local.current_group_mask))
  {
    switch(q_elem->scheduler_type)
    {
      case EM_QUEUE_TYPE_ATOMIC:
        // ev_hdr->q_elem = NULL;
        em_direct_dispatch__atomic(q_elem, event, ev_hdr);
        break;

      case EM_QUEUE_TYPE_PARALLEL:
        ev_hdr->q_elem = q_elem;
        dispatch_event(q_elem, event, EM_EVENT_TYPE_PACKET);
        break;

      case EM_QUEUE_TYPE_PARALLEL_ORDERED:
        ev_hdr->q_elem = q_elem;
        em_direct_dispatch__parallel_ordered(q_elem, event, ev_hdr);
        break;
    }
  }
  else
  {
    // Enqueue to an EM queue - NORMAL Operation
    em_status_t err = em_send_switch(ev_hdr, q_elem, queue);
    
    IF_UNLIKELY(err != EM_OK)
    {
      // Drop RX event
      em_free(event);
    }
  }

}
#endif




#if RX_DIRECT_DISPATCH == 1
static inline void
em_direct_dispatch__atomic(em_queue_element_t *const q_elem,
                           em_event_t                event,
                           em_event_hdr_t     *const ev_hdr)
{
  em_status_t       status;
  sched_q_atomic_t *sched_q_obj;
  uint64_t          qidx;
  struct multiring *sched_q;
  int               ret;  


#if LOCKLESS_ATOMIC_QUEUES == 1

  if((q_elem->u.atomic.event_count > 0) ||
     (__sync_lock_test_and_set(&q_elem->u.atomic.sched_count, 1) > 0))
  {
    // Enqueue to an EM queue - NORMAL Operation as backup.
    status = em_send_atomic(ev_hdr, q_elem, q_elem->id);

    IF_UNLIKELY(status != EM_OK) {
      intel_free(ev_hdr);
    }
    return;
  }

  // For case where queue is empty, let's use a shortcut, we have used the sched
  // bit to indicate we are using the queue
  ev_hdr->q_elem     = q_elem;
  ev_hdr->src_q_type = EM_QUEUE_TYPE_ATOMIC;

  dispatch_event(q_elem, event, EM_EVENT_TYPE_PACKET);

  ret = 1;
  
  if(q_elem->u.atomic.event_count > 0)
  {
    /* Schedule the queue for real */
    sched_q_obj = SCHED_Q_ATOMIC_SELECT(q_elem);
    qidx        = q_elem->id & (sched_q_obj->queue_mask);
    sched_q     = sched_q_obj->sched_q[qidx];

    ret = mring_enqueue(sched_q, q_elem->priority, q_elem);
  }
  else
  { 
    /* Nothing to schedule */
    union {
      struct {
        int32_t sched_count, event_count;
      };
      uint64_t atomic_counts;
    } expected_value = {{.sched_count = 1, .event_count = 0}};

    /* Try setting both event count and sched count to zero. If cmpset fails
     * someone has modified event_count (upwards) so just reschedule
     */
    if(!rte_atomic64_cmpset(&q_elem->u.atomic.atomic_counts_u64, expected_value.atomic_counts, 0))
    {
      sched_q_obj = SCHED_Q_ATOMIC_SELECT(q_elem);
      qidx        = q_elem->id & (sched_q_obj->queue_mask);
      sched_q     = sched_q_obj->sched_q[qidx];

      ret = mring_enqueue(sched_q, q_elem->priority, q_elem);
    }
  }
  
  IF_UNLIKELY(ret != 1) {
    // Should never happen
    (void) EM_INTERNAL_ERROR(EM_FATAL(EM_ERR_LIB_FAILED), EM_ESCOPE_DIRECT_DISPATCH__ATOMIC,
                             "Atomic sched queue enqueue failed, ret=%i!", ret);
  }
  
#else // LOCKLESS_ATOMIC_QUEUES == 0

  /* sched_count == 0 means that this queue is not scheduled and has no events */
  if(q_elem->u.atomic.sched_count == 0)
  {
    if(!env_spinlock_is_locked(&q_elem->lock))
    {
      if(env_spinlock_trylock(&q_elem->lock))
      {
        IF_LIKELY(q_elem->u.atomic.sched_count == 0)
        {
          // q_elem is NOT present in any sheduling queue
          // Fool em_send_atomic() to think that the queue is already scheduled.
          q_elem->u.atomic.sched_count = 1;
          
          env_spinlock_unlock(&q_elem->lock);
          
          
          // Mark that this event was received from an atomic queue
          ev_hdr->q_elem     = q_elem;
          ev_hdr->src_q_type = EM_QUEUE_TYPE_ATOMIC;
          
          //
          // Call dispatch with lock unlocked
          // 
          dispatch_event(q_elem, event, EM_EVENT_TYPE_PACKET);
          
          
          ENV_PREFETCH(&q_elem->lock);
          sched_q_obj = SCHED_Q_ATOMIC_SELECT(q_elem);
          qidx        = q_elem->id & (sched_q_obj->queue_mask);
          sched_q     = sched_q_obj->sched_q[qidx];
          
          
          env_spinlock_lock(&q_elem->lock);
          
          if(q_elem->u.atomic.event_count > 0)
          {            
            // MULTI PRODUCER
            ret = mring_enqueue(sched_q, q_elem->priority, q_elem);      
            
            IF_UNLIKELY(ret != 1) {
              q_elem->u.atomic.sched_count = 0;
            }
          }
          else {
            q_elem->u.atomic.sched_count = 0;
          }
          
          env_spinlock_unlock(&q_elem->lock);
          
          return;
        }
        
        env_spinlock_unlock(&q_elem->lock);
      }
    }
  }


  // Enqueue to an EM queue - NORMAL Operation as backup.
  status = em_send_atomic(ev_hdr, q_elem, q_elem->id);

  IF_UNLIKELY(status != EM_OK) {
    intel_free(ev_hdr);
  }
  
#endif // #if LOCKLESS_ATOMIC_QUEUES == 1
}
#endif




#if RX_DIRECT_DISPATCH == 1
static inline void
em_direct_dispatch__parallel_ordered(em_queue_element_t *const q_elem,
                                     em_event_t                event,
                                     em_event_hdr_t     *const ev_hdr)
{
  const em_queue_t queue = q_elem->id;
  em_status_t      status;    

#if PARALLEL_ORDERED__USE_SCHED_Q_LOCKS == 1
  // Use sched-Q locks to maintain order - worse in I/O but better in internal Queue scheduling...
  sched_q_parallel_ord_t *const sched_q_obj = SCHED_Q_PARALLEL_ORD_SELECT(q_elem); 
  uint64_t                const qidx        = q_elem->id & (sched_q_obj->queue_mask);
  env_spinlock_t         *const lock        = &sched_q_obj->locks[qidx].lock;  
#else
  // Default. Use Q-locks to maintain order - more of these, less contetion, better I/O perf but could be worse in internal scheduling...
  env_spinlock_t *const lock  = &q_elem->lock;
#endif


  env_spinlock_lock(lock);

  status = parallel_ordered_maintain_order(ev_hdr, q_elem, lock);

  env_spinlock_unlock(lock);


  IF_LIKELY(status == EM_OK)
  {
    dispatch_event(q_elem, event, EM_EVENT_TYPE_PACKET);
  }
  else
  {
    // Enqueue to an EM queue - Fallback operation in case of error
    status = em_send_switch(ev_hdr, q_elem, queue);
    
    IF_UNLIKELY(status != EM_OK)
    {
      // Drop RX event
      em_free(event);
    }
  }
  
}
#endif




