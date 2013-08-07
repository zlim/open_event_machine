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
 
#include "environment.h"

#include "event_machine.h"
#include "event_machine_group.h"
#include "em_intel_event_group.h"
#include "em_error.h"

#include "em_shared_data.h"



/**
 * Global event group intit function (called once at startup)
 */
void
event_group_alloc_init(void)
{
  printf("event group init\n");

  (void) memset(em.shm->em_event_group_entry_tbl, 0, sizeof(em_event_group_entry_t) * EM_MAX_EVENT_GROUPS);

  env_spinlock_init(&em.shm->em_event_group_entry_tbl_lock.u.lock);
}



/**
 * Get the next free event group identifier
 * 
 * @return Event Group identifier
 */
static inline em_event_group_t
event_group_next_free(void)
{
  int               i;
  em_event_group_t  event_group;


  event_group = EM_EVENT_GROUP_UNDEF;


  env_spinlock_lock(&em.shm->em_event_group_entry_tbl_lock.u.lock);


  for(i = 0; i < EM_MAX_EVENT_GROUPS; i++)
  {
    if(em.shm->em_event_group_entry_tbl[i].allocated == 0)
    {
      em.shm->em_event_group_entry_tbl[i].allocated = 1;
      event_group = i;
      break;
    }
  }


  env_spinlock_unlock(&em.shm->em_event_group_entry_tbl_lock.u.lock);


  return event_group;
}



/**
 * Create new event group id for fork-join.
 *
 * @return New event group id or EM_EVENT_GROUP_UNDEF
 *
 * @see em_event_group_delete(), em_event_group_apply()
 */
em_event_group_t
em_event_group_create(void)
{
  em_event_group_t event_group;
  

  event_group = event_group_next_free();

  return event_group;
}



/**
 * Delete (unallocate) an event group id
 *
 * An event group must not be deleted before it has been
 * completed (notifications sent) or canceled.
 *
 * @param event_group  Event group to delete
 *
 * @return EM_OK if successful.
 *
 * @see em_event_group_create()
 */
em_status_t
em_event_group_delete(em_event_group_t event_group)
{
  RETURN_ERROR_IF(invalid_egrp(event_group), EM_ERR_BAD_ID, EM_ESCOPE_EVENT_GROUP_DELETE,
                  "Invalid event group: %"PRI_EGRP"", event_group);

  env_spinlock_lock(&em.shm->em_event_group_entry_tbl_lock.u.lock);

  em.shm->em_event_group_entry_tbl[event_group].allocated = 0;

  env_spinlock_unlock(&em.shm->em_event_group_entry_tbl_lock.u.lock);


  return EM_OK;
}



/**
 * Apply event group configuration.
 *
 * The function sets (or resets) the event count and notification parameters
 * for the event group. After it returns events sent to the group are counted
 * against the (updated) count. Notification events are sent when all (counted)
 * events have been processed. A new apply call is needed to reset the event
 * group (counting).
 *
 * @param group        Group id
 * @param count        Number of events in the group
 * @param num_notif    Number of noticifation events to send
 * @param notif_tbl    Table of notifications (events and target queues)
 *
 * @return EM_OK if successful.
 *
 * @see em_event_group_create(), em_send_group()
 */
em_status_t
em_event_group_apply(em_event_group_t event_group, int count, int num_notif, const em_notif_t* notif_tbl)
{
  em_event_group_entry_t *group_e;
  int                     i;

 
  RETURN_ERROR_IF(invalid_egrp(event_group), EM_ERR_BAD_ID, EM_ESCOPE_EVENT_GROUP_APPLY,
                  "Invalid event group: %"PRI_EGRP"", event_group);
  
  
  group_e = &em.shm->em_event_group_entry_tbl[event_group];


  RETURN_ERROR_IF(group_e->count != 0, EM_ERR_NOT_FREE, EM_ESCOPE_EVENT_GROUP_APPLY,
                  "Event group %"PRI_EGRP" currently in use!", event_group);


  RETURN_ERROR_IF(((uint64_t) num_notif) > EM_EVENT_GROUP_MAX_NOTIF,
                  EM_ERR_TOO_LARGE, EM_ESCOPE_EVENT_GROUP_APPLY,
                  "Notif table too large: %i", num_notif);


  group_e->count     = count;
  group_e->num_notif = num_notif;

  for(i = 0; i < num_notif; i++)
  {
    group_e->notif_tbl[i].event = notif_tbl[i].event;
    group_e->notif_tbl[i].queue = notif_tbl[i].queue;
  }


  // Sync mem
  env_sync_mem();

  return EM_OK;
}


/**
 * Increment event group count
 * 
 * Increments event count of the current event group. Enables sending new events into 
 * the current group. Must be called before sending. Note that event count cannot be decremented.
 *
 * @param count        Number of events to add in the group
 *
 * @return EM_OK if successful.
 *
 * @see em_send_group()
 */
em_status_t
em_event_group_increment(int count)
{
  em_event_group_t        event_group; 
  em_event_group_entry_t *group_e;
  uint64_t                old, new;
  int                     ret;

  
  event_group = em_event_group_current();
  
  RETURN_ERROR_IF(invalid_egrp(event_group), EM_ERR_BAD_ID, EM_ESCOPE_EVENT_GROUP_INCREMENT,
                  "No current event group (%"PRI_EGRP")", event_group);


  group_e = &em.shm->em_event_group_entry_tbl[event_group];


  ret = 0;

  do
  {
    old = group_e->count;
    new = old + count;

    ret = rte_atomic64_cmpset(&group_e->count, old, new);
  }
  while(ret == 0);


  return EM_OK;
}


/**
 * Current event group
 *
 * Returns the event group of the currently received event or EM_EVENT_GROUP_UNDEF 
 * if the current event does not belong into any event group. Current group is needed 
 * when sending new events into the group.
 *
 * @return Current event group id or EM_EVENT_GROUP_UNDEF
 *
 * @see em_event_group_create()
 */
em_event_group_t
em_event_group_current(void)
{
  return em_core_local.current_event_group;
}



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
void
event_group_count_update(em_event_group_t event_group)
{
  em_event_group_entry_t *const group_e = &em.shm->em_event_group_entry_tbl[event_group];
  
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
 * @see em_send(), em_event_group_create(), em_event_group_apply()
 */
// See em_intel.c
// em_status_t
// em_send_group(em_event_t event, em_queue_t queue, em_event_group_t group)



