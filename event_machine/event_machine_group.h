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
 * Event Machine optional fork-join helper.
 *
 * An event group can be used to trigger join of parallel operations.
 * The number of parallel operations need to be known by the event group
 * creator, but the separate events handlers don't need top know anything
 * about the other related events.
 *
 * 1. a group is created with em_event_group_create()
 *
 * 2. the number of parallel events is set with em_event_group_apply()
 *
 * 3. the parallel events are sent normally, but using em_send_group() instead
 *    of em_send()
 *
 * 4. as the receive function of the last event is completed, the given notification
 *    event(s) are sent automatically and can trigger the next operation
 *
 * 5. the sequence continues from step 2. for new set of events (if the group is reused)
 *
 * So here the original initiator only needs to know how the task is split into
 * parallel events, the event handlers and the one continuing the work (join)
 * are not involved (assuming the task itself can be separately processed)
 *
 * Note, that this only works with events targeted to an EO, i.e. SW events
 *
 *
 *
 * @todo specify exact operation to cancel an aborted fork-join
 *       em_status_t em_event_group_cancel(em_event_group_t group, ...);
 *
 */
#ifndef EVENT_MACHINE_GROUP_H
#define EVENT_MACHINE_GROUP_H

#include <event_machine.h>

#ifdef __cplusplus
extern "C" {
#endif


/*
 * Optimize EM for 64 bit architecture
 * =====================================
 */
#ifdef EM_64_BIT

/**
 * Event group id. This is used for fork-join event handling.
 *
 * @see em_event_group_create()
 */
typedef uint64_t               em_event_group_t;

#define EM_EVENT_GROUP_UNDEF   EM_UNDEF_U64      /**< Invalid event group */
#define PRI_EGRP               PRIu64            /**< em_event_group_t printf format */


/*
 * Optimize EM for 32 bit architecture
 * =====================================
 */
#elif defined(EM_32_BIT)

/**
 * Event group id. This is used for fork-join event handling.
 *
 * @see em_event_group_create()
 */
typedef uint32_t               em_event_group_t;

#define EM_EVENT_GROUP_UNDEF   EM_UNDEF_U32      /**< Invalid event group id */
#define PRI_EGRP               PRIu32            /**< em_event_group_t printf format */

#else
  #error Missing architecture definition. Define EM_64_BIT or EM_32_BIT!   
#endif


/**
 * Create new event group id for fork-join.
 *
 * @return New event group id or EM_EVENT_GROUP_UNDEF
 *
 * @see em_event_group_delete(), em_event_group_apply()
 */
em_event_group_t em_event_group_create(void);


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
em_status_t em_event_group_delete(em_event_group_t event_group);


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
em_status_t em_event_group_apply(em_event_group_t group, int count, int num_notif, const em_notif_t* notif_tbl);


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
em_status_t em_event_group_increment(int count);


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
em_event_group_t em_event_group_current(void);


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
em_status_t em_send_group(em_event_t event, em_queue_t queue, em_event_group_t group);




#ifdef __cplusplus
}
#endif



#endif


