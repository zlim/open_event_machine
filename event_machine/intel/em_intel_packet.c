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
 
#include "em_intel.h"
#include "em_intel_sched.h"
#include "em_intel_packet.h"
#include "environment.h"
#include "intel_hw_init.h"
#include "em_error.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <string.h>
#include <sys/queue.h>
#include <stdarg.h>
#include <errno.h>

#include <assert.h>

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
#include <rte_jhash.h>
#include <rte_hash_crc.h>


/*
 * DEFINES
 */
#define ETH_RX_DESC_DEFAULT (128)
#define ETH_TX_DESC_DEFAULT (512)


#define RX_PTHRESH          (8) /**< Default values of RX prefetch threshold reg. */
#define RX_HTHRESH          (8) /**< Default values of RX host threshold reg. */
#define RX_WTHRESH          (4) /**< Default values of RX write-back threshold reg. */


#define TX_PTHRESH          (36) /**< Default values of TX prefetch threshold reg. */
#define TX_HTHRESH          (0)  /**< Default values of TX host threshold reg. */
#define TX_WTHRESH          (0)  /**< Default values of TX write-back threshold reg. */                            

                            
#define MAX_RX_PKT_BURST    (128) /**< Max number of packets to receive  in one burst from an Eth-port */
#define MAX_TX_PKT_BURST    (16)  /**< Max number of packets to transmit in one burst onto an Eth-port */

#define BURST_TX_DRAIN      (200000ULL)  /* around 100us  at 2 Ghz */
//#define BURST_TX_DRAIN      (2000000ULL) /* around 1000us at 2 Ghz */
//#define BURST_TX_DRAIN      (4000000ULL) /* around 2000us at 2 Ghz */

/* Configure how many packets ahead to prefetch, when reading packets */
#define PREFETCH_OFFSET     (3)

/* How many times to poll the same Rx queue and keep the RX-queue lock before moving on to the next */
#define ETH_RX_IDX_CNT_MAX  (4)


COMPILE_TIME_ASSERT(POWEROF2(MAX_TX_PKT_BURST), MAX_TX_PKT_BURST_NOT_POWER_OF_TWO);

COMPILE_TIME_ASSERT(POWEROF2(MAX_ETH_TX_MBUF_TABLES), MAX_ETH_TX_MBUF_TABLES_NOT_POWER_OF_TWO);

COMPILE_TIME_ASSERT((sizeof(struct rte_mbuf) %  ENV_CACHE_LINE_SIZE) == 0, MBUF_SIZE_ERROR1);
COMPILE_TIME_ASSERT( sizeof(struct rte_mbuf) == ENV_CACHE_LINE_SIZE,       MBUF_SIZE_ERROR2);



/* 
 * EXTERNAL VARIABLES
 */
 


/*
 * DATA TYPES AND VARIABLES
 */


/**
 * Eth Rx queue -> port mapping
 */
typedef struct
{
  uint8_t         port_id; 
  
  uint8_t         queue_id;

} eth_rx_queue_info_t;


/**
 * Array of available Rx port:queue pairs.
 * 
 * @note The FIFO 'eth_rx_queue_access' contains pointers to the available Rx port:queue pairs from this array.
 *       Core exclusive access to the shared Rx resources are enforced by en/dequeueing an Rx port:queue pair
 */
ENV_SHARED  eth_rx_queue_info_t  eth_rx_queue_info[MAX_ETH_RX_QUEUES]  ENV_CACHE_LINE_ALIGNED;

COMPILE_TIME_ASSERT((sizeof(eth_rx_queue_info) % ENV_CACHE_LINE_SIZE) == 0, ETH_RX_QUEUE_INFO_ALIGNMENT_ERROR);


/**
 * The Eth Rx port:queue access FIFO - a core dequeues a 'eth_rx_queue_info_t' and uses that port exclusively.
 * When the core is done with the port it will enqueue it again for some other core to use.
 */
typedef union
{
  struct rte_ring *queue;
    
} eth_rx_queue_access_t;


/**
 * Holds the currently dequeued eth_rx_queue_info_t and a counter that tracks the number of times
 * Rx-frames have been burst dequeued from the eth queue.
 */
typedef struct
{
  eth_rx_queue_info_t *current_info;
  
  uint64_t             access_cnt;
  
} curr_eth_rx_queue_info_t;



/**
 * Tx buffer for Eth frames that can be sent out without any ordering constraints.
 * One buffer per core.
 */
typedef struct
{
  unsigned         len;

  struct rte_mbuf* m_table[MAX_TX_PKT_BURST];

} eth_tx_mbuf_table_local_t;

ENV_LOCAL  eth_tx_mbuf_table_local_t  eth_tx_mbuf_tables_local[MAX_ETH_PORTS]  ENV_CACHE_LINE_ALIGNED;

COMPILE_TIME_ASSERT((sizeof(eth_tx_mbuf_tables_local) % ENV_CACHE_LINE_SIZE) == 0, ETH_TX_MBUF_TABLES_LOCAL_SIZE_ERROR);



/**
 * Tx buffer for Eth frames that must be sent out in-order.
 * One buffer per device (i.e. shared by all cores)
 */
typedef struct
{
  env_spinlock_t   lock    ENV_CACHE_LINE_ALIGNED;
  
  struct rte_ring *m_burst ENV_CACHE_LINE_ALIGNED;
  
} eth_tx_mbuf_table_t ENV_CACHE_LINE_ALIGNED;

ENV_SHARED  eth_tx_mbuf_table_t  eth_tx_mbuf_tables[MAX_ETH_PORTS][MAX_ETH_TX_MBUF_TABLES];

COMPILE_TIME_ASSERT((sizeof(eth_tx_mbuf_tables) % ENV_CACHE_LINE_SIZE) == 0, ETH_TX_MBUF_TABLES_SIZE_ERROR);



/**
 * Temp buffer used with ordered Tx frames: dequeue from eth_tx_mbuf_tables[x][y] into tx_burst_m_table,
 * then burst transmit from the tx_burst_m_table to Eth Tx.
 */
ENV_LOCAL struct rte_mbuf* tx_burst_m_table[MAX_TX_PKT_BURST]  ENV_CACHE_LINE_ALIGNED;

COMPILE_TIME_ASSERT((sizeof(tx_burst_m_table) % ENV_CACHE_LINE_SIZE) == 0, TX_BURST_M_TABLE_SIZE_ERROR);


/**
 * Eth Rx frame burst storage buffer.
 */
ENV_LOCAL struct rte_mbuf* rx_burst_m_table[MAX_RX_PKT_BURST]  ENV_CACHE_LINE_ALIGNED;

COMPILE_TIME_ASSERT((sizeof(rx_burst_m_table) % ENV_CACHE_LINE_SIZE) == 0, RX_BURST_M_TABLE_SIZE_ERROR);



/**
 * Used by em_eth_tx_packets_timed() to drain the queues listed in .id[]
 * Each core gets a subset of the tx queues to drain - assigned at startup.
 * Each core drains the same set of queues per eth-Tx-port.
 */
typedef union
{
  struct
  {
    uint16_t curr_idx;
    
    uint16_t len;  
    
    uint16_t id[MAX_ETH_TX_MBUF_TABLES];
  };
  
  uint8_t u8[2*ENV_CACHE_LINE_SIZE];
  
} eth_tx_drain_queues_t;

ENV_LOCAL  eth_tx_drain_queues_t  eth_tx_drain_queues__local  ENV_CACHE_LINE_ALIGNED;
COMPILE_TIME_ASSERT((sizeof(eth_tx_drain_queues__local) % ENV_CACHE_LINE_SIZE) == 0, ETH_TX_DRAIN_QUEUES__LOCAL_SIZE_ERROR);



/* Ethernet addresses of ports - used in init */
ENV_SHARED static  struct ether_addr  port_eth_addr[MAX_ETH_PORTS];



/**
 * Eth port configuration
 */
ENV_SHARED static const struct rte_eth_conf  eth_port_conf = {
  .rxmode = {
    .mq_mode        = ETH_RSS, /* Receive Side Scaling, on Niantic up to 16 Rx eth-queues */
    .split_hdr_size = 0,
    .header_split   = 0, /**< Header Split disabled */
    //.hw_ip_checksum = 0, /**< IP checksum offload disabled */
    .hw_ip_checksum = 1, /**< IP checksum offload enabled */
    .hw_vlan_filter = 0, /**< VLAN filtering disabled */
    .jumbo_frame    = 0, /**< Jumbo Frame Support disabled */
    .hw_strip_crc   = 0, /**< CRC stripped by hardware */
  },
  
  .txmode = {
  },
  
  .rx_adv_conf.rss_conf = {
    .rss_key = NULL,  /**< If not NULL, 40-byte hash key. */
    .rss_hf  = ETH_RSS_IPV4 | ETH_RSS_IPV4_UDP, /**< Hash functions to apply */
  }
};



/**
 * Eth Rx configuration
 */
ENV_SHARED static const struct rte_eth_rxconf  eth_rx_conf = {
  .rx_thresh = {
    .pthresh = RX_PTHRESH,
    .hthresh = RX_HTHRESH,
    .wthresh = RX_WTHRESH,
  },
};



/**
 * Eth Tx configuration
 */
ENV_SHARED static const struct rte_eth_txconf  eth_tx_conf = {
  .tx_thresh = {
    .pthresh = TX_PTHRESH,
    .hthresh = TX_HTHRESH,
    .wthresh = TX_WTHRESH,
  },
  .tx_free_thresh = 0, /* Use PMD default values */
  .tx_rs_thresh   = 0, /* Use PMD default values */
};



/**
 * Packet I/O hash params
 */
#define PACKET_Q_HASH_ENTRIES 4096

ENV_SHARED struct rte_hash_parameters
packet_q_hash_params =
{
  .name = "packet_q_hash",
  .entries = PACKET_Q_HASH_ENTRIES,
  .bucket_entries = 4,
  //.bucket_entries = 16,
  .key_len = sizeof(struct packet_dst_tuple),
  .hash_func = rte_hash_crc,
  //.hash_func = rte_jhash,
  .hash_func_init_val = 0,
  .socket_id = 0,
};




/**
 * Packet I/O flows lookup hash
 */
ENV_SHARED  packet_q_hash_t  packet_q_hash  ENV_CACHE_LINE_ALIGNED;

COMPILE_TIME_ASSERT((sizeof(packet_q_hash) % ENV_CACHE_LINE_SIZE) == 0, PACKET_Q_HASH_SIZE_ERROR);


/**
 * Mapping from hash val to actual EM-queue
 * EM-queue = packet_queues[packet_q_hash-result]
 */
ENV_SHARED  em_queue_t  packet_queues[PACKET_Q_HASH_ENTRIES]  ENV_CACHE_LINE_ALIGNED;
COMPILE_TIME_ASSERT((sizeof(packet_queues) % ENV_CACHE_LINE_SIZE) == 0, PACKET_QUEUES_SIZE_ERROR);

/** Hash lookup output containing a list of values, corresponding to the list of keys */
ENV_LOCAL  int32_t  positions[MAX_RX_PKT_BURST] ENV_CACHE_LINE_ALIGNED;
COMPILE_TIME_ASSERT((sizeof(positions) % ENV_CACHE_LINE_SIZE) == 0, POSITIONS_SIZE_ERROR);

ENV_LOCAL  packet_q_hash_key_t  key[MAX_RX_PKT_BURST]       ENV_CACHE_LINE_ALIGNED;
COMPILE_TIME_ASSERT((sizeof(key) % ENV_CACHE_LINE_SIZE) == 0, KEY_SIZE_ERROR);

ENV_LOCAL  packet_q_hash_key_t*  key_ptrs[MAX_RX_PKT_BURST]  ENV_CACHE_LINE_ALIGNED;
COMPILE_TIME_ASSERT((sizeof(key_ptrs) % ENV_CACHE_LINE_SIZE) == 0, KEY_PTRS_SIZE_ERROR);



/**
 * Temporary storage for q_elems and ev_hdrs after receiving a packet burst on Rx
 */
typedef struct
{
  em_queue_element_t *q_elem;
  em_event_hdr_t     *ev_hdr;
} rx_lookup_pair_t;

ENV_LOCAL  rx_lookup_pair_t  rx_lookup_pairs[MAX_RX_PKT_BURST]  ENV_CACHE_LINE_ALIGNED;

COMPILE_TIME_ASSERT((sizeof(rx_lookup_pairs) % ENV_CACHE_LINE_SIZE) == 0, RX_BURST_PAIRS_NOT_CACHE_ALIGNED);



/**
 * Grouping of core local data 
 */
typedef union
{
  struct
  {
    // Tx
    unsigned                   eth_tx_local_queue_id;

    // Previous timestamp value (used in em_eth_tx_packets_timed())
    uint64_t                   eth_tx_prev_tsc;    
    
    // Rx - Holds the currently dequeued eth_rx_queue_info_t and a counter
    // that tracks the number of times Rx-frames have been burst dequeued from the eth queue.
    curr_eth_rx_queue_info_t   curr_rx_queue_info;
  };
  
  uint8_t u8[ENV_CACHE_LINE_SIZE];
  
} em_pkt_local_t;

ENV_LOCAL  em_pkt_local_t  local  ENV_CACHE_LINE_ALIGNED;

COMPILE_TIME_ASSERT((sizeof(em_pkt_local_t) % ENV_CACHE_LINE_SIZE) == 0, EM_PKT_LOCAL_T_SIZE_ERROR);




/*
 * Grouping of shared variables that are almost always read-only
 */
typedef union
{
  struct
  {
    eth_ports_link_up_t    eth_ports_link_up;
    
    eth_rx_queue_access_t  eth_rx_queue_access;
    
    em_queue_t             em_default_queue;    
  };
    
  uint8_t u8[ENV_CACHE_LINE_SIZE];
  
} em_pkt_shared_readmostly_t;

ENV_SHARED   em_pkt_shared_readmostly_t  shared  ENV_CACHE_LINE_ALIGNED;

COMPILE_TIME_ASSERT((sizeof(em_pkt_shared_readmostly_t) % ENV_CACHE_LINE_SIZE) == 0, EM_PKT_SHARED_T_SIZE_ERROR);





/*
 * LOCAL FUNCTION PROTOTYPES
 */
 
static inline void
em_packet_lookup_enqueue(struct rte_mbuf *const mbufs[], const int n_mbuf, const int input_port);

static inline void
packet_enqueue(em_event_hdr_t *const ev_hdr, em_queue_element_t *const q_elem, const int input_port);

static inline void
eth_tx_packet_burst(const unsigned n, const uint8_t port, const uint16_t tx_queueid, struct rte_mbuf **const m_table);

static inline void
eth_tx_packet__no_order(void *mbuf, int port);

static inline void
eth_tx_packets_timed__ordered(void);

static inline void
eth_tx_packets_timed__no_order(void);




/*
 * FUNCTIONS
 */
 


/**
 * Initialize the Intel NICs (once at startup on one core)
 */
void 
intel_eth_init(void)
{
  unsigned int  nb_ports;
  unsigned int  nb_lcores;  
  unsigned int  portid;
  unsigned int  queueid;
  
  uint16_t      n_rx_queue;
  uint16_t      n_tx_queue;
  
  int           i, j;
  int           ret;
  
  struct rte_eth_dev_info  dev_info;
  struct rte_eth_link      link;
  struct rte_mempool      *eth_mempool;  
  
  
  shared.em_default_queue = EM_QUEUE_UNDEF;
  
  memset(&shared.eth_ports_link_up, 0, sizeof(shared.eth_ports_link_up));
  
  
  memset(&eth_rx_queue_info[0], 0, sizeof(eth_rx_queue_info));
  memset(&shared.eth_rx_queue_access, 0, sizeof(shared.eth_rx_queue_access));
  
  /* Eth Rx queue aceess control - queue is multi-consumer and multi-producer */
  shared.eth_rx_queue_access.queue = rte_ring_create("EthRxPortAccess", MAX_ETH_RX_QUEUES, DEVICE_SOCKET, 0);
  
  
  memset(eth_tx_mbuf_tables, 0, sizeof(eth_tx_mbuf_tables));
  
  for(j = 0; j < MAX_ETH_PORTS; j++)
  {
    char  def_name[] = "m_burst_";
    char  name[sizeof("m_burst_00000000")];
    
    for(i = 0; i < MAX_ETH_TX_MBUF_TABLES; i++)
    {
      env_spinlock_init(&eth_tx_mbuf_tables[j][i].lock);
      
      (void) snprintf(name, sizeof(name)-1, "%s-%i-%i", def_name, j, i);
      name[sizeof(name)-1] = '\0';
      /* NOTE: RING_F_SC_DEQ-flag used => rte_ring_sc_dequeue_bulk() explicitly used! Don't change flag! */
      eth_tx_mbuf_tables[j][i].m_burst = rte_ring_create(name, 128 * MAX_TX_PKT_BURST, DEVICE_SOCKET, RING_F_SC_DEQ);
    }
  }
  
  
  
  /* 
   * Init eth driver(s) 
   */
  
#ifdef RTE_LIBRTE_IGB_PMD
  /* 1GE Kawela */
  ret = rte_igb_pmd_init();
  
  ERROR_IF(ret < 0, EM_FATAL(EM_ERR_LIB_FAILED), EM_ESCOPE_PACKETIO_INTEL_ETH_INIT,
           "Cannot init 1GE (82576) pmd, ret=%i", ret);
#endif


#ifdef RTE_LIBRTE_IXGBE_PMD
  /* 10GE Niantic */
  ret = rte_ixgbe_pmd_init();
  
  ERROR_IF(ret < 0, EM_FATAL(EM_ERR_LIB_FAILED), EM_ESCOPE_PACKETIO_INTEL_ETH_INIT,
           "Cannot init 10GE (82599) pmd, ret=%i", ret);
#endif


  ret = rte_eal_pci_probe();
  
  ERROR_IF(ret < 0, EM_FATAL(EM_ERR_LIB_FAILED), EM_ESCOPE_PACKETIO_INTEL_ETH_INIT,
           "Cannot probe PCI, ret=%i", ret);


  /* Get the number of ethernet ports */
  nb_ports = rte_eth_dev_count();
  
  ERROR_IF(nb_ports == 0, EM_FATAL(EM_ERR_LIB_FAILED), EM_ESCOPE_PACKETIO_INTEL_ETH_INIT,
           "No Ethernet port - bye!");
  
  
  /* Get number of running cores */
  nb_lcores = rte_lcore_count();
  
  printf("%s(): Eth ports:%u Cores:%u \n",__func__, nb_ports, nb_lcores);
    


  /* Number of Eth TX queues per Eth port */
  n_tx_queue = MAX_ETH_TX_MBUF_TABLES + nb_lcores; // NOTE: '+nb_lcores' is Tx-Q for parallel flows not requiring Tx in order, one for each core per port
  
  /* Number of Eth RX queues per Eth port */
  // n_rx_queue = nb_lcores;
  // n_rx_queue = (nb_lcores + (nb_ports-1))/nb_ports;
  n_rx_queue = (nb_lcores / nb_ports) + 1;
  if(n_rx_queue > 16) {
    n_rx_queue = 16; // Max RSS queues
  }


    
  /*
   * Print info about each Ethernet Port
   */
  for(portid = 0; portid < nb_ports; portid++)
  {
    rte_eth_dev_info_get((uint8_t) portid, &dev_info);
    
    printf(                         "\n" 
           "Eth dev info - port %u" "\n"
           // "struct rte_pci_device *pci_dev" /**< Device PCI informations. */
           "  driver_name    = %s"  "\n"      /**< Device Driver name. */
           "  min_rx_bufsize = %u"  "\n"      /**< Minimum size of RX buffer. */
           "  max_rx_pktlen  = %u"  "\n"      /**< Maximum configurable length of RX pkt. */
           "  max_rx_queues  = %u"  "\n"      /**< Maximum number of RX queues. */
           "  max_tx_queues  = %u"  "\n"      /**< Maximum number of TX queues. */
           ,
           portid,
           // dev_info.pci_dev,
           dev_info.driver_name,
           dev_info.min_rx_bufsize,
           dev_info.max_rx_pktlen,
           dev_info.max_rx_queues,
           dev_info.max_tx_queues
           );
    
    assert(n_rx_queue <= dev_info.max_rx_queues);
    assert(n_tx_queue <= dev_info.max_tx_queues);
  }

  
  
  /* Allocate the packet buffers from the EM event pool - events and frames/packets can be interchanged */
  eth_mempool = (struct rte_mempool *) em_event_pool.pool;
  
  ERROR_IF(eth_mempool == NULL, EM_FATAL(EM_ERR_LIB_FAILED), EM_ESCOPE_PACKETIO_INTEL_ETH_INIT,
           "eth_mempool==NULL!");

  
  /* 
   * Initialise each eth port 
   */
  for(portid = 0; portid < nb_ports; portid++)
  {
    /* Init port */
    printf("Initializing Eth port %u  RxQs:%u TxQs:%u ", portid, n_rx_queue, n_tx_queue);
  
    ret = rte_eth_dev_configure((uint8_t) portid, n_rx_queue, n_tx_queue, &eth_port_conf);

    ERROR_IF(ret < 0, EM_FATAL(EM_ERR_LIB_FAILED), EM_ESCOPE_PACKETIO_INTEL_ETH_INIT,
             "rte_eth_dev_configure(): Cannot configure Eth device: err=%d, port=%u", ret, portid);


    rte_eth_macaddr_get((uint8_t) portid, &port_eth_addr[portid]);
    
    printf("MAC:%.2X:%.2X:%.2X:%.2X:%.2X:%.2X ",
           port_eth_addr[portid].addr_bytes[0], port_eth_addr[portid].addr_bytes[1], port_eth_addr[portid].addr_bytes[2],
           port_eth_addr[portid].addr_bytes[3], port_eth_addr[portid].addr_bytes[4], port_eth_addr[portid].addr_bytes[5]
          );

  
    /* Init several RX queues per port */
    for(queueid = 0; queueid < n_rx_queue; queueid++)
    {
      ret = rte_eth_rx_queue_setup((uint8_t) portid, queueid, ETH_RX_DESC_DEFAULT, DEVICE_SOCKET, &eth_rx_conf, eth_mempool);
      
      ERROR_IF(ret < 0, EM_FATAL(EM_ERR_LIB_FAILED), EM_ESCOPE_PACKETIO_INTEL_ETH_INIT,
             "rte_eth_rx_queue_setup(): err=%d, port=%u", ret, portid);
    }

  
    /* Init a TX queue for each each port on each core */
    for(queueid = 0; queueid < n_tx_queue; queueid++)
    { 
      ret = rte_eth_tx_queue_setup((uint8_t) portid, (uint16_t) queueid, ETH_TX_DESC_DEFAULT, DEVICE_SOCKET, &eth_tx_conf);

      ERROR_IF(ret < 0, EM_FATAL(EM_ERR_LIB_FAILED), EM_ESCOPE_PACKETIO_INTEL_ETH_INIT,
              "rte_eth_tx_queue_setup(): err=%d, port=%u queue=%u", ret, portid, queueid);
    }

 
    /* Start the device */
    ret = rte_eth_dev_start((uint8_t) portid);

    ERROR_IF(ret < 0, EM_FATAL(EM_ERR_LIB_FAILED), EM_ESCOPE_PACKETIO_INTEL_ETH_INIT,
            "rte_pmd_port_start(): err=%d, port=%u", ret, portid);
            
  
    printf("done: ");
  
    /* get link status */
    rte_eth_link_get((uint8_t) portid, &link);
    
    if(link.link_status)
    {
      int idx = shared.eth_ports_link_up.n_link_up;
      
      shared.eth_ports_link_up.port[idx].portid     = portid;
      shared.eth_ports_link_up.port[idx].n_rx_queue = n_rx_queue;
      shared.eth_ports_link_up.port[idx].n_tx_queue = n_tx_queue;
      
      shared.eth_ports_link_up.n_link_up++;
      shared.eth_ports_link_up.n_rx_queues += n_rx_queue;
      shared.eth_ports_link_up.n_tx_queues += n_tx_queue;
      
      printf(" Link Up - speed %u Mbps - %s\n",
             (uint32_t) link.link_speed,
             (link.link_duplex == ETH_LINK_FULL_DUPLEX) ?
             ("full-duplex") : ("half-duplex\n"));
    }
    else {
      printf(" Link Down\n");
    }

    printf("\n");
  }
  
  
  
  
  for(i = 0, j = 0; i < shared.eth_ports_link_up.n_link_up; i++)
  {
    int k;
    
    assert(shared.eth_ports_link_up.port[i].n_rx_queue <= UINT8_MAX);
    
    for(k = 0; k < shared.eth_ports_link_up.port[i].n_rx_queue; k++, j++)
    {
      assert(shared.eth_ports_link_up.port[i].portid <= UINT8_MAX);
      
      eth_rx_queue_info[j].port_id  = (uint8_t) shared.eth_ports_link_up.port[i].portid;
      eth_rx_queue_info[j].queue_id = (uint8_t) k;
      
      ret = rte_ring_enqueue(shared.eth_rx_queue_access.queue, &eth_rx_queue_info[j]);
      assert(ret == 0);
    }
  }
  assert(j == shared.eth_ports_link_up.n_rx_queues);




  
  
  env_sync_mem();
}



/**
 * Local packet I/O init (run on each core once at strtup)
 */
void
intel_eth_init_local(void)
{
  int i, j;
  
  unsigned core_id = em_core_id(); // EM core ids always start from 0
  unsigned n_cores = em_core_count();
  
  
  memset(&local, 0, sizeof(local));
  
  local.eth_tx_prev_tsc = 0; 
  local.curr_rx_queue_info.access_cnt   = 0;
  local.curr_rx_queue_info.current_info = NULL;
  
  
  
  memset(eth_tx_mbuf_tables_local, 0, sizeof(eth_tx_mbuf_tables_local));
  
  // Set Eth Tx queue id for this local core for parallel em-queues
  local.eth_tx_local_queue_id = MAX_ETH_TX_MBUF_TABLES + core_id;                              
  printf("\nEM-core%u: Eth Tx Queue Id (LOCAL):%u\n", core_id, local.eth_tx_local_queue_id);
  
  
  
  
  /*
   * Initialize the Eth Tx drain queues used by em_eth_tx_packets_timed()
   * to drain the queues listed in .id[]
   * Each core gets a subset of the tx queues to drain - assigned at startup.
   * Each core drains the same set of queues per eth-Tx-port.
   */
  printf("EM-core%u: Eth-Tx-Drain-Queues ", core_id);
  memset(&eth_tx_drain_queues__local, 0, sizeof(eth_tx_drain_queues__local));
  
  eth_tx_drain_queues__local.curr_idx = 0;
  
  /* 
   * NOTE: Nbr of Eth-Tx-queues for ordered flows per eth-port is 'MAX_ETH_TX_MBUF_TABLES' !!!
   */
  for(i = 0, j = 0; i < MAX_ETH_TX_MBUF_TABLES; i++)
  {
    unsigned tx_q_idx = (i * n_cores) + core_id;
    
    if(tx_q_idx < MAX_ETH_TX_MBUF_TABLES)
    {
      eth_tx_drain_queues__local.id[j] = tx_q_idx;
      printf("[%u]=%u ", j, tx_q_idx);
      j++;
    }
  }
  
  eth_tx_drain_queues__local.len = j;
  printf("len:%i\n", j); fflush(NULL);
  
}



/**
 * Initialize the Packet I/O flow lookup hash
 */
void
intel_init_packet_q_hash_global(void)
{
  int i; 
  
  env_spinlock_init(&packet_q_hash.lock);
  
  /* create the hash */
  packet_q_hash.hash = rte_hash_create(&packet_q_hash_params);
  
  ERROR_IF(packet_q_hash.hash == NULL, EM_FATAL(EM_ERR_LIB_FAILED),
           EM_ESCOPE_PACKETIO_INIT_PACKET_Q_HASH,
           "Unable to create the packet_q hash\n");
  
  
  for(i = 0; i < PACKET_Q_HASH_ENTRIES; i++) {
    packet_queues[i] = EM_QUEUE_UNDEF;
  }
}



/**
 * Local init of flow lookup
 */
void
intel_init_packet_q_hash_local(void)
{
  int i;
  
  memset(key,      0, sizeof(key));
  memset(key_ptrs, 0, sizeof(key_ptrs));
  
  
  for(i = 0; i < MAX_RX_PKT_BURST; i++) {
    key_ptrs[i] = &key[i];
  }
}




/**
 * Transmit an ethernet frame
 * 
 * The Tx func needs to take into account the source EM-queue type to e.g. maintain packet
 * order where necessary.
 * 
 * @param event   The EM event to be transmitted (note: event must contain eth/IP hdrs)
 * @param port    Tx port for transmission
 */
void
em_eth_tx_packet(em_event_t event, const int port)
{
  em_event_hdr_t        *const ev_hdr   = event_to_event_hdr(event);
  struct rte_mbuf       *const m        = event_hdr_to_mbuf(ev_hdr); 
  const em_queue_t       src_queue      = ev_hdr->q_elem->id;
  const em_queue_type_t  src_queue_type = ev_hdr->src_q_type;
  const uint16_t         tx_queueid     = EM_QUEUE_TO_MBUF_TBL(src_queue);
  
  
  if(src_queue_type == EM_QUEUE_TYPE_ATOMIC)
  {
    PREFETCH_RTE_RING(eth_tx_mbuf_tables[port][tx_queueid].m_burst);
    
    eth_tx_packet__ordered(m, port, tx_queueid);
  }
  else if(src_queue_type == EM_QUEUE_TYPE_PARALLEL)
  {
    eth_tx_packet__no_order(m, port);
  }  
  else if(src_queue_type == EM_QUEUE_TYPE_PARALLEL_ORDERED)
  {
    ev_hdr->io_port = port;
    em_send_from_parallel_ord_q(ev_hdr, NULL, port, OPERATION_ETH_TX);
  }
  else // EM_QUEUE_TYPE_UNDEF
  {
    /* 
     * EM_QUEUE_TYPE_UNDEF: If the EO just allocated the event and
     * directly called Tx then the type is EM_QUEUE_TYPE_UNDEF.
     */
    
    // Get the src queue type directly from the q_elem
    const em_queue_type_t queue_type = ev_hdr->q_elem->scheduler_type;
    
    if(queue_type == EM_QUEUE_TYPE_PARALLEL) {
      eth_tx_packet__no_order(m, port);
    }
    else
    { 
      // Ordered src queues: EM_QUEUE_TYPE_ATOMIC or EM_QUEUE_TYPE_PARALLEL_ORDERED
      eth_tx_packet__ordered(m, port, tx_queueid);
    }
  }
}




/**
 * Send the packet on an output interface, maintains packet order
 */
void 
eth_tx_packet__ordered(void *mbuf, int port, uint16_t tx_queueid)
{
  eth_tx_mbuf_table_t *const tx_mbuf_table = &eth_tx_mbuf_tables[port][tx_queueid];
  env_spinlock_t      *const lock          = &tx_mbuf_table->lock;
  const unsigned             len           = rte_ring_count(tx_mbuf_table->m_burst);
  int                        ret           = -1;
  
  
  IF_UNLIKELY((len >= (MAX_TX_PKT_BURST-1)) && (!env_spinlock_is_locked(lock)))
  {
    IF_LIKELY(env_spinlock_trylock(lock))
    {
      ret = rte_ring_sc_dequeue_bulk(tx_mbuf_table->m_burst, (void **) tx_burst_m_table, MAX_TX_PKT_BURST-1);
      
      IF_LIKELY(ret == 0)
      { 
        // Dequeue success!
        tx_burst_m_table[MAX_TX_PKT_BURST-1] = mbuf;
        eth_tx_packet_burst(MAX_TX_PKT_BURST, port, tx_queueid,  tx_burst_m_table);
      }
      
      env_spinlock_unlock(lock);
    }
  }
  
  
  if(ret != 0)
  {
    // Default operation - enqueue new buffer
    ret = rte_ring_enqueue(tx_mbuf_table->m_burst, mbuf);
    
    IF_UNLIKELY(ret == (-ENOBUFS))
    {
      rte_pktmbuf_free(mbuf);
      (void) EM_INTERNAL_ERROR(EM_ERR_LIB_FAILED, EM_ESCOPE_PACKETIO_ETH_TX_PACKET_ORDERED,
                               "Enqueue failed (%i) - drop frame!", ret);    
    }
  }
  
}




/**
 * Send the packet on an output interface, don't care about packet order
 */
static inline void                                               
eth_tx_packet__no_order(void *mbuf, int port)
{
  eth_tx_mbuf_table_local_t *const tx_mbuf_tbl = &eth_tx_mbuf_tables_local[port];
  struct rte_mbuf           *const m           =  mbuf;
  unsigned                   len;

  
  len = tx_mbuf_tbl->len;
  tx_mbuf_tbl->m_table[len] = m;
  len++;

  /* enough pkts to be sent */
  IF_UNLIKELY(len == MAX_TX_PKT_BURST)
  {
    const unsigned tx_queue_id = local.eth_tx_local_queue_id;
    
    eth_tx_packet_burst(MAX_TX_PKT_BURST, port, tx_queue_id,  tx_mbuf_tbl->m_table);
    len = 0;
  }

  tx_mbuf_tbl->len = len;
}




/**
 * Burst frames on an output interface
 */
static inline void
eth_tx_packet_burst(const unsigned n, const uint8_t port, const uint16_t tx_queueid, struct rte_mbuf **const m_table)
{
  unsigned ret;
  unsigned i;
  
  
  for(i = 0; i < n; i++) {
    ENV_PREFETCH(m_table[i]);
  }


  ret = rte_eth_tx_burst(port, tx_queueid, m_table, (uint16_t) n);
  
  IF_UNLIKELY(ret < n)
  {
    (void) EM_INTERNAL_ERROR(EM_ERR_LIB_FAILED, EM_ESCOPE_PACKETIO_ETH_TX_PACKET_BURST,
                             "Eth Tx burst(%u) failed port=%u, txqueue=%u\n", n, (unsigned) port, tx_queueid);
    
    do {
      rte_pktmbuf_free(m_table[ret]);
    } while (++ret < n);
  }
  
}




/**
 * Step through assigned Tx-queues per port and drain contents every once in a while
 * Improves latency on low frame rates but may slightly slow down top-speed...
 */
void
em_eth_tx_packets_timed(void)
{
  uint64_t             diff_tsc;
  uint64_t             cur_tsc;


  cur_tsc = rte_rdtsc(); // core-local timestamp


  diff_tsc = cur_tsc - local.eth_tx_prev_tsc;


  /*
   * TX burst queue drain
   */
  IF_UNLIKELY(diff_tsc > BURST_TX_DRAIN)
  {
    eth_tx_packets_timed__ordered();
  
    eth_tx_packets_timed__no_order();
    
    /* Update timestamp for next round */
    local.eth_tx_prev_tsc = cur_tsc;
  }
}




/**
 * Drain packet queues containing packets from ordered EM-queues
 */
static inline void
eth_tx_packets_timed__ordered(void)
{
  unsigned int         portid;
  unsigned int         tx_queue_id;
  unsigned int         idx;
  eth_tx_mbuf_table_t *tx_mbuf_table;
  int                  i;
  int                  ret;
  
  
  idx         = eth_tx_drain_queues__local.curr_idx;
  tx_queue_id = eth_tx_drain_queues__local.id[idx];

  
  for(i = 0; i < shared.eth_ports_link_up.n_link_up; i++)
  {
    portid        = shared.eth_ports_link_up.port[i].portid;
    tx_mbuf_table = &eth_tx_mbuf_tables[portid][tx_queue_id];
    
    
    if(rte_ring_empty(tx_mbuf_table->m_burst)) {
      continue;
    }
    

    IF_LIKELY(env_spinlock_trylock(&tx_mbuf_table->lock))
    {
      unsigned cnt = rte_ring_count(tx_mbuf_table->m_burst);
      
      cnt = (cnt < MAX_TX_PKT_BURST) ? cnt : MAX_TX_PKT_BURST;
      
      ret = rte_ring_sc_dequeue_bulk(tx_mbuf_table->m_burst, (void ** ) tx_burst_m_table, cnt);
      
      IF_LIKELY(!ret) {
        eth_tx_packet_burst(cnt, portid, tx_queue_id, tx_burst_m_table);
      }
      
      env_spinlock_unlock(&tx_mbuf_table->lock);
    }
  }
  
  
  /* Update Tx-Queue index for next round */
  eth_tx_drain_queues__local.curr_idx = (idx + 1) < eth_tx_drain_queues__local.len ? (idx + 1) : 0;
}




/**
 * Drain packet queues containing packets from unordered EM-queues
 */
static inline void
eth_tx_packets_timed__no_order(void)
{
  const unsigned             tx_queue_id = local.eth_tx_local_queue_id;
  unsigned int               portid;
  eth_tx_mbuf_table_local_t *tx_mbuf_table;
  int                        i;
  
  
  for(i = 0; i < shared.eth_ports_link_up.n_link_up; i++)
  { 
    portid        = shared.eth_ports_link_up.port[i].portid;
    tx_mbuf_table = &eth_tx_mbuf_tables_local[portid];
    
    IF_LIKELY(tx_mbuf_table->len > 0)
    {
      //printf("%s(): EM-core:%u port:%u txQ:%u frames:%u\n", __func__, em_core_id(), portid, tx_queue_id, tx_mbuf_table->len); fflush(NULL);
      eth_tx_packet_burst(tx_mbuf_table->len, (uint8_t) portid, tx_queue_id, tx_mbuf_table->m_table);
      tx_mbuf_table->len = 0;
    }    
  }
  
}




/**
 * Read frames from the Eth RX queues
 */ 
void
em_eth_rx_packets(void)
{
  int              owns_rx_queue = 1;
  int              nb_rx;
  int              ret;
  uint8_t          portid;
  uint8_t          rx_queue;


  
  IF_UNLIKELY(local.curr_rx_queue_info.access_cnt == 0)
  {
    ret = rte_ring_dequeue(shared.eth_rx_queue_access.queue, (void **) &local.curr_rx_queue_info.current_info);
    owns_rx_queue = !ret;
  }
    
  
  
  IF_LIKELY(owns_rx_queue)
  {
    portid   = local.curr_rx_queue_info.current_info->port_id;
    rx_queue = local.curr_rx_queue_info.current_info->queue_id;

    nb_rx = rte_eth_rx_burst(portid, rx_queue, rx_burst_m_table, MAX_RX_PKT_BURST);

    IF_LIKELY(nb_rx > 0)
    {
      /*
       * Enqueue the received frames into the proper queues for load-balancing and processing.
       * Alternatively, if Rx-Direct-Dispatch is enabled, start processing of packet directly.
       */
      em_packet_lookup_enqueue(rx_burst_m_table, nb_rx, portid);
    }
    
    
    local.curr_rx_queue_info.access_cnt++;
    
    IF_UNLIKELY((local.curr_rx_queue_info.access_cnt == ETH_RX_IDX_CNT_MAX) || (nb_rx == 0))
    {
      // Unlock ONLY when count==ETH_RX_IDX_CNT_MAX or no frames was received here
      ret = rte_ring_enqueue(shared.eth_rx_queue_access.queue, local.curr_rx_queue_info.current_info);
      
      // Advance to the next Rx-queue for the next iteration if little data was seen here or we have received 'enough'
      local.curr_rx_queue_info.access_cnt = 0;
    }
  }
  else
  {
    // Did not get the Rx-queue lock - next time try another Rx queue instead
    local.curr_rx_queue_info.access_cnt = 0;
  }
  
}




/**
 * Hash lookup to associate the received frame with an EM-queue into which to enqueue
 */
static inline void
em_packet_lookup_enqueue(struct rte_mbuf *const mbufs[], const int n_mbuf, const int input_port)
{
  em_queue_t           queue;
  em_queue_element_t  *q_elem;
  em_event_hdr_t      *ev_hdr;
  int32_t              ret;
  int                  i;
  int                  n_mbuf_valid;
  
  struct rte_mbuf     *m;
  struct ip_hdr       *ip;
  struct udp_hdr      *udp;


  ENV_PREFETCH(packet_q_hash.hash);
  ENV_PREFETCH_NEXT_LINE(packet_q_hash.hash);

  /*
   * Fill in the hash lookup keys from the received packets
   */
  for(i = 0; i < n_mbuf; i++)
  {
    m   = mbufs[i];
    ip  = (struct ip_hdr *)(rte_pktmbuf_mtod(m, unsigned char *) + sizeof(struct ether_hdr));
    udp = (struct udp_hdr *)((unsigned char *) ip + sizeof(struct ip_hdr));
    
    // NOTE! BE-to-CPU conversion not needed here. Setup stores BE-order in hash to avoid conversion for every packet.
    key[i].ip_dst = ip->dst_addr;
    key[i].proto  = ip->next_proto_id;
    
    #if 1
      key[i].port_dst = (likely((ip->next_proto_id == INET_IPPROTO_UDP)||(ip->next_proto_id == INET_IPPROTO_TCP))) ? udp->dst_port : 0 ;
    #else
      IF_LIKELY((ip->next_proto_id == INET_IPPROTO_UDP) ||
                (ip->next_proto_id == INET_IPPROTO_TCP))
      {
        key[i].port_dst = udp->dst_port; // Valid for both TCP & UDP
      }
      else {
        key[i].port_dst = 0;
      }
    #endif
  }
  
  
  for(i = 0; i < n_mbuf; i += RTE_HASH_LOOKUP_MULTI_MAX)
  {
    int bufs, j;
    
    bufs = n_mbuf - i;
    
    if(bufs > RTE_HASH_LOOKUP_MULTI_MAX) {
      bufs = RTE_HASH_LOOKUP_MULTI_MAX;
    }
    
    /*
     * Hash lookup for multiple keys at once.
     * NOTE: key_ptrs[] = {&key[0], &key[1], ... , &key[N-1]} set once per core at initialization
     */
    ret = rte_hash_lookup_multi(packet_q_hash.hash, (const void **) &key_ptrs[i], bufs, &positions[i]);
    
    /* Free all packets/frames if the whole lookup was a failure (should not happen!!!) */
    IF_UNLIKELY(ret < 0)
    {
      for(j = 0; j < n_mbuf; j++) {
        rte_pktmbuf_free(mbufs[j]);
      }
      return;
    }
  }
  
  
  ENV_PREFETCH(rx_lookup_pairs);
  //ENV_PREFETCH_NEXT_LINE(rx_lookup_pairs);
  
  /*
   * Store (temporarily) associated q_elem+ev_hdr pair as found from the hash
   */
  for(i = 0, n_mbuf_valid = 0; i < n_mbuf; i++)
  {
    int32_t pos;
    
    m      = mbufs[i];
    ev_hdr = mbuf_to_event_hdr(m);
    
    pos = positions[i];
    
    
    IF_LIKELY(pos >= 0) {
      queue = packet_queues[pos];
    }
    else {
      // Not found!
      IF_LIKELY(shared.em_default_queue != EM_QUEUE_UNDEF)
      {
        // Default queue set - use this.
        queue = shared.em_default_queue;
      }
      else {
        rte_pktmbuf_free(m);
        continue;  
      }      
    }
    
    q_elem = &em_queue_element_tbl[queue];
    
    rx_lookup_pairs[n_mbuf_valid].q_elem = q_elem;
    rx_lookup_pairs[n_mbuf_valid].ev_hdr = ev_hdr;
    n_mbuf_valid++;
  }

  
  
  /*
   * Enqueue (or direct dispatch) the packets/events that belong to a valid queue.
   */

  /* Prefetch first set of ev_hdr+q_elem:s */
  for(i = 0; (i < PREFETCH_OFFSET) && (i < n_mbuf_valid); i++)
  {
    ENV_PREFETCH(rx_lookup_pairs[i].ev_hdr);
    
    PREFETCH_Q_ELEM(rx_lookup_pairs[i].q_elem);
  }
  
  /* Prefetch new and enqueue() already prefetched ev_hdr+q_elem:s */
  for(i = 0; i < (n_mbuf_valid - PREFETCH_OFFSET); i++)
  {
    ENV_PREFETCH(rx_lookup_pairs[i+PREFETCH_OFFSET].ev_hdr);
    
    PREFETCH_Q_ELEM(rx_lookup_pairs[i+PREFETCH_OFFSET].q_elem);
  
    ev_hdr = rx_lookup_pairs[i].ev_hdr;
    q_elem = rx_lookup_pairs[i].q_elem;
        
    packet_enqueue(ev_hdr, q_elem, input_port);
  }
  
  /* Enqueue() the remaining prefetched ev_hdr+q_elem:s */
  for(; i < n_mbuf_valid; i++)
  {
    ev_hdr = rx_lookup_pairs[i].ev_hdr;
    q_elem = rx_lookup_pairs[i].q_elem;
        
    packet_enqueue(ev_hdr, q_elem, input_port);    
  }
  
  
}




/**
 * Enqueue a received packet-IO event into the associated EM-queue.
 *
 * Note: RX_DIRECT_DISPATCH is a performance optimization that dispatches the event directly
 * for processing on the receiving core if possible. By bypassing the EM-queue enqueue+dequeue
 * a potential speedup can be achieved.
 */
static inline void
packet_enqueue(em_event_hdr_t *const ev_hdr, em_queue_element_t *const q_elem, const int input_port)
{
  const em_event_t event = event_hdr_to_event(ev_hdr);
   
  
  /* Only fill essential ev_hdr fields, the rest get filled in later based on queue type */
  ev_hdr->q_elem          = NULL;
  ev_hdr->src_q_type      = EM_QUEUE_TYPE_UNDEF;
  ev_hdr->event_type      = EM_EVENT_TYPE_PACKET;
  ev_hdr->event_group     = EM_EVENT_GROUP_UNDEF;
  
  // ev_hdr->lock_p          = NULL; // no lock needed yet
  // ev_hdr->dst_q_elem      = NULL;
  // ev_hdr->processing_done = 0;
  // ev_hdr->operation       = 0;
  
  ev_hdr->io_port         = input_port;
      
  
  #if RX_DIRECT_DISPATCH == 1
  {
    // Rx Optimized version
    em_direct_dispatch(event, ev_hdr, q_elem, q_elem->id);
  }
  #else
  {
    // Enqueue to an EM queue - NORMAL Operation
    em_status_t err = em_send_switch(ev_hdr, q_elem, q_elem->id);
    
    IF_UNLIKELY(err != EM_OK){
      em_free(event);
    }
  }
  #endif  
}




/**
 * Set the default EM-queue for packet I/O
 */ 
int
em_packet_default_queue(em_queue_t queue)
{
  shared.em_default_queue = queue;

  env_sync_mem();

  return 0;
}




/**
 * Associate an EM-queue with a packet-I/O flow.
 *
 * Received packets matching the set destination IP-addr/port will end up in the EM-queue 'queue'.
 */
void
em_packet_add_io_queue(uint8_t proto, uint32_t ipv4_dst, uint16_t port_dst, em_queue_t queue)
{
  /* populate the hash */

  packet_q_hash_key_t       key;
  int32_t                   ret;
  em_queue_element_t *const q_elem = get_queue_element(queue);
  
  
  ERROR_IF(invalid_q_elem(q_elem), EM_FATAL(EM_ERR_BAD_POINTER), EM_ESCOPE_PACKETIO_ADD_IO_QUEUE,
           "Invalid q_elem=0x%"PRIX64", queue=%"PRI_QUEUE"!", (uint64_t) q_elem, queue);
  
  
  // memset(&key, 0, sizeof(key));
  key.ip_dst   = rte_cpu_to_be_32(ipv4_dst);
  key.port_dst = rte_cpu_to_be_16(port_dst);
  key.proto    = proto;
  
  
  env_spinlock_lock(&packet_q_hash.lock);
  
  ret = rte_hash_add_key(packet_q_hash.hash, (void *) &key);
  
  ERROR_IF(ret < 0, EM_FATAL(EM_ERR_LIB_FAILED), EM_ESCOPE_PACKETIO_ADD_IO_QUEUE,
           "Unable to add entry to the packet_q_hash (ret=%i)", ret);
           
  
  packet_queues[ret] = queue;
  
  
  q_elem->pkt_io_proto    = proto;
  q_elem->pkt_io_ipv4_dst = ipv4_dst;
  q_elem->pkt_io_port_dst = port_dst;
  
  q_elem->pkt_io_enabled = 1;
  
  
  env_spinlock_unlock(&packet_q_hash.lock);
  
  env_sync_mem();
  
  // printf("%s()-queue=%"PRI_QUEUE" ret=%i packet_queues[ret]=%"PRI_QUEUE"\n", __func__, queue, ret, packet_queues[ret]);
}




/**
 * Remove the association between a packet-IO flow and an EM-queue.
 * 
 * No further received frames will en up in the EM-queue 'queue'
 */
void
em_packet_rem_io_queue(uint8_t proto, uint32_t ipv4_dst, uint16_t port_dst, em_queue_t queue)
{
  /* populate the hash */

  packet_q_hash_key_t       key;
  int32_t                   ret;
  em_queue_element_t *const q_elem = get_queue_element(queue);
  

  ERROR_IF(invalid_q_elem(q_elem), EM_FATAL(EM_ERR_BAD_POINTER), EM_ESCOPE_PACKETIO_REM_IO_QUEUE,
           "Invalid q_elem=0x%"PRIX64", queue=%"PRI_QUEUE"!", (uint64_t) q_elem, queue);
  

  // memset(&key, 0, sizeof(key));
  key.ip_dst   = rte_cpu_to_be_32(ipv4_dst);
  key.port_dst = rte_cpu_to_be_16(port_dst);
  key.proto    = proto;
  
  
  env_spinlock_lock(&packet_q_hash.lock);
  
  ret = rte_hash_del_key(packet_q_hash.hash, (void *) &key);


  ERROR_IF(ret < 0, EM_FATAL(EM_ERR_LIB_FAILED), EM_ESCOPE_PACKETIO_ADD_IO_QUEUE,
           "Unable to remove entry from the packet_q_hash (ret=%i)", ret);
           
  ERROR_IF(packet_queues[ret] != queue, EM_FATAL(EM_ERR_LIB_FAILED), EM_ESCOPE_PACKETIO_ADD_IO_QUEUE,
           "Queue given as arg(%"PRI_QUEUE") does not match the stored lookup-queue(%"PRI_QUEUE")! packet_q_hash ret=%i\n",
           queue, packet_queues[ret], ret);
                      
  
  packet_queues[ret] = EM_QUEUE_UNDEF;

  q_elem->pkt_io_enabled  = 0;
  q_elem->pkt_io_proto    = 0;
  q_elem->pkt_io_ipv4_dst = 0;
  q_elem->pkt_io_port_dst = 0;  
  
  env_spinlock_unlock(&packet_q_hash.lock);
  
  env_sync_mem();
  
  // printf("%s()-queue=%"PRI_QUEUE" ret=%i packet_queues[ret]=%"PRI_QUEUE"\n", __func__, queue, ret, packet_queues[ret]);
}




/**
 * Provide applications a way to do a hash-lookup (e.g. sanity check etc.)
 */
em_queue_t
em_packet_queue_lookup_sw(uint8_t proto, uint32_t ipv4_dst, uint16_t port_dst)
{
  packet_q_hash_key_t  key;
  int32_t              ret;
  em_queue_t           queue;
  
  
  // memset(&key, 0, sizeof(key));
  key.ip_dst   = rte_cpu_to_be_32(ipv4_dst);
  key.port_dst = rte_cpu_to_be_16(port_dst);
  key.proto    = proto;
  
  ret = rte_hash_lookup(packet_q_hash.hash, (const void *)&key);
  
  queue = (ret < 0) ? EM_QUEUE_UNDEF : packet_queues[ret];
    
  return queue;
}



