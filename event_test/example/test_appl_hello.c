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
 * Event Machine hello world example.
 *
 * Creates two Execution Objects (EOs), each with a dedicated queue for incoming events,
 * and ping-pongs an event between the EOs while printing "Hello world" each time the
 * event is received.
 *
 */
 
#include "event_machine.h"
#include "environment.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>



#define SPIN_COUNT   50000000


/**
 * The hello world event
 */
typedef struct
{
  // Destination queue for the reply event
  em_queue_t   dest;

  // Sequence number
  unsigned int seq;

} hello_event_t;


/**
 * EO context in hello world test
 */
typedef struct 
{
  // Init before start
  em_eo_t      this_eo;
  em_eo_t      other_eo;
  int          is_a;

  // Init in start
  char         name[16];
  volatile int spins;

}my_eo_context_t;


typedef struct
{
  em_queue_t queue;
 
} my_queue_context_t;



// Allocate EO contexts from shared memory region
ENV_SHARED static my_eo_context_t eo_context_a;
ENV_SHARED static my_eo_context_t eo_context_b;

// Queue context
ENV_SHARED static my_queue_context_t queue_context_a;
ENV_SHARED static my_queue_context_t queue_context_b;

// EO A's queue
ENV_SHARED static em_queue_t queue_a = EM_QUEUE_UNDEF;


/* 
 * Local function prototypes
 */
static void
delay_spin(my_eo_context_t* eo_ctx);




/**
 * @private
 * 
 * EO start function.
 *
 */
static em_status_t
hello_start(my_eo_context_t* eo_ctx, em_eo_t eo)
{
  em_queue_t queue;
  const char* queue_name;
  my_queue_context_t* q_ctx;
  em_status_t status;

  // Copy EO name
  em_eo_get_name(eo, eo_ctx->name, sizeof(eo_ctx->name));


  if(eo_ctx->is_a)
  {
    queue_name = "queue A";
    q_ctx      = &queue_context_a;
  }
  else
  {
    queue_name = "queue B";
    q_ctx      = &queue_context_b;
  }


  queue = em_queue_create(queue_name, EM_QUEUE_TYPE_ATOMIC, EM_QUEUE_PRIO_NORMAL, EM_QUEUE_GROUP_DEFAULT);
  
  if(queue == EM_QUEUE_UNDEF)
  {
    printf("%s creation failed! \n", queue_name);
    return EM_ERROR;
  }


  q_ctx->queue = queue;

  status = em_queue_set_context(queue, q_ctx);
  
  if(status != EM_OK)
  {
    printf("Set queue context failed! status: %i,  eo: %"PRI_EO", queue: %"PRI_QUEUE" \n", status, eo, queue);
    return EM_ERROR;
  }  

  
  status = em_eo_add_queue(eo, queue);
  
  if(status != EM_OK)
  {
    printf("Add queue failed! status: %i,  eo: %"PRI_EO", queue: %"PRI_QUEUE" \n", status, eo, queue);
    return EM_ERROR;
  }

  
  status = em_queue_enable(queue);
  
  if(status != EM_OK)
  {
    printf("Queue enable failed! status: %i,  eo: %"PRI_EO", queue: %"PRI_QUEUE" \n", status, eo, queue);
    return EM_ERROR;
  }
  

  printf("Hello world started %s. I'm EO %"PRI_EO". My queue is %"PRI_QUEUE".\n", eo_ctx->name, eo, queue);


  if(eo_ctx->is_a)
  {
    // Save queue ID for EO B.
    queue_a = queue;
  }
  else
  {
    em_event_t     event;
    hello_event_t* hello;

    //
    // Send the first event to EO A.
    // Store queue ID as the destination queue for EO A.
    //
    event = em_alloc(sizeof(hello_event_t), EM_EVENT_TYPE_SW, EM_POOL_DEFAULT);
  
    if(event == EM_EVENT_UNDEF)
    {
      printf("Event allocation failed! \n");
      return EM_ERROR;
    }
  
    hello = em_event_pointer(event);
    hello->dest = queue;
    hello->seq  = 0;


    status = em_send(event, queue_a);

    if(status != EM_OK)
    {
      printf("em_send() failed! status: %i,  eo: %"PRI_EO", queue: %"PRI_QUEUE" \n", status, eo, queue_a);
      return EM_ERROR;
    }
    
  }
  

  return EM_OK;
}




/**
 * @private
 * 
 * EO stop function.
 *
 */
static em_status_t
hello_stop(my_eo_context_t* eo_ctx, em_eo_t eo)
{
  printf("Hello world stop (%s, eo id %"PRI_EO")\n", eo_ctx->name, eo);

  return EM_OK;
}




/**
 * @private
 * 
 * EO receive function.
 *
 * Print "Hello world" and send back to the sender of the event.
 *
 */
static void
hello_receive_event(my_eo_context_t* eo_ctx, em_event_t event, em_event_type_t type, em_queue_t queue, my_queue_context_t* q_ctx)
{
  hello_event_t* hello;
  em_queue_t     dest;
  em_status_t    status;

  hello = em_event_pointer(event);

  dest        = hello->dest;
  hello->dest = queue;

  printf("Hello world from %s! My queue is %"PRI_QUEUE". I'm on core %02i. Event seq is %u.\n", eo_ctx->name, q_ctx->queue, em_core_id(), hello->seq++);

  delay_spin(eo_ctx);

  status = em_send(event, dest);
  
  if(status != EM_OK) {
    printf("em_send() failed! status: %i,  eo: %"PRI_EO", queue: %"PRI_QUEUE" \n", status, eo_ctx->this_eo, queue_a);
  }  

}




/**
 * Global init and startup of the hello world test application.
 */
void
test_appl_hello_start(void)
{
  em_eo_t        eo_a, eo_b;

  memset(&eo_context_a, 0, sizeof(my_eo_context_t));
  memset(&eo_context_b, 0, sizeof(my_eo_context_t));

  memset(&queue_context_a, 0, sizeof(my_queue_context_t));
  memset(&queue_context_b, 0, sizeof(my_queue_context_t));


  //
  // Create both EOs
  //
  eo_a = em_eo_create("EO A", (em_start_func_t)hello_start, NULL, (em_stop_func_t)hello_stop, NULL, (em_receive_func_t)hello_receive_event, &eo_context_a);

  if(eo_a == EM_EO_UNDEF)
  {
    printf("EO A creation failed! \n");
    return;
  }

  eo_b = em_eo_create("EO B", (em_start_func_t)hello_start, NULL, (em_stop_func_t)hello_stop, NULL, (em_receive_func_t)hello_receive_event, &eo_context_b);
  
  if(eo_b == EM_EO_UNDEF)
  {
    printf("EO B creation failed! \n");
    return;
  }

  // Init EO contexts
  eo_context_a.this_eo  = eo_a;
  eo_context_a.other_eo = eo_b;
  eo_context_a.is_a     = 1;

  eo_context_b.this_eo  = eo_b;
  eo_context_b.other_eo = eo_a;
  eo_context_b.is_a     = 0;

  // Start EO A
  em_eo_start(eo_a, NULL, 0, NULL);

  // Start EO B
  em_eo_start(eo_b, NULL, 0, NULL);

  
}



/**
 * Delay spinloop
 */
static void
delay_spin(my_eo_context_t* eo_ctx)
{
  int i;

  for(i = 0; i < SPIN_COUNT; i++)
  {
    eo_ctx->spins++;
  }
}



