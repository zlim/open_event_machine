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
 *  Replaced arrays used for priorities as part of multiqueue integration
 */


/**
 * @file
 *
 * EM Intel Scheduler header file
 *
 */
#ifndef EM_INTEL_SCHED_H
#define EM_INTEL_SCHED_H


#include "event_machine.h"
#include "event_machine_group.h"
#include "event_machine_helper.h"

#include "environment.h"

#include "em_intel.h"


#ifdef __cplusplus
extern "C" {
#endif



/**
 * EM Intel scheduling related defines, data types and functions
 */

/**
 * Select Intel optimized lockless atomic queues (=1) or a more generic version using spinlocks (=0)
 */
#define LOCKLESS_ATOMIC_QUEUES  (1)  // 1=On, 0=Off(can heavily impact performance)


// Scheduling priority levels
#define  SCHED_PRIO_LEVELS           (EM_QUEUE_PRIO_NUM)
COMPILE_TIME_ASSERT(EM_QUEUE_PRIO_LOWEST == 0, EM_QUEUE_PRIO_LOWEST__NOT_ZERO_ERROR);

// A scheduling queue per queue group (per sched type (atomic, parallel, parallel-ordered)
#define  SCHED_QS                    (EM_MAX_QUEUE_GROUPS)
// Maximum number of _actual_ queues in a single scheduling queue object
#define  SCHED_Q_MAX_QUEUES          (16) // Note: same amount as bits in uint16_t - don't change!

#define SCHED_Q_ATOMIC_CNT_MAX       (4)
#define SCHED_Q_PARALLEL_CNT_MAX     (4)
#define SCHED_Q_PARALLEL_ORD_CNT_MAX (4)




/**
 * Scheduling Queues Info on a core
 */
typedef struct
{
   // Scheduling queue object index for atomic queues
   uint32_t            sched_q_atomic_idx;
   // The number of times a certain FIFO has been used 
   uint16_t            sched_q_atomic_cnt;   
   // Actual FIFO index inside the above indexed Sched queue object
   uint8_t             atomic_qidx[SCHED_QS];
   
   
   // Scheduling queue object index for parallel queues
   uint32_t            sched_q_parallel_idx;
   // The number of times a certain FIFO has been used 
   uint16_t            sched_q_parallel_cnt;
   // Actual FIFO index inside the above indexed Sched queue object
   uint8_t             parallel_qidx[SCHED_QS];   
   
   
   // Scheduling queue object index for parallel-ordered queues
   uint32_t            sched_q_parallel_ord_idx;
   // The number of times a certain FIFO has been used 
   uint16_t            sched_q_parallel_ord_cnt;
   // Actual FIFO index inside the above indexed Sched queue object
   uint8_t             parallel_ord_qidx[SCHED_QS];
   
} sched_qs_info_local_t;



/**
 * Core local scheduling masks, one set per priority level
 */

typedef struct
{
  volatile  uint64_t  q_grp_mask;
  
  volatile  uint16_t  qidx_mask[SCHED_QS];
  
} sched_type_mask_t;


typedef struct 
{
  sched_type_mask_t  atomic_masks;
  
  sched_type_mask_t  parallel_masks;
  
  sched_type_mask_t  parallel_ord_masks;
  
} sched_masks_t;


typedef union 
{  
  sched_masks_t sched_masks_prio;
  
  uint8_t u8[32 * ENV_CACHE_LINE_SIZE];
  
} core_sched_masks_t  ENV_CACHE_LINE_ALIGNED;

COMPILE_TIME_ASSERT((sizeof(core_sched_masks_t) % ENV_CACHE_LINE_SIZE) == 0,  CORE_QUEUE_GROUP_MASKS_T__SIZE_ERROR);



/**
 * The number of EM queues associated with a bit set in the core_sched_masks_t.
 * The number is needed to know when a bit in the mask can be cleared upon EM-Q deletion
 */

typedef struct
{
  volatile  uint16_t  q_grp_cnt;
  
  volatile  uint16_t  qidx_cnt[SCHED_Q_MAX_QUEUES];
  
} sched_add_counts_t;



typedef struct
{
  struct
  {
    sched_add_counts_t  atomic_add_cnt[SCHED_QS];
    
    sched_add_counts_t  parallel_add_cnt[SCHED_QS];
    
    sched_add_counts_t  parallel_ord_add_cnt[SCHED_QS];
    
  } add_count;
  
} core_sched_add_counts_t  ENV_CACHE_LINE_ALIGNED;



/**
 * Lock serializing access to the queue group counts 
 */
typedef union
{
  env_spinlock_t  lock;
  
  uint8_t u8[ENV_CACHE_LINE_SIZE];
  
} sched_add_counts_lock_t;




/*
 * Externs
 */
extern ENV_SHARED  core_sched_masks_t       core_sched_masks[MAX_CORES];
extern ENV_SHARED  core_sched_add_counts_t  core_sched_add_counts[MAX_CORES];
extern ENV_SHARED  sched_add_counts_lock_t  sched_add_counts_lock;




/*
 * Functions
 */
void em_schedule(void);


#define OPERATION_SEND      (0) // Normal send _FROM_ a parallel-ordered queue
#define OPERATION_MARK_FREE (1) // Called from em_free() for an event originating from a parallel-ordered queue
#define OPERATION_ETH_TX    (2) // Called from packet-IO output for an event originating from a parallel-ordered queue
em_status_t
em_send_from_parallel_ord_q(em_event_hdr_t *const ev_hdr, em_queue_element_t *const q_elem, const em_queue_t queue, const int operation);


void
em_direct_dispatch(em_event_t                event,
                   em_event_hdr_t     *const ev_hdr,
                   em_queue_element_t *const q_elem,
                   const em_queue_t          queue);


em_status_t                                                                                            
em_send_switch(em_event_hdr_t *const ev_hdr, em_queue_element_t *const q_elem, const em_queue_t queue);


em_status_t
sched_masks_add_queue(const em_queue_t queue,
                const em_queue_type_t  type,
                const em_queue_group_t group);
                
em_status_t
sched_masks_rem_queue(const em_queue_t queue,
                const em_queue_type_t  type,
                const em_queue_group_t group);
                
void
sched_masks_rem_queue_group__local(const em_queue_group_t grp);

void
sched_masks_add_queue_group__local(const em_queue_group_t grp);

em_status_t
sched_init_global(void);

void
sched_init_local(void);

 
#ifdef __cplusplus
}
#endif


#endif


