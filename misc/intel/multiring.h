/*-
 *   BSD LICENSE
 *
 *   Copyright(c) 2010-2012 Intel Corporation. All rights reserved.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Derived from rte_ring.h
 *
 * Derived from FreeBSD's bufring.h
 *
 **************************************************************************
 *
 * Copyright (c) 2007-2009 Kip Macy kmacy@freebsd.org
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. The name of Kip Macy nor the names of other
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ***************************************************************************/

#ifndef _MULTIRING_H_
#define _MULTIRING_H_

/**
 * The Ring Manager is a fixed-size queue, implemented as a table of
 * pointers. Head and tail pointers are modified atomically, allowing
 * concurrent access to it. It has the following features:
 *
 * - FIFO (First In First Out)
 * - Maximum size is fixed; the pointers are stored in a table.
 * - Lockless implementation.
 * - Multi- or single-consumer dequeue.
 * - Multi- or single-producer enqueue.
 * - Bulk dequeue.
 * - Bulk enqueue.
 *
 * Note: the ring implementation is not preemptable. A lcore must not
 * be interrupted by another task that uses the same ring.
 *
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <errno.h>
#include <rte_common.h>
#include <rte_memory.h>
#include <rte_lcore.h>
#include <rte_atomic.h>
#include <rte_branch_prediction.h>
#include <rte_string_fns.h>
#include <xmmintrin.h>

#define MRING_NAMESIZE 32 /**< The maximum length of a ring name. */

#define NUM_PRIORITIES	4
//#define RING_SIZE 1024
#define RING_SIZE (4*1024)
#define RING_MASK (RING_SIZE-1)

/**
 * Unsigned multi-int value. Can be accessed/used as
 *  - Four 32-bit values e.g. four head/tail indexes
 *  - Single 128-bit value for SSE operations
 *  - Two 64-bit values.
 */
union umultiint {
	uint32_t val[NUM_PRIORITIES];
	__m128i mval;
	struct {
		uint64_t hiqw;
		uint64_t loqw;
	};
};

/**
 * Signed multi-int value. Can be accessed/used as
 *  - Four 32-bit values e.g. four head/tail indexes
 *  - Single 128-bit value for SSE operations
 *  - Two 64-bit values.
 */
union smultiint {
	int32_t val[NUM_PRIORITIES];
	__m128i mval;
	struct {
		uint64_t hiqw;
		uint64_t loqw;
	};
};

/**
 * multi-ring structure.
 *
 * The producer and the consumer have a head and a tail index. The particularity
 * of these index is that they are not between 0 and size(ring). These indexes
 * are between 0 and 2^32, and we mask their value when we access the ring[]
 * field. Thanks to this assumption, we can do subtractions between 2 index
 * values in a modulo-32bit base: that's why the overflow of the indexes is not
 * a problem.
 */
struct multiring {

	char name[MRING_NAMESIZE];    /**< Name of the ring. */
	int flags;                       /**< Flags supplied at creation. */

	/** Ring producer status. */
	struct mprod {
		volatile union umultiint head;  /**< Producer head. */
		volatile union umultiint tail;  /**< Producer tail. */
		uint32_t sp_enqueue;     /**< True, if single producer. */
	} prod __rte_cache_aligned;

	/** Ring consumer status. */
	struct mcons {
		volatile union umultiint head;  /**< Consumer head. */
		volatile union umultiint tail;  /**< Consumer tail. */
		uint32_t sc_dequeue;     /**< True, if single consumer. */
	} cons __rte_cache_aligned;


	void * volatile ring[NUM_PRIORITIES][RING_SIZE] \
			__rte_cache_aligned; /**< Memory space of ring starts here. */
};

#define RING_F_SP_ENQ 0x0001 /**< The default enqueue is "single-producer". */
#define RING_F_SC_DEQ 0x0002 /**< The default dequeue is "single-consumer". */
#define RTE_RING_SZ_MASK  (unsigned)(0x0fffffff) /**< Ring size mask */

/**
 * Create a new ring named *name* in memory.
 *
 * This function uses ``memzone_reserve()`` to allocate memory. Its size is
 * set to *count*, which must be a power of two. Water marking is
 * disabled by default.
 * Note that the real usable ring size is *count-1* instead of
 * *count*.
 *
 * @param name
 *   The name of the ring.
 * @param socket_id
 *   The *socket_id* argument is the socket identifier in case of
 *   NUMA. The value can be *SOCKET_ID_ANY* if there is no NUMA
 *   constraint for the reserved zone.
 * @param flags
 *   An OR of the following:
 *    - RING_F_SP_ENQ: If this flag is set, the default behavior when
 *      using ``mring_enqueue()`` or ``mring_enqueue_bulk()``
 *      is "single-producer". Otherwise, it is "multi-producers".
 *    - RING_F_SC_DEQ: If this flag is set, the default behavior when
 *      using ``mring_dequeue()`` or ``mring_dequeue_bulk()``
 *      is "single-consumer". Otherwise, it is "multi-consumers".
 * @return
 *   On success, the pointer to the new allocated ring. NULL on error with
 *    rte_errno set appropriately.
 */
static inline struct multiring *
mring_create(const char *name, int socket_id, unsigned flags)
{
    char mz_name[RTE_MEMZONE_NAMESIZE];
    struct multiring *r;
    const struct rte_memzone *mz;
    int mz_flags = 0;

    /* compilation-time checks */
    RTE_BUILD_BUG_ON((sizeof(struct multiring) &
              CACHE_LINE_MASK) != 0);
    RTE_BUILD_BUG_ON((offsetof(struct multiring, cons) &
              CACHE_LINE_MASK) != 0);
    RTE_BUILD_BUG_ON((offsetof(struct multiring, prod) &
              CACHE_LINE_MASK) != 0);

    rte_snprintf(mz_name, sizeof(mz_name), "MRG_%s", name);

    /* reserve a memory zone for this ring. If we can't get rte_config or
     * we are secondary process, the memzone_reserve function will set
     * rte_errno for us appropriately - hence no check in this this function */
    mz = rte_memzone_reserve(mz_name, sizeof(*r), socket_id, mz_flags);
    if (mz == NULL)
        return NULL;

    r = mz->addr;

    /* init the ring structure */
    memset(r, 0, sizeof(*r));
    rte_snprintf(r->name, sizeof(r->name), "%s", name);
    r->flags = flags;
    r->prod.sp_enqueue = !!(flags & RING_F_SP_ENQ);
    r->cons.sc_dequeue = !!(flags & RING_F_SC_DEQ);

    return r;
}

/**
 * Enqueue several objects on the ring (multi-producers safe).
 *
 * This function uses a "compare and set" instruction to move the
 * producer index atomically.
 *
 * @param r
 *   A pointer to the ring structure.
 * @param priority
 *   The ring within the multi-ring to be used
 * @param obj_table
 *   A pointer to a table of void * pointers (objects).
 * @param max
 *   The number of objects to add in the ring from the obj_table.
 * @return
 *   - n: Actual number of objects enqueued.
 */
static inline int
__mring_mp_do_enqueue(struct multiring *r, const uint8_t priority,
		void * const *obj_table, const unsigned max)
{
	uint32_t prod_head, prod_next;
	uint32_t cons_tail, free_entries;
	unsigned n;
	int success;
	unsigned i;

	/* move prod.head atomically */
	do {
		/* Reset n to the initial burst count */
		n = max;

		prod_head = r->prod.head.val[priority];
		cons_tail = r->cons.tail.val[priority];
		/* The subtraction is done between two unsigned 32bits value
		 * (the result is always modulo 32 bits even if we have
		 * prod_head > cons_tail). So 'free_entries' is always between 0
		 * and size(ring)-1. */
		free_entries = (RING_MASK + cons_tail - prod_head);

		/* check that we have enough room in ring */
		if (unlikely(n > free_entries))
			n = free_entries;

		if (unlikely(n == 0))
			return 0;

		prod_next = prod_head + n;
		success = rte_atomic32_cmpset(&r->prod.head.val[priority], prod_head,
					      prod_next);
	} while (unlikely(success == 0));

	/* write entries in ring */
	for (i = 0; likely(i < n); i++)
		r->ring[priority][(prod_head + i) & RING_MASK] = obj_table[i];
	rte_wmb();

	/*
	 * If there are other enqueues in progress that preceeded us,
	 * we need to wait for them to complete
	 */
	while (unlikely(r->prod.tail.val[priority] != prod_head))
		rte_pause();

	r->prod.tail.val[priority] = prod_next;
	return n;
}

/**
 * Enqueue several objects on a ring (NOT multi-producers safe).
 *
 * @param r
 *   A pointer to the ring structure.
 * @param priority
 *   The ring within the multi-ring to be used
 * @param obj_table
 *   A pointer to a table of void * pointers (objects).
 * @param n
 *   The number of objects to add in the ring from the obj_table.
 * @return
 *   - n: Actual number of objects enqueued.
 */
static inline int
__mring_sp_do_enqueue(struct multiring *r, const uint8_t priority,
		void * const *obj_table, unsigned n)
{
	uint32_t prod_head, cons_tail;
	uint32_t prod_next, free_entries;
	unsigned i;

	prod_head = r->prod.head.val[priority];
	cons_tail = r->cons.tail.val[priority];
	/* The subtraction is done between two unsigned 32bits value
	 * (the result is always modulo 32 bits even if we have
	 * prod_head > cons_tail). So 'free_entries' is always between 0
	 * and size(ring)-1. */
	free_entries = RING_MASK + cons_tail - prod_head;

	/* check that we have enough room in ring */
	if (unlikely(n > free_entries))
		n = free_entries;

	if (unlikely(n == 0))
		return 0;

	prod_next = prod_head + n;
	r->prod.head.val[priority] = prod_next;

	/* write entries in ring */
	for (i = 0; likely(i < n); i++)
		r->ring[priority][(prod_head + i) & RING_MASK] = obj_table[i];
	rte_wmb();

	r->prod.tail.val[priority] = prod_next;
	return n;
}

/**
 * Dequeue several objects from a ring (multi-consumers safe). When the request
 * objects are more than the available objects, only dequeue the actual number
 * of objects
 *
 * This function uses a "compare and set" instruction to move the
 * consumer index atomically.
 *
 * @param r
 *   A pointer to the ring structure.
 * @param priority
 *   The ring within the multi-ring to be used
 * @param obj_table
 *   A pointer to a table of void * pointers (objects) that will be filled.
 * @param n
 *   The number of objects to dequeue from the ring to the obj_table.
 * @return
 *   - n: Actual number of objects dequeued.
 */

static inline int
__mring_mc_do_dequeue(struct multiring *r, const uint8_t priority,
		void **obj_table, unsigned n)
{
	uint32_t cons_head, prod_tail;
	uint32_t cons_next, entries;
	const unsigned max = n;
	int success;
	unsigned i;

	/* move cons.head atomically */
	do {
		/* Restore n as it may change every loop */
		n = max;

		cons_head = r->cons.head.val[priority];
		prod_tail = r->prod.tail.val[priority];
		/* The subtraction is done between two unsigned 32bits value
		 * (the result is always modulo 32 bits even if we have
		 * cons_head > prod_tail). So 'entries' is always between 0
		 * and size(ring)-1. */
		entries = (prod_tail - cons_head);

		/* Set the actual entries for dequeue */
		if (unlikely(n > entries))
			n = entries;
		if (unlikely(n == 0))
			return 0;

		cons_next = cons_head + n;
		success = rte_atomic32_cmpset(&r->cons.head.val[priority], cons_head,
					      cons_next);
	} while (unlikely(success == 0));

	/* copy in table */
	rte_rmb();
	for (i = 0; likely(i < n); i++) {
		obj_table[i] = r->ring[priority][(cons_head + i) & RING_MASK];
	}

	/*
	 * If there are other dequeues in progress that preceded us,
	 * we need to wait for them to complete
	 */
	while (unlikely(r->cons.tail.val[priority] != cons_head))
		rte_pause();

	r->cons.tail.val[priority] = cons_next;

	return n;
}

/**
 * Dequeue several objects from a ring (NOT multi-consumers safe).When the
 * request objects are more than the available objects, only dequeue the
 * actual number of objects
 *
 * @param r
 *   A pointer to the ring structure.
 * @param priority
 *   The ring within the multi-ring to be used
 * @param obj_table
 *   A pointer to a table of void * pointers (objects) that will be filled.
 * @param n
 *   The number of objects to dequeue from the ring to the obj_table.
 * @return
 *   - n: Actual number of objects dequeued.
 */
static inline int
__mring_sc_do_dequeue(struct multiring *r, const uint8_t priority,
		void **obj_table, unsigned n)
{
	uint32_t cons_head, prod_tail;
	uint32_t cons_next, entries;
	unsigned i;

	cons_head = r->cons.head.val[priority];
	prod_tail = r->prod.tail.val[priority];
	/* The subtraction is done between two unsigned 32bits value
	 * (the result is always modulo 32 bits even if we have
	 * cons_head > prod_tail). So 'entries' is always between 0
	 * and size(ring)-1. */
	entries = prod_tail - cons_head;

	if (unlikely(n > entries))
		n = entries;
	if (unlikely(n == 0))
		return 0;

	cons_next = cons_head + n;
	r->cons.head.val[priority] = cons_next;

	/* copy in table */
	rte_rmb();
	for (i = 0; likely(i < n); i++) {
		obj_table[i] = r->ring[priority][(cons_head + i) & RING_MASK];
	}

	r->cons.tail.val[priority] = cons_next;
	return n;
}

/**
 * Enqueue several objects on a ring.
 *
 * This function calls the multi-producer or the single-producer
 * version depending on the default behavior that was specified at
 * ring creation time (see flags).
 *
 * @param r
 *   A pointer to the ring structure.
 * @param priority
 *   The ring within the multi-ring to be used
 * @param obj_table
 *   A pointer to a table of void * pointers (objects).
 * @param n
 *   The number of objects to add in the ring from the obj_table.
 * @return
 *   - n: Actual number of objects enqueued. 0 on error
 */
static inline int
mring_enqueue_burst(struct multiring *r, const uint8_t priority,
		void * const *obj_table, unsigned n)
{
	if (r->prod.sp_enqueue)
		return 	__mring_sp_do_enqueue(r, priority, obj_table, n);
	else
		return 	__mring_mp_do_enqueue(r, priority, obj_table, n);
}


/**
 * Dequeue multiple objects from a ring up to a maximum number.
 *
 * This function calls the multi-consumers or the single-consumer
 * version, depending on the default behaviour that was specified at
 * ring creation time (see flags).
 *
 * @param r
 *   A pointer to the ring structure.
 * @param priority
 *   The ring within the multi-ring to be used
 * @param obj_table
 *   A pointer to a table of void * pointers (objects) that will be filled.
 * @param n
 *   The number of objects to dequeue from the ring to the obj_table.
 * @return
 *   - Number of objects dequeued, 0 on error
 */
static inline int
mring_dequeue_burst(struct multiring *r, const uint8_t priority,
		void **obj_table, unsigned n)
{
	if (r->cons.sc_dequeue)
		return __mring_sc_do_dequeue(r, priority, obj_table, n);
	else
		return __mring_mc_do_dequeue(r, priority, obj_table, n);
}

/**
 * Enqueue one object on a ring.
 *
 * This function calls the multi-producer or the single-producer
 * version, depending on the default behaviour that was specified at
 * ring creation time (see flags).
 *
 * @param r
 *   A pointer to the ring structure.
 * @param priority
 *   The ring within the multi-ring to be used
 * @param obj
 *   A pointer to the object to be added.
 * @return
 *   - 1 if object enqueued, 0 on error.
 */
static inline int
mring_enqueue(struct multiring *r, const uint8_t priority, void *obj)
{
	return mring_enqueue_burst(r, priority, &obj, 1);
}

/**
 * Dequeue one object from a ring.
 *
 * This function calls the multi-consumers or the single-consumer
 * version depending on the default behaviour that was specified at
 * ring creation time (see flags).
 *
 * @param r
 *   A pointer to the ring structure.
 * @param priority
 *   The ring within the multi-ring to be used
 * @param obj_p
 *   A pointer to a void * pointer (object) that will be filled.
 * @return
 *   - 1 on success, 0 on error.
 */
static inline int
mring_dequeue(struct multiring *r, const uint8_t priority, void **obj_p)
{
	if (r->cons.sc_dequeue)
		return __mring_sc_do_dequeue(r, priority, obj_p, 1);
	else
		return __mring_mc_do_dequeue(r, priority, obj_p, 1);
}

/**
 * Dequeue multiple objects from a ring, taking objects from the queues
 * of different priorities with different weightings.
 *
 * @param r
 *   A pointer to the ring structure.
 * @param obj_p
 *   A pointer to void * pointers (object) that will be filled.
 * @param max
 *   Max number of items to dequeue, limited to 16, 32 or 64.
 * @return
 *   - Number of objects dequeued.
 */
static inline int
mring_dequeue_mp_burst(struct multiring *r, void **obj_p, const int max)
{
	union umultiint quota = {.val = {1, 2, 4, 9}};
	union umultiint cons_head, prod_tail;
	union umultiint cons_next, entries;
	unsigned j, buf_idx=0;
	int i, n;
	uint8_t success;

	switch (max) {
	case 16: break;
	case 32: quota.mval = _mm_slli_epi32(quota.mval, 1); break;
	case 64: quota.mval = _mm_slli_epi32(quota.mval, 2);break;
	default: return 0;
	}

	do {
		cons_head.mval = r->cons.head.mval;
		prod_tail.mval = r->prod.tail.mval;

		entries.mval = _mm_sub_epi32(prod_tail.mval, cons_head.mval);
		if (entries.hiqw == 0 && entries.loqw == 0)
			return 0;

		n = entries.val[0] + entries.val[1] + entries.val[2] + entries.val[3];
		if (n > max) {
			union smultiint diff;
			diff.mval = _mm_sub_epi32(entries.mval, quota.mval);
			for (i = 0; i < NUM_PRIORITIES && n > max; i++) {
				if (diff.val[i] > 0) {
					const int delta = (n-max) < diff.val[i] \
							? (n-max) : diff.val[i];
					n -= delta, entries.val[i] -= delta;
				}
			}
		}

		cons_next.mval = _mm_add_epi32(cons_head.mval, entries.mval);

		// atomically update the four head pointers in one operation
		asm volatile ("lock cmpxchg16b %0; "
				"setz %1"
				: "+m" (r->cons.head), "=r" (success),
				  "+d" (cons_head.loqw), "+a" (cons_head.hiqw)
				: "c" (cons_next.loqw), "b" (cons_next.hiqw)
				: "cc");

	} while (unlikely(success == 0));

	rte_rmb();
	// for (i = 0; i < NUM_PRIORITIES; i++) {
	for (i = (NUM_PRIORITIES-1); i >= 0; i--) {
#ifdef mr_dbg_printf
		mr_dbg_printf("Priority %d: %d, ", i, entries.val[i]);
#endif
		if (entries.val[i] == 0)
			continue;
		for (j = 0; j < entries.val[i]; j++)
			obj_p[buf_idx++] = r->ring[i][(cons_head.val[i] + j) & RING_MASK];
		while (unlikely(r->cons.tail.val[i] != cons_head.val[i]))
			rte_pause();
		r->cons.tail.val[i] = cons_next.val[i];
	}
#ifdef mr_dbg_printf
	mr_dbg_printf("\n");
#endif

	return n;
}

/**
 * Determine if a ring is empty or not
 *
 * @param r
 *   The multiring to query
 * @return
 *   non-zero if ring is empty, zero otherwise.
 */
static inline int
mring_empty(struct multiring *r)
{
	union umultiint entries;
	entries.mval = _mm_sub_epi32(r->prod.tail.mval, r->cons.head.mval);
	return (entries.hiqw == 0 && entries.loqw == 0);
}


#ifdef __cplusplus
}
#endif

#endif /* _MULTIRING_H_ */
