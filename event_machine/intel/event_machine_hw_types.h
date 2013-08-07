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
 * Event Machine HW specific types
 *
 */

#ifndef EVENT_MACHINE_HW_TYPES_H
#define EVENT_MACHINE_HW_TYPES_H



#ifdef __cplusplus
extern "C" {
#endif

/**
 * Event.
 *
 * In many implementations event can be a direct pointer to
 * a buffer of (coherent/shared) memory .
 */
typedef void* em_event_t;

/** Undefined event */
#define EM_EVENT_UNDEF (NULL)



/**
 * Event Machine run-time configuration options given at startup to em_init()
 * 
 * Content is copied into EM.
 * 
 * @note Several EM options are configured through compile-time defines.
 *       Run-time options allow using the same EM-lib with different configs.
 * 
 * @see em_init()
 */
typedef struct
{
  int em_instance_id;   /**< Event Machine Instance Id */
                        
  int pkt_io;           /**< Packet I/O: enable=1, disable=0 */
                        
  int evt_timer;        /**< Event Timer: enable=1, disable=0 */

  int process_per_core; /**< RunMode: EM run with one process per core */
  
  int thread_per_core;  /**< RunMode: EM run with one thread per core */
  
  int core_count;       /**< Number of EM-cores (== number of EM-threads or number of EM-processes) */
  
  int proc_idx;         /**< EM process index (thread-mode=0, process-mode=[0 ... core_count-1]) */

  em_core_mask_t phys_mask;
    
  /* Add further as needed. */
   
} em_conf_t;



#ifdef __cplusplus
}
#endif



#endif


