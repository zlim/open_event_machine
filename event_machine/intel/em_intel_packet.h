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

#ifndef EM_INTEL_PACKET__H
#define EM_INTEL_PACKET__H


#include <stdint.h>
#include "environment.h"
#include "event_machine_types.h"


/**
 * Eth Rx Direct Dispatch: if enabled (=1) will try to dispatch the input Eth Rx event
 * for processing as fast as possible by bypassing the event scheduler queues.
 * Direct dispatch will, however, preserve packet order for each flow and for atomic
 * flows/queues also the atomic context is maintained.
 * Directly dispatching an event reduces the number of enqueue+dequeue operations and keeps the 
 * event processing on the same core as it was received on, thus giving better performance.
 * Event priority handling is weakened by enabling direct dispatch as newer events
 * can be dipatched before older events (of another flow) that are enqueued in the scheduling
 * queues - this is the reason why it has been set to '0' by default. An application that do
 * not care about strict priority cound significantly benefit from enabling this feature.
 */
#define RX_DIRECT_DISPATCH       (0) // 0=Off(lower performance,  better priority handling)
                                     // 1=On (better performance, weaker priority)

                                   
#define MAX_ETH_PORTS            (16)


#define MAX_ETH_RX_QUEUES        (MAX_ETH_PORTS * MAX_CORES)


#if RX_DIRECT_DISPATCH == 1
  #define MAX_ETH_TX_MBUF_TABLES (16) // Keep power-of-two!
#else
  #define MAX_ETH_TX_MBUF_TABLES (32) // Keep power-of-two!
#endif

#define TX_MBUF_TABLE_MASK       (MAX_ETH_TX_MBUF_TABLES - 1)


#define EM_QUEUE_TO_MBUF_TBL(em_queue_id) ((em_queue_id) & (TX_MBUF_TABLE_MASK))



/**< IP version 4 Packet Header */
#define INET_IPPROTO_TCP 6  /**< Transmission Control Protocol. */
#define INET_IPPROTO_UDP 17 /**< User Datagram Protocol. */


#if 0
#define DEBUG_PRINT(...)     {printf("%s(L:%u)  ", __func__, __LINE__); \
                              printf(__VA_ARGS__); printf("\n"); fflush(NULL);}
#else
#define DEBUG_PRINT(...)   
#endif


/**< IP Header */
struct ip_hdr {
  uint8_t  version_ihl;
  uint8_t  type_of_service;
  uint16_t total_length;
  uint16_t packet_id;
  uint16_t fragment_offset;
  uint8_t  time_to_live;
  uint8_t  next_proto_id;
  uint16_t hdr_checksum;
  uint32_t src_addr;
  uint32_t dst_addr;
} __attribute__((__packed__));


/**< UDP Header */
struct udp_hdr {
  uint16_t src_port;    /**< UDP source port. */
  uint16_t dst_port;    /**< UDP destination port. */
  uint16_t dgram_len;   /**< UDP datagram length */
  uint16_t dgram_cksum; /**< UDP datagram checksum */
} __attribute__((__packed__));


/**< TCP Header */
struct tcp_hdr {
  uint16_t src_port;  /**< TCP source port. */
  uint16_t dst_port;  /**< TCP destination port. */
  uint32_t sent_seq;  /**< TX data sequence number. */
  uint32_t recv_ack;  /**< RX data acknowledgement sequence number. */
  uint8_t  data_off;  /**< Data offset. */
  uint8_t  tcp_flags; /**< TCP flags */
  uint16_t rx_win;    /**< RX flow control window. */
  uint16_t seg_sum;   /**< TCP checksum. */
  uint16_t tcp_urp;   /**< TCP urgent pointer, if any. */
} __attribute__((__packed__));



typedef struct packet_q_hash_
{
  struct rte_hash  *hash  ENV_CACHE_LINE_ALIGNED;
  
  env_spinlock_t    lock  ENV_CACHE_LINE_ALIGNED; // Some hash-management functions are not multicore-safe and require locking
  
} packet_q_hash_t;

COMPILE_TIME_ASSERT(sizeof(packet_q_hash_t) == (ENV_CACHE_LINE_SIZE * 2), PACKET_Q_HASH_ALIGNMENT_ERROR);



typedef struct
{
  int           n_link_up    ENV_CACHE_LINE_ALIGNED;
  int           n_rx_queues; // TOTAL AMOUNT
  int           n_tx_queues; // TOTAL AMOUNT
  
  struct
  {
    uint16_t     portid;
    uint16_t     n_rx_queue;
    uint16_t     n_tx_queue;
  } port[MAX_ETH_PORTS];
  
} eth_ports_link_up_t;

COMPILE_TIME_ASSERT(sizeof(eth_ports_link_up_t) == (ENV_CACHE_LINE_SIZE * 2), ETH_PORTS_LINK_UP_SIZE_ERROR);




struct packet_dst_tuple
{
    // uint32_t ip_src;
  uint32_t ip_dst;
  
  // uint16_t port_src;
  uint16_t port_dst;
  
  uint16_t  proto;
} __attribute__((__packed__));

/* Use the struct packet_dst_tuple as hash key for EM-queue lookups */
typedef  struct packet_dst_tuple  packet_q_hash_key_t;

/* Keep size multiple of 32-bits for faster hash-crc32 calculation*/
COMPILE_TIME_ASSERT((sizeof(packet_q_hash_key_t) % sizeof(uint32_t)) == 0, HASH_KEY_NOT_MULTIP_OF_32__ERROR);




void
intel_eth_init(void);

void
intel_eth_init_local(void);


void
intel_init_packet_q_hash_global(void);

void
intel_init_packet_q_hash_local(void);

void
em_eth_rx_packets(void);

void
em_eth_tx_packet(em_event_t event, const int port);

void
em_eth_tx_packets_timed(void);

void 
eth_tx_packet__ordered(void *mbuf, int port, uint16_t tx_queueid);

int
em_packet_default_queue(em_queue_t queue);

void
em_packet_add_io_queue(uint8_t proto, uint32_t ipv4_dst, uint16_t port_dst, em_queue_t queue);

void
em_packet_rem_io_queue(uint8_t proto, uint32_t ipv4_dst, uint16_t port_dst, em_queue_t queue);

em_queue_t
em_packet_queue_lookup_sw(uint8_t proto, uint32_t ipv4_dst, uint16_t port_dst);

#endif  // EM_INTEL_PACKET__H

