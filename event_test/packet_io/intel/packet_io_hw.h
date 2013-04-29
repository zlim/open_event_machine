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
 
#ifndef PACKET_IO_HW_H
#define PACKET_IO_HW_H


#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
//#include <sys/types.h>
#include <string.h>
#include <sys/queue.h>
#include <errno.h>

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
#include <rte_byteorder.h>

#include "misc_packet.h"
#include "em_intel.h"
#include "em_intel_packet.h"



/*
 * Make sure Intel types match the generic types in misc_packet.h
 */
COMPILE_TIME_ASSERT(sizeof(struct ether_hdr) == sizeof(eth_hdr_t), ETH_HDR_SIZE_ERROR);
COMPILE_TIME_ASSERT(sizeof(struct ip_hdr)    == sizeof(ipv4_hdr_t), IP_HDR_SIZE_ERROR);
COMPILE_TIME_ASSERT(sizeof(struct udp_hdr)   == sizeof(udp_hdr_t), UDP_HDR_SIZE_ERROR);


/*
 * Defines
 */
#define  PAD_CACHE_LINE_MULT (9)




/**
 * Events:
 */




/*
 * Functions
 */

static inline void
packet_io_send(em_event_t event, const int ipd_port)
{
  em_eth_tx_packet(event, ipd_port);
}


static inline void
packet_io_free(em_event_t event)
{
  struct rte_mbuf *const m = event_to_mbuf(event);
  
  rte_pktmbuf_free(m);
}


static inline void
packet_io_send_and_free(em_event_t event, const int ipd_port)
{
  em_eth_tx_packet(event, ipd_port);
}



static inline int
packet_io_input_port(em_event_t event)
{
  em_event_hdr_t *const ev_hdr = event_to_event_hdr(event);
  
  return ev_hdr->io_port;
}


static inline void
packet_io_drop(em_event_t event)
{
  packet_io_free(event);
}


static inline void
packet_io_swap_addrs(em_event_t event)
{
  struct ether_hdr *const eth = (struct ether_hdr *) em_event_pointer(event);
  struct ip_hdr    *const ip  = (struct ip_hdr *)   ((unsigned char *)eth + sizeof(struct ether_hdr));
  ENV_PREFETCH(&ip->src_addr);
  struct udp_hdr   *const udp = (struct udp_hdr *)  ((unsigned char *)ip  + sizeof(struct ip_hdr));
  
  struct ether_addr tmp_eth_addr;
  uint32_t          tmp_ip_addr;
  uint16_t          tmp_udp_port;
  
  
  tmp_eth_addr = eth->s_addr;
  eth->s_addr  = eth->d_addr;  
  eth->d_addr  = tmp_eth_addr;  
  
  
  tmp_ip_addr  =  ip->src_addr;
  tmp_udp_port = udp->src_port;
    
   ip->src_addr =  ip->dst_addr;
  udp->src_port = udp->dst_port;

   ip->dst_addr = tmp_ip_addr;
  udp->dst_port = tmp_udp_port;
}



static inline void
packet_io_add_queue(uint8_t proto, uint32_t ipv4_dst, uint16_t port_dst, em_queue_t queue)
{
  em_packet_add_io_queue(proto, ipv4_dst, port_dst, queue);
}



static inline void
packet_io_rem_queue(uint8_t proto, uint32_t ipv4_dst, uint16_t port_dst, em_queue_t queue)
{
  em_packet_rem_io_queue(proto, ipv4_dst, port_dst, queue);
}



static inline int
packet_io_default_queue(em_queue_t queue)
{
  return em_packet_default_queue(queue);
}



static inline em_queue_t
packet_io_lookup_sw(uint8_t proto, uint32_t ipv4_dst, uint16_t port_dst)
{
  return em_packet_queue_lookup_sw(proto, ipv4_dst, port_dst);
}



static inline ipv4_hdr_t*
packet_io_ipv4_for_writing(em_event_t event)
{
  struct ether_hdr *const eth = (struct ether_hdr *) em_event_pointer(event);
  ipv4_hdr_t       *const ip  = (ipv4_hdr_t *)      ((unsigned char *)eth + sizeof(struct ether_hdr));
  
  return ip;
}



static inline ipv4_hdr_t*
packet_io_ipv4_for_read(em_event_t event)
{
  return packet_io_ipv4_for_writing(event);
}



static inline uint8_t*
packet_io_get_frame(em_event_t event)
{
  return (uint8_t *) em_event_pointer(event);
}



static inline uint32_t
packet_io_get_frame_len(em_event_t event)
{
  struct rte_mbuf *const m = event_to_mbuf(event);
  
  return rte_pktmbuf_data_len(m);
}



/*
static inline void
packet_io_set_frame_len(em_event_t event, uint32_t len)
{

}
*/



static inline em_event_t
packet_io_alloc(size_t len)
{
  return em_alloc(len, EM_EVENT_TYPE_PACKET, EM_POOL_DEFAULT);
}



static inline void
packet_io_copy_descriptor(em_event_t new_event, em_event_t event)
{
  struct rte_mbuf      *const m_new      = event_to_mbuf(new_event);
  struct rte_mbuf      *const m          = event_to_mbuf(event);
  em_event_hdr_t       *const new_ev_hdr = event_to_event_hdr(new_event);
  const em_event_hdr_t *const ev_hdr     = event_to_event_hdr(event);
  
  
  // Adjust the len of the new mbuf (NOTE: meant here only for newly allocated mbufs)
  rte_pktmbuf_append(m_new, rte_pktmbuf_data_len(m));
  // alloc has initialized the event header, copy only some fields
  new_ev_hdr->io_port     = ev_hdr->io_port;
}



static inline void
extract_payload(em_event_t event, uint8_t **payload__out, uint32_t *len__out)
{
  struct ether_hdr *const eth = (struct ether_hdr *) em_event_pointer(event);
  struct ip_hdr    *const ip  = (struct ip_hdr *)   ((unsigned char *)eth + sizeof(struct ether_hdr));
  struct udp_hdr   *const udp = (struct udp_hdr *)  ((unsigned char *)ip  + sizeof(struct ip_hdr));  
  
  
  *len__out     = rte_be_to_cpu_16(udp->dgram_len) - sizeof(struct udp_hdr);
  *payload__out = ((uint8_t *)udp) + sizeof(struct udp_hdr);
}



/*
 * Get the protocol, IPv4 destination address and destination port the packet-event was sent to.
 */
static inline void
packet_io_get_dst(em_event_t event, uint8_t *proto__out, uint32_t *ipv4_dst__out, uint16_t *port_dst__out)
{
  struct ether_hdr *const eth = (struct ether_hdr *) em_event_pointer(event);
  struct ip_hdr    *const ip  = (struct ip_hdr *)   ((unsigned char *)eth + sizeof(struct ether_hdr));
  struct udp_hdr   *const udp = (struct udp_hdr *)  ((unsigned char *)ip  + sizeof(struct ip_hdr));

  // ip->next_proto_id == INET_IPPROTO_UDP
  *proto__out    = ip->next_proto_id;
  *ipv4_dst__out = rte_be_to_cpu_32(ip->dst_addr);
  *port_dst__out = rte_be_to_cpu_16(udp->dst_port);
}



#endif // PACKET_IO_HW_H



