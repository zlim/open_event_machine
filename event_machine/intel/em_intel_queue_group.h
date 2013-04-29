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
 * Event Machine Intel Queue Group header file
 *
 */

#ifndef EM_INTEL_QUEUE_GROUP_H_
#define EM_INTEL_QUEUE_GROUP_H_


#include "em_intel.h"
#include "em_internal_event.h"


/**
 * The name of the EM Default Queue Group
 */
#define EM_QUEUE_GROUP_DEFAULT_NAME         "default"

COMPILE_TIME_ASSERT(sizeof(EM_QUEUE_GROUP_DEFAULT_NAME)         <= EM_QUEUE_GROUP_NAME_LEN, EM_QUEUE_GROUP_DEFAULT_NAME_SIZE_ERROR);


/**
 * The base-name of the messaging queue groups, one per core.
 */
#define EM_QUEUE_GROUP_CORE_LOCAL_BASE_NAME "core00"

COMPILE_TIME_ASSERT(sizeof(EM_QUEUE_GROUP_CORE_LOCAL_BASE_NAME) <= EM_QUEUE_GROUP_NAME_LEN, EM_QUEUE_GROUP_CORE_LOCAL_BASE_NAME_SIZE_ERROR);




typedef struct
{
  // List of q_elems that belong to this queue group
  m_list_head_t   list_head;
  
  // Mask of logical EM core ids.  LSB=core0 ...  1<<em_core_id
  em_core_mask_t mask;

  union
  {
    char         name[EM_QUEUE_GROUP_NAME_LEN+1]; // extra char for ending zero
    uint64_t     name_u64;                        // ignore extra char in 64bit comparisons
  };

  // boolean "is q-grp-elem allocated?" 1=True, 0=False 
  uint8_t        allocated;
  // boolean "is queue group modification pending?" 1=True, 0=False 
  uint8_t        pending_modify;

} em_queue_group_element_t;




/*
 * Macros
 */
#define invalid_qgrp(queue_group) (ENV_UNLIKELY((queue_group) >= EM_MAX_QUEUE_GROUPS))


/*
 * Externs
 */

extern ENV_SHARED  em_queue_group_element_t  em_queue_group[EM_MAX_QUEUE_GROUPS];
extern ENV_SHARED  em_spinlock_t             em_queue_group_lock;


/*
 * Functions and prototypes
 */
void
queue_group_init_global(void);

em_status_t
queue_group_init_local(void);

void 
print_queue_groups(void);


void i_event__queue_group_add_req(em_internal_event_t *const i_ev, const em_queue_t queue);
void i_event__queue_group_rem_req(em_internal_event_t *const i_ev, const em_queue_t queue);
void i_event__queue_group_done(em_internal_event_t    *const i_ev, const em_queue_t queue);

void
queue_group_add_queue_list(em_queue_group_t queue_group, em_queue_t queue);

void
queue_group_rem_queue_list(em_queue_group_t queue_group, em_queue_t queue);



static inline void
group_mask_set(volatile uint64_t *const group_mask, const em_queue_group_t queue_group)
{
  uint64_t  old, new;
  int       ret;  
  
  
  ret = 0;

  do
  {
    old = *group_mask;
    new = old | (((uint64_t)0x1) << queue_group);

    ret = rte_atomic64_cmpset(group_mask, old, new);
  }
  while(ret == 0);
}


static inline void
group_mask_clr(volatile uint64_t *const group_mask, const em_queue_group_t queue_group)
{
  uint64_t  old, new;
  int       ret;  
  
  
  ret = 0;

  do
  {
    old = *group_mask;
    new = old & (~(((uint64_t)0x1) << queue_group));

    ret = rte_atomic64_cmpset(group_mask, old, new);
  }
  while(ret == 0);
}



static inline int 
group_mask_cnt(volatile uint64_t *const group_mask)
{
  uint64_t n = *group_mask;
  int      cnt;
  

  for(cnt = 0; n; cnt++)
  {
    n &= (n - 1); // Clear the least significant bit set
  }
  
  return cnt;
}



/**
 * Modify the EM_QUEUE_GROUP_CORE_LOCAL_BASE_NAME ("core00") for a core
 */
static inline void
core_queue_grp_name(char* name, int core)
{
  int tens;
  
  // core00
  tens = core / 10;
  name[4] = '0' + tens;
  name[5] = '0' + (core-(tens*10));
}


#endif


