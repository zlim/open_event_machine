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
 * Event Machine core mask functions. This implementation is generic C - non HW optimized. 
 *
 */

#ifndef EVENT_MACHINE_CORE_MASK_H_
#define EVENT_MACHINE_CORE_MASK_H_

#ifdef __cplusplus
extern "C" {
#endif


#include <event_machine.h>




#if EM_CORE_MASK_SIZE != 64
#error Core mask functions support only 64 bit mask size
#endif



#ifdef EM_64_BIT
/*
 *
 * 64 bit versions.
 * --------------------------------------------
 */


/**
 * Zero the whole mask.
 *
 * @param mask      Core mask
 */
static inline void em_core_mask_zero(em_core_mask_t* mask)
{
  mask->u64[0] = 0;
}


/**
 * Set a bit in the mask.
 *
 * @param core      Core id
 * @param mask      Core mask
 */
static inline void em_core_mask_set(int core, em_core_mask_t* mask)
{
  mask->u64[0] |= ((uint64_t)1 << core);
}


/**
 * Clear a bit in the mask.
 *
 * @param core      Core id
 * @param mask      Core mask
 */
static inline void em_core_mask_clr(int core, em_core_mask_t* mask)
{
  mask->u64[0] &= ~((uint64_t)1 << core);
}


/**
 * Test if a bit is set in the mask.
 *
 * @param core      Core id
 * @param mask      Core mask
 *
 * @return Non-zero if core id is set in the mask
 */
static inline int em_core_mask_isset(int core, const em_core_mask_t* mask)
{
  return (mask->u64[0] & ((uint64_t)1 << core));
}


/**
 * Test if the mask is all zero.
 *
 * @param mask      Core mask
 *
 * @return Non-zero if the mask is all zero
 */
static inline int em_core_mask_iszero(const em_core_mask_t* mask)
{
  return (mask->u64[0] == 0);
}


/**
 * Test if two masks are equal
 *
 * @param mask1     First core mask
 * @param mask2     Second core mask
 *
 * @return Non-zero if the two masks are equal
 */
static inline int em_core_mask_equal(const em_core_mask_t* mask1, const em_core_mask_t* mask2)
{
  return (mask1->u64[0] == mask2->u64[0]);
}


/**
 * Set a range (0...count-1) of bits in the mask.
 * 
 * @param count     Number of bits to set
 * @param mask      Core mask
 */
static inline void em_core_mask_set_count(int count, em_core_mask_t* mask)
{
  mask->u64[0] |= (((uint64_t)1 << count) - 1);
}


/**
 * Copy core mask
 *
 * @param dest      Destination core mask
 * @param src       Source core mask
 */
static inline void em_core_mask_copy(em_core_mask_t* dest, const em_core_mask_t* src)
{
  dest->u64[0] = src->u64[0];
}


/**
 * Count the number of bits set in the mask.
 *
 * @param mask      Core mask
 *
 * @return Number of bits set
 */
static inline int em_core_mask_count(const em_core_mask_t* mask)
{
  uint64_t n = mask->u64[0];
  int      cnt;
  

  for(cnt = 0; n; cnt++)
  {
    n &= (n - 1); // Clear the least significant bit set
  }
  
  return cnt;
}




#elif defined(EM_32_BIT)
/*
 *
 * 32 bit versions.
 * --------------------------------------------
 */


/**
 * Zero the whole mask.
 *
 * @param mask      Core mask
 */
static inline void em_core_mask_zero(em_core_mask_t* mask)
{
  mask->u32[0] = 0;
  mask->u32[1] = 0;
}


/**
 * Set a bit in the mask.
 *
 * @param core      Core id
 * @param mask      Core mask
 */
static inline void em_core_mask_set(int core, em_core_mask_t* mask)
{
  if(core < 32)
  {
    mask->u32[1] |= ((uint32_t)1 << core);
  }
  else
  {
    mask->u32[0] |= ((uint32_t)1 << (core - 32) );
  }
}


/**
 * Clear a bit in the mask.
 *
 * @param core      Core id
 * @param mask      Core mask
 */
static inline void em_core_mask_clr(int core, em_core_mask_t* mask)
{
  if(core < 32)
  {
    mask->u32[1] &= ~((uint32_t)1 << core);
  }
  else
  {
    mask->u32[0] &= ~((uint32_t)1 << (core - 32) );
  }
}


/**
 * Test if a bit is set in the mask.
 *
 * @param core      Core id
 * @param mask      Core mask
 *
 * @return Non-zero if core id is set in the mask
 */
static inline int em_core_mask_isset(int core, const em_core_mask_t* mask)
{
  if(core < 32)
  {
    return (mask->u32[1] & ((uint32_t)1 << core));
  }
  else
  {
    return (mask->u32[0] & ((uint32_t)1 << (core - 32)));
  }
}


/**
 * Test if the mask is all zero.
 *
 * @param mask      Core mask
 *
 * @return Non-zero if the mask is all zero
 */
static inline int em_core_mask_iszero(const em_core_mask_t* mask)
{
  return ((mask->u32[0] == 0) && (mask->u32[1] == 0));
}


/**
 * Test if two masks are equal
 *
 * @param mask1     First core mask
 * @param mask2     Second core mask
 *
 * @return Non-zero if the two masks are equal
 */
static inline int em_core_mask_equal(const em_core_mask_t* mask1, const em_core_mask_t* mask2)
{
  return ((mask1->u32[0] == mask2->u32[0]) && (mask1->u32[1] == mask2->u32[1]));
}


/**
 * Set a range (0...count-1) of bits in the mask.
 * 
 * @param count     Number of bits to set
 * @param mask      Core mask
 */
static inline void em_core_mask_set_count(int count, em_core_mask_t* mask)
{
  if(count <= 32)
  {
    mask->u32[1] |= (((uint32_t)1 << count) - 1);
  }
  else
  {
    mask->u32[1] |= 0xffffffff;
    mask->u32[0] |= (((uint32_t)1 << (count - 32)) - 1);
  }
}


/**
 * Copy core mask
 *
 * @param dest      Destination core mask
 * @param src       Source core mask
 */
static inline void em_core_mask_copy(em_core_mask_t* dest, const em_core_mask_t* src)
{
  dest->u32[0] = src->u32[0];
  dest->u32[1] = src->u32[1];
}


/**
 * Count the number of bits set in the mask.
 *
 * @param mask      Core mask
 *
 * @return Number of bits set
 */
static inline int em_core_mask_count(const em_core_mask_t* mask)
{
  uint32_t n;
  int      cnt, i;
  
  for(i = 0, cnt = 0; i < 2; i++)
  {
    n = mask->u32[i];
    
    for(; n; cnt++) {
      n &= (n - 1); // clear the least significant bit set
    }
  }
  
  return cnt;
}

#endif




/*
 *  These mask functions could be added also
 *
 *  void em_core_mask_and(em_core_mask_t* dest, const em_core_mask_t* src1, const em_core_mask_t* src2);
 *  void em_core_mask_or(em_core_mask_t* dest, const em_core_mask_t* src1, const em_core_mask_t* src2);
 *  void em_core_mask_xor(em_core_mask_t* dest, const em_core_mask_t* src1, const em_core_mask_t* src2);
 *
 */




#ifdef __cplusplus
}
#endif


#endif /* EVENT_MACHINE_CORE_MASK_H_ */

