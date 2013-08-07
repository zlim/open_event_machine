/*
 *   Copyright (c) 2013, Nokia Siemens Networks
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
 * Event Machine Packet-I/O example initialization
 *
 */

#include "event_machine.h"
#include "environment.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <fcntl.h>
#include <sys/mman.h>
#include <signal.h>
#include <sys/prctl.h>


#include "packet_io.h"
#include "test_common.h"




/*
 * Local function prototypes
 */
static int
application_start(appl_conf_t *appl_conf);


/*
 * Functions
 */

/**
 * Packet-I/O example main function
 * 
 * Intel+DPDK specific - modify for other target envs
 *
 * Initialize the example application/env, create processes/threads for EM and launch the
 * EM-core specific init on each EM-core.
 */
int
main(int argc, char *argv[])
{
  em_conf_t    em_conf;
  appl_conf_t  appl_conf;
  em_status_t  em_status;
  int          ret;


  /*
   * Parse command line arguments & set config options
   * for both application and EM
   */
  (void) memset(&em_conf,   0, sizeof(em_conf));
  (void) memset(&appl_conf, 0, sizeof(appl_conf));

  parse_args(argc, argv, &em_conf, &appl_conf);



  /*
   * Set application specific EM-config
   */
  em_conf.pkt_io    = 1; // Packet-I/O:  disable=0, enable=1
  em_conf.evt_timer = 0; // Event-Timer: disable=0, enable=1  



  /*
   * Initialize the run-time environment.
   * EM process-per-core mode: Initialize the parent process before forking children.
   *                           Each EM-core is run in a separate process.
   * EM thread-per-core  mode: Initialze and create threads.
   *                           Each EM-core is run in a separate thread of the same process.
   */
  ret = init_rt_env(argc, argv, &em_conf, &appl_conf);
  if(ret < 0) {
    APPL_EXIT_FAILURE("init_rt_env() fails! ret=%i", ret);
  }
  
  
  
  /*
   * Initialize the Event Machine - called once per process
   */
  em_status = em_init(&em_conf);
  if(em_status != EM_OK) {
    APPL_EXIT_FAILURE("Cannot init the Event Machine!");
  }



  /*
   * Launch example on each EM core/thread
   */
  launch_application(application_start, &appl_conf);
  /* Never return */


  APPL_EXIT_FAILURE("Never reached...");

  return 0;
}



/**
 * Packet-I/O entry on each EM-core
 *
 * Application setup (test_init()) and event dispatch loop run by each EM-core.
 * A call to em_init_core() MUST be made on each EM-core before using other EM API
 * functions to create EOs, queues etc. or calling em_dispatch().
 *
 * @param appl_conf  Application configuration
 */
static int
application_start(appl_conf_t *appl_conf)
{
  em_status_t em_status;


  /*
   * Initialize this thread of execution (proc, thread or baremetal-core)
   */
  em_status = em_init_core();
  if(em_status != EM_OK)
  {
    APPL_EXIT_FAILURE("em_init_core() fails with status=0x%"PRIx32" on EM-core %i\n",
                 em_status, em_core_id());
  }


  /*
   * EM is ready on this EM-core (= proc, thread or core)
   * It is now OK to start creating EOs, queues etc.
   *
   * Note that only one core needs to create the shared memory, EO's, queues etc. needed by the application,
   * all other cores need only look up the shared mem and go directly into the em_dispatch()-loop,
   * where they are ready to process events as soon as the EOs have been started and queues enabled.
   */
  if(em_core_id() == 0)
  {
    test_init(appl_conf);
  }

  pthread_barrier_wait(&appl_conf->startup_sync->barrier);

  if(em_core_id() != 0)
  {
    test_init(appl_conf);
  }


  /*
   * Enter the EM event dispatch loop (0=ForEver) on this EM-core.
   */
  printf("Entering the event dispatch loop() on EM-core %i\n", em_core_id());

  for(;;) {
    em_dispatch(0);
  }


  /* Never reached */
  return 0;
}



