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
 

/**
 * @file
 *
 * Common test include file
 *
 */

#ifndef TEST_COMMON_H
#define TEST_COMMON_H


#define _GNU_SOURCE /* See feature_test_macros(7) */
#include <semaphore.h>
#include <pthread.h>

#include "event_machine.h"
#include "environment.h"


#ifdef __cplusplus
extern "C" {
#endif



/** Get rid of path in filename - only for unix-type paths using '/' */
#define NO_PATH(file_name) (strrchr((file_name), '/') ? strrchr((file_name), '/') + 1 : (file_name))


/** Simple appl error handling: print & exit */
#define APPL_EXIT_FAILURE(...)            \
  {                                       \
    fprintf(stderr,                       \
            "Appl Error: "                \
            "%s(), line:%i - ",           \
            NO_PATH(__FILE__), __LINE__); \
    fprintf(stderr, __VA_ARGS__);         \
    fprintf(stderr, "\n\n");              \
    fflush(stderr);                       \
    exit(EXIT_FAILURE);                   \
  }



#define APPL_NAME_LEN  (32)


/**
 * Application startup synchronization
 */
typedef struct
{
  sem_t                 sem;

  pthread_barrier_t     barrier;

  pthread_barrierattr_t attr;

} sync_t;



/**
 * Application configuration
 */
typedef struct
{
  char name[APPL_NAME_LEN];  /**< application name */
  
  unsigned num_procs;        /**< number of processes */
                                  
  unsigned num_threads;      /**< number of threads */
                             
  /* Temp vars */            
                             
  int phys_mask_idx;         /**< index of argv[] containing the physical coremask, found in parsing */
                             
  int my_argv_idx;           /**< index of argv[] from where the user args start (i.e. non-dpdk args), found in parsing */
  
  /* Add further if needed */
  /* ... */
  
  
  sync_t *startup_sync;      /**< Application startup synchronization vars
                                 (not a config option, but included here for convenience) */
} appl_conf_t;




void
parse_args(int argc, char *argv[],
           em_conf_t      *em_conf   /* out param */,
           appl_conf_t    *appl_conf /* out param */);

int
init_rt_env(int argc, char *argv[], em_conf_t *em_conf, appl_conf_t *appl_conf);

void
launch_application(int (*appl_start_fptr)(appl_conf_t *), appl_conf_t *appl_conf);

void
usage(char *progname);




#ifdef __cplusplus
}
#endif



#endif


