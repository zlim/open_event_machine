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
 * Load Balanced, multi-stage, Packet-IO test application.
 *
 * The created UDP flows are received and processed by three (3) chained EOs before
 * sending the datagrams back out. Uses EM queues of different priority and type.
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

#define NUM_IP_ADDRS        4
#define NUM_PORTS_PER_IP  256
#define NUM_FLOWS        (NUM_IP_ADDRS * NUM_PORTS_PER_IP)

#define IP_ADDR_A         192
#define IP_ADDR_B         168
#define IP_ADDR_C           1
#define IP_ADDR_D          16

#define IP_ADDR_BASE      ((IP_ADDR_A << 24) |  (IP_ADDR_B << 16) | (IP_ADDR_C << 8) | (IP_ADDR_D))
#define UDP_PORT_BASE     1024




/**
 * The number of different EM queue priority levels to use - fixed.
 */
#define Q_PRIO_LEVELS (4)

/**
 * The number of processing stages for a flow, i.e. the number of EO's a packet will go through before
 * being sent back out.
 */
#define PROCESSING_STAGES 3

/**
 * The number of Queue Type permutations: 3 types (ATOMIC, PARALLELL, PARALLEL-ORDERED)
 * gives 3*3*3 = 27 permutations
 */
#define QUEUE_TYPE_PERMUTATIONS  (3*3*3)

/**
 * Select whether the UDP ports should be unique over all IP-interfaces (set to 1)
 * or reused per IP-interface (thus each UDP port is configured once for each IP-interface)
 * Using '0' (not unique) makes it easier to copy traffic generator settings from one IF-port
 * to another and only change the dst-IP address.
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



/**
 * Test with all different queue types: ATOMIC, PARALLELL, PARALLEL_ORDERED
 * 
 */
#define QUEUE_TYPE_MIX       1 // 0=False or 1=True(default)




#if QUEUE_TYPE_MIX == 0
/*
 * Set the used Queue-type for the benchmarking cases when using only one queue type
 */
#define  QUEUE_TYPE          (EM_QUEUE_TYPE_ATOMIC)
//#define  QUEUE_TYPE          (EM_QUEUE_TYPE_PARALLEL)
//#define  QUEUE_TYPE          (EM_QUEUE_TYPE_PARALLEL_ORDERED)
#endif




/**
 * Macros
 */

#define ERROR_PRINT(...)     {printf("ERROR: file: %s line: %d\n", __FILE__, __LINE__); \
                              printf(__VA_ARGS__); fflush(NULL); abort();}

#define IS_ERROR(cond, ...)   \
  if(ENV_UNLIKELY(cond)) {    \
    ERROR_PRINT(__VA_ARGS__); \
  }

#define IS_ODD(x)   (((x) & 0x1))
#define IS_EVEN(x)  (!IS_ODD(x))



/**
 * EO context
 */
typedef struct 
{
  em_eo_t     eo;
  em_queue_t  default_queue;

  uint8_t      pad[ENV_CACHE_LINE_SIZE
                   - sizeof(em_eo_t)
                   - sizeof(em_queue_t)
                  ];
    
} eo_context_t ENV_CACHE_LINE_ALIGNED;

COMPILE_TIME_ASSERT(sizeof(eo_context_t) == ENV_CACHE_LINE_SIZE, EO_CTX_CACHE_LINE_ALIGN_FAILS);




/**
 * Save the dst IP, protocol and port in the queue-context,
 * verify (if set) that received packet matches the configuration for the queue.
 */

typedef struct flow_params_
{
  uint32_t ipv4;
  uint16_t port;
  uint8_t  proto;
  uint8_t  _pad;
} flow_params_t;




/**
 * Queue context for this test case
 */
typedef union 
{ 
  /** Queue context data*/ 
  struct
  {
    uint32_t      state;
    uint32_t      index;
    flow_params_t flow_params;
    
    uint64_t      count_1;
    uint64_t      count_2;
    
    em_queue_t    dst_queue;
  };
  
  /** Pad to cache line size */
  uint8_t      u8[ENV_CACHE_LINE_SIZE];
  
} queue_context_t  ENV_CACHE_LINE_ALIGNED;

COMPILE_TIME_ASSERT(sizeof(queue_context_t) == ENV_CACHE_LINE_SIZE, Q_CTX_CACHE_LINE_ALIGN_FAILS);




/**
 * Queue types used by the three chained EOs processing a flow
 */
typedef struct
{
  em_queue_type_t queue_type_1st;

  em_queue_type_t queue_type_2nd;

  em_queue_type_t queue_type_3rd;

} queue_type_tuple_t;




/**
 * Global Variables
 */
ENV_SHARED static  eo_context_t  EO_ctx[PROCESSING_STAGES] ENV_CACHE_LINE_ALIGNED;
                                       
ENV_SHARED static  queue_context_t  EO_q_ctx_1st[NUM_FLOWS]   ENV_CACHE_LINE_ALIGNED;
ENV_SHARED static  queue_context_t  EO_q_ctx_2nd[NUM_FLOWS]   ENV_CACHE_LINE_ALIGNED;
ENV_SHARED static  queue_context_t  EO_q_ctx_3rd[NUM_FLOWS]   ENV_CACHE_LINE_ALIGNED;

ENV_SHARED static  queue_type_tuple_t  q_type_permutations[QUEUE_TYPE_PERMUTATIONS] ENV_CACHE_LINE_ALIGNED;



/**
 * Local Function Prototypes
 */
static em_status_t
start(void* eo_context, em_eo_t eo);
static em_status_t
start_local(void* eo_context, em_eo_t eo);

static void
receive_packet_1st(void* eo_context, em_event_t event, em_event_type_t type, em_queue_t queue, void *q_ctx);
static void
receive_packet_2nd(void* eo_context, em_event_t event, em_event_type_t type, em_queue_t queue, void *q_ctx);
static void
receive_packet_3rd(void* eo_context, em_event_t event, em_event_type_t type, em_queue_t queue, void *q_ctx);

static em_status_t
stop(void* eo_context, em_eo_t eo);
static em_status_t
stop_local(void* eo_context, em_eo_t eo);

/*
 * Helpers:
 */
static void ipaddr_tostr(uint32_t ip_addr, char *const ip_addr_str__out);
static queue_type_tuple_t* get_queue_type_tuple(int cnt);
static void fill_q_type_permutations(void);
static em_queue_type_t queue_types(int cnt);
#if ALLOC_COPY_FREE
static em_event_t alloc_copy_free(em_event_t event);
#endif



/**
 * Init and startup of the Packet Multi-stage test application.
 *
 * @see main() and packet_io_start() for setup and dispatch.
 */
void
test_init(packet_io_conf_t *const packet_io_conf)
{
  em_eo_t             eo_1st, eo_2nd, eo_3rd;
  em_queue_t          default_queue;
  em_queue_t          queue_1st,  queue_2nd,  queue_3rd;
  queue_context_t    *q_ctx_1st, *q_ctx_2nd, *q_ctx_3rd;
  queue_type_tuple_t *q_type_tuple;  
  em_status_t         ret;
  uint16_t            port_offset = (uint16_t) -1; // increment before usage - first used is 0.
  uint32_t            q_ctx_idx   = 0;
  int                 i, j;
  
  
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

  
#if 1 // Use different prios for the queues
  const em_queue_prio_t q_prio[Q_PRIO_LEVELS] = {EM_QUEUE_PRIO_LOW, EM_QUEUE_PRIO_NORMAL,
                                                 EM_QUEUE_PRIO_HIGH, EM_QUEUE_PRIO_HIGHEST
                                                };
#else // for comparison, try with all queues of same priority
  const em_queue_prio_t q_prio[Q_PRIO_LEVELS] = {EM_QUEUE_PRIO_HIGHEST, EM_QUEUE_PRIO_HIGHEST,
                                                 EM_QUEUE_PRIO_HIGHEST, EM_QUEUE_PRIO_HIGHEST
                                                };
#endif


  // Initialize the Queue-type permutations array
  fill_q_type_permutations();
  

  //
  // Create EOs, 3 stages of processing for each flow
  //
  (void) memset(EO_ctx, 0, sizeof(EO_ctx));
  eo_1st = em_eo_create("packet_mstage_1st", start, start_local, stop, stop_local, receive_packet_1st, &EO_ctx[0]);
  eo_2nd = em_eo_create("packet_mstage_2nd", start, start_local, stop, stop_local, receive_packet_2nd, &EO_ctx[1]);
  eo_3rd = em_eo_create("packet_mstage_3rd", start, start_local, stop, stop_local, receive_packet_3rd, &EO_ctx[2]);



  //
  // Default queue for all packets, handled by EO 1, receives all unwanted packets (EO 1 drops them)
  // Note: The queue type is EM_QUEUE_TYPE_PARALLEL !
  //
  default_queue = em_queue_create("default", EM_QUEUE_TYPE_PARALLEL, EM_QUEUE_PRIO_LOWEST, EM_QUEUE_GROUP_DEFAULT);
  IS_ERROR(default_queue == EM_QUEUE_UNDEF, "Default Queue creation failed!\n");
  
  // Store the default queue Id on the EO-context data
  EO_ctx[0].default_queue = default_queue;
  
  // Associate the queue with EO 1
  ret = em_eo_add_queue(eo_1st, default_queue);
  IS_ERROR(ret != EM_OK, "EO or queue creation failed (%i). EO: %"PRI_EO", queue: %"PRI_QUEUE"\n", ret, eo_1st, default_queue);

  /*
   * Direct all non-lookup hit packets into this queue.
   * Note: if QUEUE_PER_FLOW is set to 0 then ALL packets end up in this queue.
   */
  packet_io_default_queue(default_queue);



   // Zero the Queue context arrays
  (void) memset(EO_q_ctx_1st, 0, sizeof(EO_q_ctx_1st));
  (void) memset(EO_q_ctx_2nd, 0, sizeof(EO_q_ctx_2nd));
  (void) memset(EO_q_ctx_3rd, 0, sizeof(EO_q_ctx_3rd));


  /*
   * Create the queues for the packet flows
   */
  
  for(i = 0; i < NUM_IP_ADDRS; i++)
  {
    char     ip_str[sizeof("255.255.255.255")];
    uint32_t ip_addr = IP_ADDR_BASE + i;
          
    ipaddr_tostr(ip_addr, ip_str);      
    
    
    for(j = 0; j < NUM_PORTS_PER_IP; j++)
    {
      uint16_t         udp_port;
      em_queue_prio_t  prio;
      
      #if UDP_PORTS_UNIQUE == 1
        port_offset++;    // Every UDP-port is different: [UDP_PORT_BASE, UDP_PORT_BASE+(NUM_PORTS_PER_IP*NUM_IP_ADDRS)[
      #else
        port_offset = j;  // Same UDP-ports per IP-IF:    NUM_IP_ADDRS * [UDP_PORT_BASE, UDP_PORT_BASE+NUM_PORTS_PER_IP[
      #endif
      
      udp_port = UDP_PORT_BASE + port_offset;
      
      
      //
      // Get the queue types for this 3-tuple
      //
      q_type_tuple = get_queue_type_tuple(q_ctx_idx);
      
      //
      // Get the queue priority for this 3-tuple
      //
      prio = q_prio[q_ctx_idx % Q_PRIO_LEVELS];
      
      //
      // Create the packet-IO (input/Rx) queue for 'eo_1st' for this flow 
      // 
      queue_1st = em_queue_create("udp_port", q_type_tuple->queue_type_1st, prio, EM_QUEUE_GROUP_DEFAULT);
      IS_ERROR(queue_1st == EM_QUEUE_UNDEF, "Queue creation for UDP-port %d failed!\n", udp_port);
      
      ret = em_eo_add_queue(eo_1st, queue_1st);
      IS_ERROR(ret != EM_OK, "EO or queue creation failed (%i). EO: %"PRI_EO", queue: %"PRI_QUEUE"\n", ret, eo_1st, queue_1st);
      
      q_ctx_1st = &EO_q_ctx_1st[q_ctx_idx];
      ret       = em_queue_set_context(queue_1st, q_ctx_1st);
      IS_ERROR(ret != EM_OK, "EO Queue context assignment failed (%i). EO-ctx: %d, queue: %"PRI_QUEUE"\n", ret, q_ctx_idx, queue_1st);
      
      
      // Direct this ip_addr:udp_port into this queue
      packet_io_add_queue(IPV4_PROTO_UDP, ip_addr, udp_port, queue_1st);
      
      q_ctx_1st->index = q_ctx_idx;
      
      // Save the flow params for debug checks in Rx
      q_ctx_1st->flow_params.ipv4  = ip_addr;
      q_ctx_1st->flow_params.port  = udp_port;
      q_ctx_1st->flow_params.proto = IPV4_PROTO_UDP;
      
      
      // Sanity checks (lookup what was configured above)
      {
        em_queue_t  tmp_q;
        
        tmp_q = packet_io_lookup_sw(IPV4_PROTO_UDP, ip_addr, udp_port);
        IS_ERROR((tmp_q == EM_QUEUE_UNDEF) ||
                 (tmp_q != queue_1st),
                 "Lookup failed for IP:UDP %s:%d (Q:%"PRI_QUEUE"!=lookup-Q:%"PRI_QUEUE")!\n",
                 ip_str, udp_port, queue_1st, tmp_q);
        
        // Print first and last mapping (don't worry about performance here...)
        if((q_ctx_idx == 0) || (q_ctx_idx == (NUM_IP_ADDRS*NUM_PORTS_PER_IP - 1))) {
          printf("IP:port -> Q   %s:%u -> %"PRI_QUEUE" \n", ip_str, udp_port, tmp_q);
        }
      }
      
      


      //
      // Create the middle queue for 'eo_2nd' for this flow
      //
      queue_2nd = em_queue_create("udp_port", q_type_tuple->queue_type_2nd, prio, EM_QUEUE_GROUP_DEFAULT);
      IS_ERROR(queue_2nd == EM_QUEUE_UNDEF, "Queue creation for UDP-port %d failed!\n", udp_port);
      
      ret = em_eo_add_queue(eo_2nd, queue_2nd);
      IS_ERROR(ret != EM_OK, "EO or queue creation failed (%i). EO: %"PRI_EO", queue: %"PRI_QUEUE"\n", ret, eo_2nd, queue_2nd);
      
      q_ctx_2nd = &EO_q_ctx_2nd[q_ctx_idx];
      ret       = em_queue_set_context(queue_2nd, q_ctx_2nd);
      IS_ERROR(ret != EM_OK, "EO Queue context assignment failed (%i). EO-ctx: %d, queue: %"PRI_QUEUE"\n", ret, q_ctx_idx, queue_2nd);
      
      q_ctx_2nd->index = q_ctx_idx;
      
      // Save the flow params for debug checks in Rx
      q_ctx_2nd->flow_params.ipv4  = ip_addr;
      q_ctx_2nd->flow_params.port  = udp_port;
      q_ctx_2nd->flow_params.proto = IPV4_PROTO_UDP;
 
      
      // Save stage1 dst queue
      q_ctx_1st->dst_queue = queue_2nd;




      //
      // Create the last queue for 'eo_3rd' for this flow - eo-3rd sends the event/packet out to where it originally came from 
      //      
      queue_3rd = em_queue_create("udp_port", q_type_tuple->queue_type_3rd, prio, EM_QUEUE_GROUP_DEFAULT);
      IS_ERROR(queue_3rd == EM_QUEUE_UNDEF, "Queue creation for UDP-port %d failed!\n", udp_port);
      
      ret = em_eo_add_queue(eo_3rd, queue_3rd);
      IS_ERROR(ret != EM_OK, "EO or queue creation failed (%i). EO: %"PRI_EO", queue: %"PRI_QUEUE"\n", ret, eo_3rd, queue_3rd);
      
      q_ctx_3rd = &EO_q_ctx_3rd[q_ctx_idx];
      ret       = em_queue_set_context(queue_3rd, q_ctx_3rd);
      IS_ERROR(ret != EM_OK, "EO Queue context assignment failed (%i). EO-ctx: %d, queue: %"PRI_QUEUE"\n", ret, q_ctx_idx, queue_3rd);
      
      q_ctx_3rd->index = q_ctx_idx;
      
      // Save the flow params for debug checks in Rx
      q_ctx_3rd->flow_params.ipv4  = ip_addr;
      q_ctx_3rd->flow_params.port  = udp_port;
      q_ctx_3rd->flow_params.proto = IPV4_PROTO_UDP;
      
      
       // Save stage2 dst queue
      q_ctx_2nd->dst_queue = queue_3rd;
      
      // 3rd stage is output - no EM dst queue
      q_ctx_3rd->dst_queue = EM_QUEUE_UNDEF;
      
      
      // Update the Queue Context Index for the next iteration round
      q_ctx_idx++;
      
      // printf("Q-types(%u): %u %u %u\n", q_ctx_idx, q_type_tuple->queue_type_1st, q_type_tuple->queue_type_2nd, q_type_tuple->queue_type_3rd); fflush(NULL);
    }
  }


  env_sync_mem();  
  
  //
  // Start the EOs
  //
  em_eo_start(eo_3rd, NULL, 0, NULL);
  em_eo_start(eo_2nd, NULL, 0, NULL);
  em_eo_start(eo_1st, NULL, 0, NULL);
  
  ret = em_queue_enable_all(eo_3rd);
  IS_ERROR(ret != EM_OK, "EO-3rd em_queue_enable_all() failed (%i). EO: %"PRI_EO"\n", ret, eo_3rd);

  ret = em_queue_enable_all(eo_2nd);
  IS_ERROR(ret != EM_OK, "EO-2nd em_queue_enable_all() failed (%i). EO: %"PRI_EO"\n", ret, eo_2nd);
  
  ret = em_queue_enable_all(eo_1st);
  IS_ERROR(ret != EM_OK, "EO-1st em_queue_enable_all() failed (%i). EO: %"PRI_EO"\n", ret, eo_1st);    

  env_sync_mem();  
}




/**
 * EO start
 */
static em_status_t
start(void* eo_context,
      em_eo_t          eo)
{
  eo_context_t* eo_ctx = eo_context;

  printf("EO %"PRI_EO" starting.\n", eo);

  eo_ctx->eo = eo;
  // eo_ctx->default_queue = Stored earlier in packet_multi_stage_start

  env_sync_mem();

  return EM_OK;
}



/**
 * EO local start
 */
static em_status_t
start_local(void* eo_context,
            em_eo_t          eo)
{
  printf("Core%i: EO %"PRI_EO" local start.\n", em_core_id(), eo);
  
  return EM_OK;
}




/**
 * EO stop
 */
static em_status_t
stop(void* eo_context,
     em_eo_t          eo)
{
  printf("EO %"PRI_EO" stopping.\n", eo);

  return EM_OK;
}




/**
 * EO local stop
 */
static em_status_t 
stop_local(void* eo_context,
                            em_eo_t          eo)
{
  printf("Core%i: EO %"PRI_EO" local stop.\n", em_core_id(), eo);
  
  return EM_OK;
}




/**
 * EO_1st receive
 *
 */
static void
receive_packet_1st(void               *eo_context,
                   em_event_t          event,
                   em_event_type_t     type,
                   em_queue_t          queue,
                   void               *q_ctx)
{
  eo_context_t          *eo_ctx     = eo_context;
  queue_context_t *const appl_q_ctx = q_ctx;
  em_status_t            status;
  
  
  //
  // Drop everything from the default queue, should not receive anything here
  //
  if(ENV_UNLIKELY(queue == eo_ctx->default_queue))
  {
    static ENV_LOCAL uint64_t drop_cnt = 1;
    uint8_t  proto;
    uint32_t ipv4_dst;
    uint16_t port_dst;
    
    
    packet_io_get_dst(event, &proto, &ipv4_dst, &port_dst);
    
    printf("Data received from IP:port=%"PRIx32":%"PRIx16" into the default queue on core%i, dropping #%"PRIu64"\n",
           ipv4_dst, port_dst, em_core_id(), drop_cnt++);
    
    packet_io_drop(event);
    return;
  }



#if ENABLE_ERROR_CHECKS  

  //
  // Check IP address and port
  //
  {
    uint8_t              proto;
    uint32_t             ipv4_dst;
    uint16_t             port_dst;
    flow_params_t *const fp = &appl_q_ctx->flow_params;
    
    packet_io_get_dst(event, &proto, &ipv4_dst, &port_dst);
    
    IS_ERROR(((fp->ipv4  != ipv4_dst) || 
              (fp->port  != port_dst) || 
              (fp->proto != proto)),
             "Queue %"PRI_QUEUE" received illegal packet! \n"
             "Received   = IP:0x%"PRIx32" Proto:%c Port:%u \n"
             "Configured = IP:0x%"PRIx32" Proto:%c Port:%u \n",
             queue,
             ipv4_dst, proto,     port_dst,
             fp->ipv4, fp->proto, fp->port
             );
  }

#endif
  
  // Send to the next stage for further processing.
  status = em_send(event, appl_q_ctx->dst_queue);
  
  if(unlikely(status != EM_OK)) {
    em_free(event);
  }
}




/**
 * EO_2nd receive
 *
 */
static void                                    
receive_packet_2nd(void               *eo_context, 
                   em_event_t          event,  
                   em_event_type_t     type,   
                   em_queue_t          queue,  
                   void               *q_ctx)  
{ 
  queue_context_t *const appl_q_ctx = q_ctx;
  em_status_t            status;
   
  
  // Send to the next stage for further processing.
  status = em_send(event, appl_q_ctx->dst_queue);
  
  if(unlikely(status != EM_OK)) {
    em_free(event);
  }  
}




/**
 * EO_3rd receive
 *
 */
static void                                    
receive_packet_3rd(void               *eo_context, 
                   em_event_t          event,  
                   em_event_type_t     type,   
                   em_queue_t          queue,  
                   void               *q_ctx)  
{
  int                    in_port;
  int                    out_port;


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

                                                
  // Touch packet.
  // Swap MAC, IP-addrs and UDP-ports: scr<->dst
  packet_io_swap_addrs(event);
  

#if ALLOC_COPY_FREE
  event = alloc_copy_free(event);
#endif

  // send packet buffer out through out_port and free packet discriptors (if needed)
  packet_io_send_and_free(event, out_port);
}





#if ALLOC_COPY_FREE
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




/**
 * Helper func to determine queue types at startup
 */
static queue_type_tuple_t *
get_queue_type_tuple(int cnt)
{
#if QUEUE_TYPE_MIX == 0
  
  // Always return the same kind of Queue types
  return &q_type_permutations[0];

#else
  
  // Spread out over the 3 different queue-types
  return &q_type_permutations[cnt%(QUEUE_TYPE_PERMUTATIONS)];
  
#endif
}




/**
 * Helper func to initialize the Queue Type permutations array
 *
 * 3 queue types gives 3*3*3=27 permutations - store these.
 */
static void
fill_q_type_permutations(void)
{
#if QUEUE_TYPE_MIX == 0
  
  // Use the same type of queues everywhere.
  q_type_permutations[0].queue_type_1st = QUEUE_TYPE;
  q_type_permutations[0].queue_type_2nd = QUEUE_TYPE;
  q_type_permutations[0].queue_type_3rd = QUEUE_TYPE;

#else

  int i, j, k;
  em_queue_type_t queue_type_1st, queue_type_2nd, queue_type_3rd;
  int nbr_q = 0;


  for(i = 0; i < 3; i++)
  {
    for(j = 0; j < 3; j++)
    {
      for(k = 0; k < 3; k++, nbr_q++)
      {  
        queue_type_1st = queue_types(i);
        queue_type_2nd = queue_types(j);
        queue_type_3rd = queue_types(k);
        
        q_type_permutations[nbr_q].queue_type_1st = queue_type_1st;
        q_type_permutations[nbr_q].queue_type_2nd = queue_type_2nd;
        q_type_permutations[nbr_q].queue_type_3rd = queue_type_3rd;
      }
    }
  }
  
#endif
}




/**
 * Helper func, returns a Queue Type based on the input count.
 */
static em_queue_type_t
queue_types(int cnt)
{
  switch(cnt % 3)
  {
    case 0:
      return EM_QUEUE_TYPE_ATOMIC;
      break; 

    case 1:
      return EM_QUEUE_TYPE_PARALLEL;
      break;

    default:
      return EM_QUEUE_TYPE_PARALLEL_ORDERED;
      break;       
  }
}






