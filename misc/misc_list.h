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
 
#ifndef MISC_LIST_H_
#define MISC_LIST_H_


#ifdef __cplusplus
extern "C" {
#endif




/**
 * Double linked list head / node
 */
typedef struct m_list_head_t
{
  struct m_list_head_t* next;
  
  struct m_list_head_t* prev;
  
} m_list_head_t;




/**
 * Initialize the list
 */
static inline void
m_list_init(m_list_head_t *const list_head)
{
  list_head->next = list_head;
  list_head->prev = list_head;
}



/**
 * Check whether the list is empty
 */
static inline int
m_list_is_empty(m_list_head_t *const list_head)
{
  return (list_head->next == list_head);
}



/**
 * Double linked list add node
 */
static inline void
m_list_add(m_list_head_t *const list_head, m_list_head_t *const node)
{
  m_list_head_t* next;
  m_list_head_t* prev;

  next  = list_head;
  prev  = list_head->prev;

  node->next  = next;
  node->prev  = prev;

  prev->next  = node;
  next->prev  = node;

}


/**
 * Double linked list remove node
 */
static inline void
m_list_rem(m_list_head_t *const list_head, m_list_head_t *const node)
{
  m_list_head_t* next;
  m_list_head_t* prev;

  
  (void) list_head;  // unused

  next = node->next;
  prev = node->prev;

  prev->next = node->next;
  next->prev = node->prev;

  // just for safety
  node->next = NULL;
  node->prev = NULL;
}



/**
 * Double linked list remove first node (LIFO-mode)
 */
static inline m_list_head_t* m_list_rem_first(m_list_head_t *const list_head)
{
  m_list_head_t* node;

  node = NULL;

  if(!m_list_is_empty(list_head))
  {
    // first node in the list
    node = list_head->next;

    m_list_rem(list_head, node);
  }

  return node;
}



/**
 * Double linked list remove last node (FIFO-mode)
 */
static inline m_list_head_t* m_list_rem_last(m_list_head_t *const list_head)
{
  m_list_head_t* node;

  node = NULL;

  if(!m_list_is_empty(list_head))
  {
    // last node in the list
    node = list_head->prev;

    m_list_rem(list_head, node);
  }

  return node;
}



/**
 * Macro for accessing each node in a queue list
 *
 * @param head  Points to the head node of the list
 * @param pos   Pointer for holding the current position
 * @param cur   Points to the current node
 *
 * Usage example:
 *
 * queue_list_for_each(m_list_head_t* head, m_list_head_t* pos, em_queue_element_t* q_elem)
 * {
 *   q_elem->...
 * }
 *
 */
#define m_list_for_each(head, pos, cur)  for((pos) = (head)->next; (cur) = (void*)(pos), (pos) != (head); (pos) = (pos)->next)





#ifdef __cplusplus
}
#endif

#endif








