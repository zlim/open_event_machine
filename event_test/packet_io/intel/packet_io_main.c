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

#include "packet_io.h"



/**
 * Local Function Prototypes
 */
static int
packet_io_start(packet_io_conf_t *packet_io_conf);

static void
parse_args(int argc, char   *argv[],
           em_conf_t        *em_conf      /* out param */,
           packet_io_conf_t *packet_io_conf /* out param */);



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
  int              ret;
  unsigned         lcore_id;
  em_status_t      em_status;
  em_conf_t        em_conf;
  packet_io_conf_t packet_io_conf;

  
  /*
   * Initialize the environment - here DPDK.
   */
  ret = rte_eal_init(argc, argv);
  if(ret < 0) {
    rte_panic("Cannot init the Intel DPDK EAL\n");
  }
  argc -= ret;
  argv += ret;
  

  /*
   * Parse command line arguments & set config options 
   * for both application and EM.
   */
  (void) memset(&packet_io_conf, 0, sizeof(packet_io_conf));
  (void) memset(&em_conf,        0, sizeof(em_conf));
  
  parse_args(argc, argv, &em_conf, &packet_io_conf);
  
  
  /*
   * Initialize the Event Machine
   */
  em_status = em_init(&em_conf);
  if(em_status != EM_OK) {
    rte_panic("Cannot init the Event Machine!\n");
  }
  
  
  /*
   * Launch example on each core/thread
   */ 
  RTE_LCORE_FOREACH_SLAVE(lcore_id) {
    rte_eal_remote_launch((int (*)(void *))packet_io_start, &packet_io_conf, lcore_id);
  }     
  /* call also on master lcore */
  (void) packet_io_start(&packet_io_conf);
 
 
  rte_panic("Never reached...\n");
  
  return 0;
}



/**
 * Packet-I/O entry on each EM-core
 *
 * Application setup (test_init()) and event dispatch loop run by each EM-core.
 * A call to em_init_core() MUST be made on each EM-core before using other EM API
 * functions to create EOs, queues etc. or calling em_dispatch().
 *
 * @param packet_io_conf  Packet-I/O application configuration
 */
static int
packet_io_start(packet_io_conf_t *packet_io_conf)
{
  em_status_t em_status;
  
  
  /*
   * Initialize this thread of execution (proc, thread or baremetal-core)
   */
  em_status = em_init_core();
  if(em_status != EM_OK)
  {
    printf("em_init_core() fails with status=%u on EM-core %i\n", em_status, em_core_id());
    exit(1);
  }
  
  
  /* 
   * EM is ready on this EM-core (= proc, thread or core)
   * It is now OK to start creating EOs, queues etc.
   *
   * Note that only one core needs to create the EO's, queues etc. needed by the application,
   * all other cores go directly into the em_dispatch()-loop, where they are ready to
   * process events as soon as the EOs have been started and queues enabled.
   */
  if(em_core_id() == 0)
  {
    test_init(packet_io_conf);
  }
  
  
  /*
   * Enter the EM event dispatch loop (0=ForEver) on this EM-core.
   */
  printf("Entering event dispatch loop() on EM-core %i\n", em_core_id());
  
  for(;;) {
    em_dispatch(0);
  }
 
  
  /* Never reached */
  return 0;
}



/** 
 * Parse and store relevant command line arguments. Set config options for both
 * application and EM.
 * 
 * EM options are stored into em_conf and application specific options into appl_conf.
 * Note that both application and EM parsing is done here since EM should not, by design,
 * be concerned with the parsing of options, instead em_conf_t specifies the options
 * needed by the EM-implementation (HW, device and env specific).
 *
 * @param argc            Command line argument count
 * @param argv[]          Command line arguments
 * @param em_conf         EM config options parsed from argv[]
 * @param packet_io_conf  Application config options parsed from argv[]
 */
static void
parse_args(int argc, char   *argv[],
           em_conf_t        *em_conf        /* out param */,
           packet_io_conf_t *packet_io_conf /* out param */)
{
  /*
   * Set application specific config
   */
  if(argc > 0)
  {
    (void) strncpy(packet_io_conf->name, NO_PATH(argv[0]), PACKET_IO_NAME_LEN);
    packet_io_conf->name[PACKET_IO_NAME_LEN-1] = '\0'; // always '\0'-terminate string
  }

  packet_io_conf->num_procs   = 1;
  packet_io_conf->num_threads = env_get_core_count(); // Cannot use em_core_count() since em_init() has not been called yet!
  
  
  /*
   * Set application specific EM-config
   */
  em_conf->em_instance_id = 0; // Currently only support one (1) EM-instance
  em_conf->pkt_io         = 1; // Packet-I/O:  disable=0, ENABLE=1 
  em_conf->evt_timer      = 0; // Event-Timer: disable=0, enable=1 
  
  
  // No other command line arguments to be parsed 
}


