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


#include "environment.h"
#include <stdio.h>
#include <stdlib.h>

#include "intel_hw_init.h"



typedef union
{
  uint64_t core_hz ENV_CACHE_LINE_ALIGNED;
  
  uint8_t  u8[ENV_CACHE_LINE_SIZE];
  
} intel_env_core_local_t;



// Per core
ENV_LOCAL  intel_env_core_local_t  intel_env_core_local = {.core_hz = 0};



/**
 * Return the used core frequency
 */
uint64_t
env_core_hz_linux(void)
{

  if(likely(intel_env_core_local.core_hz))
  {
    return intel_env_core_local.core_hz;
  }
  else
  {
    FILE     *file;
    uint64_t  hz  = 0;
    double    mhz = 0.0;
    char      tmp[256] = {'\0'};


#if 0
    // CPU0, expect that all cores at same freq
    file = fopen("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_cur_freq", "rt");

    if(file == NULL)
    {
      printf("file open failed !\n");
    }

    if( fscanf(file, "%u", &khz) != 1)
    {
      printf("fscanf failed !\n");
    }
#else

    file = fopen("/proc/cpuinfo", "rt");
    if(file == NULL)
    {
     printf("%s:L%i file open failed !", __func__, __LINE__);
     abort(); 
    }
    
    // Read /proc/cpuinfo and search for the substring "cpu MHz".
    // CPU0, expect that all cores at same freq...
    while(fgets(tmp, sizeof(tmp), file) != NULL)
    {
      if(strstr(tmp, "cpu MHz"))
      {
        // Found! Now extract the MHz info from the substring. 
        if(sscanf(tmp, "cpu MHz : %lf", &mhz) == 1) {
          break;
        }
        else {
          printf("%s:L%i No cpu MHz info found !", __func__, __LINE__);
          abort();
        }
      }
    }

#endif

 
    fclose(file);    

 
    hz = (uint64_t) (mhz * 1000 * 1000);
    
    intel_env_core_local.core_hz = hz;

    // printf("CPU freq: %f MHz (%"PRIu64"Hz)\n", mhz, hz);

    return hz;
  }

}



/**
 * Reserve hugepage memory (static allocation) that can be shared by multiple processes.
 *
 * @note Memory reserved with env_shared_reserve() can NOT be unreserved.
 * Use at initialization time only.
 * 
 * @param name   
 * @param size  
 * 
 */
void *
env_shared_reserve(const char *name, size_t size)
{
  const struct rte_memzone *mz;
  void                     *buf = NULL;
  
  
  IF_UNLIKELY((name == NULL) || (size == 0)) {
    return NULL;
  }
  
  
  mz = rte_memzone_reserve(name, size, DEVICE_SOCKET, 0);  
  
  IF_LIKELY(mz != NULL)
  {
		buf = mz->addr; // virtual address
	}
	
	return buf;
}



/**
 * Lookup shared hugepage memory previously reserved by env_shared_reserve().
 * 
 * @note Memory reserved with env_shared_reserve() can NOT be unreserved.
 * Use at initialization time only.
 *
 * @param name  
 * 
 * @see env_shared_reserve()
 */
void *
env_shared_lookup(const char *name)
{
  const struct rte_memzone *mz;
  void                     *buf = NULL;
    

  IF_UNLIKELY(name == NULL) {
    return NULL;
  }
  
  
  mz = rte_memzone_lookup(name);
  
  IF_LIKELY(mz != NULL)
  {
    buf = mz->addr; // virtual address
  }
  
  return buf;
}



/**
 * Allocate shared hugepage memory.
 * 
 * @note Not for use in the fast path.
 */
void *
env_shared_malloc(size_t size)
{                
  return rte_malloc(NULL, size, 0); // Cache-line aligned alloc
}



/**
 * Frees memory previously allocated by env_shared_malloc()
 *
 * @note Not for use in the fast path.
 */
void
env_shared_free(void *buf)
{
  rte_free(buf); // rte_free() performs NULL check
}



