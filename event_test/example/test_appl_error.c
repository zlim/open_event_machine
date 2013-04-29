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
 * Event Machine error handler example.
 *
 * Demonstrate and test the Event Machine error handling functionality,
 * see the API calls em_error(), em_register_error_handler(), em_eo_register_error_handler()
 * etc.
 *
 * Three application EOs are created, each with a dedicated queue. An application specific
 * global error handler is registered (thus replacing the EM default). Additionally EO A 
 * will register an EO specific error handler.
 * When the EOs receive events (error_receive) they will generate errors by explicit calls to
 * em_error() and by calling EM-API functions with invalid arguments.
 * The registered error handlers simply print the error information on screen.
 *
 * Note: Lots of the API-call return values are left unchecked for errors (especially in setup) since
 * the error handler demostrated in this example is not designed ta handle 'real' errors.
 *
 */

#include "event_machine.h"
#include "event_machine_helper.h"
#include "environment.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "example.h"


/*
 * Defines
 */

#define APPL_ESCOPE_OTHER      1
#define APPL_ESCOPE_STR        2
#define APPL_ESCOPE_STR_Q      3
#define APPL_ESCOPE_STR_Q_SEQ  4
 
 
#define DELAY_SPIN_COUNT       50000000




/**
 * Error test event
 */
typedef struct
{
  /** Destination queue for the reply event */
  em_queue_t   dest;

  /** Sequence number */
  unsigned int seq;
  
  /** Indicate whether to report a fatal error or not */
  int          fatal;

} error_event_t;



/**
 * EO context of error test application 
 */
typedef struct 
{
  /** EO Id */
  em_eo_t      eo;
  
  /** EO name */
  char         name[16];
  
  /** Delay spin counter */
  volatile int spins;
}eo_context_t;



/**
 * EO context padded to cache line size.
 */
typedef union 
{
  /** The EO context data */
  eo_context_t eo_ctx;
  
  /** Pad EO context to cache line size */
  uint8_t u8[ENV_CACHE_LINE_SIZE];
  
} eo_context_pad_t;



/** Allocate EO contexts from shared memory region */
ENV_SHARED  static  eo_context_pad_t  eo_error_a  ENV_CACHE_LINE_ALIGNED;
ENV_SHARED  static  eo_context_pad_t  eo_error_b  ENV_CACHE_LINE_ALIGNED;
ENV_SHARED  static  eo_context_pad_t  eo_error_c  ENV_CACHE_LINE_ALIGNED;


// Queue IDs - use shared vars since this test is NOT concerned with performance.
ENV_SHARED static em_queue_t queue_a, queue_b, queue_c;



/*
 * Local function prototypes
 */
static em_status_t
error_start(void* eo_context, em_eo_t eo);

static em_status_t
error_stop(void* eo_context, em_eo_t eo);

static void
error_receive(void* eo_context, em_event_t event, em_event_type_t type, em_queue_t queue, void* q_ctx);

static em_status_t
global_error_handler(em_eo_t eo, em_status_t error, em_escope_t escope, va_list args);

static em_status_t
eo_specific_error_handler(em_eo_t eo, em_status_t error, em_escope_t escope, va_list args);

static em_status_t
combined_error_handler(const char* handler_name, em_eo_t eo, em_status_t error, em_escope_t escope, va_list args); 
 
static void
delay_spin(eo_context_t* eo_ctx);



/**
 * Init and startup of the Error Handler test application.
 *
 * @see main() and example_start() for setup and dispatch.
 */
void
test_init(example_conf_t *const example_conf)
{
  em_eo_t        eo;
  em_event_t     event;
  error_event_t* error;
  em_status_t    ret;  

  
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
         example_conf->name,
         NO_PATH(__FILE__), __func__,
         em_core_id(),
         em_core_count(),
         example_conf->num_procs,
         example_conf->num_threads);
         
  
  /* 
   * Register the application specifig global error handler
   * This replaces the EM internal default error hanlder
   */
  em_register_error_handler(global_error_handler); 


  /*
   * Create and start EO "A"
   */
  eo      = em_eo_create("EO A", error_start, NULL, error_stop, NULL, error_receive, &eo_error_a.eo_ctx);
  queue_a = em_queue_create("queue A", EM_QUEUE_TYPE_ATOMIC, EM_QUEUE_PRIO_NORMAL, EM_QUEUE_GROUP_DEFAULT);
  
  if((ret = em_eo_add_queue(eo, queue_a)) != EM_OK)
  {
    printf("EO or queue creation failed (%i). EO: %"PRI_EO", queue: %"PRI_QUEUE"\n", ret, eo, queue_a);
    return;
  }

  if((ret = em_queue_enable(queue_a)) != EM_OK)
  {
    printf("Queue A enable failed (%i). EO: %"PRI_EO", queue: %"PRI_QUEUE"\n", ret, eo, queue_a);
    return;
  }

  /*
   * Register an application 'EO A'-specific error handler
   */
  em_eo_register_error_handler(eo, eo_specific_error_handler); 
  em_eo_start(eo, NULL, 0, NULL);


  //
  // Create and start EO "B"
  //
  eo      = em_eo_create("EO B", error_start, NULL, error_stop, NULL, error_receive, &eo_error_b.eo_ctx);
  queue_b = em_queue_create("queue B", EM_QUEUE_TYPE_ATOMIC, EM_QUEUE_PRIO_NORMAL, EM_QUEUE_GROUP_DEFAULT);

  if((ret = em_eo_add_queue(eo, queue_b)) != EM_OK)
  {
    printf("EO or queue creation failed (%i). EO: %"PRI_EO", queue: %"PRI_QUEUE"\n", ret, eo, queue_b);
    return;
  }
  
  if((ret = em_queue_enable(queue_b)) != EM_OK)
  {
    printf("Queue B enable failed (%i). EO: %"PRI_EO", queue: %"PRI_QUEUE"\n", ret, eo, queue_b);
    return;
  }
  
  /* Note: No 'EO B' specific error handler, use the application specific global error handler instead. */
  em_eo_start(eo, NULL, 0, NULL);


  /*
   * Create and start EO "C"
   */
  eo      = em_eo_create("EO C", error_start, NULL, error_stop, NULL, error_receive, &eo_error_c.eo_ctx);
  queue_c = em_queue_create("queue C", EM_QUEUE_TYPE_ATOMIC, EM_QUEUE_PRIO_NORMAL, EM_QUEUE_GROUP_DEFAULT);

  if((ret = em_eo_add_queue(eo, queue_c)) != EM_OK)
  {
    printf("EO or queue creation failed (%i). EO: %"PRI_EO", queue: %"PRI_QUEUE"\n", ret, eo, queue_c);
    return;
  }

  if((ret = em_queue_enable(queue_c)) != EM_OK)
  {
    printf("Queue C enable failed (%i). EO: %"PRI_EO", queue: %"PRI_QUEUE"\n", ret, eo, queue_c);
    return;
  }

  /* Note: No 'EO C' specific error handler, use the application specific global error handler instead. */
  em_eo_start(eo, NULL, 0, NULL);
  
  
  
  
  /*
   * Send an event to EO A.
   * Store EO B's queue as the destination queue for EO A.
   */
  event = em_alloc(sizeof(error_event_t), EM_EVENT_TYPE_SW, EM_POOL_DEFAULT);

  error = em_event_pointer(event);

  error->dest  = queue_b;
  error->seq   = 0;
  error->fatal = 0;

  em_send(event, queue_a);


  /*
   * Send an event to EO C.
   * No dest queue stored since the fatal flag is set
   */
  event = em_alloc(sizeof(error_event_t), EM_EVENT_TYPE_SW, EM_POOL_DEFAULT);

  error = em_event_pointer(event);

  error->dest  = 0; // don't care, never resent
  error->seq   = 0;
  error->fatal = 1; // generate a fatal error when received

  em_send(event, queue_c);
}



/**
 * @private
 * 
 * EO specific error handler.
 *
 * @return The function may not return depending on implementation/error code/error scope. If it
 * returns, the return value is the original (or modified) error code from the caller. 
 */
static em_status_t
eo_specific_error_handler(em_eo_t eo, em_status_t error, em_escope_t escope, va_list args)
{
  return combined_error_handler("Appl EO specific error handler", eo, error, escope, args);
}




/**
 * @private
 * 
 * Global error handler.
 *
 * @return The function may not return depending on implementation/error code/error scope. If it
 * returns, the return value is the original (or modified) error code from the caller. 
 */
static em_status_t
global_error_handler(em_eo_t eo, em_status_t error, em_escope_t escope, va_list args)
{
  return combined_error_handler("Appl Global error handler     ", eo, error, escope, args);
}




/**
 * @private
 * 
 * Error handler implementation for both global and EO specific handlers registered by the application.
 *
 * @return The function may not return depending on implementation/error code/error scope. If it
 * returns, the return value is the original (or modified) error code from the caller.
 */
static em_status_t
combined_error_handler(const char* handler_name, em_eo_t eo, em_status_t error, em_escope_t escope, va_list args)
{
  const char  *str;
  em_queue_t   queue;
  unsigned int seq;


  if(EM_ERROR_IS_FATAL(error))
  {
    /* 
     * Application registered handling of FATAL errors.
     * Just print it and return since it's a fake fatal error.
     */
    printf("THIS IS A FATAL ERROR!!"                    "\n"
           "%s: EO %"PRI_EO"  error 0x%08X  escope 0x%X" "\n"
           "Return from fatal."                       "\n\n",
           handler_name, eo, error, escope);

    return error;
  }
  
  
  if(EM_ESCOPE_API(escope))
  {
    /*
     * EM API error: call em_error_format_string() to format a string
     */
    char error_str[128];
    
    em_error_format_string(error_str, sizeof(error_str), eo, error, escope, args);

    printf("%s: EO %"PRI_EO"  error 0x%08X  escope 0x%X - "
           "EM info: %s"
           , handler_name, eo, error, escope, error_str);
  }
  else
  {
    /*
     * Application specific error handling.
     */ 
    switch(escope)
    {
      case APPL_ESCOPE_STR:
        str   = va_arg(args, const char*);
        printf("%s: EO %"PRI_EO"  error 0x%08X  escope 0x%X ARGS: %s \n", handler_name, eo, error, escope, str);
        break;
        
      case APPL_ESCOPE_STR_Q:
        str   = va_arg(args, const char*);
        queue = va_arg(args, em_queue_t);
        printf("%s: EO %"PRI_EO"  error 0x%08X  escope 0x%X ARGS: %s %"PRI_QUEUE" \n", handler_name, eo, error, escope, str, queue);
        break;
        
      case APPL_ESCOPE_STR_Q_SEQ:
        str   = va_arg(args, const char*);
        queue = va_arg(args, em_queue_t);
        seq   = va_arg(args, unsigned int);
        printf("%s: EO %"PRI_EO"  error 0x%08X  escope 0x%X ARGS: %s %"PRI_QUEUE" %u \n", handler_name, eo, error, escope, str, queue, seq);
        break;
        
      default:
        printf("%s: EO %"PRI_EO"  error 0x%08X  escope 0x%X \n", handler_name, eo, error, escope);
    };
  }


  return error;
}




/**
 * @private
 * 
 * EO receive function.
 * 
 * Report various kinds of errors to demonstrate the EM error handling API.
 *
 */
static void
error_receive(void* eo_context, em_event_t event, em_event_type_t type, em_queue_t queue, void* q_ctx)
{
  error_event_t* error;
  em_queue_t     dest;
  eo_context_t* eo_ctx = eo_context;

  error = em_event_pointer(event);

  dest        = error->dest;
  error->dest = queue;

  

  if(error->fatal)
  {
    printf("\nError log from %s [%u] on core %i!\n", eo_ctx->name, error->seq, em_core_id());
    
    em_free(event);
    
    /* Report a fatal error */
    em_error(EM_ERROR_SET_FATAL(0xdead), 0);
    
    return;
  }
  
  
  printf("Error log from %s [%u] on core %i!\n", eo_ctx->name, error->seq, em_core_id());

  /*       error   escope                 args  */
  em_error(0x1111, APPL_ESCOPE_OTHER);
  em_error(0x2222, APPL_ESCOPE_STR,       "Second error");
  em_error(0x3333, APPL_ESCOPE_STR_Q,     "Third  error", queue);
  em_error(0x4444, APPL_ESCOPE_STR_Q_SEQ, "Fourth error", queue, error->seq);

  /*
   * Example of an API call error - generates an EM API error
   */ 
  em_free(NULL);

  
  
  error->seq++;

  delay_spin(eo_ctx);

  em_send(event, dest);

  
  /* Request a fatal error to be generated every 8th event by 'EO C' */
  if((error->seq & 0x7) == 0x7)
  {
    // Send a new event to EO 'C' to cause a fatal error
    event = em_alloc(sizeof(error_event_t), EM_EVENT_TYPE_SW, EM_POOL_DEFAULT);

    error = em_event_pointer(event);

    error->dest  = 0; // don't care, never resent
    error->seq   = 0;
    error->fatal = 1;

    em_send(event, queue_c);
  }

}




/**
 * @private
 * 
 * EO start function.
 *
 */
static em_status_t
error_start(void* eo_context, em_eo_t eo)
{
  eo_context_t* eo_ctx = eo_context;
  
  memset(eo_ctx, 0, sizeof(eo_context_t));

  eo_ctx->eo = eo;
  
  em_eo_get_name(eo, eo_ctx->name, sizeof(eo_ctx->name));


  printf("Error test start (%s, eo id %"PRI_EO")\n", eo_ctx->name, eo);

  return EM_OK;
}




/**
 * @private
 * 
 * EO stop function.
 *
 */
static em_status_t
error_stop(void* eo_context, em_eo_t eo)
{
  
  printf("Error test stop function (eo %"PRI_EO")\n", eo);

  return EM_OK;
}



/**
 * Delay spinloop
 */
static void
delay_spin(eo_context_t* eo_ctx)
{
  int i;

  for(i = 0; i < DELAY_SPIN_COUNT; i++) {
    eo_ctx->spins++;
  }
}

