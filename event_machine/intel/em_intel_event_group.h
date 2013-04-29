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
 * Event Machine Intel Event Group header file
 *
 */

#ifndef EM_INTEL_EVENT_GROUP_H_
#define EM_INTEL_EVENT_GROUP_H_


#include "em_intel.h"
#include "em_error.h"


typedef struct
{
  em_notif_t        notif_tbl[EM_EVENT_GROUP_MAX_NOTIF];

  volatile uint64_t count;
  int               num_notif;

  uint8_t           allocated;

} em_event_group_entry_t;



typedef struct
{
  union
  {
    env_spinlock_t  lock;
    
    uint8_t u8[ENV_CACHE_LINE_SIZE];
  } u;
  
} em_event_group_entry_tbl_lock_t;

COMPILE_TIME_ASSERT(sizeof(em_event_group_entry_tbl_lock_t) == ENV_CACHE_LINE_SIZE, EM_EVENT_GROUP_ENTRY_TBL_LOCK_T_SIZE_ERROR);




/*
 * Externs
 */
extern ENV_SHARED em_event_group_entry_t em_event_group_entry_tbl[];



/*
 * Macros
 */
#define invalid_egrp(event_group)  (ENV_UNLIKELY((event_group) >= EM_MAX_EVENT_GROUPS))



/*
 * Functions and prototypes
 */
 
void
event_group_alloc_init(void);




/**
 * Updates the event group count
 * 
 * Updates the event count of the event group. Only called by the
 * EM-dispatcher to track when to send the notifications when the
 * event group is done.
 *
 * @param group_e       Pointer to an event group entry
 *
 * @return EM_OK if successful.
 */
static inline void
event_group_count_update(em_event_group_t event_group)
{
  em_event_group_entry_t *const group_e = &em_event_group_entry_tbl[event_group];
  
  uint64_t old, new;
  int      ret;
  

  env_sync_mem();

  ret = 0;

  do
  {
    old = group_e->count;

    IF_UNLIKELY(old == 0)
    {
      (void) EM_INTERNAL_ERROR(EM_ERR_BAD_ID, EM_ESCOPE_EVENT_GROUP_UPDATE,
                               "Group count already 0!\n");
      return;
    }

    new = old - 1;

    ret = rte_atomic64_cmpset(&group_e->count, old, new);
  }
  while(ret == 0);



  if(new == 0)
  { // Last event in the group

    int i;

    for(i = 0; i < group_e->num_notif; i++)
    {
      em_send(group_e->notif_tbl[i].event, group_e->notif_tbl[i].queue);
    }
  }

  return;
}


#endif


