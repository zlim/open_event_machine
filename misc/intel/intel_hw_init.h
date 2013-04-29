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
 
#ifndef INTEL_HW_INIT_H
#define INTEL_HW_INIT_H


#include <stdint.h>
#include <rte_config.h>
#include "environment.h"

// em_event_pool:
#define MBUF_HDRS_SIZE   (sizeof(struct rte_mbuf) + RTE_PKTMBUF_HEADROOM)
#define MBUF_SIZE        (2048 + MBUF_HDRS_SIZE)

//#define NB_MBUF          (RTE_MAX_LCORE * 8192)
//#define MBUF_CACHE_SIZE  (1024)
//#define NB_MBUF          (524286)       // 2*3*3*3*7*19*73 = 512k - 2 (DPDK API suggests slightly smaller than 2^X to avoid wasting memory)
//#define NB_MBUF          ((256*1024)-1) // 3*3*3*7*19*73 = 256k - 1 (DPDK API suggests slightly smaller than 2^X to avoid wasting memory)
#define NB_MBUF          (295*3*7*73)     // between 256k and 512k  (DPDK API suggests slightly smaller than 2^X to avoid wasting memory)
#define MBUF_CACHE_SIZE  (1533)           // 3*7*73, chosen so that 'NB_MBUF%MBUF_CACHE_SIZE==0', according to DPDK API suggestion

COMPILE_TIME_ASSERT(MBUF_CACHE_SIZE <= RTE_MEMPOOL_CACHE_MAX_SIZE, CONFIGURED_MEMPOOL_CACHE_TOO_SMALL);
COMPILE_TIME_ASSERT((NB_MBUF % MBUF_CACHE_SIZE) == 0, CONFIGURED_MEMPOOL_CACHE_ERR);


#define SOCKET0          (0)
#define SOCKET1          (1)

#define DEVICE_SOCKET    (SOCKET0)




void *
intel_pool_init(void);




#endif // INTEL_HW_INIT_H


