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
 * EM HW specific functions 
 */
#ifndef EVENT_MACHINE_HW_SPECIFIC_H
#define EVENT_MACHINE_HW_SPECIFIC_H



#ifdef __cplusplus
extern "C" {
#endif


#include <event_machine_group.h>


/**
 * Send an event to a queue.
 *
 * Event must have been allocated with em_alloc(), or
 * received via receive-function. Sender must not touch the
 * event after calling em_send as the ownership is moved to system
 * and then to the receiver. If return status is *not* EM_OK, the ownership
 * has not moved and the application is still responsible for the event (e.g. 
 * may free it).
 *
 * EM does not define guaranteed event delivery, i.e. EM_OK return value only
 * means the event was accepted for delivery. It could still be lost during
 * the delivery (e.g. due to disabled/removed queue, queue or system 
 * congestion, etc).
 *
 * @param event         Event to be sent
 * @param queue         Destination queue
 *
 * @return EM_OK if successful (accepted for delivery).
 * 
 * @see em_alloc()
 */
static inline em_status_t
em_send(em_event_t event, em_queue_t queue)
{
  return em_send_group(event, queue, EM_EVENT_GROUP_UNDEF);
}


/**
 * HW specific init and inlined functions (if any)
 */


/**
 * Global initialization of EM internals. Only one core does this and this must be called
 * before any other call.
 *
 * @return status, EM_OK on success
 */
em_status_t em_init_global(void);


/**
 * Local initialization of EM internals. All cores call this and it must be called
 * after em_init_global(), but before any other call.
 * Implementation may be actually empty, but this might be needed later for some
 * core specific initializations, so application startup should call this always.
 *
 * @return status, EM_OK on success
 */
em_status_t em_init_local(void);




#ifdef __cplusplus
}
#endif



#endif


