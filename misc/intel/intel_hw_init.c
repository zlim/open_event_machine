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

#include "intel_hw_init.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <string.h>
#include <sys/queue.h>
#include <stdarg.h>
#include <errno.h>

#include <rte_byteorder.h>
#include <rte_log.h>
#include <rte_memory.h>
#include <rte_memzone.h>
#include <rte_eal.h>
#include <rte_per_lcore.h>
#include <rte_launch.h>
#include <rte_atomic.h>
#include <rte_cycles.h>
#include <rte_prefetch.h>
#include <rte_lcore.h>
#include <rte_per_lcore.h>
#include <rte_branch_prediction.h>
#include <rte_pci.h>
#include <rte_random.h>
#include <rte_debug.h>
#include <rte_ether.h>
#include <rte_ethdev.h>
#include <rte_ring.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_hash.h>
#include <rte_hash_crc.h>
#include <rte_spinlock.h>


#include "environment.h"




void *                
intel_pool_init(void)
{
  struct rte_mempool *pktmbuf_pool_S0 = NULL;
  
  /* create the memory pools for mbuf use, if NUMA use 2 sockets.  */
  /* create cache rings for performance so cores can get/put mbufs */
  /* without locks. Note that cores reading mbufs from a PCIe PMD  */
  /* use the same cache ring on the core, depleting it quicker     */
  /* The PMD for each interface allocates an equal number of mbufs */
  /* in a ring for the nm of Rx descriptors on the port. As pkts   */
  /* are received a new mbuf is allocated from the cache ring.     */
  /*                                                               */
  /* for load balance cores assume 2 ports with 128 Rx descriptors */
  /* Therefore 256 mbufs for Rx descriptors turning over as new    */
  /* packes arrive. Plus mbufs partition between worker cores in   */
  /* local rings until we have enough to move to worker cores      */
  /* n = 256 (RxD) +                                               */
  /* 8K x 2K m_bufs                                                */
  
  pktmbuf_pool_S0 =
    rte_mempool_create("mbuf_pool_0",
                       NB_MBUF,
                       MBUF_SIZE,
                       MBUF_CACHE_SIZE,
                       sizeof(struct rte_pktmbuf_pool_private),
                       rte_pktmbuf_pool_init,
                       NULL,
                       rte_pktmbuf_init,
                       NULL,
                       DEVICE_SOCKET,
                       0);
  
  if(pktmbuf_pool_S0 == NULL) {
    rte_panic("Cannot init mbuf pool on socket 0\n\n");
  }

#if 0
  if (numa_support) {
    pktmbuf_pool_S1 =
      rte_mempool_create("mbuf_pool_1", NB_MBUF,
             MBUF_SIZE, 512,
             sizeof(struct rte_pktmbuf_pool_private),
             rte_pktmbuf_pool_init, NULL,
             rte_pktmbuf_init, NULL,
             SOCKET1, 0);
    if (pktmbuf_pool_S1 == NULL)
      rte_panic("Cannot init mbuf pool on socket 1\n\n");
  }
#endif

  

  return pktmbuf_pool_S0;
}



