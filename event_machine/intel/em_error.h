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
 * Event Machine Error Handler
 *
 */

#ifndef EM_ERROR_H
#define EM_ERROR_H

#include "environment.h"




/**
 * Internal error reporting macro
 */
#define EM_INTERNAL_ERROR(error, escope, format, ...)  _em_internal_error((error), (escope), __FILE__, __func__, __LINE__, (format), ## __VA_ARGS__)




/**
 * Usage: 
 * 1) ERROR_IF(...);
 *
 * 2) ERROR_IF(...){
 *      ...
 *    } 
 *    else {
 *      ...
 *    }
 */
#define ERROR_IF(cond, error, escope, format, ...)                         \
  IF_UNLIKELY(em_core_local.error_cond = (cond))                           \
  {                                                                        \
    const int tmp = em_core_local.error_cond;                              \
                                                                           \
    (void) EM_INTERNAL_ERROR((error), (escope), (format), ## __VA_ARGS__); \
                                                                           \
    em_core_local.error_cond = tmp;                                        \
  }                                                                        \
  IF_UNLIKELY(em_core_local.error_cond)




/**
 * Internal macro for return on error
 */
#define RETURN_ERROR_IF(cond, error, escope, format, ...)                  \
  IF_UNLIKELY((cond)) {                                                    \
    return EM_INTERNAL_ERROR((error), (escope), (format), ## __VA_ARGS__); \
  }




/**
 * EM Error Handler
 */
 
typedef union
{
  struct 
  {
    // Global Error Handler (func ptr or NULL)
    em_error_handler_t  em_error_handler  ENV_CACHE_LINE_ALIGNED;
    
    // Global Error Count
    rte_atomic64_t      global_error_count;
    
    // Spinlock
    env_spinlock_t      lock;
  };
  
  // Guarantees that size is 1*cache-line-size
  uint8_t u8[ENV_CACHE_LINE_SIZE];

} em_error_handler_aligned_t;


COMPILE_TIME_ASSERT(sizeof(em_error_handler_aligned_t) == ENV_CACHE_LINE_SIZE, EM_ERROR_HANDER_ALIGNED_T_SIZE_ERROR);




/**
 * EM internal error 
 */
 
/* Don't call _em_internal_error() directly, should _always_ be used from within the
 * EM_INTERNAL_ERROR(), ERROR_IF() or RETURN_ERROR_IF() macros
 */ 
em_status_t                                                                                              
_em_internal_error(em_status_t error, em_escope_t escope, ...);




void em_error_init(void);

void
em_error_init_secondary(void);



#endif

