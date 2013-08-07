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

#include "example.h"



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
 * EO context in the hello world test
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

} my_eo_context_t;


/**
 * Queue context data
 */
typedef struct
{
  em_queue_t queue;
 
} my_queue_context_t;



/**
 * Hello World shared memory 
 */
typedef union
{
  struct
  {
    // Allocate EO contexts from shared memory region
    my_eo_context_t eo_context_a;
    my_eo_context_t eo_context_b;
    
    // Queue context
    my_queue_context_t queue_context_a;
    my_queue_context_t queue_context_b;
    
    // EO A's queue
    em_queue_t queue_a; 
  };
  
  /** Pad to cache line size */
  uint8_t u8[2*ENV_CACHE_LINE_SIZE];
  
} hello_shm_t;

COMPILE_TIME_ASSERT((sizeof(hello_shm_t) % ENV_CACHE_LINE_SIZE) == 0, HELLO_SHM_T__SIZE_ERROR);


/** EM-core local pointer to shared memory */
static ENV_LOCAL hello_shm_t *hello_shm = NULL;



/* 
 * Local function prototypes
 */
static em_status_t
hello_start(my_eo_context_t* eo_ctx, em_eo_t eo);

static em_status_t
hello_stop(my_eo_context_t* eo_ctx, em_eo_t eo);

static void
hello_receive_event(my_eo_context_t* eo_ctx, em_event_t event, em_event_type_t type, em_queue_t queue, my_queue_context_t* q_ctx);
 
static void
delay_spin(my_eo_context_t* eo_ctx);



/**
 * Init and startup of the Hello World test application.
 *
 * @see main() and application_start() for setup and dispatch.
 */
void
test_init(appl_conf_t *const appl_conf)
{
  em_eo_t        eo_a, eo_b;
  
  
  if(em_core_id() == 0) {
    hello_shm = env_shared_reserve("HelloSharedMem", sizeof(hello_shm_t));
  }
  else {
    hello_shm = env_shared_lookup("HelloSharedMem");
  }
  

  if(hello_shm == NULL) {
    em_error(EM_ERROR_SET_FATAL(0xec0de), 0xdead, "Hello init failed on EM-core: %u\n", em_core_id());
  }
    

  /*
   * Rest of the initializations only on one EM-core, return on all others.
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
         appl_conf->name,
         NO_PATH(__FILE__), __func__,
         em_core_id(),
         em_core_count(),
         appl_conf->num_procs,
         appl_conf->num_threads);
  
  

  memset(&hello_shm->eo_context_a, 0, sizeof(my_eo_context_t));
  memset(&hello_shm->eo_context_b, 0, sizeof(my_eo_context_t));

  memset(&hello_shm->queue_context_a, 0, sizeof(my_queue_context_t));
  memset(&hello_shm->queue_context_b, 0, sizeof(my_queue_context_t));


  //
  // Create both EOs
  //
  eo_a = em_eo_create("EO A",
                      (em_start_func_t)  hello_start, NULL,
                      (em_stop_func_t)   hello_stop,  NULL, 
                      (em_receive_func_t)hello_receive_event, 
                      &hello_shm->eo_context_a
                     );
                     
  if(eo_a == EM_EO_UNDEF)
  {
    printf("EO A creation failed! \n");
    return;
  }


  eo_b = em_eo_create("EO B",
                      (em_start_func_t)  hello_start, NULL,
                      (em_stop_func_t)   hello_stop,  NULL, 
                      (em_receive_func_t)hello_receive_event, 
                      &hello_shm->eo_context_b
                     );
  
  if(eo_b == EM_EO_UNDEF)
  {
    printf("EO B creation failed! \n");
    return;
  }


  // Init EO contexts
  hello_shm->eo_context_a.this_eo  = eo_a;
  hello_shm->eo_context_a.other_eo = eo_b;
  hello_shm->eo_context_a.is_a     = 1;

  hello_shm->eo_context_b.this_eo  = eo_b;
  hello_shm->eo_context_b.other_eo = eo_a;
  hello_shm->eo_context_b.is_a     = 0;

  // Start EO A
  em_eo_start(eo_a, NULL, 0, NULL);

  // Start EO B
  em_eo_start(eo_b, NULL, 0, NULL);
 
}




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
    q_ctx      = &hello_shm->queue_context_a;
  }
  else
  {
    queue_name = "queue B";
    q_ctx      = &hello_shm->queue_context_b;
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
    hello_shm->queue_a = queue;
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


    status = em_send(event, hello_shm->queue_a);

    if(status != EM_OK)
    {
      printf("em_send() failed! status: %i,  eo: %"PRI_EO", queue: %"PRI_QUEUE" \n", status, eo, hello_shm->queue_a);
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

  
  (void)type;
  

  hello = em_event_pointer(event);

  dest        = hello->dest;
  hello->dest = queue;

  printf("Hello world from %s! My queue is %"PRI_QUEUE". I'm on core %02i. Event seq is %u.\n", eo_ctx->name, q_ctx->queue, em_core_id(), hello->seq++);

  delay_spin(eo_ctx);

  status = em_send(event, dest);
  
  if(status != EM_OK) {
    printf("em_send() failed! status: %i,  eo: %"PRI_EO", queue: %"PRI_QUEUE" \n", status, eo_ctx->this_eo, hello_shm->queue_a);
  }  

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



