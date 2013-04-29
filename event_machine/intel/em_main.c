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
#include <errno.h>
#include <getopt.h>

#include <rte_memory.h>
#include <rte_memzone.h>
#include <rte_launch.h>
#include <rte_eal.h>
#include <rte_per_lcore.h>
#include <rte_lcore.h>
#include <rte_debug.h>


#include "event_machine.h"
#include "environment.h"
#include "em_intel.h"
#include "em_intel_sched.h"



/*
 * Defines
 */

#ifdef RTE_EXEC_ENV_BAREMETAL
  #define MAIN _main
#else
  #define MAIN main
#endif


/*
 * Macros
 */ 



/**
 * Local Function Prototypes
 */
int         MAIN(int argc, char *argv[]);
static int  main_loop(__attribute__((unused)) void *arg);

/* Wrappers to suit func-ptr interface */
static int  em_init_local__wrap(__attribute__((unused)) void *arg);
static int  em_app_init_local__wrap(__attribute__((unused)) void *arg);

static int  parse_args(int argc, char *argv[]);
static void print_usage(const char *prgname);

/*
 * Extern Function prototypes;
 */
extern void em_app_init_global(int app_argc, char *app_argv[]);
extern void em_app_init_local(void);




/*
 * MAIN-function - Executed ONLY on master lcore!
 */
int
MAIN(int argc, char *argv[])
{
  /*
   * MAIN only executed on MASTER-core
   */
  int          ret;
  unsigned     lcore_id;
  
  em_status_t  em_ret = EM_ERR;


  ret = rte_eal_init(argc, argv);
  if (ret < 0) {
    rte_panic("Cannot init EAL\n");
  }
  argc -= ret;
  argv += ret;


  /* Parse EM/application arguments (after the EAL ones) */
  ret = parse_args(argc, argv);
  if (ret < 0) {
    rte_panic("Cannot parse EM args\n");
  }
  /* Possible application parameters are common with EM, don't remove any more args */

  
  /*
   * GLOBAL EM initializations - only on master-lcore
   */
   
  em_ret = em_init_global();
  if(em_ret != EM_OK) {
    rte_panic("em_init_global() fails!\n");
  }
  
  env_sync_mem();  
  
  
  /* 
   * LOCAL EM initializations - on all lcores
   */
   
  RTE_LCORE_FOREACH_SLAVE(lcore_id) {
    rte_eal_remote_launch(em_init_local__wrap, NULL, lcore_id);
  }
  /* call em_init_local() on master lcore too */
  (void) em_init_local__wrap(NULL);

  rte_eal_mp_wait_lcore();
  
  
  env_sync_mem();
  


  /*
   * Print some info about the Env&HW
   */
  em_print_info();
  
  
  
  /* 
   * EM application init
   */
  
  /* call em_app_init_global only on the master-lcore */
  em_app_init_global(argc, argv);


  env_sync_mem();


  /* call em_init_local() on every slave lcore */
  RTE_LCORE_FOREACH_SLAVE(lcore_id) {
    rte_eal_remote_launch(em_app_init_local__wrap, NULL, lcore_id);
  }    
  /* call also on master lcore */
  (void) em_app_init_local__wrap(NULL);
  
  rte_eal_mp_wait_lcore();

  env_sync_mem();



  /*
   * Jump to main-loop on each core
   */

  RTE_LCORE_FOREACH_SLAVE(lcore_id) {
    rte_eal_remote_launch(main_loop, NULL, lcore_id);
  }    
  
  /* call also on master lcore */
  (void) main_loop(NULL);
  
  
  rte_panic("Never reached...\n");
  return 0;
}



/*
 * Main-Loop executed on each lcore
 */
static int
main_loop(__attribute__((unused)) void *arg)
{
  unsigned core_id;
  
  core_id = em_core_id();
  printf("Entering main_loop() on EM-core %u\n", core_id);
  
  
  /*
   * Schedule events to the core from queues
   */
  em_schedule();
  
  /* Never Return */
  
  
  // Never reached
  rte_panic("Error - exit main-loop on EM-core %u\n", core_id);
  return 0;
}



static int
em_init_local__wrap(__attribute__((unused)) void *arg)
{
  em_status_t stat;
  
  
  stat = em_init_local();
  
  if(stat != EM_OK) {
    exit(-1);
  }
  
  return 0;
}


/* Wrap em_app_init_local() to fit rte_eal_remote_launch(*f ...) func-ptr */
static int
em_app_init_local__wrap(__attribute__((unused)) void *arg)
{
  em_app_init_local();
  
  return 0;
}



/* Parse the arguments given on the command line of the application (exclude EAL args) */
static int
parse_args(int argc, char *argv[])
{
  /* Currently no EM specific command line args */
  return 0;
}



/* display usage */
static void
print_usage(const char *prgname)
{
  printf("%s [DPDK options] -- -x=VALUE [-q=VALUE]\n",
         prgname);
}
