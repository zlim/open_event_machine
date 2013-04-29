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
 * EM Internal Control 
 *
 */
 
 
#include "em_internal_event.h"
#include "em_intel_queue_group.h"
#include "em_error.h"

#include <rte_debug.h>




/*
 * Local function prototypes
 */

static void 
i_event__em_internal_done(em_internal_event_t *const i_ev, const em_queue_t queue);

static void
i_event__eo_local_func_call_req(em_internal_event_t *const i_ev, const em_queue_t queue);




/*
 * Functions
 */


/**
 * Sends an internal control event to each core set in 'mask'.
 * 
 * When all cores set in 'mask' has processed the event an additional
 * internal 'done' msg is sent to synchronize - this done-event can then
 * trigger any notifications for the user that the operation was completed.
 * Processing of the 'done' event will also call the 'f_done_callback'
 * function if given.
 */
em_status_t
em_internal_notif(const em_core_mask_t *const mask,
                  em_event_t                  event,
                  void                      (*f_done_callback)(void *arg_ptr),
                  void                       *f_done_arg_ptr,
                  int                         num_notif,
                  const em_notif_t            notif_tbl[])
{
  em_event_group_t    event_group;
  em_status_t         err;
  em_event_t          event_tmp;
  em_internal_event_t *i_event;
  em_internal_event_t *i_event_tmp;
  int                 core_count;
  int                 mask_count;
  int                 i;


  RETURN_ERROR_IF(num_notif > EM_EVENT_GROUP_MAX_NOTIF, EM_ERR_TOO_LARGE, EM_ESCOPE_INTERNAL_NOTIF,
                  "Too large notif table (%i)", num_notif);


  event_group = em_event_group_create();
  
  RETURN_ERROR_IF(event_group == EM_EVENT_GROUP_UNDEF, EM_ERR_ALLOC_FAILED, EM_ESCOPE_INTERNAL_NOTIF,
                  "Event group alloc failed");

  
  mask_count = em_core_mask_count(mask); // subset of the running cores
  core_count = em_core_count();          // all running cores
    

  //
  // Internal notification when all cores are done.
  //
  err = em_internal_done_w_notif_req(event_group, mask_count, f_done_callback, f_done_arg_ptr, num_notif, notif_tbl);

  RETURN_ERROR_IF(err != EM_OK, err, EM_ESCOPE_INTERNAL_NOTIF, 
                  "Notif Request failed");
  
  
  //
  // Send 
  //
  i_event = em_event_pointer(event);
    
  for(i = 0; i < (core_count - 1); i++)
  {
    if(em_core_mask_isset(i, mask))
    {
      event_tmp = em_alloc(sizeof(em_internal_event_t), EM_EVENT_TYPE_SW, EM_POOL_DEFAULT);
      
      RETURN_ERROR_IF(event == EM_EVENT_UNDEF, EM_ERR_ALLOC_FAILED, EM_ESCOPE_INTERNAL_NOTIF,
                      "Internal event alloc failed");
      
      i_event_tmp = em_event_pointer(event_tmp);
      
      // Copy input event
      *i_event_tmp = *i_event;
      
      err = em_send_group(event_tmp, FIRST_INTERNAL_QUEUE + i, event_group);
      
      RETURN_ERROR_IF(err != EM_OK, err, EM_ESCOPE_INTERNAL_NOTIF,
                      "Event group send failed");
    }
  }
  
  
  // Send last (input event), if not sent then free it.
  if(em_core_mask_isset(i, mask))
  {
    err = em_send_group(event, FIRST_INTERNAL_QUEUE + i, event_group);
    
    RETURN_ERROR_IF(err != EM_OK, err, EM_ESCOPE_INTERNAL_NOTIF,
                    "Event group send failed");
  }
  else {
    em_free(event);
  }
  
  
  return EM_OK;
}




/**
 * Receives EM internal events
 */
void
em_internal_event_receive_func(void* eo_ctx, em_event_t event, em_event_type_t type, em_queue_t queue, void* q_ctx)
{
  em_internal_event_t *i_event;


  i_event = em_event_pointer(event);

  switch(i_event->id)
  {
    /*
     * Internal Done event
     */ 
    case EM_INTERNAL_DONE:
      i_event__em_internal_done(i_event, queue);
      break;

    
    /*
     * Internal events related to Queue Group creation and removal
     */
    case QUEUE_GROUP_ADD_REQ:
      i_event__queue_group_add_req(i_event, queue);
      break;
      
    case QUEUE_GROUP_REM_REQ:
      i_event__queue_group_rem_req(i_event, queue);
      break;
    
    /*
     * Internal events related to EO local start&stop functionality
     */
    case EO_LOCAL_START_REQ:
    case EO_LOCAL_STOP_REQ:
      i_event__eo_local_func_call_req(i_event, queue);
      break;

    case EM_QUEUE_DISABLE:
    case EM_EO_REMOVE_QUEUE:
    case EO_START_REQ:
    case EO_STOP_REQ: 
      /* No need to do anything - it's enough that the core is not processing EO or queue specific code */
      break;

    default:
      (void) EM_INTERNAL_ERROR(EM_ERR_BAD_ID, EM_ESCOPE_INTERNAL_EVENT_RECEIVE_FUNC,
                               "Bad internal event id 0x%"PRIx64" from queue %"PRI_QUEUE"\n",
                               i_event->id, queue);
      break;

  };


  em_free(event);
}

 


/**
 * Handle the internal 'done' event 
 */
static void 
i_event__em_internal_done(em_internal_event_t *const i_ev, const em_queue_t queue)
{
  int         i;
  int         num_notif;
  em_status_t ret;
  
  
  // Release the event group, we are done with it
  ret = em_event_group_delete(i_ev->done.event_group);
  
  ERROR_IF(ret != EM_OK, ret, EM_ESCOPE_EVENT_INTERNAL_DONE,
           "Event group %"PRI_EGRP" delete failed (ret=%u)\n",
           i_ev->done.event_group, ret);
  
  
  // Call the callback funtion (if given), performs custom actions at 'done'
  if(i_ev->done.f_done_callback != NULL) {
    i_ev->done.f_done_callback(i_ev->done.f_done_arg_ptr);
  }
  
  // Send requested notifications (requested by caller of em_queue_group_set())
  num_notif = i_ev->done.num_notif;
  
  if(num_notif > 0)
  { 
    for(i = 0; i < num_notif; i++)
    {
      em_event_t event = i_ev->done.notif_tbl[i].event;
      em_queue_t queue = i_ev->done.notif_tbl[i].queue;
      em_event_hdr_t *const ev_hdr = event_to_event_hdr(event);
      
      // Reset the src q_elem, not valid here
      ev_hdr->q_elem = NULL;
      
      // printf("%s(): sending notifications to Queue:%"PRI_QUEUE" in Queue Group:%"PRI_QGRP"\n",
      //        __func__, i_ev->done.notif_tbl[i].queue, em_queue_get_group(i_ev->done.notif_tbl[i].queue)); fflush(NULL);
             
      ret = em_send(event, queue);
      
      ERROR_IF(ret != EM_OK, ret, EM_ESCOPE_EVENT_INTERNAL_DONE,
               "em_send() of notification failed (%i of %i)", i, num_notif);
    }
  }
  
} 




/**
 * Handle the internal event requesting a local function call.
 */
static void
i_event__eo_local_func_call_req(em_internal_event_t *const i_ev, const em_queue_t queue)
{
  const uint64_t         f_type  = i_ev->loc_func.id;
  em_eo_element_t *const eo_elem = i_ev->loc_func.eo_elem;
  void            *const eo_ctx  = eo_elem->eo_ctx;
  
  
  if(f_type == EO_LOCAL_START_REQ)
  {
    eo_elem->start_local_func(eo_ctx, eo_elem->id);
  }
  else if(f_type == EO_LOCAL_STOP_REQ)
  {
    eo_elem->stop_local_func(eo_ctx, eo_elem->id);
  }
  else {
    (void) EM_INTERNAL_ERROR(EM_FATAL(EM_ERR_BAD_ID), EM_ESCOPE_EVENT_INTERNAL_LOCAL_FUNC_CALL,
                             "Unknown operation(%i)!\n", f_type);
  }
}




/**
 * Helper func: Allocate & set up the internal 'done' event with
 * function callbacks and notification events.
 */
em_status_t
em_internal_done_w_notif_req(em_event_group_t  event_group,
                             int               event_group_count,
                             void            (*f_done_callback)(void *arg_ptr),
                             void             *f_done_arg_ptr,
                             int               num_notif,
                             const em_notif_t *notif_tbl)
{
  em_event_t          event;
  em_internal_event_t *i_event;
  em_notif_t          i_notif;
  em_status_t         err;
  int                 i;
  
  
  event = em_alloc(sizeof(em_internal_event_t), EM_EVENT_TYPE_SW, EM_POOL_DEFAULT);
  
  RETURN_ERROR_IF(event == EM_EVENT_UNDEF, EM_ERR_ALLOC_FAILED, EM_ESCOPE_INTERNAL_DONE_W_NOTIF_REQ,
                  "Internal event 'DONE' alloc failed!");
  
  i_event = em_event_pointer(event);
  
  i_event->id                   = EM_INTERNAL_DONE;
  i_event->done.event_group     = event_group;
  i_event->done.f_done_callback = f_done_callback;
  i_event->done.f_done_arg_ptr  = f_done_arg_ptr;
  i_event->done.num_notif       = num_notif;

  for(i = 0; i < num_notif; i++)
  {
    i_event->done.notif_tbl[i].event = notif_tbl[i].event;
    i_event->done.notif_tbl[i].queue = notif_tbl[i].queue;
  }

  i_notif.event = event;
  i_notif.queue = SHARED_INTERNAL_QUEUE;

  // Request sending of EM_INTERNAL_DONE when 'event_group_count' events in 'event_group' have been seen.
  // The 'Done' event will trigger the notifications to be sent.
  err = em_event_group_apply(event_group, event_group_count, 1, &i_notif);
  
  RETURN_ERROR_IF(err != EM_OK, err, EM_ESCOPE_INTERNAL_DONE_W_NOTIF_REQ,
                  "Event group apply failed");
  
  return EM_OK;
}



 