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
 * Event Machine basic types
 */

#ifndef EVENT_MACHINE_TYPES_H_
#define EVENT_MACHINE_TYPES_H_

#ifdef __cplusplus
extern "C" {
#endif


/* Basic definitions */
#include <stddef.h>

/* Basic integer types */
#include <inttypes.h>

/* Variable arguments */
#include <stdarg.h>



/**
 * Event type. This is given to application for each received
 * event and also needed for event allocation.
 * It's an integer, but split into major and minor part.
 * major-field categorizes the event and minor is
 * more detailed system specific description.
 * Major-part will not change by HW, but minor can be HW/SW platform
 * specific and thus could be split into more sub-fields as needed.
 * Application should use the access functions for reading major 
 * and minor part.
 *
 * The only event type with defined content is EM_EVENT_TYPE_SW with
 * minor type 0, which needs to be portable (direct pointer to data).
 *
 * @see em_get_type_major(), em_get_type_minor(), em_receive_func_t()
 */
typedef uint32_t em_event_type_t;



/* HW specific configuration */
#include <event_machine_hw_config.h>





#ifdef EM_64_BIT

/*
 * Optimize EM for 64 bit architecture
 * =====================================
 */

/**
 *
 * @page page_version 64 bit version
 * This is documentation represent the 64 bit version of Event Machine API.
 * Define EM_64_BIT or EM_32_BIT to select between 64 and 32 bit versions.
 *
 */


/*
 * Printf formats
 */
#define PRI_EO      PRIu64                      /**< em_eo_t printf format */
#define PRI_QUEUE   PRIu64                      /**< em_queue_t printf format */
#define PRI_QGRP    PRIu64                      /**< em_queue_group_t printf format */



/**
 * Execution Object identifier
 *
 * @see em_eo_create() 
 */
typedef uint64_t em_eo_t;
#define EM_EO_UNDEF             EM_UNDEF_U64    /**< Invalid EO id */

/**
 * Queue identifier
 *
 * @see em_queue_create(), em_receive_func_t(), em_send()
 */
typedef uint64_t em_queue_t;
#define EM_QUEUE_UNDEF          EM_UNDEF_U64    /**< Invalid queue */


/**
 * Queue group identifier
 *
 * Each queue belongs to one queue group, that defines a core mask for scheduling
 * events, i.e. define which cores participate in the load balancing. Group can
 * also allow only a single core for no load balancing.
 *
 * Groups needs to be created as needed. One default group (EM_QUEUE_GROUP_DEFAULT)
 * always exists, and that allows scheduling to all the cores running this execution
 * binary instance.
 *
 * @see em_queue_group_create()
 */
typedef uint64_t em_queue_group_t;
#define EM_QUEUE_GROUP_UNDEF    EM_UNDEF_U64    /**< Invalid queue group */



#elif defined(EM_32_BIT)
/*
 * Optimize EM for 32 bit architecture
 * =====================================
 */

/**
 *
 * @page page_version 32 bit version
 * This is documentation represent the 32 bit version of Event Machine API.
 * Define EM_64_BIT or EM_32_BIT to select between 64 and 32 bit versions.
 *
 */


/* ************* 32 bit ************************ */


#define PRI_EO       PRIu32                     /**< em_eo_t printf format */         
#define PRI_QUEUE    PRIu32                     /**< em_queue_t printf format */      
#define PRI_QGRP     PRIu32                     /**< em_queue_group_t printf format */


/**
 * Execution Object identifier
 *
 * @see em_eo_create() 
 */
typedef uint32_t em_eo_t;
#define EM_EO_UNDEF      EM_UNDEF_U32           /**< Invalid EO id */

/**
 * Queue identifier
 *
 * @see em_queue_create(), em_receive_func_t(), em_send()
 */
typedef uint32_t em_queue_t;
#define EM_QUEUE_UNDEF   EM_UNDEF_U32           /**< Invalid queue */

/**
 * Queue group identifier
 *
 * Each queue belongs to one queue group, that defines a core mask for scheduling
 * events, i.e. define which cores participate in the load balancing. Group can
 * also allow only a single core for no load balancing.
 *
 * Groups needs to be created as needed. One default group (EM_QUEUE_GROUP_DEFAULT)
 * always exists, and that allows scheduling to all the cores running this execution
 * binary instance.
 *
 * @see em_queue_group_create()
 */
typedef uint32_t em_queue_group_t;
#define EM_QUEUE_GROUP_UNDEF     EM_UNDEF_U32   /**< Invalid queue group id */




#else

  #error Missing architecture definition. Define EM_64_BIT or EM_32_BIT!

  /**
   *
   * @page page_version 64/32 bit version not selected
   * This is documentation has not selected between 64/32 bit version of Event Machine API.
   * Some types might be missing. Define EM_64_BIT or EM_32_BIT to select between 64 and 32 bit versions.
   *
   */
   
#endif



/**
 * Error/Status code.
 * EM_OK (0) is the general code for success, other values
 * describe failed operation.
 * 
 * @see event_machine_hw_config.h, em_error_handler_t(), em_error()
 */
typedef uint32_t em_status_t;

#define EM_OK    0           /**< Operation successful */
#define EM_ERROR 0xffffffff  /**< Operation not successful. Generic error code, other error codes are system specific. */


/**
 * Error scope.
 *
 * Identifies the error scope for interpreting error codes and variable arguments.
 * 
 * @see em_error_handler_t(), em_error()
 */
typedef uint32_t em_escope_t;



/**
 * All EM internal error scopes should have bit 31 set
 * NOTE: High bit is RESERVED for EM internal escopes and should not be
 * used by the application.
 */
#define EM_ESCOPE_BIT                  (0x80000000u)


/**
 * Test if the error scope identifies an EM function (API or other internal)
 */
#define EM_ESCOPE(escope)              (EM_ESCOPE_BIT & (escope))


/**
 * Mask selects the high byte of the 32-bit escope
 */
#define EM_ESCOPE_MASK                 (0xFF000000)


/**
 * EM API functions error scope & mask 
 */
#define EM_ESCOPE_API_TYPE             (0xFFu)
#define EM_ESCOPE_API_MASK             (EM_ESCOPE_BIT | (EM_ESCOPE_API_TYPE << 24))


/**
 * Test if the error scope identifies an EM API function
 */
#define EM_ESCOPE_API(escope)          (((escope) & EM_ESCOPE_MASK) == EM_ESCOPE_API_MASK)


/*
 * EM API functions error scopes
 */
#define EM_ESCOPE_QUEUE_CREATE                    (EM_ESCOPE_API_MASK | 0x0001)
#define EM_ESCOPE_QUEUE_CREATE_STATIC             (EM_ESCOPE_API_MASK | 0x0002)
#define EM_ESCOPE_QUEUE_DELETE                    (EM_ESCOPE_API_MASK | 0x0003)
#define EM_ESCOPE_QUEUE_ENABLE                    (EM_ESCOPE_API_MASK | 0x0004)
#define EM_ESCOPE_QUEUE_ENABLE_ALL                (EM_ESCOPE_API_MASK | 0x0005)
#define EM_ESCOPE_QUEUE_DISABLE                   (EM_ESCOPE_API_MASK | 0x0006)
#define EM_ESCOPE_QUEUE_DISABLE_ALL               (EM_ESCOPE_API_MASK | 0x0007)
#define EM_ESCOPE_QUEUE_SET_CONTEXT               (EM_ESCOPE_API_MASK | 0x0008)
#define EM_ESCOPE_QUEUE_GET_CONTEXT               (EM_ESCOPE_API_MASK | 0x0009)
#define EM_ESCOPE_QUEUE_GET_NAME                  (EM_ESCOPE_API_MASK | 0x000A)
#define EM_ESCOPE_QUEUE_GET_PRIORITY              (EM_ESCOPE_API_MASK | 0x000B)
#define EM_ESCOPE_QUEUE_GET_TYPE                  (EM_ESCOPE_API_MASK | 0x000C)
#define EM_ESCOPE_QUEUE_GET_GROUP                 (EM_ESCOPE_API_MASK | 0x000D)

#define EM_ESCOPE_QUEUE_GROUP_CREATE              (EM_ESCOPE_API_MASK | 0x0101)
#define EM_ESCOPE_QUEUE_GROUP_DELETE              (EM_ESCOPE_API_MASK | 0x0102)
#define EM_ESCOPE_QUEUE_GROUP_MODIFY              (EM_ESCOPE_API_MASK | 0x0103)
#define EM_ESCOPE_QUEUE_GROUP_FIND                (EM_ESCOPE_API_MASK | 0x0104)
#define EM_ESCOPE_QUEUE_GROUP_MASK                (EM_ESCOPE_API_MASK | 0x0105)

#define EM_ESCOPE_EO_CREATE                       (EM_ESCOPE_API_MASK | 0x0201)
#define EM_ESCOPE_EO_DELETE                       (EM_ESCOPE_API_MASK | 0x0202)
#define EM_ESCOPE_EO_GET_NAME                     (EM_ESCOPE_API_MASK | 0x0203)
#define EM_ESCOPE_EO_ADD_QUEUE                    (EM_ESCOPE_API_MASK | 0x0204)
#define EM_ESCOPE_EO_REMOVE_QUEUE                 (EM_ESCOPE_API_MASK | 0x0205)
#define EM_ESCOPE_EO_REGISTER_ERROR_HANDLER       (EM_ESCOPE_API_MASK | 0x0206)
#define EM_ESCOPE_EO_UNREGISTER_ERROR_HANDLER     (EM_ESCOPE_API_MASK | 0x0207)
#define EM_ESCOPE_EO_START                        (EM_ESCOPE_API_MASK | 0x0208)
#define EM_ESCOPE_EO_STOP                         (EM_ESCOPE_API_MASK | 0x0209)

#define EM_ESCOPE_CORE_ID                         (EM_ESCOPE_API_MASK | 0x0301)
#define EM_ESCOPE_CORE_COUNT                      (EM_ESCOPE_API_MASK | 0x0302)

#define EM_ESCOPE_ALLOC                           (EM_ESCOPE_API_MASK | 0x0401)
#define EM_ESCOPE_FREE                            (EM_ESCOPE_API_MASK | 0x0402)
#define EM_ESCOPE_SEND                            (EM_ESCOPE_API_MASK | 0x0403)
#define EM_ESCOPE_ATOMIC_PROCESSING_END           (EM_ESCOPE_API_MASK | 0x0404)

#define EM_ESCOPE_REGISTER_ERROR_HANDLER          (EM_ESCOPE_API_MASK | 0x0501)
#define EM_ESCOPE_UNREGISTER_ERROR_HANDLER        (EM_ESCOPE_API_MASK | 0x0502)
#define EM_ESCOPE_ERROR                           (EM_ESCOPE_API_MASK | 0x0503)


/**
 * Queue type.
 *
 * Affects the scheduling principle
 *
 * @see em_queue_create(), event_machine_hw_config.h
 */
typedef uint32_t em_queue_type_t;


/**
 * Queue priority class
 *
 * Queue priority defines a system dependent QoS class, not just an absolute
 * priority. EM gives freedom to implement the actual scheduling disciplines
 * and the corresponding numeric values as needed, i.e. the actual values are
 * system dependent and thus not portable, but the 5 pre-defined enums
 * (em_queue_prio_e) are always valid.
 * Application platform or middleware needs to define and distribute the
 * other available values.
 *
 * @see em_queue_create(), event_machine_hw_config.h
 */
typedef uint32_t em_queue_prio_t;





/**
 * Memory pool id.
 *
 * Defines memory pool in em_alloc(). Default pool id is defined by
 * EM_POOL_DEFAULT.
 *
 * @see em_alloc(), event_machine_hw_config.h
 */
typedef uint32_t em_pool_id_t;



/**
 * Size of the core mask in bits
 */
#define EM_CORE_MASK_SIZE  64


/**
 * Type for queue group core mask.
 * Each bit represents one core, core 0 is the lsb (1 << em_core_id())
 * Note, that EM will enumerate the core identifiers to always start from 0 and
 * be contiguous meaning the core numbers are not necessarily physical.
 * This type can handle up to 64 cores.
 *
 * Use the functions in event_machine_core_mask.h to manipulate the core masks.
 * 
 * @see em_queue_group_create()
 */
typedef union em_core_mask_t em_core_mask_t;

union em_core_mask_t
{
  uint8_t   u8[EM_CORE_MASK_SIZE / 8];
  uint16_t u16[EM_CORE_MASK_SIZE / 16];
  uint32_t u32[EM_CORE_MASK_SIZE / 32];
  uint64_t u64[EM_CORE_MASK_SIZE / 64];
};





#ifdef __cplusplus
}
#endif

#endif /* EVENT_MACHINE_TYPES_H_ */

