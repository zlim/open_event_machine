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


/**
 * @file
 *
 * Simple Load Balanced Packet-IO test application.
 *
 * An application (EO) that receives UDP datagrams and exchanges
 * the src-dst addesses before sending the datagram back out.
 */

#include <string.h>
#include <stdio.h>

// Event Machine API
#include "event_machine.h"

// Generic HW abstraction
#include "environment.h"

// Packet IO HW abstraction
#include "packet_io_hw.h"

// Application config params and function prototypes
#include "packet_io.h"



/**
 * Test configuration
 */


/*
 * Set the used Queue-type for.
 *
 * Try also with EM_QUEUE_TYPE_PARALLEL or EM_QUEUE_TYPE_PARALLEL_ORDERED.
 * Alternatively set QUEUE_TYPE_MIX to '1' to use all queue types simultaneously.
 */
#define  QUEUE_TYPE          (EM_QUEUE_TYPE_ATOMIC)
//#define  QUEUE_TYPE          (EM_QUEUE_TYPE_PARALLEL)        
//#define  QUEUE_TYPE          (EM_QUEUE_TYPE_PARALLEL_ORDERED)


/**
 * Test with all different queue types simultaneously: ATOMIC, PARALLELL, PARALLEL_ORDERED
 */
#define QUEUE_TYPE_MIX       0 // 0=False or 1=True


/*
 * Create an EM queue per UDP/IP flow or use the default queue.
 *
 * If set to '0' then all traffic is routed through one 'default queue',
 * if set to '1' each traffic flow is routed to its own EM-queue.
 *
 * Note that the queue is of type EM_QUEUE_TYPE_PARALLEL when using the default queue,
 *       and thus not necessarily matching 'QUEUE_TYPE'
 */
#define QUEUE_PER_FLOW       1 // 0=False or 1=True


/**
 * Select whether the UDP ports should be unique over all the IP-interfaces (set to 1)
 * or reused per IP-interface (thus each UDP port is configured once for  each IP-interface)
 * Using '0' (not unique) makes it easier to copy traffic generator settings from one IF-port to another
 * and only change the dst-IP address.
 */
#define UDP_PORTS_UNIQUE     0 // 0=False or 1=True


/**
 * Select whether the input and output ports should be cross-connected.
 */
#define X_CONNECT_PORTS      0 // 0=False or 1=True


/**
 * Enable per packet error checking
 */
#define ENABLE_ERROR_CHECKS  0 // 0=False or 1=True


/**
 * Test em_alloc and em_free per packet
 *
 * Alloc new event, copy event, free old event
 */
#define ALLOC_COPY_FREE      0 // 0=False or 1=True




/* Configure the IP addresses and UDP ports that this application will use */
#define NUM_IP_ADDRS      4
#define NUM_PORTS_PER_IP  1024
#define NUM_QUEUES        (NUM_IP_ADDRS * NUM_PORTS_PER_IP)

#define IP_ADDR_A         192
#define IP_ADDR_B         168
#define IP_ADDR_C           1
#define IP_ADDR_D          16

#define IP_ADDR_BASE      ((IP_ADDR_A << 24) |  (IP_ADDR_B << 16) | (IP_ADDR_C << 8) | (IP_ADDR_D))
#define UDP_PORT_BASE     1024




/**
 * Macros
 */

#define ERROR_PRINT(...)     {printf("ERROR: file: %s line: %d\n", __FILE__, __LINE__); \
                              printf(__VA_ARGS__); fflush(NULL); abort();}

#define IS_ERROR(cond, ...)    \
  if(ENV_UNLIKELY( (cond) )) { \
    ERROR_PRINT(__VA_ARGS__);  \
  }

#define IS_ODD(x)   (((x) & 0x1))
#define IS_EVEN(x)  (!IS_ODD(x))



/**
 * EO context
 */
typedef struct
{
  em_eo_t     eo;
  
  char        name[16];
  
  em_queue_t  default_queue;
  
  em_queue_t  queue[NUM_QUEUES];
    
} eo_context_t ENV_CACHE_LINE_ALIGNED;




/**
 * Save the dst IP, protocol and port in the queue-context,
 * verify (if error checking enabled) that received packet matches the configuration for the queue.
 */

typedef struct flow_params_
{
  uint32_t ipv4;
  
  uint16_t port;
  
  uint8_t  proto;
  
  uint8_t  _pad;
  
} flow_params_t;




/**
 * Queue-Context 
 */
typedef union
{
  // User defined Q-context
  struct
  {
    // Saved flow params for the EM-queue
    flow_params_t flow_params;
    
    // Q-context index
    uint32_t      index;
  };
  
  
  // Pad to cache-line size
  uint8_t      u8[ENV_CACHE_LINE_SIZE];
  
} queue_context_t ENV_CACHE_LINE_ALIGNED;


COMPILE_TIME_ASSERT(sizeof(queue_context_t) == ENV_CACHE_LINE_SIZE, Q_CTX_CACHE_LINE_ALIGN_FAILS);



/**
 * Global Variables
 */
 
// EO (application) context
ENV_SHARED static  eo_context_t  EO_ctx  ENV_CACHE_LINE_ALIGNED;


// Array containing the contexts of all the queues handled by the EO
// A queue context contains the flow/queue specific data for the application EO.
ENV_SHARED static  queue_context_t  EO_q_ctx[NUM_QUEUES]  ENV_CACHE_LINE_ALIGNED;



/**
 * Local Function Prototypes
 */
static em_status_t
start(void *eo_ctx, em_eo_t eo);

static em_status_t
start_local(void *eo_ctx, em_eo_t eo);

static void
receive_packet(void *eo_ctx, em_event_t event, em_event_type_t type, em_queue_t queue, void *q_ctx);

static em_status_t
stop(void *eo_ctx, em_eo_t eo);

#if ENABLE_ERROR_CHECKS == 1
inline static int
rx_error_check(void *eo_ctx, em_event_t event, em_queue_t queue, void *q_ctx);
#endif

#if ALLOC_COPY_FREE == 1
static em_event_t
alloc_copy_free(em_event_t event);
#endif

// Helpers:
static void
ipaddr_tostr(uint32_t ip_addr, char *const ip_addr_str__out);




/**
 * Init and startup of the Packet Loopback test application.
 *
 * @see main() and packet_io_start() for setup and dispatch.
 */
void
test_init(packet_io_conf_t *const packet_io_conf)
{
  em_eo_t       eo;
  eo_context_t *app_eo_ctx;
  
  
  /*
   * Initializations only on one EM-core, return on all others.
   */  
  if(em_core_id() != 0)
  {
    return;
  }
  

  printf("\n**********************************************************************\n"
         "EM APPLICATION: '%s' initializing: \n"
         "  %s: %s() - EM-core:%i \n"
         "  Application running on %d EM-cores (procs:%d, threads:%d)."
         "\n**********************************************************************\n"
         "\n"
         ,
         packet_io_conf->name,
         NO_PATH(__FILE__), __func__,
         em_core_id(),
         em_core_count(),
         packet_io_conf->num_procs,
         packet_io_conf->num_threads);
         

  //
  // Create EO
  //
  app_eo_ctx = &EO_ctx;

  eo = em_eo_create("packet_loopback", start, start_local, stop, NULL, receive_packet, app_eo_ctx);
  
  em_eo_start(eo, NULL, 0, NULL);
}



/**
 * EO start (run once at startup on ONE core)
 * 
 * The global start function creates the application specific queues and
 * associates the queues with the EO and the packet flows it wants to process.
 */
static em_status_t
start(void    *eo_ctx,
      em_eo_t  eo)
{
  em_queue_t    queue;
  em_status_t   ret;
  eo_context_t *app_eo_ctx = eo_ctx;

  
  // Initialize EO context data to '0'
  (void) memset(app_eo_ctx, 0, sizeof(eo_context_t));
  // Store the EO-Id on the EO-context data
  app_eo_ctx->eo = eo;
  // Store the EO name in the EO-context data
  (void) em_eo_get_name(eo, app_eo_ctx->name, sizeof(app_eo_ctx->name));
  
  
  
  printf("EO %"PRI_EO":%s global start.\n", eo, app_eo_ctx->name);


  //
  // Default queue for all packets
  // Note: The queue type is EM_QUEUE_TYPE_PARALLEL !
  //
  queue = em_queue_create("default", EM_QUEUE_TYPE_PARALLEL, EM_QUEUE_PRIO_NORMAL, EM_QUEUE_GROUP_DEFAULT);
  IS_ERROR(queue == EM_QUEUE_UNDEF, "Default Queue creation failed!\n");
  
  // Store the default queue Id on the EO-context data
  app_eo_ctx->default_queue = queue;
  
  // Associate the queue with this EO
  ret = em_eo_add_queue(eo, queue);
  IS_ERROR(ret != EM_OK, "EO or queue creation failed (%i). EO: %"PRI_EO", queue: %"PRI_QUEUE"\n", ret, eo, queue);

  /*
   * Direct all non-lookup hit packets into this queue.
   * Note: if QUEUE_PER_FLOW is set to 0 then ALL packets end up in this queue.
   */
  packet_io_default_queue(queue);



  #if QUEUE_PER_FLOW == 1
  {
    /*
     * Create a queue per packet flow
     */
    int                 i, j;
    uint16_t            port_offset = (uint16_t) -1; // increment before usage - first used is 0.
    uint32_t            q_ctx_idx   = 0;
    queue_context_t    *q_ctx;
    em_queue_type_t     queue_type;    
    
    
    (void) memset(EO_q_ctx, 0, sizeof(EO_q_ctx));
    
    
    for(i = 0; i < NUM_IP_ADDRS; i++)
    {
      char     ip_str[sizeof("255.255.255.255")];
      uint32_t ip_addr = IP_ADDR_BASE + i;
            
      ipaddr_tostr(ip_addr, ip_str);      
      
      
      for(j = 0; j < NUM_PORTS_PER_IP; j++)
      {
        uint16_t udp_port;
        
        
      #if UDP_PORTS_UNIQUE == 1
        port_offset++;    // Every UDP-port is different: [UDP_PORT_BASE, UDP_PORT_BASE+(NUM_PORTS_PER_IP*NUM_IP_ADDRS)[
      #else
        port_offset = j;  // Same UDP-ports per IP-IF:    NUM_IP_ADDRS * [UDP_PORT_BASE, UDP_PORT_BASE+NUM_PORTS_PER_IP[
      #endif
        
        
        udp_port = UDP_PORT_BASE + port_offset;
        
        
        //
        // Note: QUEUE_TYPE differs depending on the test cfg
        //
      #if QUEUE_TYPE_MIX == 0
        queue_type = QUEUE_TYPE; // Use only queues of a single type
      #else
        {
          // Spread out over the three different queue-types
          int nbr_q = ((i * NUM_PORTS_PER_IP) + j) % 3;
          
          if(nbr_q == 0) {
            queue_type = EM_QUEUE_TYPE_ATOMIC;
          }
          else if(nbr_q == 1) {
            queue_type = EM_QUEUE_TYPE_PARALLEL;
          }
          else {
            queue_type = EM_QUEUE_TYPE_PARALLEL_ORDERED;
          }
        }
      #endif
        
        
        // Create a queue
        queue = em_queue_create("udp-flow", queue_type, EM_QUEUE_PRIO_NORMAL, EM_QUEUE_GROUP_DEFAULT);
        IS_ERROR(queue == EM_QUEUE_UNDEF, "Queue creation for UDP-port %d failed!\n", udp_port);
        
        // Store the id of the created queue into the application specific EO-context
        app_eo_ctx->queue[q_ctx_idx] = queue;
        
        // Add the queue to the EO
        ret = em_eo_add_queue(eo, queue);
        IS_ERROR(ret != EM_OK, "EO or queue creation failed (%i). EO: %"PRI_EO", queue: %"PRI_QUEUE"\n", ret, eo, queue);
        
        // Set the queue specific application (EO) context
        q_ctx = &EO_q_ctx[q_ctx_idx];
        
        ret = em_queue_set_context(queue, q_ctx);
        IS_ERROR(ret != EM_OK, "EO Queue context assignment failed (%i). EO-ctx: %d, queue: %"PRI_QUEUE"\n", ret, q_ctx_idx, queue);
        
        
        // Direct this ip_addr:udp_port into this queue
        packet_io_add_queue(IPV4_PROTO_UDP, ip_addr, udp_port, queue);
        
        // Store the queue-context index
        q_ctx->index = q_ctx_idx;
        
        // Save the flow params for debug checks in Rx
        q_ctx->flow_params.ipv4  = ip_addr;
        q_ctx->flow_params.port  = udp_port;
        q_ctx->flow_params.proto = IPV4_PROTO_UDP;
        
        
        // Sanity checks (lookup what was configured above)
        {
          em_queue_t  tmp_q;
          
          tmp_q = packet_io_lookup_sw(IPV4_PROTO_UDP, ip_addr, udp_port);
          IS_ERROR((tmp_q == EM_QUEUE_UNDEF) ||
                   (tmp_q != queue),
                   "Lookup failed for IP:UDP %s:%d (Q:%"PRI_QUEUE"!=lookup-Q:%"PRI_QUEUE")!\n",
                   ip_str, udp_port, queue, tmp_q);
          
          // Print first and last mapping (don't worry about performance here...)
          if((q_ctx_idx == 0) || (q_ctx_idx == (NUM_IP_ADDRS*NUM_PORTS_PER_IP - 1))) {
            printf("IP:port -> Q   %s:%u -> %"PRI_QUEUE" \n", ip_str, udp_port, tmp_q);
          }
        }
        
        // Update the Queue Context Index
        q_ctx_idx++;
      }
    }
    
  }
  #endif // QUEUE_PER_FLOW


  env_sync_mem();
  
  ret = em_queue_enable_all(eo);
  IS_ERROR(ret != EM_OK, "em_queue_enable_all() failed (%i). EO: %"PRI_EO"\n", ret, eo);
  
  printf("EO %"PRI_EO" global start done. \n", eo);
  
  return EM_OK;
}



/**
 * EO Local start (run once at startup on EACH core)
 
 * Not really needed in this application, but included 
 * to demonstrate usage.
 */
static em_status_t
start_local(void    *eo_ctx,
            em_eo_t  eo)
{
  eo_context_t *app_eo_ctx = eo_ctx;
  
  
  printf("EO %"PRI_EO":%s local start on EM-core%u\n", eo, app_eo_ctx->name, em_core_id());
  
  return EM_OK;
}



/**
 * EO stop
 */
static em_status_t
stop(void    *eo_ctx,
     em_eo_t  eo)
{
  printf("EO %"PRI_EO" stopping.\n", eo);

  return EM_OK;
}




/**
 * EO receive
 *
 */
static void
receive_packet(void            *eo_ctx,
               em_event_t       event,
               em_event_type_t  type,
               em_queue_t       queue,
               void            *q_ctx)
{
  int in_port;
  int out_port;


  // printf("packet input from queue %"PRI_QUEUE"\n", queue);

  // if(queue == ((eo_context_t *)eo_ctx)->default_queue)
  // {
  //   printf("queue default: %"PRI_QUEUE"\n", queue);
  // }


  in_port = packet_io_input_port(event);


#if X_CONNECT_PORTS == 1

  if(IS_EVEN(in_port)) {
    out_port = in_port + 1;
  }
  else {
    out_port = in_port - 1;
  }

#else

  out_port = in_port;

#endif



#if ENABLE_ERROR_CHECKS == 1
  if(ENV_UNLIKELY(rx_error_check(eo_ctx, event, queue, q_ctx) != 0)) {
    return;
  }
#endif



  // Touch packet.
  // Swap MAC, IP-addrs and UDP-ports: scr<->dst
  packet_io_swap_addrs(event);



#if ALLOC_COPY_FREE == 1
  event = alloc_copy_free(event);
#endif


  // send packet buffer out through out_port and free packet discriptors (if needed)
  packet_io_send_and_free(event, out_port);

}




#if ENABLE_ERROR_CHECKS == 1
inline static int
rx_error_check(void       *eo_ctx,
               em_event_t  event,
               em_queue_t  queue,
               void       *q_ctx)
{
  ENV_LOCAL static uint64_t  drop_cnt  = 1;


  #if QUEUE_PER_FLOW == 1
  {
    uint8_t                proto;
    uint32_t               ipv4_dst;
    uint16_t               port_dst;
    flow_params_t         *fp;
    eo_context_t    *const app_eo_ctx = eo_ctx;
    queue_context_t *const app_q_ctx  = q_ctx;    
    
    //
    // Drop everything from the default queue
    //    
    if(ENV_UNLIKELY(queue == app_eo_ctx->default_queue))
    {
      printf("Data received from the default port, dropping #%"PRIu64"\n", drop_cnt++);
      packet_io_drop(event);
      
      return -1;
    }
    
    
    //
    // Check IP address and port: compare packet against the stored values in the queue context
    //
    fp = &app_q_ctx->flow_params;
    
    packet_io_get_dst(event, &proto, &ipv4_dst, &port_dst);
    
    IS_ERROR(((fp->ipv4  != ipv4_dst) || 
              (fp->port  != port_dst) || 
              (fp->proto != proto)),
             "Queue %"PRI_QUEUE" received illegal packet!  \n"
             "Received   = IP:0x%"PRIx32" Proto:%c Port:%u \n"
             "Configured = IP:0x%"PRIx32" Proto:%c Port:%u \n"
             "Abort! \n"
             ,
             queue,
             ipv4_dst, proto,     port_dst,
             fp->ipv4, fp->proto, fp->port
             );
  }  
  #elif QUEUE_PER_FLOW == 0
  {
    uint8_t   proto;
    uint32_t  ipv4_dst;
    uint16_t  port_dst;  

    //     
    // Drop everything that's not UDP
    //     
  
    packet_io_get_dst(event, &proto, &ipv4_dst, &port_dst);
    
    if(ENV_UNLIKELY(proto != IPV4_PROTO_UDP))
    {
      printf("Data received from the default port was not UDP, dropping #%"PRIu64"\n", drop_cnt++);
      packet_io_drop(event);
      
      return -1;      
    }
    
    IS_ERROR((ipv4_dst < IP_ADDR_BASE)  || (ipv4_dst >= (IP_ADDR_BASE+NUM_IP_ADDRS)) ||
             (port_dst < UDP_PORT_BASE) || (port_dst >= (UDP_PORT_BASE+NUM_QUEUES))  ||
             (proto != IPV4_PROTO_UDP),
             "Queue %"PRI_QUEUE" received illegal packet!  \n"
             "Received   = IP:0x%"PRIx32" Proto:%c Port:%u \n"
             "Values not within the configurated range!    \n"
             "Abort! \n"
             ,
             queue,
             ipv4_dst, proto, port_dst
             );  
  }
  #else
    #error "QUEUE_PER_FLOW EITHER 0 or 1!"
  #endif
  
  
  // Everything OK, return zero.
  return 0;  
}
#endif




#if ALLOC_COPY_FREE == 1
/**
 * Alloc a new event, copy the contents&header into the new event
 * and finally free the original event. Returns a pointer to the new event.
 *
 * Used for testing the performance impact of alloc-copy-free operations.
 */
inline static em_event_t
alloc_copy_free(em_event_t event)
{
  uint8_t    *frame;
  uint32_t    len;
  em_event_t  new_event;
  uint8_t    *new_frame;


  // Allocate new a packet event
  frame     = packet_io_get_frame(event);
  len       = packet_io_get_frame_len(event); 
  new_event = packet_io_alloc(len);
  IS_ERROR(new_event == EM_EVENT_UNDEF, "packet_io_alloc(len=%"PRIu32") failed! \n", len);
  new_frame = packet_io_get_frame(new_event);

  // Copy packet descriptor
  packet_io_copy_descriptor(new_event, event);
  // Copy the frame data
  memcpy(new_frame, frame, len);

  // Free old event
  em_free(event);  
   
  return new_event;
}

#endif




/**
 * Convert an IP-address to ascii string format.
 */
static void
ipaddr_tostr(uint32_t ip_addr, char *const ip_addr_str__out)
{
  unsigned char *const ucp = (unsigned char *) &ip_addr;

#ifndef BYTE_ORDER
  #error BYTE_ORDER not defined
#endif

#if BYTE_ORDER == LITTLE_ENDIAN

  sprintf(ip_addr_str__out,
          "%d.%d.%d.%d",
          ucp[3] & 0xff,
          ucp[2] & 0xff,
          ucp[1] & 0xff,
          ucp[0] & 0xff);    
#else // BIG_ENDIAN
  sprintf(ip_addr_str__out,
          "%d.%d.%d.%d",
          ucp[0] & 0xff,
          ucp[1] & 0xff,
          ucp[2] & 0xff,
          ucp[3] & 0xff);
#endif
}



