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
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <rte_debug.h>

#include "event_machine.h"
#include "environment.h"
#include "em_intel.h"
#include "em_error.h"


/*
 * Defines
 */


/*
 * Macros
 */ 


/*
 * Local Data Types
 */

ENV_SHARED  em_internal_conf_t  em_internal_conf  ENV_CACHE_LINE_ALIGNED;

COMPILE_TIME_ASSERT((sizeof(em_internal_conf) % ENV_CACHE_LINE_SIZE) == 0, EM_INIT_SIZE_ERROR);



/**
 * Local Function Prototypes
 */

/* - */



/**
 * Initialize the Event Machine.
 *
 * Called once at startup (per process). Additionally each EM-core needs to call the 
 * em_init_core() function before using any further EM API functions/resources.
 *
 * @param argc   Command line argument count (if any)
 * @param argv   Command line arguments (if any) 
 * @param conf   EM runtime config options
 *
 * @return EM_OK if successful.
 * 
 * @see em_init_core() for EM-core specific init after em_init().
 */
em_status_t
em_init(em_conf_t *conf)
{ 
  em_status_t em_ret;
 
  
  (void) memset(&em_internal_conf, 0, sizeof(em_internal_conf));
  
  /* Copy configuration contents */
  em_internal_conf.conf = *conf;
  
  /* Global internal EM-config */
  em_ret = em_init_global(&em_internal_conf);
  
  if(em_ret != EM_OK)
  {
    // Call rte_panic() directly - the EM error handler might not be properly initialized yet.
    rte_panic("%s(): Internal em_init_global() fails! retval=%u\n",
              __func__, em_ret);
  }
  
  return em_ret;
}



/**
 * Initialize an EM-core.
 *
 * Called by each EM-core (= process, thread or baremetal core). 
 * EM queues, EOs, queue groups etc. can be created after a succesful return from this function.
 *
 * @return EM_OK if successful.
 *
 * @see em_init()
 */
em_status_t
em_init_core(void)
{
  /*
   * Init executed on all running cores
   */
  em_status_t em_ret;

  
  
  /* 
   * LOCAL EM initializations - on all lcores
   */
  em_ret = em_init_local(&em_internal_conf);
  
  RETURN_ERROR_IF(em_ret != EM_OK, EM_FATAL(em_ret), EM_ESCOPE_INIT_CORE,
                  "Internal em_init_local() fails on core%i",
                  em_core_id());
   

  /*
   * Print some info about the Env&HW
   */
  if(em_core_id() == 0) {
    em_print_info();
  }
  

  env_sync_mem();
  
  
  return em_ret;
}




