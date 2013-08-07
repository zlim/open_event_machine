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
 * EM Error Handling
 */
 
#include <libgen.h> // basename()

#include "event_machine.h"
#include "event_machine_group.h"
#include "event_machine_helper.h"

#include "environment.h"

#include "event_machine_hw_config.h"

#include "em_error.h"
#include "em_intel.h"

#include "em_shared_data.h"

#include "em_intel_inline.h"

#include <rte_debug.h>
#include <rte_atomic.h>




/*
 * Make sure that EM internally used error scopes are "seen" by EM_ESCOPE()
 */
COMPILE_TIME_ASSERT(EM_ESCOPE(EM_ESCOPE_API_MASK)     , EM_ESCOPE_API_IS_NOT_PART_EM_ESCOPE__ERROR);
COMPILE_TIME_ASSERT(EM_ESCOPE(EM_ESCOPE_INTERNAL_MASK), EM_ESCOPE_INTERNAL_IS_NOT_PART_OF_EM_ESCOPE__ERROR);


static int error_handler_initialized = 0;


/*
 * Local function prototypes
 */
 
static em_status_t
em_default_error_handler(em_eo_t eo, em_status_t error, em_escope_t escope, va_list args);

static em_status_t
em_early_error_handler(em_eo_t eo, em_status_t error, em_escope_t escope, va_list args);

static em_status_t
select_error_handler(em_status_t error, em_escope_t escope, va_list args_list);




/**
 * Register EO specific error handler.
 *
 * The EO specific error handler is called if error is noticed or em_error() is 
 * called in the context of the EO. Note, the function will override any previously 
 * registered error handler.
 *
 * @param eo            EO id
 * @param handler       New error handler.
 *
 * @return EM_OK if successful.
 *
 * @see em_register_error_handler(), em_error_handler_t()
 */
em_status_t
em_eo_register_error_handler(em_eo_t eo, em_error_handler_t handler)
{
  em_eo_element_t* eo_elem;


  RETURN_ERROR_IF(invalid_eo(eo), EM_ERR_BAD_ID, EM_ESCOPE_EO_REGISTER_ERROR_HANDLER,
                  "Invalid EO id %"PRI_EO"", eo);

  eo_elem = get_eo_element(eo);
  
  eo_elem->error_handler_func = handler;
  
  return EM_OK;
}




/**
 * Unregister EO specific error handler.
 *
 * Removes previously registered EO specific error handler.
 *
 * @param eo            EO id
 *
 * @return EM_OK if successful.
 */
em_status_t
em_eo_unregister_error_handler(em_eo_t eo)
{
  em_eo_element_t* eo_elem;
  
  
  RETURN_ERROR_IF(invalid_eo(eo), EM_ERR_BAD_ID, EM_ESCOPE_EO_UNREGISTER_ERROR_HANDLER,
                  "Invalid EO id %"PRI_EO"", eo);
  
  eo_elem = get_eo_element(eo);

  eo_elem->error_handler_func = NULL;

  return EM_OK;
}




/**
 * Register the global error handler.
 *
 * The global error handler is called on errors (or em_error() calls)  
 * outside of any EO context or if there's no EO specific error 
 * handler registered. Note, the function will override any previously 
 * registered global error handler.
 *
 * @param handler       Error handler.
 *
 * @return EM_OK if successful.
 *
 * @see em_eo_register_error_handler(), em_unregister_error_handler(), em_error_handler_t()
 */
em_status_t
em_register_error_handler(em_error_handler_t handler)
{
  RETURN_ERROR_IF(!error_handler_initialized, EM_ERR_BAD_CONTEXT, EM_ESCOPE_REGISTER_ERROR_HANDLER,
                  "Error Handling not yet initialized!");
  
  env_spinlock_lock(&em.shm->em_error_handler_aligned.lock);
  
  em.shm->em_error_handler_aligned.em_error_handler = handler;
  
  env_spinlock_unlock(&em.shm->em_error_handler_aligned.lock);

  return EM_OK;
}




/**
 * Unregister the global error handler.
 *
 * Removes previously registered global error handler.
 *
 * @return EM_OK if successful.
 *
 * @see em_register_error_handler()
 */
em_status_t
em_unregister_error_handler(void)
{
  RETURN_ERROR_IF(!error_handler_initialized, EM_ERR_BAD_CONTEXT, EM_ESCOPE_UNREGISTER_ERROR_HANDLER,
                  "Error Handling not yet initialized!");
  
  env_spinlock_lock(&em.shm->em_error_handler_aligned.lock);  
  
  em.shm->em_error_handler_aligned.em_error_handler = em_default_error_handler;
  
  env_spinlock_unlock(&em.shm->em_error_handler_aligned.lock);  
  
  return EM_OK;
}




/**
 * Report an error.
 *
 * Reported errors are handled by the appropriate (EO specific or the global) error handler.
 *
 * Depending on the error/scope/implementation, the function call may not return.
 * 
 * @param error         Error code
 * @param escope        Error scope. Identifies the scope for interpreting the error code and variable arguments.
 * @param ...           Variable number and type of arguments
 * 
 * @see em_register_error_handler(), em_error_handler_t()
 */
void
em_error(em_status_t error, em_escope_t escope, ...)
{
  va_list args_list;
  
  
  va_start(args_list, escope);
  
  (void) select_error_handler(error, escope, args_list);
  
  va_end(args_list);
}




/**
 * Format error string
 *
 * Creates an implementation dependent error report string from EM 
 * internal errors.
 *
 * @param str       Output string pointer
 * @param size      Maximum string lenght in characters
 * @param eo        EO id
 * @param error     Error code (EM internal)
 * @param escope    Error scope (EM internal)
 * @param args      Variable arguments
 *
 * @return Output string length
 */
int
em_error_format_string(char* str, size_t size, em_eo_t eo, em_status_t error, em_escope_t escope, va_list args)
{
  int ret = -1;


  if(EM_ESCOPE(escope))
  {    
    //
    // Note: va_list contains: __FILE__, __func__, __LINE__, (format), ## __VA_ARGS__
    // as reported by the EM_INTERNAL_ERROR macro
    //
    char       *file   = va_arg(args, char*);
    const char *func   = va_arg(args, const char*);
    const int   line   = va_arg(args, const int);
    const char *format = va_arg(args, const char*);
    const char *base   = basename(file);
    char        eo_str[sizeof("EO:xxxxxx-abdc  ") + EM_EO_NAME_LEN];
    uint64_t loc_err_cnt  = em_core_local.error_count;                                            
    uint64_t glob_err_cnt = rte_atomic64_read(&em.shm->em_error_handler_aligned.global_error_count); 


    if(eo == EM_EO_UNDEF) {
      eo_str[0] = '\0';
    }
    else {
      char   eo_name[EM_EO_NAME_LEN];
      size_t nlen;
      
      nlen = em_eo_get_name(eo, eo_name, sizeof(eo_name));
      nlen = nlen > 0 ? nlen+1 : 0;
      eo_name[nlen] = '\0';
      
      (void) snprintf(eo_str, sizeof(eo_str), "EO:%"PRI_EO"-\"%s\"  ", eo, eo_name);
      eo_str[sizeof(eo_str)-1] = '\0';
    }
  
  
    ret = snprintf(str, size,
                   "\n"
                   "EM ERROR:0x%08X  ESCOPE:0x%08X  %s" 
                   "core:%02i ecount:%"PRIu64"(%"PRIu64")  "
                   "%s(L:%i) "
                   "%s  "
                   ,
                   error, escope, eo_str,
                   em_core_id(), glob_err_cnt, loc_err_cnt, 
                   func, line,
                   base
                  );
    
    if((ret > 0) && (ret < (int64_t)size)) {      
      ret += vsnprintf(str+ret, size-ret, format, args);
      
      if((ret > 0) && (ret < (int64_t)size)) {
        ret += snprintf(str+ret, size-ret, "\n\n");
      }
    }
    
    str[size-1] = '\0';
  }

  return MIN((int64_t)size, ret+1);
}




/**
 * Default EM Error Handler
 *
 * The default error handler called upon error if the application(s) have not registered
 * their own global and/or EO-specific error handlers
 *
 * @param eo      EO reporting the error (if applicable)      
 * @param error   The error code (reason), see em_status_e
 * @param escope  The error scope from within the error was reported, also tells whether 
                  the error was EM internal or application specific
 * @param args    va_list of args
 *
 * @return The function may not return depending on implementation/error code/error scope. If it
 * returns, the return value is the original (or modified) error code from the caller.
 */
static em_status_t
em_default_error_handler(em_eo_t eo, em_status_t error, em_escope_t escope, va_list args)
{
  char     eo_str[sizeof("EO:xxxxxx-abdc  ") + EM_EO_NAME_LEN];
  int      core_id      = em_core_id();
  uint64_t loc_err_cnt  = em_core_local.error_count;
  uint64_t glob_err_cnt = rte_atomic64_read(&em.shm->em_error_handler_aligned.global_error_count); 
  
  
  if(eo == EM_EO_UNDEF) {
    eo_str[0] = '\0';
  }
  else {
      char   eo_name[EM_EO_NAME_LEN];
      size_t nlen;
      
      nlen = em_eo_get_name(eo, eo_name, sizeof(eo_name));
      nlen = nlen > 0 ? nlen+1 : 0;
      eo_name[nlen] = '\0';
      
      (void) snprintf(eo_str, sizeof(eo_str), "EO:%"PRI_EO"-\"%s\"  ", eo, eo_name);
      eo_str[sizeof(eo_str)-1] = '\0';
  }
  
  
  if(EM_ESCOPE(escope))
  {
    //
    // Note: va_list contains: __FILE__, __func__, __LINE__, (format), ## __VA_ARGS__
    // as reported by the EM_INTERNAL_ERROR macro
    //
    char       *file   = va_arg(args, char*);
    const char *func   = va_arg(args, const char*);
    const int   line   = va_arg(args, const int);
    const char *format = va_arg(args, const char*);
    const char *base   = basename(file);
  
    fprintf(stderr,
           "\n"
           "EM ERROR:0x%08X  ESCOPE:0x%08X  %s" 
           "core:%02i ecount:%"PRIu64"(%"PRIu64")  "
           "%s(L:%i) "
           "%s  "
           ,
           error, escope, eo_str,
           core_id, glob_err_cnt, loc_err_cnt,
           func, line,
           base
          );
    
    vfprintf(stderr, format, args);    
    
    fprintf(stderr, "\n\n");
  }
  else
  {
    //
    // Note: Unknown va_list from application - don't touch.
    //
    fprintf(stderr,
           "\n"
           "APPL ERROR:0x%08X  ESCOPE:0x%08X  %s"
           "core:%02i ecount:%"PRIu64"(%"PRIu64")  "
           "\n\n"
           ,
           error, escope, eo_str,
           core_id, glob_err_cnt, loc_err_cnt);
  }
  
  
  IF_UNLIKELY(EM_ERROR_IS_FATAL(error))
  {
    /* Abort process, flush all open streams, dump stack, generate core dump, never return */
    rte_panic("\n"
              "FATAL ERROR:0x%08X on core:%02i ecount:%"PRIu64"(%"PRIu64") - ABORT!"
              "\n\n"
              , error, core_id, glob_err_cnt, loc_err_cnt);
  }
  
  
  return error;
}




/**
 * Early EM Error Handler
 *
 * The early error handler is used when reporting errors at startup before the default, 
 * application and EO specific error handlers have been set up.
 *
 * @param eo      unused      
 * @param error   The error code (reason), see em_status_e
 * @param escope  The error scope from within the error was reported, also tells whether 
                  the error was EM internal or application specific
 * @param args    va_list of args
 *
 * @return The function may not return depending on implementation/error code/error scope. If it
 * returns, the return value is the original (or modified) error code from the caller.
 */
static em_status_t
em_early_error_handler(em_eo_t eo, em_status_t error, em_escope_t escope, va_list args)
{
  int core_id = em_internal_conf.conf.proc_idx; // use proc_idx since EM-core ID might not have been initialized yet.
  
  
  if(EM_ESCOPE(escope))
  {
    //
    // Note: va_list contains: __FILE__, __func__, __LINE__, (format), ## __VA_ARGS__
    // as reported by the EM_INTERNAL_ERROR macro
    //
    char       *file    = va_arg(args, char*);
    const char *func    = va_arg(args, const char*);
    const int   line    = va_arg(args, const int);
    const char *format  = va_arg(args, const char*);
    const char *base    = basename(file);
    
    
    (void) eo; // Unused
    
    fprintf(stderr,
            "\n"
            "EM ERROR:0x%08X  ESCOPE:0x%08X  (EarlyError)  " 
            "core:%02i ecount:N/A  "
            "%s(L:%i) "
            "%s  "
            ,
            error, escope,
            core_id,
            func, line, base
           );
    
    vfprintf(stderr, format, args);
    
    fprintf(stderr, "\n\n");
  }
  else
  {
    //
    // Note: Unknown va_list from application - don't touch.
    //
    fprintf(stderr,
            "\n"
            "APPL ERROR:0x%08X  ESCOPE:0x%08X  (EarlyError)"
            "core:%02i  ecount:N/A  "
            "\n\n"
            ,
            error, escope,
            core_id
           );
    
  }
  
  
  IF_UNLIKELY(EM_ERROR_IS_FATAL(error))
  {
    /* Abort process, flush all open streams, dump stack, generate core dump, never return */
    rte_panic("\n"
              "FATAL ERROR:0x%08X on core:%02i ecount:N/A - ABORT!"
              "\n\n",
              error, core_id);
  }
  
  
  return error;
}




/**
 * Select and call an error handler.
 *
 * @param error         Error code
 * @param escope        Error scope. Identifies the scope for interpreting the error code and variable arguments.
 * @param args_list     Variable number and type of arguments
 *
 * @return Returns the 'error' argument given as input if the called error handler has not changed this value.
 */
static em_status_t
select_error_handler(em_status_t error, em_escope_t escope, va_list args_list)
{
  IF_UNLIKELY(!error_handler_initialized)
  {
    /* Early errors reported at startup before errhandling is properly initialized */
    error = em_early_error_handler(EM_EO_UNDEF, error, escope, args_list);
  }
  else
  {
    em_eo_element_t   *eo_elem       = get_current_eo_elem();
    em_eo_t            eo            = EM_EO_UNDEF;
    em_error_handler_t error_handler = em_default_error_handler;
    
    if(em.shm != NULL) {
      error_handler = em.shm->em_error_handler_aligned.em_error_handler;
    }
    
    if(eo_elem)
    {
      eo = eo_elem->id;
    
      if(eo_elem->error_handler_func)
      {
        error_handler = eo_elem->error_handler_func;
      }
    }
    
    if(error_handler)
    {
      // Call the selected error handler and possibly change the error code.
      error = error_handler(eo, error, escope, args_list);
    }
    
    if(error != EM_OK) {
      // Increase the error count, used in logs/printouts
      rte_atomic64_inc(&em.shm->em_error_handler_aligned.global_error_count);
      em_core_local.error_count += 1; 
    }
  }
  
  // Return input error or value changed by error_handler
  return error;
}




/**
 * Called ONLY from EM_INTERNAL_ERROR macro - do not use for anything else!
 * _em_internal_error((error), (escope), __FILE__, __func__, __LINE__, (format), ## __VA_ARGS__)
 */
em_status_t
_em_internal_error(em_status_t error, em_escope_t escope, ...)
{
  //
  // Note: va_list contains: __FILE__, __func__, __LINE__, (format), ## __VA_ARGS__
  // 
  va_list args;
  
  
  va_start(args, escope);
  
  // Select and call an error handler. Possibly modifies the error code.
  error = select_error_handler(error, escope, args);
  
  va_end(args);
  
  // Return input error or value changed by error_handler
  return error;
}



/**
 * Internal
 * Initialize the EM Error Handling
 */
void
em_error_init(void)
{
  env_spinlock_init(&em.shm->em_error_handler_aligned.lock);

  em.shm->em_error_handler_aligned.em_error_handler = em_default_error_handler; 
  
  rte_atomic64_init(&em.shm->em_error_handler_aligned.global_error_count); 
  
  error_handler_initialized = 1;
  
  // TEST:
  // (void) EM_INTERNAL_ERROR(0x0000acdc, EM_ESCOPE_INTERNAL_TEST, "Test Error Reporting 1/2");
  // (void) EM_INTERNAL_ERROR(0x0000abba, EM_ESCOPE_INTERNAL_TEST, "Test Error Reporting 2/2  args:%u %s %f", 1, "arg2", 0.3);
}


/**
 * Internal
 * Initialze the EM error handling for child processes
 */
void
em_error_init_secondary(void)
{
  if((em.shm == NULL) || (em.shm->em_error_handler_aligned.em_error_handler != em_default_error_handler))
  {
    // Report error, uses em_early_error_handler()
    em_error(EM_FATAL(EM_ERR_BAD_CONTEXT), EM_ESCOPE_INIT, "Error handling not properly initialized!");
  }
  
  error_handler_initialized = 1;
}







