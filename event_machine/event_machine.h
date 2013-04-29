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
 * Event Machine API v1.0
 *
 */
  
#ifndef EVENT_MACHINE_H
#define EVENT_MACHINE_H

#ifdef __cplusplus
extern "C" {
#endif


#define EM_API_VERSION_MAJOR 1     /**< Major API version number. Step if not backwards compatible  */
#define EM_API_VERSION_MINOR 0     /**< Minor API version number. Updates and additions             */


/** @mainpage
 *
 *  @section section_1 General
 *  Event Machine (EM) is an architectural abstraction and framework of an event driven
 *  multicore optimized processing concept originally developed for networking data plane.
 *  It offers an easy programming concept for scalable and dynamically load balanced
 *  multicore applications with a very low overhead run-to-completion principle.
 *
 *  Main elements in the concept are events, queues, scheduler, dispatcher and the
 *  execution objects (EO). Event is an application specific piece of data (like a message
 *  or a network packet) describing work, something to do. Any processing in EM must be
 *  triggered by an event. Events are sent to asynchronous application specific queues.
 *  A single thread on all cores of an EM instance runs a dispatcher loop (a "core" is
 *  used here to refer to a core or one HW thread on multi-threaded cores). Dispatcher
 *  interfaces with the scheduler and asks for an event. Scheduler evaluates the state
 *  of all queues and gives the highest priority event to the dispatcher, which forwards
 *  it to the EO mapped to the queue the event came from by calling the registered receive
 *  function. As the event is handled and receive function returns, the next event is
 *  received from the scheduler and again forwarded to the mapped EO. This happens in
 *  parallel on all cores included. Everything is highly efficient run to completion
 *  single thread, no context switching nor pre-emption (priorities are handled by the
 *  scheduler).
 *  EM can run on bare metal for best performance or under an operating system with special
 *  arrangements (e.g. one thread per core with thread affinity).
 *
 *  The concept and this API are made to be easy to implement for multiple general purpose or
 *  networking oriented multicore packet processing systems on chip, which typically also
 *  contain accelerators for packet processing needs. Efficient integration with modern HW
 *  accelerators has been a major driver in EM concept.
 *
 *  One general principle of this API is that all calls are multicore safe, i.e.
 *  no data structure gets broken, if calls are simultaneously made by multiple cores, but
 *  unless explicitly documented per API call, the application also needs to take the parallel
 *  world into consideration. For example if one core asks for a queue mode and another one
 *  changes the mode at the same time, the returned mode may be invalid (valid data, but the old
 *  or the new!). Thus modifications should be done under atomic context (if load balancing is
 *  used) or otherwise synchronized by the application. One simple way of achieving this is to use
 *  one EO with an atomic queue to do all the management functionality. That guarantees
 *  synchronized operations (but also serializes them limiting performance)
 *
 *  EM_64_BIT or EM_32_BIT (needs to be defined at makefile) defines whether (most of)
 *  the types used in the API are 32 or 64 bits wide. NOTE, that this is a major decision,
 *  since it may limit value passing between different systems using the defined types directly.
 *  Using 64-bits may allow more efficient underlying implementation, as more data can be coded
 *  in 64-bit identifiers for instance.
 *
 *  @section section_2 Some principles
 *  - This API attempts to guide towards a portable application architecture, but is not defined
 *   for portability by re-compilation. Many things are system specific giving more possibilities
 *   for efficient use of HW resources.
 *  - EM does not define event content (one exception, see em_alloc()). This is a choice made for
 *  performance, since most HW devices use proprietary descriptors. This API enables to use those directly.
 *  - EM does not define detailed queue scheduling disciplines or API to set those up (or actually
 *  anything to configure a system). The priority value in this API is a (mapped) system specific
 *  QoS class label only
 *  - In general EM does not implement full SW platform or middleware solution, it implements a sub-
 *  set of such, a driver level part. For best performance it can be used directly from applications.
 *
 *
 *  @section section_3 Files
 *  - event_machine.h
 *    - Event Machine main API, all applications need to include
 *  - event_machine_types.h
 *    - Event Machine basic types, included by event_machine.h
 *  - event_machine_helper.h
 *    - optional helper routines
 *  - event_machine_group.h
 *    - optional event group feature for fork-join - type of operations using events
 *  - event_machine_hw_config.h
 *    - HW specific constants and enumerations, included by event_machine_types.h
 *  - event_machine_hw_specific.h
 *    - HW specific functions and macros, included by event_machine.h
 *
 *  @example test_appl_hello.c
 *  @example test_appl_perf.c
 *  @example test_appl_error.c
 *  @example test_appl_event_group.c
 *
 */


/* Basic EM types and HW configuration */
#include <event_machine_types.h>

/* HW specific EM types */
#include <event_machine_hw_types.h>


/**
 * Event
 *
 * In Event Machine application processing is driven by events. An event 
 * describes a piece of work. Structure of an event is implementation and
 * event type specific. It may be a directly accessible buffer of memory, 
 * a descriptor containing a list of buffer pointers, a descriptor of 
 * a packet buffer, etc.
 *
 * Applications use event type to interpret the event structure.
 *
 * Since em_event_t may not carry a direct pointer value to the event
 * structure, em_event_pointer() must be used to translate an event to
 * an event structure pointer (for maintaining portability).
 *
 * em_event_t is defined in event_machine_hw_types.h
 *
 * @see em_event_pointer()
 */


/**
 * Notification
 *
 * Notification structure allows user to define a notification event and
 * destination queue pair. EM will notify user by sending the event into
 * the queue.
 */
typedef struct em_notif_t
{
  em_event_t  event;  /**< User defined notification event */
  em_queue_t  queue;  /**< Destination queue */

} em_notif_t;



/*
 *
 * From Event Machine to application interface (call-back functions)
 * ----------------------------------------------------------------------------
 * 
 */


/**
 * Receive event
 *
 * Application receives events through the EO receive function. It implements main part of
 * the application logic. EM calls the receive function when it has dequeued an event from one
 * of the EO's queues. Application processes the event and returns immediately (in 
 * run-to-completion fashion).
 *
 * On multicore systems several events (from the same or different queues) may be dequeued
 * in parallel and thus same receive function may be executed concurrently on several cores.
 * Parallel execution may be limited by queue group setup or using queues with atomic
 * scheduling mode.
 *
 * EO and queue context pointers are user defined (in EO and queue creation time) and 
 * may be used any way needed. For example, EO context may be used to store global EO state
 * information, which is common to all queues and events. In addition, queue context may be
 * used to store queue (or user data flow) specific state data. EM never touches the data, 
 * just passes the pointers. 
 *
 * Event (handle) must be converted to an event structure pointer with em_event_pointer().
 * Event type specifies the event structure, which is implementation or application specific.
 * Queue id specifies the queue which the event was dequeued from.
 *
 * @param eo_ctx        EO context data. The pointer is passed in em_eo_create(), EM does not touch the data.
 * @param event         Event handle
 * @param type          Event type
 * @param queue         Queue from which this event came from
 * @param q_ctx         Queue context data. The pointer is passed in em_queue_set_context(), EM does not touch the data.
 *
 * @see em_event_pointer(), em_free(), em_alloc(), em_send(), em_queue_set_context(), em_eo_create()
 */
typedef void (*em_receive_func_t)(void* eo_ctx, em_event_t event, em_event_type_t type, em_queue_t queue, void* q_ctx);

/**
 * Execution object start, global.
 * 
 * If load balancing/several cores share the EO, this function is called
 * once on one core only (any). Purpose of this global start is to provide
 * a placeholder for first level initialization, like allocating memory and
 * initializing shared data. After this global start returns, the core local
 * version (if defined), is called (see em_start_local_func_t below). If there
 * is no core local start, then event dispatching is immediately enabled.
 *
 * If Execution object does not return EM_OK,
 * the system will not call the core local init and will not enable event
 * dispatching.
 *
 * This function should never be called directly from the application,
 * but using the em_eo_start() instead, which maintains state information!
 *
 * @param eo_ctx        Execution object internal state/instance data
 * @param eo            Execution object id
 *
 * @return EM_OK if successful.
 *
 * @see em_eo_start(), em_eo_create()
 */
typedef em_status_t (*em_start_func_t)(void* eo_ctx, em_eo_t eo);

/**
 * Execution object start, core local.
 * 
 * This is similar to the global start above, but this one is called after the
 * global start has completed and is called on all cores.
 *
 * Purpose of this optional local start is to work as a placeholder for
 * core local initialization, e.g. allocating core local memory for example.
 * The global start is only called on one core. The use of local start is
 * optional.
 * Note, that application should never directly call this function, this
 * will be called via em_eo_start().
 *
 * If this does not return EM_OK on all involved cores, the event dispatching
 * is not enabled.
 *
 * @param eo_ctx        Execution object internal state/instance data
 * @param eo            Execution object id
 *
 * @return EM_OK if successful.
 *
 * @see em_eo_start(), em_eo_create()
 */
typedef em_status_t (*em_start_local_func_t)(void* eo_ctx, em_eo_t eo);

/**
 * Execution object stop, core local.
 *
 * If load balancing/several cores share the EO, this function is
 * called once on each core before the global stop (reverse order of start).
 * System disables event dispatching before calling this
 * and also makes sure this does not get called before the core
 * has been notified the stop condition for this EO (won't dispatch new events)
 * Return value is only for logging purposes, EM does not use it currently.
 *
 * Note, that application should never directly call this stop function,
 * em_eo_stop() will trigger this.
 *
 * @param eo_ctx        Execution object internal state data
 * @param eo            Execution object id
 *
 * @return EM_OK if successful.
 *
 * @see em_eo_stop(), em_eo_create()
 */
typedef em_status_t (*em_stop_local_func_t)(void* eo_ctx, em_eo_t eo);

/**
 * Execution object stop, global.
 *
 * If load balancing/several cores share the EO, this function is
 * called once on one core (any) after the (optional) core local
 * stop return on all cores.
 * System disables event dispatching before calling this and also
 * makes sure this does not get called before all cores have
 * been notified the stop condition for this EO (can't dispatch new events)
 * event if there is no core local stop defined.
 * Return value is only for logging purposes, EM does not use it currently.
 *
 * Note, that application should never directly call this stop function,
 * but use the em_eo_stop() instead, which maintains state information and
 * guarantees synchronized operation.
 *
 * @param eo_ctx        Execution object internal state data
 * @param eo            Execution object id
 *
 * @return EM_OK if successful.
 *
 * @see em_eo_stop(), em_eo_create()
 */
typedef em_status_t (*em_stop_func_t)(void* eo_ctx, em_eo_t eo);

/**
 * Error handler.
 *
 * Error handler maybe called after EM notices an error or user have called em_error().
 * 
 * User can register EO specific and/or EM global error handlers. When an error is noticed, 
 * EM calls EO specific error handler, if registered. If there's no EO specific handler registered 
 * (for the EO) or the error is noticed outside of an EO context, EM calls the global error 
 * handler (if registered). If no error handlers are found, EM just returns an error code depending on 
 * the API function.
 *
 * Error handler is called with the original error code from the API call or em_error(). Error 
 * scope identifies the source of the error and how the error code and variable arguments 
 * should be interpreted (number of arguments and types).
 *
 * @param eo            Execution object id
 * @param error         The error code
 * @param escope        Error scope. Identifies the scope for interpreting the error code and variable arguments.
 * @param args          Variable number and type of arguments
 *
 * @return The function may not return depending on implementation/error code/error scope. If it
 * returns, it can return the original or modified error code or even EM_OK, if it could fix the problem.
 *
 * @see em_register_error_handler(), em_eo_register_error_handler()
 */
typedef em_status_t (*em_error_handler_t)(em_eo_t eo, em_status_t error, em_escope_t escope, va_list args);



/*
 * HW specific init and inlined functions
 */
#include <event_machine_hw_specific.h>



/*
 *
 * From application to Event Machine interface
 * ----------------------------------------------------------------------------
 *
 */


/**
 * Create a new queue with a dynamic queue id.
 *
 * The given name string is copied to EM internal data structure. The maximum
 * string length is EM_QUEUE_NAME_LEN. 
 *
 * @param name          Queue name for debugging purposes (optional, NULL ok)
 * @param type          Queue scheduling type
 * @param prio          Queue priority
 * @param group         Queue group for this queue
 *
 * @return New queue id or EM_QUEUE_UNDEF on an error
 *
 * @see em_queue_group_create(), em_queue_delete()
 */
em_queue_t em_queue_create(const char* name, em_queue_type_t type, em_queue_prio_t prio, em_queue_group_t group);

/**
 * Create a new queue with a static queue id.
 *
 * Note, that system may have limited amount of static identifiers,
 * so unless really needed use dynamic queues instead.
 * The range of static identifiers is system dependent, but macros
 * EM_QUEUE_STATIC_MIN and EM_QUEUE_STATIC_MAX can be used to abstract,
 * i.e. use EM_QUEUE_STATIC_MIN+x for the application.
 *
 * The given name string is copied to EM internal data structure. The maximum
 * string length is EM_QUEUE_NAME_LEN.
 *
 * @param name          Queue name for debugging purposes (optional, NULL ok)
 * @param type          Queue scheduling type
 * @param prio          Queue priority
 * @param group         Queue group for this queue
 * @param queue         Requested queue id from the static range
 *
 * @return EM_OK if successful.
 *
 * @see em_queue_group_create(), em_queue_delete()
 */
em_status_t em_queue_create_static(const char* name, em_queue_type_t type, em_queue_prio_t prio, em_queue_group_t group, em_queue_t queue);

/**
 * Delete a queue.
 *
 * Unallocates the queue id.
 * NOTE: this is an immediate deletion and can *only*
 * be done after the queue has been removed from scheduling
 * using em_eo_remove_queue() !
 *
 * @param queue         Queue id to delete
 *
 * @return EM_OK if successful.
 *
 * @see em_eo_remove_queue(), em_queue_create(), em_queue_create_static()
 */
em_status_t em_queue_delete(em_queue_t queue);

/**
 * Enable event scheduling for the queue.
 *
 * All events sent to a non-enabled queue may get discarded or held
 * depending on the system. Queue enable/disable is not meant to be used
 * for additional scheduling nor used frequently. Main purpose is to
 * synchronize startup or recovery actions.
 *
 * @param queue         Queue to enable
 *
 * @return EM_OK if successful.
 *
 * @see em_eo_start(), em_queue_enable_all(), em_queue_disable()
 */
em_status_t em_queue_enable(em_queue_t queue);

/**
 * Enable event scheduling for all the EO's queues.
 * 
 * Otherwise identical to em_queue_enable().
 *
 * @param eo            EO id 
 *
 * @return EM_OK if successful.
 *
 * @see em_queue_enable(), em_queue_disable_all()
 */
em_status_t em_queue_enable_all(em_eo_t eo);

/**
 * Disable scheduling for the queue.
 *
 * Note, that this might be an asynchronous operation and actually complete later as
 * other cores may still be handling existing events. If application needs to
 * know exactly when all processing is completed, it can use the notification
 * arguments - the given notification(s) are sent after all cores have completed.
 *
 * Implicit disable is done for all queues, that are mapped to an EO when
 * it's stop-function is called (via em_eo_stop()).
 *
 * All events sent to a non-enabled queue may get discarded or held
 * depending on the system. Queue enable/disable is not meant to be used
 * for additional scheduling nor used frequently. Main purpose is to
 * synchronize startup or recovery actions.
 *
 * @param queue         Queue to disable
 * @param num_notif     Number of entries in notif_tbl, use 0 for no notification
 * @param notif_tbl     Notification events to send
 *
 * @return EM_OK if successful.
 *
 * @see em_eo_stop(), em_queue_disable_all(), em_queue_enable()
 */
em_status_t em_queue_disable(em_queue_t queue, int num_notif, const em_notif_t* notif_tbl);

/**
 * Disable scheduling for all the EO's queues.
 *
 * Otherwise identical to em_queue_disable().
 *
 * @param eo            EO id
 * @param num_notif     Number of entries in notif_tbl, use 0 for no notification
 * @param notif_tbl     Notification events to send
 *
 * @return EM_OK if successful.
 *
 * @see em_eo_stop(), em_queue_disable(), em_queue_enable_all()
 */
em_status_t em_queue_disable_all(em_eo_t eo, int num_notif, const em_notif_t* notif_tbl);

/**
 * Set queue specific (application) context.
 * 
 * This is just a single pointer associated with a queue. Application can use it
 * to access some context data quickly (without a lookup). The context is given
 * as argument for the receive function. EM does not use the value, it just
 * passes it.
 *                        
 * @param queue         Queue to which associate the context
 * @param context       Context pointer
 *
 * @return EM_OK if successful.
 *
 * @see em_receive_func_t(), em_queue_get_context()
 */
em_status_t em_queue_set_context(em_queue_t queue, const void* context);

/**
 * Get queue specific (application) context.
 *
 * Returns the value application has earlier set with em_queue_set_context().
 *
 * @param queue         Queue which context is requested
 *
 * @return Queue specific context pointer or NULL on an error
 *
 * @see em_queue_set_context()
 */
void* em_queue_get_context(em_queue_t queue);

/**
 * Get queue name.
 *
 * Returns the name given to a queue when it was created.
 * A copy of the queue name string (up to 'maxlen' characters) is
 * written to the user given buffer.
 * String is always null terminated even if the given buffer length
 * is less than the name length.
 *
 * If the queue has no name, function returns 0 and writes empty string.
 *
 * This is only for debugging purposes.
 *
 * @param queue         Queue id
 * @param name          Destination buffer
 * @param maxlen        Maximum length (including the terminating '0')
 *
 * @return Number of characters written (excludes the terminating '0').
 * 
 * @see em_queue_create()
 */
size_t em_queue_get_name(em_queue_t queue, char* name, size_t maxlen);

/**
 * Get queue priority.
 *
 * @param  queue        Queue identifier
 *
 * @return Priority class or EM_QUEUE_PRIO_UNDEF on an error
 *
 * @see em_queue_create()
 */
em_queue_prio_t em_queue_get_priority(em_queue_t queue);

/**
 * Get queue type (scheduling mode).
 *
 * @param  queue        Queue identifier
 *
 * @return Queue type or EM_QUEUE_TYPE_UNDEF on an error
 *
 * @see em_queue_create()
 */
em_queue_type_t em_queue_get_type(em_queue_t queue);

/**
 * Get queue's queue group
 *
 * @param  queue        Queue identifier 
 *
 * @return Queue group or EM_QUEUE_GROUP_UNDEF on error.
 * 
 * @see em_queue_create(), em_queue_group_create(), em_queue_group_modify()
 */
em_queue_group_t em_queue_get_group(em_queue_t queue);

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
em_queue_group_t em_queue_group_create(const char* name, const em_core_mask_t* mask, int num_notif, const em_notif_t* notif_tbl);

/**
 * Delete the queue group.
 * 
 * Removes all cores from the queue group and free's the identifier for re-use. 
 * All queues in the group must be deleted with em_queue_delete() before
 * deleting the group.
 *
 * @param group         Queue group to delete
 * @param num_notif     Number of entries in notif_tbl (use 0 for no notification)
 * @param notif_tbl     Array of notifications to send to signal completion of operation
 *
 * @return EM_OK if successful.
 *
 * @see em_queue_group_create(), em_queue_group_modify(), em_queue_delete()
 */
em_status_t em_queue_group_delete(em_queue_group_t group, int num_notif, const em_notif_t* notif_tbl);

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
em_status_t em_queue_group_modify(em_queue_group_t group, const em_core_mask_t* new_mask, int num_notif, const em_notif_t* notif_tbl);

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
em_queue_group_t em_queue_group_find(const char* name);

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
em_status_t em_queue_group_mask(em_queue_group_t group, em_core_mask_t* mask);

/**
 * Create Execution Object (EO).
 * 
 * This will allocate identifier and initialize internal data for a new EO.
 * It is left in a non-active state, i.e. no events are dispatched before
 * em_eo_start() has called. Start, stop and receive callback function
 * pointers are mandatory parameters.
 *
 * The name given is copied to EO internal data and can be used e.g. for debugging.
 * The maximum length stored is EM_EO_NAME_LEN.
 *
 * @param name          Name of the EO (NULL if no name)
 * @param start         Start function
 * @param local_start   Core local start function (NULL if no local start)
 * @param stop          Stop function
 * @param local_stop    Core local stop function (NULL if no local stop)
 * @param receive       Receive function
 * @param eo_ctx        User defined EO context data, EM just passes the pointer (NULL if not context)
 *
 * @return New EO id if successful, otherwise EM_EO_UNDEF
 *
 * @see em_eo_start(), em_eo_delete(), em_queue_create(), em_eo_add_queue()
 * @see em_start_func_t(), em_stop_func_t(), em_receive_func_t()
 */
em_eo_t em_eo_create(const char* name,
                     em_start_func_t start,
                     em_start_local_func_t local_start,
                     em_stop_func_t stop,
                     em_stop_local_func_t local_stop,
                     em_receive_func_t receive,
                     const void* eo_ctx);


/**
 * Delete Execution Object (EO).
 *
 * This will immediately delete the given EO and free the identifier.
 *
 * NOTE, that EO can only be deleted after it has been stopped using
 * em_eo_stop(), otherwise another core might still access the EO data!
 * Deletion will fail, if the EO is not stopped.
 *
 * This will delete all possibly remaining queues.
 *
 * @param eo            EO id to delete
 *
 * @return EM_OK if successful.
 *
 * @see em_eo_stop(), em_eo_create()
 */
em_status_t em_eo_delete(em_eo_t eo);

/**
 * Returns the name given to the EO when it was created.
 *
 * A copy of the name string (up to 'maxlen' characters) is
 * written to the user buffer 'name'.
 * String is always null terminated even if the given buffer length
 * is less than the name length.
 *
 * If the EO has no name, function returns 0 and writes empty string. 
 *
 * This is only for debugging purposes.
 *
 * @param eo            EO id
 * @param name          Destination buffer
 * @param maxlen        Maximum length (including the terminating '0')
 *
 * @return Number of characters written (excludes the terminating '0')
 *
 * @see em_eo_create() 
 */
size_t em_eo_get_name(em_eo_t eo, char* name, size_t maxlen);

/**
 * Add a queue to an EO.
 *
 * Note, that this does not enable the queue. Although queues added in
 * (or before) the start function will be enabled automatically.
 *
 * @param eo            EO id
 * @param queue         Queue id
 *
 * @return EM_OK if successful.
 *
 * @see em_queue_create(), em_eo_create(), em_queue_enable(), em_eo_remove_queue()
 */
em_status_t em_eo_add_queue(em_eo_t eo, em_queue_t queue);

/**
 * Removes a queue from an EO.
 * 
 * Function disables scheduling of the queue and removes the queue from the
 * EO. The operation is asynchronous, to quarantee that all cores have completed
 * processing of events from the queue (i.e. there's no cores in middle of the
 * receive function) before removing it.
 *
 * If the caller needs to know when the context deletion actually occurred,
 * the num_notif and notif_tbl can be used. The given notification event(s)
 * will be sent to given queue(s), when the removal has completed.
 * If such notification is not needed, use 0 as num_notif.
 *
 * If the queue to be removed is still enabled, it will first be disabled.
 *
 * @param eo            EO id
 * @param queue         Queue id to remove
 * @param num_notif     How many notification events given, 0 for no notification
 * @param notif_tbl     Array of pairs of event and queue identifiers
 *
 * @return EM_OK if successful.
 *
 * @see em_eo_add_queue(), em_queue_disable(), em_queue_delete()
 */
em_status_t em_eo_remove_queue(em_eo_t eo, em_queue_t queue, int num_notif, const em_notif_t* notif_tbl);

/**
 * Register EO specific error handler.
 *
 * The EO specific error handler is called if error is noticed or em_error() is 
 * called in the context of the EO. Note, the function will override any previously 
 * registered error hanler.
 *
 * @param eo            EO id
 * @param handler       New error handler.
 *
 * @return EM_OK if successful.
 *
 * @see em_register_error_handler(), em_error_handler_t()
 */
em_status_t em_eo_register_error_handler(em_eo_t eo, em_error_handler_t handler);

/**
 * Unregister EO specific error handler.
 *
 * Removes previously registered EO specific error handler.
 *
 * @param eo            EO id
 *
 * @return EM_OK if successful.
 */
em_status_t em_eo_unregister_error_handler(em_eo_t eo);

/**
 * Start Execution Object (EO).
 *
 * Calls global EO start function. If that returns EM_OK,
 * an internal event to trigger local start is sent to all cores belonging to
 * the queue group of this EO.
 * If the global start function does not return EM_OK the local start is
 * not called and event dispatching is not enabled for this EO.
 *
 * If the caller needs to know when the EO start was actually completed
 * on all cores, the num_notif and notif_tbl can be used. The given
 * notification event(s) will be sent to given queue(s), when the
 * start is completed on all cores.
 * If local start does not exist the notification(s) are sent as the global
 * start returns.
 * If such notification is not needed, use 0 as num_notif.
 *
 * @param eo            EO id
 * @param result        Optional pointer to em_status_t, which gets updated to the
 *                      return value of the actual EO global start function
 *
 * @param num_notif     If not 0, defines the number of events to send as all cores
 *                      have returned from the start function (in notif_tbl).
 *
 * @param notif_tbl     Array of em_notif_t, the optional notification events (data copied)
 *
 * @return EM_OK if successful.
 *
 * @see em_start_func_t(), em_start_local_func_t(), em_eo_stop()
 *
 * @todo Way to read core local start value or status?
 */
em_status_t em_eo_start(em_eo_t eo, em_status_t *result, int num_notif, const em_notif_t* notif_tbl);

/**
 * Stop Execution Object (EO).
 *
 * Disables event dispatch from all related queues, calls core local stop
 * on all cores and finally calls the global stop function of the EO,
 * when all cores have returned from the (optional) core local stop.
 * Call to the global EO stop is asynchronous and only done, when all cores
 * have completed processing of the receive function and/or core local stop.
 * This guarantees no core is accessing EO data during EO global stop function.
 *
 * This function returns immediately.
 *
 * If the caller needs to know when the EO stop was actually completed,
 * the num_notif and notif_tbl can be used. The given notification event(s)
 * will be sent to given queue(s), when the stop actually completes.
 * If such notification is not needed, use 0 as num_notif.
 *
 * @param eo            EO id
 * @param num_notif     How many notification events given, 0 for no notification
 * @param notif_tbl     Array of pairs of event and queue identifiers
 *
 * @return EM_OK if successful.
 *
 * @see em_stop_func_t(), em_stop_local_func_t(), em_eo_start()
 *
 * @todo Method for the application to get the final stop status
 */
em_status_t em_eo_stop(em_eo_t eo, int num_notif, const em_notif_t* notif_tbl);

/**
 * Logical core id.
 *
 * Returns the logical id of the current core.
 * EM enumerates cores (or HW threads) to always start from 0 and be contiguous,
 * i.e. valid core identifiers are 0...em_core_count()-1
 *
 * @return Current logical core id
 *
 * @see em_core_count()
 */
int em_core_id(void);

/**
 * The number of cores running within the same EM instance (sharing the EM state).
 *
 * @return Number of EM cores (or HW threads)
 *
 * @see em_core_id()
 *
 * @todo CPU hot plugging support
 */
int em_core_count(void);

/**
 * Allocate an event.
 *
 * Memory address of the allocated event is system specific and
 * can depend on given pool id, event size and type. Returned 
 * event (handle) may refer to a memory buffer or a HW specific 
 * descriptor, i.e. the event structure is system specific. 
 *
 * Use em_event_pointer() to convert an event (handle) to a pointer to 
 * the event structure. 
 *
 * EM_EVENT_TYPE_SW with minor type 0 is reserved for direct portability.
 * It is always guaranteed to return a 64-bit aligned contiguous 
 * data buffer, that can directly be used by the application up to
 * the given size (no HW specific descriptors etc are visible).
 *
 * EM_POOL_DEFAULT can be used as pool id if there's no need to 
 * use any specific memory pool.
 *
 * Additionally it is guaranteed, that two separate buffers
 * never share a cache line to avoid false sharing.
 *
 * @param size          Event size in octets
 * @param type          Event type to allocate
 * @param pool_id       Event pool id 
 *
 * @return the allocated event or EM_EVENT_UNDEF on an error
 *
 * @see em_free(), em_send(), em_event_pointer(), em_receive_func_t()
 */
em_event_t em_alloc(size_t size, em_event_type_t type, em_pool_id_t pool_id);

/**
 * Free an event.
 *
 * It is assumed the implementation can detect from which
 * memory area/pool the event was originally allocated from.
 *
 * Free transfers the ownership of the event to the system and
 * application must not touch the event (or related memory buffers)
 * after calling it.
 *
 * Application must only free events it owns. For example, sender must
 * not free an event after sending it. 
 * 
 * @param event         Event to be freed
 *
 * @see em_alloc(), em_receive_func_t()
 *
 * @todo OK or not to free EM_EVENT_UNDEF?
 */
void em_free(em_event_t event);

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
em_status_t em_send(em_event_t event, em_queue_t queue);

/**
 * Release atomic processing context.
 *
 * When an event was received from an atomic queue, the function can be used to 
 * release the atomic context before receive function return. After the call,
 * scheduler is allowed to schedule another event from the same queue
 * to another core. This increases parallelism and may improve performance - 
 * however the exclusive processing and ordering (!) might be lost after the call.
 *
 * Can only be called from within the event receive function!
 *
 * The call is ignored, if current event was not received from an atomic queue.
 *
 * Pseudo-code example:
 * @code
 *  receive_func(void* eo_ctx, em_event_t event, em_event_type_t type, em_queue_t queue, void* q_ctx);
 *  {
 *      if(is_my_atomic_queue(q_ctx))
 *      {
 *          update_sequence_number(event);  // this needs to be done atomically
 *          em_atomic_processing_end();
 *          ...                             // do other processing (potentially) in parallel
 *      }
 *   }
 * @endcode
 *
 * @see em_receive_func_t()
 */
void em_atomic_processing_end(void);

/**
 * Register the global error handler.
 *
 * The global error handler is called on errors (or em_error() calls)  
 * outside of any EO context or if there's no EO specific error 
 * handler registered. Note, the function will override any previously 
 * registered global error handler.
 *
 * @param handler       Error handler.
 *
 * @return EM_OK if successful.
 *
 * @see em_eo_register_error_handler(), em_unregister_error_handler(), em_error_handler_t()
 */
em_status_t em_register_error_handler(em_error_handler_t handler);

/**
 * Unregister the global error handler.
 *
 * Removes previously registered global error handler.
 *
 * @return EM_OK if successful.
 *
 * @see em_register_error_handler()
 */
em_status_t em_unregister_error_handler(void);

/**
 * Report an error.
 *
 * Reported errors are handled by the appropriate (EO specific or the global) error handler.
 *
 * Depending on the error/scope/implementation, the function call may not return.
 * 
 * @param error         Error code
 * @param escope        Error scope. Identifies the scope for interpreting the error code and variable arguments.
 * @param ...           Variable number and type of arguments
 * 
 * @see em_register_error_handler(), em_error_handler_t()
 */
void em_error(em_status_t error, em_escope_t escope, ...);




#ifdef __cplusplus
}
#endif

#endif // EVENT_MACHINE_H


