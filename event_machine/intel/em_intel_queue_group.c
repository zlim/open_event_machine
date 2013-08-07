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
#include "em_intel_queue_group.h"
#include "em_intel_sched.h"
#include "em_internal_event.h"
#include "em_error.h"

#include "em_shared_data.h"

#include "em_intel_inline.h"

/**
 * em_queue_group_modify() triggers an internal 'Done'-notification event
 * that updates the queue group mask. This struct contains the callback args.
 */
typedef struct
{
  em_queue_group_t queue_group;
  
  em_core_mask_t   new_mask;
  
} q_grp_modify_done_callback_args_t;



typedef struct
{  
  em_queue_group_t queue_group;
  
} q_grp_delete_done_callback_args_t;



static inline void
group_mask_set(volatile uint64_t *const group_mask, const em_queue_group_t queue_group);

static inline void
group_mask_clr(volatile uint64_t *const group_mask, const em_queue_group_t queue_group);


static void 
q_grp_modify_done_callback(void *arg_ptr);
static void 
q_grp_modify_done(em_queue_group_t queue_group, em_core_mask_t *new_mask);

static void 
q_grp_delete_done_callback(void *arg_ptr);
static void 
q_grp_delete_done(em_queue_group_t queue_group);

static em_status_t
queue_group_modify(em_queue_group_t group, const em_core_mask_t* new_mask, int num_notif, const em_notif_t* notif_tbl, int is_delete);


/*
 * Queue groups
 * ******************************************************
 */


/**
 * Find the queue group based on it's name.
 */
// Assuming EM_QUEUE_GROUP_NAME_LEN == 8
#if EM_QUEUE_GROUP_NAME_LEN != 8
  #error "Assuming max 8 character queue group names"
#endif

static inline em_queue_group_t
queue_group_find(const char* name)
{
  em_queue_group_t queue_group;
  uint64_t         name_u64 = 0;         
  int              i;
  

  queue_group = EM_QUEUE_GROUP_UNDEF;

  // copy name string to correct alignment
  for(i = 0; i < EM_QUEUE_GROUP_NAME_LEN; i++)
  {
    if(name[i] == 0)
    {
      break;
    }

    ((char*)&name_u64)[i] = name[i];
  }
  

  for(i = 0; i < EM_MAX_QUEUE_GROUPS; i++)
  {
    if(em.shm->em_queue_group[i].allocated  && (em.shm->em_queue_group[i].name_u64 == name_u64) )
    {
      queue_group = i;
      break;
    }
  }

  return queue_group;
}




/**
 * Queue group inits done at global init (once at startup on one core)
 */
void
queue_group_init_global(void)
{
  em_queue_group_t    group;
  em_queue_t          queue;
  em_queue_element_t *q_elem;
  char                q_grp_name[] = EM_QUEUE_GROUP_CORE_LOCAL_BASE_NAME;
  int                 i;
  int                 num_cores;
  char                queue_name[EM_QUEUE_NAME_LEN] = "EM_Q_INTERNAL_000000";


  num_cores = em_core_count();

  env_spinlock_init(&em.shm->em_queue_group_lock.lock);

  (void) memset(em.shm->em_queue_group, 0, sizeof(em.shm->em_queue_group));


  for(i = 0; i < EM_MAX_QUEUE_GROUPS; i++) {
    m_list_init(&em.shm->em_queue_group[i].list_head);
  }


  //
  // Reserve the default queue group.
  // 
  em.shm->em_queue_group[EM_QUEUE_GROUP_DEFAULT].allocated = 1;
  // Copy at most EM_QUEUE_GROUP_NAME_LEN characters
  (void) strncpy(em.shm->em_queue_group[EM_QUEUE_GROUP_DEFAULT].name,
                 EM_QUEUE_GROUP_DEFAULT_NAME, EM_QUEUE_GROUP_NAME_LEN);
  // Force trailing zero, name-len is EM_QUEUE_GROUP_NAME_LEN+1 
  em.shm->em_queue_group[EM_QUEUE_GROUP_DEFAULT].name[EM_QUEUE_GROUP_NAME_LEN] = '\0'; 
  // Set bit for all cores, they are now part of the default queue group - Note that em_queue_group_modify() canot be used yet.
  em_core_mask_zero(&em.shm->em_queue_group[EM_QUEUE_GROUP_DEFAULT].mask);
  em_core_mask_set_count(num_cores, &em.shm->em_queue_group[EM_QUEUE_GROUP_DEFAULT].mask);  


  //
  // Create a shared internal queue, belongs to the default queue group
  //
  queue_init("EM shared internal", SHARED_INTERNAL_QUEUE, EM_QUEUE_TYPE_ATOMIC, INTERNAL_QUEUE_PRIORITY, EM_QUEUE_GROUP_DEFAULT);
  (void) sched_masks_add_queue(SHARED_INTERNAL_QUEUE, EM_QUEUE_TYPE_ATOMIC, EM_QUEUE_GROUP_DEFAULT);

  // Bind the shared internal queue
  q_elem = get_queue_element(SHARED_INTERNAL_QUEUE);

  q_elem->receive_func  = em_internal_event_receive_func;
  q_elem->eo_ctx        = NULL;
  q_elem->status        = EM_QUEUE_STATUS_READY;


  //
  // Create a queue group and a queue per core, for internal messaging
  //
  queue = FIRST_INTERNAL_QUEUE;
  
  for(i = 0; i < num_cores; i++)
  {
    em_core_mask_t mask;
    

    core_queue_grp_name(q_grp_name, i);
    em_core_mask_zero(&mask);
  
    // Create the queue group with a zero mask to prevent triggering a modify-operation (mod does not work yet!).
    // Add sched masks manually later on.
    group = em_queue_group_create(q_grp_name, &mask, 0, NULL);
    
    em_core_mask_set(i, &mask);
    
    IF_UNLIKELY(invalid_qgrp(group))
    {
      print_queue_groups();
      
      EM_INTERNAL_ERROR(EM_FATAL(EM_ERR_ALLOC_FAILED), EM_ESCOPE_QUEUE_GROUP_INIT_GLOBAL,
                        "Core local queue group create failed.");
      return;
    }
  
    // Set queue group masks manually, em_queue_group_modify() cannot be used yet
    em_core_mask_copy(&em.shm->em_queue_group[group].mask, &mask);
  
    (void) snprintf(&queue_name[14], EM_QUEUE_NAME_LEN-14, "%"PRI_QUEUE"", queue);
    queue_name[EM_QUEUE_NAME_LEN-1] = '\0';
    
    queue_init(queue_name, queue, EM_QUEUE_TYPE_ATOMIC, INTERNAL_QUEUE_PRIORITY, group);

    // Bind internal queue
    q_elem = get_queue_element(queue);

    q_elem->receive_func  = em_internal_event_receive_func;
    q_elem->eo_ctx        = NULL;
    q_elem->status        = EM_QUEUE_STATUS_READY;
    
    // Manually set the scheduling masks, cannot use em_queue_group_modify() yet
    (void) sched_masks_add_queue(queue, EM_QUEUE_TYPE_ATOMIC, group);

    queue++;
  }


  env_sync_mem();
}




/**
 * Queue group inits done at local init (once at startup on each core)
 */
em_status_t
queue_group_init_local(void)
{
  int               logic_core;
  em_queue_group_t  local_group;
  em_core_mask_t    mask, mask_group;
  char              q_grp_name[] = EM_QUEUE_GROUP_CORE_LOCAL_BASE_NAME;
  

  logic_core = em_core_id();
  
  core_queue_grp_name(q_grp_name, logic_core);
  local_group = queue_group_find(q_grp_name);
  
  RETURN_ERROR_IF(invalid_qgrp(local_group), EM_ERR_NOT_FOUND, EM_ESCOPE_QUEUE_GROUP_INIT_LOCAL,
                  "Did not find a queue group for the logic core:%i \n", logic_core);


  // Sanity check:
  em_core_mask_zero(&mask);
  em_core_mask_set(logic_core, &mask);  
  
  em_core_mask_zero(&mask_group);
  em_queue_group_mask(local_group, &mask_group);

  RETURN_ERROR_IF(!em_core_mask_equal(&mask, &mask_group), EM_ERROR, EM_ESCOPE_QUEUE_GROUP_INIT_LOCAL,
                  "Bad core mask (0x%lx) for logic core %i", mask_group.u64[0], logic_core);



  // Add core local queue group to the core's group mask
  em_core_local.current_group_mask |= ( ((uint64_t)0x1) << local_group );
  
  // Add the default group to the core's group mask
  em_core_local.current_group_mask |= ( ((uint64_t)0x1) << EM_QUEUE_GROUP_DEFAULT );
  


  return EM_OK;
}




/*
 * Queue groups
 * ******************************************************
 */

/**
 * Create a new queue group to control queue to core mapping.
 *
 * Allocates a new queue group identifier with a given core mask. The group name
 * can have max EM_QUEUE_GROUP_NAME_LEN characters and must be unique 
 * since it's used to identify the group. Cores added to the queue group can be 
 * changed later with em_queue_group_modify().
 *
 * This operation may be asynchronous, i.e. the creation may complete well
 * after this function has returned. Provide notification events, if
 * application cares about the actual completion time. EM will
 * send notifications when the operation has completed.
 *
 * The core mask is visible through em_queue_group_mask() only after the create
 * operation is complete. 
 * 
 * Note, that depending on the system, the operation can also happen one core at a time, so
 * an intermediate mask may be active momentarily.
 *
 * Only manipulate the core mask with the access macros defined in event_machine_core_mask.h
 * as the implementation underneath may change.
 *
 * EM has a default group (EM_QUEUE_GROUP_DEFAULT) containing 
 * all cores. It's named "default", otherwise naming scheme is system specific.
 *
 * Note, some systems may have a low number of queue groups available.
 *
 * @attention  Only call em_queue_enable() after em_queue_group_create() has completed - use
 *             notifications to synchronize.
 *
 * @param name          Queue group name. Unique name for identifying the group. 
 * @param mask          Core mask for the queue group
 * @param num_notif     Number of entries in notif_tbl (use 0 for no notification)
 * @param notif_tbl     Array of notifications to send to signal completion of operation
 *
 * @return Queue group or EM_QUEUE_GROUP_UNDEF on error.
 *
 * @see em_queue_group_find(), em_queue_group_modify(), em_queue_group_delete()
 */
em_queue_group_t
em_queue_group_create(const char* name, const em_core_mask_t* mask, int num_notif, const em_notif_t* notif_tbl)
{
  int              i;
  em_queue_group_t queue_group;
  em_status_t      err;


  ERROR_IF(name == NULL, EM_ERR_BAD_POINTER, EM_ESCOPE_QUEUE_GROUP_CREATE,
           "Queue group name ptr is NULL!")
  {
    return EM_QUEUE_GROUP_UNDEF;
  }


  ERROR_IF(mask == NULL, EM_ERR_BAD_POINTER, EM_ESCOPE_QUEUE_GROUP_CREATE,
           "Queue group mask NULL!")
  {
    return EM_QUEUE_GROUP_UNDEF;
  }  


  env_spinlock_lock(&em.shm->em_queue_group_lock.lock);
  
  
  queue_group = queue_group_find(name);

  
  ERROR_IF(queue_group != EM_QUEUE_GROUP_UNDEF, EM_ERR_NOT_FREE, EM_ESCOPE_QUEUE_GROUP_CREATE,
           "Queue group name (%s) already in use!", name)
  {
    // Name already reserved
    env_spinlock_unlock(&em.shm->em_queue_group_lock.lock);
    return EM_QUEUE_GROUP_UNDEF;
  }
  
  
  queue_group = EM_QUEUE_GROUP_UNDEF;

  // If no matching group found, find next free
  for(i = 0; i < EM_MAX_QUEUE_GROUPS; i++)
  {
    if(em.shm->em_queue_group[i].allocated == 0)
    {
      // Init queue group
      queue_group = i;
      (void) memset(&em.shm->em_queue_group[queue_group], 0, sizeof(em.shm->em_queue_group[0]));
      em.shm->em_queue_group[queue_group].allocated = 1;
      //em_core_mask_copy(&em.shm->em_queue_group[queue_group].mask, mask);

      strncpy(em.shm->em_queue_group[queue_group].name, name, EM_QUEUE_GROUP_NAME_LEN); // copy at most EM_QUEUE_GROUP_NAME_LEN characters
      em.shm->em_queue_group[queue_group].name[EM_QUEUE_GROUP_NAME_LEN] = '\0';         // force trailing zero
      
      m_list_init(&em.shm->em_queue_group[queue_group].list_head);
      break;
    }
  }

  env_spinlock_unlock(&em.shm->em_queue_group_lock.lock);


  ERROR_IF(queue_group == EM_QUEUE_GROUP_UNDEF, EM_ERR_ALLOC_FAILED, EM_ESCOPE_QUEUE_GROUP_CREATE,
           "Queue qroup allocation failed - no free queue group available!");

  
  /*
   * Use queue group modify to set the core mask for the group on the concerned cores.
   * Handles zero-mask properly.
   */
  err = em_queue_group_modify(queue_group, mask, num_notif, notif_tbl);
  
  ERROR_IF(err != EM_OK, err, EM_ESCOPE_QUEUE_GROUP_CREATE,
          "Queue group create: mask modification failed (err=%u, mask=0x%"PRIX64")",
          err, mask->u64[0])
  {
    err = em_queue_group_delete(queue_group, 0, NULL);
    ERROR_IF(err != EM_OK, err, EM_ESCOPE_QUEUE_GROUP_CREATE,
             "Cleanup operation failed, could not delete the queue group!");
    
    return EM_QUEUE_GROUP_UNDEF;
  }

  
  return queue_group;
}




/**
 * Modify core mask of an existing queue group.
 *
 * The function compares the new core mask to the current mask and changes the 
 * queue group to core mapping accordingly. 
 *
 * This operation may be asynchronous, i.e. the change may complete well
 * after this function has returned. Provide notification events, if
 * application cares about the actual completion time. EM will
 * send notifications when the operation has completed.
 *
 * The new core mask is visible through em_queue_group_mask() only after the modify
 * operation is complete. 
 * 
 * Note, that depending on the system, the change can also happen one core at a time, so
 * an intermediate mask may be active momentarily.
 *
 * Only manipulate core mask with the access macros defined in event_machine_core_mask.h
 * as the implementation underneath may change.
 *
 * @param group         Queue group to modify
 * @param new_mask      New core mask 
 * @param num_notif     Number of entries in notif_tbl (use 0 for no notification)
 * @param notif_tbl     Array of notifications to send
 *
 * @return EM_OK if successful.
 *
 * @see em_queue_group_create(), em_queue_group_find(), em_queue_group_delete(), em_queue_group_mask()
 */
em_status_t                                                                                                              
em_queue_group_modify(em_queue_group_t group, const em_core_mask_t* new_mask, int num_notif, const em_notif_t* notif_tbl)
{
  return queue_group_modify(group, new_mask, num_notif, notif_tbl, 0 /*is_delete=0*/);
}

/**
 * Called by em_queue_group_modify with flag is_delete=0 an by em_queue_group_delete() with flag is_delete=1
 * 
 * @param is_delete     is modify triggered by em_queue_group_delete()? 1=Yes, 0=No
 */
static em_status_t
queue_group_modify(em_queue_group_t group, const em_core_mask_t* new_mask, int num_notif, const em_notif_t* notif_tbl, int is_delete)
{
  em_core_mask_t       old_mask, max_mask;
  em_event_group_t     event_group;
  int                  adds, rems, i;
  uint8_t              add_core[EM_MAX_CORES];
  uint8_t              rem_core[EM_MAX_CORES];
  em_status_t          err;
  em_event_t           event;
  em_internal_event_t *i_event;
  int                  core_count;
  int                  immediate_send_notifs = 0; // 'new_mask' equal to mask in use => immediately send notifs and return

  void               (*f_done_callback)(void *arg_ptr);  
  q_grp_modify_done_callback_args_t *modify_callback_args;
  q_grp_delete_done_callback_args_t *delete_callback_args;


  // printf("EM-core%02i: %s(): "
  //        "Queue Group (%"PRI_QGRP") Mask update request: new=0x%"PRIX64" old=0x%"PRIX64"\n",
  //        em_core_id(), __func__, group, new_mask->u64[0], em.shm->em_queue_group[group].mask.u64[0]);


  RETURN_ERROR_IF(invalid_qgrp(group), EM_ERR_BAD_ID, EM_ESCOPE_QUEUE_GROUP_MODIFY,
                  "Invalid queue group: %"PRI_QGRP"", group); 
  
  RETURN_ERROR_IF(new_mask == NULL, EM_ERR_BAD_POINTER, EM_ESCOPE_QUEUE_GROUP_MODIFY,
                  "Queue group mask NULL! Queue group:%"PRI_QGRP"", group);   
  
  em_core_mask_zero(&max_mask);
  em_core_mask_set_count(em_core_count(), &max_mask);
  
  // Can only set core mask bits for running cores - veify this.
  RETURN_ERROR_IF(new_mask->u64[0] > max_mask.u64[0], EM_ERR_TOO_LARGE, EM_ESCOPE_QUEUE_GROUP_MODIFY,
                  "Queue group:%"PRI_QGRP" - Invalid new mask: 0x%"PRIX64" (max valid is: 0x%"PRIX64")",
                  group, new_mask->u64[0], max_mask.u64[0]);
  
  
  RETURN_ERROR_IF(((uint64_t) num_notif) > EM_EVENT_GROUP_MAX_NOTIF,
                  EM_ERR_TOO_LARGE, EM_ESCOPE_QUEUE_GROUP_MODIFY,
                  "Notif table too large: %i", num_notif);



  env_spinlock_lock(&em.shm->em_queue_group_lock.lock);
  

  IF_UNLIKELY(!em.shm->em_queue_group[group].allocated)
  {
    env_spinlock_unlock(&em.shm->em_queue_group_lock.lock);

    return EM_INTERNAL_ERROR(EM_ERR_BAD_ID, EM_ESCOPE_QUEUE_GROUP_MODIFY, "Queue group not allocated...");
  }
  
  
  IF_UNLIKELY(em.shm->em_queue_group[group].pending_modify)
  {
    env_spinlock_unlock(&em.shm->em_queue_group_lock.lock);

    return EM_INTERNAL_ERROR(EM_ERROR, EM_ESCOPE_QUEUE_GROUP_MODIFY, "Pending queue group modify.");
  }
  
  
  //
  // If the new mask is equal to the one in use: send notifs immediately and return
  //
  em_core_mask_copy(&old_mask, &em.shm->em_queue_group[group].mask);
  
  if(em_core_mask_equal(&old_mask, new_mask))
  {
    // set flag, notifs sent after releasing lock
    immediate_send_notifs = 1;
  }
  else
  {
    // Set the pending modify flag to catch contending modifies
    em.shm->em_queue_group[group].pending_modify = 1;
  }
  
  
  env_spinlock_unlock(&em.shm->em_queue_group_lock.lock);



  if(immediate_send_notifs)
  {
    // new mask equal to mask in use (both can also be zero), send notifs & return
    
    if(is_delete)
    {
      // modify called from em_queue_group_delete():
      // delete the queue group before sending immediate notifs
      q_grp_delete_done(group);
    }
    
    if(num_notif > 0)
    { 
      for(i = 0; i < num_notif; i++)
      {
        em_event_t event = notif_tbl[i].event;
        em_queue_t queue = notif_tbl[i].queue;
        em_event_hdr_t *const ev_hdr = event_to_event_hdr(event);
        
        // Reset the src q_elem, not valid here
        ev_hdr->q_elem = NULL;
                       
        err = em_send(event, queue);
        
        RETURN_ERROR_IF(err != EM_OK, err, EM_ESCOPE_QUEUE_GROUP_MODIFY,
                        "em_send() of immediate notification failed (%i of %i)", i, num_notif);
      }
    }

    return EM_OK;
  }

  
  //
  // Note: .pending_modify = 1 from here onwards:
  //       Threat all errors as EM_FATAL because failures will leave .pending_modify = 1 
  //       until restart for the group.
  

  //
  // Send queue group add/rem commands to relevant cores
  //

  event_group = em_event_group_create();

  RETURN_ERROR_IF(event_group == EM_EVENT_GROUP_UNDEF, EM_FATAL(EM_ERR_ALLOC_FAILED),
                  EM_ESCOPE_QUEUE_GROUP_MODIFY, "Event group alloc failed");

  adds       = 0;
  rems       = 0;
  core_count = em_core_count();

  // Count added and removed cores
  for(i = 0; i < core_count; i++)
  {
    if( !em_core_mask_isset(i, &old_mask) &&
         em_core_mask_isset(i,  new_mask)    )
    {
      add_core[adds] = i;
      adds++;
    }
    else if(  em_core_mask_isset(i, &old_mask) &&
             !em_core_mask_isset(i,  new_mask)    )
    {
      rem_core[rems] = i;
      rems++;
    }
  }


  // printf("Queue group %"PRI_QGRP" modify: %i adds  %i rems. Using event group %"PRI_EGRP" \n", group, adds, rems, event_group);



  //
  // Internal notification when all adds and rems are done. Also save user requested notifs if given
  //

  event = em_alloc(sizeof(em_internal_event_t), EM_EVENT_TYPE_SW, EM_POOL_DEFAULT);
    
  RETURN_ERROR_IF(event == EM_EVENT_UNDEF, EM_FATAL(EM_ERR_ALLOC_FAILED), EM_ESCOPE_QUEUE_GROUP_MODIFY,
                 "Internal event QUEUE_GROUP_REM_REQ alloc failed!");
  
  
  if(is_delete)
  {
    f_done_callback = q_grp_delete_done_callback;
    
    delete_callback_args = em_event_pointer(event);
    // Init the callback function arguments
    delete_callback_args->queue_group = group;    
  }
  else {
    f_done_callback = q_grp_modify_done_callback;

    modify_callback_args = em_event_pointer(event);
    // Init the callback function arguments
    modify_callback_args->queue_group = group;
    em_core_mask_copy(&modify_callback_args->new_mask, new_mask);
  }
  
  err = em_internal_done_w_notif_req(event_group, adds+rems, f_done_callback, event /*=callback args*/, num_notif, notif_tbl);

  RETURN_ERROR_IF(err != EM_OK, EM_FATAL(err), EM_ESCOPE_QUEUE_GROUP_MODIFY, "em_internal_done_w_notif_req() failed.");


  
  //
  // Send rems
  //
  for(i = 0; i < rems; i++)
  {
    event = em_alloc(sizeof(em_internal_event_t), EM_EVENT_TYPE_SW, EM_POOL_DEFAULT);
    
    RETURN_ERROR_IF(event == EM_EVENT_UNDEF, EM_FATAL(EM_ERR_ALLOC_FAILED), EM_ESCOPE_QUEUE_GROUP_MODIFY,
                   "Internal event QUEUE_GROUP_REM_REQ alloc failed!");
    
    i_event = em_event_pointer(event);
    i_event->id                = QUEUE_GROUP_REM_REQ;
    i_event->q_grp.queue_group = group;
    
    err = em_send_group(event, FIRST_INTERNAL_QUEUE + rem_core[i], event_group);
    
    // FATAL error, abort execution
    RETURN_ERROR_IF(err != EM_OK, EM_FATAL(err), EM_ESCOPE_QUEUE_GROUP_MODIFY,
                    "Event group send failed (rem_core[%i]=%i)", i, rem_core[i]);
  }



  //
  // Send adds
  // 
  for(i = 0; i < adds; i++)
  {
    event = em_alloc(sizeof(em_internal_event_t), EM_EVENT_TYPE_SW, EM_POOL_DEFAULT);
    
    RETURN_ERROR_IF(event == EM_EVENT_UNDEF, EM_FATAL(EM_ERR_ALLOC_FAILED), EM_ESCOPE_QUEUE_GROUP_MODIFY,
                   "Internal event QUEUE_GROUP_ADD_REQ alloc failed!");
    
    i_event = em_event_pointer(event);
    i_event->id                = QUEUE_GROUP_ADD_REQ;
    i_event->q_grp.queue_group = group;

    err = em_send_group(event, FIRST_INTERNAL_QUEUE + add_core[i], event_group);
    
    // FATAL error, abort execution
    RETURN_ERROR_IF(err != EM_OK, EM_FATAL(err), EM_ESCOPE_QUEUE_GROUP_MODIFY,
                    "Event group send failed (add_core[%i]=%i)", i, add_core[i]);
  }


  return EM_OK;
}


/**
 * Finds queue group by name.
 *
 * This returns the situation at the moment of the inquiry. If another core is modifying the group
 * at the same time the result may not be up-to-date. Application may need to synchronize group modifications. 
 *
 * @param name          Name of the queue qroup to find
 *
 * @return  Queue group or EM_QUEUE_GROUP_UNDEF on an error
 *
 * @see em_queue_group_create(), em_queue_group_modify()
 *
 */
em_queue_group_t
em_queue_group_find(const char* name)
{
  em_queue_group_t queue_group;

  queue_group = queue_group_find(name);

  return queue_group;
}




/**
 * Get current core mask for a queue group.
 *
 * This returns the situation at the moment of the inquiry. If another core is modifying the group
 * at the same time the result may not be up-to-date. Application may need to synchronize group modifications.
 *
 * @param group         Queue group
 * @param mask          Core mask for the queue group
 *
 * @return EM_OK if successful.
 * 
 * @see em_queue_group_create(), em_queue_group_modify()
 */
em_status_t
em_queue_group_mask(em_queue_group_t group, em_core_mask_t* mask)
{
  int allocated, pending_modify;
  
  RETURN_ERROR_IF(invalid_qgrp(group), EM_ERR_BAD_ID, EM_ESCOPE_QUEUE_GROUP_MASK,
                  "Invalid queue group: %"PRI_QGRP"", group);
  
  
  env_spinlock_lock(&em.shm->em_queue_group_lock.lock);
  
  allocated      = em.shm->em_queue_group[group].allocated;
  pending_modify = em.shm->em_queue_group[group].pending_modify;
  
  em_core_mask_copy(mask, &em.shm->em_queue_group[group].mask);
  
  env_spinlock_unlock(&em.shm->em_queue_group_lock.lock);
  
  
  RETURN_ERROR_IF((!allocated) || (pending_modify), EM_FATAL(EM_ERR_BAD_STATE), EM_ESCOPE_QUEUE_GROUP_MASK,
                  "Queue group (id=%"PRI_QGRP") in bad state: allocated=%i, pending_modify=%i",
                  group, allocated, pending_modify);

  return EM_OK;
}




/**
 * Delete the queue group.
 * 
 * Free's the queue group identifier for re-use. Application must remove cores from the 
 * group with em_queue_group_modify() before calling the delete.
 *
 * @param group         Queue group to delete
 *
 * @return EM_OK if successful.
 *
 * @see em_queue_group_create(), em_queue_group_modify()
 */
em_status_t
em_queue_group_delete(em_queue_group_t group, int num_notif, const em_notif_t* notif_tbl)
{ 
  em_core_mask_t zero_mask;
  em_status_t    err;
  
  
  em_core_mask_zero(&zero_mask);
   
  //
  // Use modify and the notif mechanism to set the core mask to zero
  //
  err = queue_group_modify(group, &zero_mask, num_notif, notif_tbl, 1 /*is_delete=1*/);
 
  
  RETURN_ERROR_IF(err != EM_OK, err, EM_ESCOPE_QUEUE_GROUP_DELETE,
                  "Queue group modify for delete failed!");
   
  return EM_OK;
}




/*
 * EM internal event handlers (see em_internal_event.c&h)
 */

void
i_event__queue_group_add_req(em_internal_event_t *const i_ev, const em_queue_t queue)
{
  /*
   * Set all scheduling masks associated with this queue group locally on this core.
   */
  const em_queue_group_t grp = i_ev->q_grp.queue_group;
  
  
  group_mask_set(&em_core_local.current_group_mask, i_ev->q_grp.queue_group);
  
  sched_masks_add_queue_group__local(grp);
  
  
  // printf("EM-core%02i: QUEUE_GROUP_ADD_REQ internal event from queue %"PRI_QUEUE". "
  //        "Core-local Queue Group Mask updated to 0x%"PRIX64"\n",
  //        em_core_id(), queue, em_core_local.current_group_mask); fflush(NULL);
}



void
i_event__queue_group_rem_req(em_internal_event_t *const i_ev, const em_queue_t queue)
{
  /*
   * Clear all scheduling masks associated with this queue group locally on this core.
   */ 
  const em_queue_group_t grp = i_ev->q_grp.queue_group;
  
  
  group_mask_clr(&em_core_local.current_group_mask, grp);

  sched_masks_rem_queue_group__local(grp);
  
  
  // printf("EM-core%02i: QUEUE_GROUP_REM_REQ internal event from queue %"PRI_QUEUE". "
  //        "Core-local Queue Group Mask updated to 0x%"PRIX64"\n",
  //        em_core_id(), queue, em_core_local.current_group_mask); fflush(NULL);
}




/**
 * Callback function when a em_queue_group_modify() completes with the internal DONE-event
 */
static void 
q_grp_modify_done_callback(void *arg_ptr)
{
  em_event_t                         event = (em_event_t) arg_ptr;
  q_grp_modify_done_callback_args_t *args  = em_event_pointer(event);


  q_grp_modify_done(args->queue_group, &args->new_mask);
  
  em_free(event);
}



static void 
q_grp_modify_done(em_queue_group_t queue_group, em_core_mask_t *new_mask)
{
  env_spinlock_lock(&em.shm->em_queue_group_lock.lock);
  
  // Now modify is complete, update the mask
  em_core_mask_copy(&em.shm->em_queue_group[queue_group].mask, new_mask);
  em.shm->em_queue_group[queue_group].pending_modify = 0;
  
  env_spinlock_unlock(&em.shm->em_queue_group_lock.lock);

  // printf("EM-core%02i: QUEUE_GROUP_MODIFY DONE internal event - "
  //        "Queue Group (%"PRI_QGRP") Mask updated to 0x%"PRIX64" (0x%"PRIX64")\n",
  //        em_core_id(), queue_group, new_mask->u64[0], em.shm->em_queue_group[queue_group].mask.u64[0]);
  //        
  // print_queue_groups();
  // fflush(NULL);
}




/**
 * Callback function when a em_queue_group_modify(delete flag set) completes with the internal DONE-event
 */
static void 
q_grp_delete_done_callback(void *arg_ptr)
{
  em_event_t                         event = (em_event_t) arg_ptr;
  q_grp_delete_done_callback_args_t *args  = em_event_pointer(event);
  

  q_grp_delete_done(args->queue_group);

  em_free(event);
}



static void 
q_grp_delete_done(em_queue_group_t queue_group)
{
  env_spinlock_lock(&em.shm->em_queue_group_lock.lock);
 
  
  IF_UNLIKELY(!em_core_mask_iszero(&em.shm->em_queue_group[queue_group].mask))
  {
    env_spinlock_unlock(&em.shm->em_queue_group_lock.lock);

    EM_INTERNAL_ERROR(EM_FATAL(EM_ERR_BAD_STATE), EM_ESCOPE_QUEUE_GROUP_DELETE,
                      "Queue group mask not zero in delete...");
  }
  
  
  IF_UNLIKELY(!m_list_is_empty(&em.shm->em_queue_group[queue_group].list_head))
  {
    env_spinlock_unlock(&em.shm->em_queue_group_lock.lock);

    EM_INTERNAL_ERROR(EM_FATAL(EM_ERR_BAD_STATE), EM_ESCOPE_QUEUE_GROUP_DELETE,
                      "Queue group still has queues associated with it, cannot delete!");
  }  
  
  
  // Clear the Queue Group data
  (void) memset(&em.shm->em_queue_group[queue_group], 0, sizeof(em.shm->em_queue_group[0]));

  env_spinlock_unlock(&em.shm->em_queue_group_lock.lock);  
}



void
queue_group_add_queue_list(em_queue_group_t queue_group, em_queue_t queue)
{
  em_queue_element_t *const q_elem = get_queue_element(queue);
  
  
  env_spinlock_lock(&em.shm->em_queue_group_lock.lock);
  
  m_list_add(&em.shm->em_queue_group[queue_group].list_head, &q_elem->qgrp_node);
  
  env_spinlock_unlock(&em.shm->em_queue_group_lock.lock);
}



void
queue_group_rem_queue_list(em_queue_group_t queue_group, em_queue_t queue)
{
  em_queue_element_t *const q_elem    = get_queue_element(queue);
  m_list_head_t      *const list_head = &em.shm->em_queue_group[queue_group].list_head;
  
  env_spinlock_lock(&em.shm->em_queue_group_lock.lock);
  
  if(!m_list_is_empty(list_head)) {
    m_list_rem(list_head, &q_elem->qgrp_node);
  }
  
  env_spinlock_unlock(&em.shm->em_queue_group_lock.lock);
}



void 
print_queue_groups(void)
{
  int i;

  printf("\nQueue groups\n------------\n");
  printf("  id      name mask\n");

  for(i = 0; i < EM_MAX_QUEUE_GROUPS; i++)
  {
    if(em.shm->em_queue_group[i].allocated)
    {
      printf("  %2i  %8s 0x%lx\n", i, em.shm->em_queue_group[i].name, em.shm->em_queue_group[i].mask.u64[0]);
    }
  }

  printf("\n");

}




