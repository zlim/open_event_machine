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

#include <fcntl.h>     /* For O_* constants */
#include <sys/stat.h>  /* For mode constants */
#include <semaphore.h>



#include <rte_debug.h>

#include "event_machine.h"
#include "environment.h"
#include "em_intel.h"
#include "em_error.h"
#include "em_shared_data.h"


/*
 * Defines
 */


/*
 * Macros
 */ 


/*
 * Local Data Types
 */

/** Per-process EM internal configuration (shared between threads) */
em_internal_conf_t  em_internal_conf  ENV_CACHE_LINE_ALIGNED;

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
 * @param conf   EM runtime config options
 *
 * @return EM_OK if successful.
 * 
 * @see em_init_core() for EM-core specific init after em_init().
 */
em_status_t
em_init(em_conf_t *conf)
{ 
  em_status_t          em_ret;
  sem_t               *sem;
  
  char em_shared_conf_name[RTE_MEMZONE_NAMESIZE];
  

  (void) memset(&em_internal_conf, 0, sizeof(em_internal_conf));
  
  /* Copy configuration contents */
  em_internal_conf.conf = *conf;

  /* Open named semaphore and initilize as 'free'(=1) */
  sem = sem_open("/em_sync_sem", O_CREAT, (S_IRUSR | S_IWUSR), 1);
  RETURN_ERROR_IF(sem == SEM_FAILED, EM_ERR_LIB_FAILED, EM_ESCOPE_INIT,
                  "sem_open(\"/em_sync_sem\") failed! errno(%i)=%s",
                  errno, strerror(errno));


  if(sem_wait(sem) == -1)
  {
    em_ret = EM_INTERNAL_ERROR(EM_ERR_LIB_FAILED, EM_ESCOPE_INIT,
               "sem_wait() failed! errno(%i)=%s", errno, strerror(errno));
    (void) sem_unlink("/em_sync_sem");
    return em_ret;
  }
  
  
  /* Set EM master conf name - append the instance id */
  (void) snprintf(em_shared_conf_name, sizeof(em_shared_conf_name), "EM-Shared-Conf-%i", conf->em_instance_id);
  em_shared_conf_name[RTE_MEMZONE_NAMESIZE-1] = '\0';
  
  
  /*
   * Create or lookup&attach to the em shared conf
   */
  em_internal_conf.shared = env_shared_lookup(em_shared_conf_name);
  
  if(em_internal_conf.shared == NULL)
  {
    em_internal_conf.shared = env_shared_reserve(em_shared_conf_name, sizeof(em_shared_conf_t));
    
    if(em_internal_conf.shared == NULL) 
    {
      em_ret = EM_INTERNAL_ERROR(EM_ERR_LIB_FAILED, EM_ESCOPE_INIT,
                 "env_shared_reserve(%s, %zu) returns NULL!\n", em_shared_conf_name, sizeof(em_shared_conf_t));
      (void) sem_unlink("/em_sync_sem");
      return em_ret;
    }
    
    /* Create barrier to synchronize calls to em_init_global() later */
    env_barrier_init(&em_internal_conf.shared->barrier, env_core_mask_count(conf->core_count));
    
    /* Store the EM-instance ID */                                      
    em_internal_conf.shared->em_instance_id = conf->em_instance_id;
  }


  if(sem_post(sem) == -1) 
  {
    em_ret = EM_INTERNAL_ERROR(EM_ERR_LIB_FAILED, EM_ESCOPE_INIT,
               "sem_post() failed! errno(%i)=%s", errno, strerror(errno));
    (void) sem_unlink("/em_sync_sem");
    return em_ret;
  }
  
  
  /*
   * Global internal EM-config 
   */
  if(em_internal_conf.conf.process_per_core)
  {
    /*
     * Process-per-core:
     * Use barrier to ensure that EM-core0 first enters em_init_global().
     * (use proc_idx instead of EM-core-id since EM ids are not set up yet).
     */
    if(conf->proc_idx == 0)
    {
      /* Run first on EM-core0 */
      em_ret = em_init_global(&em_internal_conf);
    }

    
    env_barrier_sync(&em_internal_conf.shared->barrier);

    
    if(conf->proc_idx != 0) 
    {
      if(sem_wait(sem) == -1)
      {
        em_ret = EM_INTERNAL_ERROR(EM_ERR_LIB_FAILED, EM_ESCOPE_INIT,
                   "sem_wait() failed! errno(%i)=%s", errno, strerror(errno));
        (void) sem_unlink("/em_sync_sem");
        return em_ret;
      }
      
      /* Run on each EM-core (0 already run) */
      em_ret = em_init_global(&em_internal_conf);

      if(sem_post(sem) == -1) 
      {
        em_ret = EM_INTERNAL_ERROR(EM_ERR_LIB_FAILED, EM_ESCOPE_INIT,
                   "sem_post() failed! errno(%i)=%s", errno, strerror(errno));
        (void) sem_unlink("/em_sync_sem");
        return em_ret;
      }
    }
    
    
    env_barrier_sync(&em_internal_conf.shared->barrier);
  }
  else
  {
    /* 
     * Thread-per-core:
     * Don't use barriers here since we might run only one thread -> deadlock.
     * Note: em_init() is called once per process and not once per thread.
     */
    em_ret = em_init_global(&em_internal_conf);
  }
 
  
  (void) sem_close(sem);
  (void) sem_unlink("/em_sync_sem");
  
  RETURN_ERROR_IF(em_ret != EM_OK, em_ret, EM_ESCOPE_INIT,
                  "Internal em_init_global() fails!");
  
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




