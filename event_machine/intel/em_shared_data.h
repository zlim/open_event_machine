/*
 *   Copyright (c) 2013, Nokia Siemens Networks
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
 
#ifndef EM_SHARED_DATA_H_
#define EM_SHARED_DATA_H_

#include "event_machine.h"
#include "event_machine_group.h"
#include "event_machine_helper.h"

#include "environment.h"

// Generic double linked list
#include "misc_list.h"


#include "em_intel.h"
#include "em_error.h"
#include "em_intel_event_group.h"
#include "em_intel_queue_group.h"
#include "em_intel_sched.h"

#ifdef EVENT_PACKET
  #include "em_intel_packet.h"
#endif

#ifdef EVENT_TIMER
  #include "event_timer.h"  
#endif  


#ifdef __cplusplus
extern "C" {
#endif





/**
 * EM shared data
 *
 * Struct contains data that is shared between all EM-cores,
 * i.e. shared between all EM-processes or EM-threads depending on the setup.
 * 
 */
typedef struct
{
  /*
   * em_intel.c|h
   */
  
  /** EO table */
  em_eo_element_t         em_eo_element_tbl[EM_MAX_EOS]  ENV_CACHE_LINE_ALIGNED;
  
  /** EM Pool table */
  em_pool_t               em_eo_pool[EO_POOLS]  ENV_CACHE_LINE_ALIGNED;
  
  /** Queue element table */
  em_queue_element_t      em_queue_element_tbl[EM_MAX_QUEUES]  ENV_CACHE_LINE_ALIGNED;  // Static queues first followed by dynamic queues
  
  /** EM dynamic queue pool */
  em_pool_t               em_dyn_queue_pool[DYN_QUEUE_POOLS]   ENV_CACHE_LINE_ALIGNED;  // Dynamic queue ID FIFOs
  
  /** Spinlocks for the static numbered queues */
  em_spinlock_t           em_static_queue_lock[STATIC_QUEUE_LOCKS]  ENV_CACHE_LINE_ALIGNED;  // Static queue ID locks
  
  /** Queue name table */
  char                    em_queue_name_tbl[EM_MAX_QUEUES][EM_QUEUE_NAME_LEN]  ENV_CACHE_LINE_ALIGNED;
  
  /** Lock used by queue_init() to serialize rte_ring_create() calls */
  em_spinlock_t           queue_create_lock  ENV_CACHE_LINE_ALIGNED;
  
  /** EM core map: physical core id <-> logical EM core id */
  em_core_map_t           em_core_map  ENV_CACHE_LINE_ALIGNED;
  
  /** Queues/rings of rte_rings for atomic and parallel-ordered EM queues (q_elem->rte_ring) */
  queue_init_rings_t      queue_init_rings  ENV_CACHE_LINE_ALIGNED;
  
  
  /*
   * em_error.c|h
   */
  
  em_error_handler_aligned_t  em_error_handler_aligned  ENV_CACHE_LINE_ALIGNED;
  

  /*
   * em_intel_event_group.c|h
   */

  /** Event group entry table (em_event_group_t used as index into table) */
  em_event_group_entry_t           em_event_group_entry_tbl[EM_MAX_EVENT_GROUPS]  ENV_CACHE_LINE_ALIGNED;
  /** Event group entry table access lock */
  em_event_group_entry_tbl_lock_t  em_event_group_entry_tbl_lock                  ENV_CACHE_LINE_ALIGNED;


  /*
   * em_intel_queue_group.c|h
   */
  
  /** Queue group table */
  em_queue_group_element_t  em_queue_group[EM_MAX_QUEUE_GROUPS]  ENV_CACHE_LINE_ALIGNED;
  /** Queue group table access lock */
  em_spinlock_t             em_queue_group_lock                  ENV_CACHE_LINE_ALIGNED;
  
  
  /*
   * em_intel_sched.c|h
   */
   
  /** All scheduling queues */
  sched_qs_t  sched_qs_prio  ENV_CACHE_LINE_ALIGNED;
  
  /** The cores' queue group masks used in scheduling, cores accesses array based on EM core ID (reads). Updates can be done by any core */
  core_sched_masks_t       core_sched_masks[EM_MAX_CORES]      ENV_CACHE_LINE_ALIGNED;
  core_sched_add_counts_t  core_sched_add_counts[EM_MAX_CORES] ENV_CACHE_LINE_ALIGNED;
  sched_add_counts_lock_t  sched_add_counts_lock               ENV_CACHE_LINE_ALIGNED;
  
  
  /*
   * em_intel_packet.c|h
   */

  /**
   * Array of available Rx port:queue pairs.
   * 
   * @note The FIFO 'eth_rx_queue_access' contains pointers to the available Rx port:queue pairs from this array.
   *       Core exclusive access to the shared Rx resources are enforced by en/dequeueing an Rx port:queue pair
   */
  eth_rx_queue_info_t  eth_rx_queue_info[MAX_ETH_RX_QUEUES]  ENV_CACHE_LINE_ALIGNED;
  
  /**
   * Tx buffer for Eth frames that must be sent out in-order.
   * One buffer per device (i.e. shared by all cores)
   */
  eth_tx_mbuf_table_t  eth_tx_mbuf_tables[MAX_ETH_PORTS][MAX_ETH_TX_MBUF_TABLES]  ENV_CACHE_LINE_ALIGNED;

  /**
   * Packet I/O flows lookup hash
   */
  packet_q_hash_t  packet_q_hash  ENV_CACHE_LINE_ALIGNED;
  
  /**
   * Mapping from hash val to actual EM-queue
   * EM-queue = packet_queues[packet_q_hash-result]
   */
  em_queue_t  packet_queues[PACKET_Q_HASH_ENTRIES]  ENV_CACHE_LINE_ALIGNED;
  
  /*
   * Grouping of shared variables that are almost always read-only
   */
  em_pkt_shared_readmostly_t  rdmostly  ENV_CACHE_LINE_ALIGNED;
  
  /** Pointer to this struct for sanity check (verify vm mapping on all procs) */
  void *this_shm;

} em_shared_data_t;

COMPILE_TIME_ASSERT((sizeof(em_shared_data_t) % ENV_CACHE_LINE_SIZE) == 0, EM_SHARED_DATA_SIZE_ERROR);




typedef union
{
  struct 
  {
    /* EM shared data */
    em_shared_data_t *shm;
    
    /** Event pool */
    em_event_pool_t   event_pool;
  };
  
  uint8_t u8[ENV_CACHE_LINE_SIZE];
  
} em_data_t;

COMPILE_TIME_ASSERT((sizeof(em_data_t) % ENV_CACHE_LINE_SIZE) == 0, EM_DATA_T_SIZE_ERROR);



/*
 * Externs
 */
extern em_data_t em;



#ifdef __cplusplus
}
#endif

#endif  // EM_SHARED_DATA_H