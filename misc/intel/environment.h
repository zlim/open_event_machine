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

#ifndef _ENVIRONMENT_H
#define _ENVIRONMENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>

#include <sys/queue.h>

#include <rte_memory.h>
#include <rte_memzone.h>
#include <rte_launch.h>
#include <rte_eal.h>
#include <rte_per_lcore.h>
#include <rte_lcore.h>
#include <rte_branch_prediction.h>
#include <rte_prefetch.h>
#include <rte_atomic.h>
#include <rte_malloc.h>
#include <rte_cycles.h> 
#include <rte_spinlock.h>
#include <rte_ring.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>

#ifndef RTE_EXEC_ENV_BAREMETAL
  #include <pthread.h>
#else
  #error "No Core Sync Barrier yet implemented for BareMetal!"
#endif

#include "event_machine_helper.h"





/**
 * Compile time assertion-macro - fail compilation if cond is false.
 */
#define COMPILE_TIME_ASSERT(cond, msg)  typedef char msg[(cond) ? 1: -1]



/**
 * Core SHARED global variables
 */
#define ENV_SHARED  // Global shared variables by default on intel

/**
 * Core LOCAL global variables
 */
#ifdef RTE_EXEC_ENV_BAREMETAL
  #define ENV_LOCAL __attribute__ ((section (".per_core")))
#else
  #define ENV_LOCAL __thread 
#endif



/**
 * Cache line size
 */
#define ENV_CACHE_LINE_SIZE           (CACHE_LINE_SIZE)
#define ENV_CACHE_LINE_SIZE_MASK      (CACHE_LINE_MASK)

                                      
/*                                    
 * Cache line alignment               
 */                                   
#define ENV_CACHE_LINE_ALIGNED        __rte_cache_aligned

                                      
/*                                    
 * Cache Prefetch-macros              
 */

/* Prefetch into all cache levels */
#define ENV_PREFETCH(addr)            rte_prefetch0((addr))
//#define ENV_PREFETCH_NEXT_LINE(addr)  rte_prefetch0((void *)(CACHE_LINE_ROUNDUP((((size_t)(addr))+1))))
#define ENV_PREFETCH_NEXT_LINE(addr)  rte_prefetch0((void *) ((((uint64_t)(addr)) + ENV_CACHE_LINE_SIZE) & (~((uint64_t)ENV_CACHE_LINE_SIZE_MASK))) )

/* Prefetch up to L2-cache level */
#define ENV_PREFETCH_L2(addr)           rte_prefetch1((addr))
#define ENV_PREFETCH_NEXT_LINE_L2(addr) rte_prefetch1((void *) ((((uint64_t)(addr)) + ENV_CACHE_LINE_SIZE) & (~((uint64_t)ENV_CACHE_LINE_SIZE_MASK))) )

/* Prefetch to L3 cache */
#define ENV_PREFETCH_L3(addr)           rte_prefetch2((addr))
#define ENV_PREFETCH_NEXT_LINE_L3(addr) rte_prefetch2((void *) ((((uint64_t)(addr)) + ENV_CACHE_LINE_SIZE) & (~((uint64_t)ENV_CACHE_LINE_SIZE_MASK))) )


/*
 * Can be used to avoid unnecessary external memory reads for memory
 * locations whose content value does not matter.
 */
#define ENV_PREPARE_FOR_STORE(addr)   


/**
 * Branch Prediction macros
 * (gcc has also built-in macros for this, consider?)
 */
#define ENV_LIKELY(cond)    likely((cond))
#define ENV_UNLIKELY(cond)  unlikely((cond))


#define IF_LIKELY(cond)     if(ENV_LIKELY((cond)))
#define IF_UNLIKELY(cond)   if(ENV_UNLIKELY((cond)))



/*
 * Atomic operations - types & functions
 */
typedef  rte_atomic64_t  env_atomic_t;


static inline void env_atomic_init(env_atomic_t *ptr)
{
  rte_atomic64_init(ptr);
}


static inline void env_atomic_set(env_atomic_t *ptr, int64_t new_val)
{
  rte_atomic64_set(ptr, new_val);
}


static inline int64_t env_atomic_get(env_atomic_t *ptr)
{
  return rte_atomic64_read(ptr);
}


static inline void env_atomic_dec(env_atomic_t *ptr)
{
  rte_atomic64_dec(ptr);
}


static inline void env_atomic_inc(env_atomic_t *ptr)
{
  rte_atomic64_inc(ptr);
}


static inline int64_t env_atomic_add_return(env_atomic_t *ptr, int64_t add_val)
{
  return rte_atomic64_add_return(ptr, add_val);
}



/**
 * map memory allocation routines
 *
 */

// static inline void *env_shared_malloc(size_t size)
// {
//     return NULL;
// }
// 
// static inline void env_shared_free(void *buf)
// {
//     
// }



static inline void *env_local_malloc(size_t size)
{
    return rte_malloc("env_local_malloc", size, 0);
}



static inline void env_local_free(void *buf)
{
    rte_free(buf);
}



/**
 * Synchronize memory
 * @todo  read barrier?
 */
static inline void env_sync_mem(void)
{
  rte_wmb();
  //rte_mb();
}



/**
 * Core cycles
 *
 * Works both in 32 and 64 bit. Todo: optimize 64 bit
 */
static inline uint64_t env_get_cycle(void)
{
  return rte_rdtsc();

  //  uint32_t a,d;

  // __asm__ __volatile__ ("rdtsc" : "=a" (a), "=d" (d) : : "memory");
  //return (uint64_t)a | ((uint64_t)d)<<32;

  //  return rte_get_hpet_cycles();
}



/**
 * Current Core number (Note: NOT the same as em_core_id())
 * This is the physical core-id!
 */
static inline int env_get_core_num(void)
{
  return rte_lcore_id();
}



/**
 * Number of running cores
 */
static inline int env_get_core_count(void)
{
  return rte_lcore_count();
}



/**
 * Creates a mask for given number of cores starting from core 0
 */
static inline uint64_t env_core_mask_count(int count)
{
    return (((uint64_t)1) << count) - 1;
}


// intel_environment.c 
uint64_t env_core_hz_linux(void);


/**
 * Core MHz
 */
static inline uint32_t env_core_mhz(void)
{
  return env_core_hz_linux() / 1000000;
}



/**
 * Core Hz
 */
static inline uint64_t env_core_hz(void)
{
  return env_core_hz_linux();
}



#ifndef RTE_EXEC_ENV_BAREMETAL /* Linux */

typedef struct env_barrier_t
{
  pthread_barrier_t  pthread_barrier;
  
} env_barrier_t;



static inline void env_barrier_init(env_barrier_t* barrier, uint64_t core_mask)
{
  em_core_mask_t  mask;
  int             core_count;
  
  
  mask.u64[0] = core_mask;
  core_count  = em_core_mask_count(&mask);
  
  pthread_barrier_init(&barrier->pthread_barrier, NULL, core_count);
}



static inline void env_barrier_sync(env_barrier_t* barrier)
{
  pthread_barrier_wait(&barrier->pthread_barrier);
}

#else // RTE_EXEC_ENV_BAREMETAL defined

typedef struct env_barrier_t
{
  uint64_t core_mask;
  
} env_barrier_t;


static inline void env_barrier_init(env_barrier_t* barrier, uint64_t core_mask)
{
  
}

static inline void env_barrier_sync(env_barrier_t* barrier)
{
  
}
  
#endif // RTE_EXEC_ENV_BAREMETAL



/*
 * Spinlocks - NOTE: Avoid spinlocks in application code if possible
 */
#if 0 // Recursive spinlocks

typedef rte_spinlock_recursive_t env_spinlock_t;


static inline void
env_spinlock_init(env_spinlock_t *const lock)
{
  rte_spinlock_recursive_init((rte_spinlock_recursive_t *) lock);
}


static inline void
env_spinlock_lock(env_spinlock_t *const lock)
{
  rte_spinlock_recursive_lock((rte_spinlock_recursive_t *) lock);
}


static inline int
env_spinlock_trylock(env_spinlock_t *const lock)
{
  return rte_spinlock_recursive_trylock((rte_spinlock_recursive_t *) lock);
}


static inline int
env_spinlock_is_locked(env_spinlock_t *const lock)
{
  rte_spinlock_recursive_t *const rte_lock = (rte_spinlock_recursive_t *) lock;
  
  return rte_spinlock_is_locked(&lock->sl);
}


static inline void
env_spinlock_unlock(env_spinlock_t *const lock)
{
  //env_sync_mem();
  rte_spinlock_recursive_unlock((rte_spinlock_recursive_t *) lock);
}

#else // Normal Spinlocks

typedef rte_spinlock_t env_spinlock_t;


static inline void
env_spinlock_init(env_spinlock_t *const lock)
{
  rte_spinlock_init((rte_spinlock_t *) lock);
}


static inline void
env_spinlock_lock(env_spinlock_t *const lock)
{
  rte_spinlock_lock((rte_spinlock_t *) lock);
}


static inline int
env_spinlock_trylock(env_spinlock_t *const lock)
{
  return rte_spinlock_trylock((rte_spinlock_t *) lock);
}


static inline int
env_spinlock_is_locked(env_spinlock_t *const lock)
{
  return rte_spinlock_is_locked((rte_spinlock_t *) lock);
}


static inline void
env_spinlock_unlock(env_spinlock_t *const lock)
{
  //env_sync_mem();
  rte_spinlock_unlock((rte_spinlock_t *) lock);
}
#endif


#ifdef __cplusplus
}
#endif


#endif

