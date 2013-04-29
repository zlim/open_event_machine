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

#ifndef MISC_PACKET_H
#define MISC_PACKET_H


#define ETH_TYPE_IP       0x0800
#define ETH_TYPE_ARP      0x0806

#define IPV4_VER_HLEN       0x45

#define IPV4_PROTO_ICMP        1
#define IPV4_PROTO_UDP        17

#define ICMP_TYPE_ECHO_REQ     8
#define ICMP_TYPE_ECHO_REPLY   0

#define ARP_OPCODE_REQ         1
#define ARP_OPCODE_REPLY       2



typedef union
{
  uint8_t  u8[6];
  uint16_t u16[3];
  
} mac_addr_t;



typedef union
{
  uint64_t u64;

  struct
  {
    uint8_t reserved[2];
    mac_addr_t mac_addr;
  }s;
 
} mac_addr_u64_t;



typedef union
{
  uint8_t  u8[4];
  uint16_t u16[2];
  uint32_t u32;

} ipv4_addr_t;



// Ethernet header
typedef struct
{
  mac_addr_t dst;
  mac_addr_t src;
  uint16_t   type;
  uint8_t    data[];

} eth_hdr_t;



// IPv4 header
typedef struct
{
  uint8_t      ver_hlen;
  uint8_t      tos;
  uint16_t     len;
  uint16_t     id;
  uint16_t     frag;
  uint8_t      ttl;
  uint8_t      proto;
  uint16_t     chksum;
  ipv4_addr_t  src;
  ipv4_addr_t  dst;
  uint8_t      data[];

} ipv4_hdr_t;



// UDP header
typedef struct
{
  uint16_t src;
  uint16_t dst;
  uint16_t len;
  uint16_t chksum;
  uint8_t  data[];  // UDP data 64 bits aligned in memory.

} udp_hdr_t;



// ARP header
typedef struct
{
  uint16_t    hw_type;
  uint16_t    prot_type;
  uint8_t     hw_len;
  uint8_t     prot_len;
  uint16_t    opcode;
  uint8_t     src_hw[6]; 
  uint8_t     src_ip[4];
  uint8_t     dst_hw[6]; 
  uint8_t     dst_ip[4];

} arp_hdr_t;



// ICMP header
typedef struct
{
  uint8_t   type;
  uint8_t   code;
  uint16_t  chksum;
  uint16_t  id;
  uint16_t  seq;
  uint8_t   data[];

} icmp_echo_hdr_t;



#endif


