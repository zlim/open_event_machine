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
 
#ifndef EM_INLINE_H_
#define EM_INLINE_H_

#include "event_machine.h"
#include "event_machine_group.h"
#include "event_machine_helper.h"

#include "environment.h"

// Generic double linked list
#include "misc_list.h"


#include "em_intel.h"
#include "em_error.h"
#include "em_intel_event_group.h"

#ifdef EVENT_TIMER
  #include "event_timer.h"  
#endif  


#include "em_shared_data.h"


#ifdef __cplusplus
extern "C" {
#endif



static inline em_queue_element_t*
m_list_node_to_queue_elem(m_list_head_t* list_node)
{
  em_queue_element_t *const q_elem = (em_queue_element_t*) (((uint64_t)list_node) - offsetof(em_queue_element_t, list_node));
  
  return (likely(list_node != NULL) ? q_elem : NULL);
}


static inline em_queue_element_t*
m_list_qgrp_node_to_queue_elem(m_list_head_t* qgrp_node)
{
  em_queue_element_t *const q_elem = (em_queue_element_t*) (((uint64_t)qgrp_node) - offsetof(em_queue_element_t, qgrp_node));
  
  return (likely(qgrp_node != NULL) ? q_elem : NULL);
}


static inline em_eo_element_t*
m_list_head_to_eo_elem(m_list_head_t* list_head)
{
  return (em_eo_element_t*) list_head;
}
// Verify that the function above returns the correct pointer
COMPILE_TIME_ASSERT(offsetof(em_eo_element_t, list_head) == 0, EM_EO_ELEMENT_T__LIST_HEAD_OFFSET_ERROR);



static inline em_event_hdr_t*
event_to_event_hdr(em_event_t event)
{
  return (em_event_hdr_t *) (((uint8_t *) event) - RTE_PKTMBUF_HEADROOM);
}


static inline em_event_t
event_hdr_to_event(em_event_hdr_t* event_hdr)
{
  return (em_event_t) (((uint8_t *) event_hdr) + RTE_PKTMBUF_HEADROOM);
}



static inline em_event_hdr_t*
mbuf_to_event_hdr(struct rte_mbuf *const mbuf)
{
  return (em_event_hdr_t *) &mbuf[1];
}

static inline struct rte_mbuf*
event_hdr_to_mbuf(em_event_hdr_t *const ev_hdr)
{
  return (struct rte_mbuf *) (((size_t) ev_hdr) - sizeof(struct rte_mbuf));
}



static inline em_event_t
mbuf_to_event(struct rte_mbuf *const mbuf)
{
  return (em_event_t) mbuf->pkt.data;
}

static inline struct rte_mbuf*
event_to_mbuf(em_event_t event)
{
  return (struct rte_mbuf *) (((size_t) event) - (RTE_PKTMBUF_HEADROOM+sizeof(struct rte_mbuf)));
}




static inline em_queue_element_t*
get_queue_element(const em_queue_t queue)
{
  IF_LIKELY(queue < EM_MAX_QUEUES) {
    return &(em.shm->em_queue_element_tbl[queue]);
  }
  else {
    return NULL;
  }
}



static inline
em_eo_element_t*
get_eo_element(const em_eo_t eo)
{
  IF_LIKELY(eo < EM_MAX_EOS) {
    return &(em.shm->em_eo_element_tbl[eo]);
  }
  else {
    return NULL;
  }
}



static inline
em_eo_element_t*
get_current_eo_elem(void)
{

  IF_LIKELY(em_core_local.current_q_elem != NULL) {
    return em_core_local.current_q_elem->eo_elem;
  }
  else {
    return NULL;
  }
}



static inline void
intel_free(void *const ev_hdr)
{
  struct rte_mbuf *const m = event_hdr_to_mbuf(ev_hdr);
  
  rte_pktmbuf_free(m);
}


#ifdef __cplusplus
}
#endif

#endif  // EM_INLINE_H_