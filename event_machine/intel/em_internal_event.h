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
 * EM Internal control header file
 *
 */
 
#ifndef EM_INTERNAL_EVENT_H
#define EM_INTERNAL_EVENT_H


#include "event_machine.h"
#include "event_machine_group.h"
#include "event_machine_helper.h"

#include "environment.h"
#include "em_intel.h"


#ifdef __cplusplus
extern "C" {
#endif


/**
 * Internal event
 */
typedef union
{
  uint64_t           id;
  
  struct
  {
    uint64_t         id;
    
    em_event_group_t event_group;
    
    void           (*f_done_callback)(void *arg_ptr);
    
    void            *f_done_arg_ptr;

    int              num_notif;
    
    em_notif_t       notif_tbl[EM_EVENT_GROUP_MAX_NOTIF];
    
  } done;
  
  
  struct
  {
    uint64_t         id;
    
    em_event_group_t event_group;
        
    em_queue_group_t queue_group;
    
    int              num_notif;
    
    em_notif_t       notif_tbl[EM_EVENT_GROUP_MAX_NOTIF];
    
  } q_grp;
  
  
  struct
  {
    uint64_t         id;
    
    em_event_group_t event_group;
    
    em_eo_element_t *eo_elem;
    
  } loc_func;

} em_internal_event_t;



/*
 * Internal event IDs
 */
#define EVENT_ID_MASK           (0xabcd0000)

#define EM_INTERNAL_DONE        (EVENT_ID_MASK | 0x00) // First

#define QUEUE_GROUP_ADD_REQ     (EVENT_ID_MASK | 0x01)
#define QUEUE_GROUP_REM_REQ     (EVENT_ID_MASK | 0x02)

#define EO_START_REQ            (EVENT_ID_MASK | 0x10)
#define EO_STOP_REQ             (EVENT_ID_MASK | 0x11)
#define EO_LOCAL_START_REQ      (EVENT_ID_MASK | 0x12)
#define EO_LOCAL_STOP_REQ       (EVENT_ID_MASK | 0x13)


#define EM_QUEUE_DISABLE        (EVENT_ID_MASK | 0x20)
#define EM_EO_REMOVE_QUEUE      (EVENT_ID_MASK | 0x21)



void
em_internal_event_receive_func(void* eo_ctx, em_event_t event, em_event_type_t type, em_queue_t queue, void* q_ctx);

em_status_t
em_internal_done_w_notif_req(em_event_group_t  event_group,
                             int               event_group_count,
                             void            (*f_done_callback)(void *arg_ptr),
                             void             *f_done_arg_ptr,
                             int               num_notif,
                             const em_notif_t *notif_tbl);

em_status_t
em_internal_notif(const em_core_mask_t *const mask,
                  em_event_t                  event,
                  void                      (*f_done_callback)(void *arg_ptr),
                  void                       *f_done_arg_ptr,
                  int                         num_notif,
                  const em_notif_t            notif_tbl[]);

#endif