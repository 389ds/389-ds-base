/** BEGIN COPYRIGHT BLOCK
 * Copyright (c) 2016, William Brown <william at blackhats dot net dot au>
 * Copyright (c) 2017, Red Hat, Inc
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include "queue.h"
/*
 * The tqueue, or "Thread safe queue" attempts to use a lock free queue
 * if we have platform support, or it will us the mutex locked variant of queue.
 */

#ifdef ATOMIC_QUEUE_OPERATIONS

#include <liblfds711.h>

#define GC_HISTORY 32

/* Due to the masking in sds.h, you don't need to typedef this */
struct _sds_lqueue {
    /**
     * The lfds queue state.
     */
    struct lfds711_queue_umm_state queue;
    /**
     * Function pointer that can free enqueued values.
     */
    void (*value_free_fn)(void *value);
    /**
     * NSPR thread private data index for this queue to allow garbage collection.
     * without conflicting other running queues.
     */
    PRUintn gc_index;
};

typedef struct _sds_lqueue_gc {
    uint64_t current_generation;
    struct lfds711_queue_umm_element *garbage[GC_HISTORY];
} sds_lqueue_gc;

/* Thread local storage for last dequeued element */
/*
 * How and why does this work?
 * LFDS711 claims:
 * "Unusually, once a queue element has been dequeued, it can be deallocated. "
 * http://liblfds.org/mediawiki/index.php?title=r7.1.0:Queue_%28unbounded,_many_producer,_many_consumer%29#Lock-free_Specific_Behaviour
 *
 * However, during testing, this is not the case:
 * AddressSanitizer: heap-use-after-free on address 0x60300006cc58
 * ...
 *  #4 0x7fcf7e00bfc4 in job_queue_dequeue /home/william/development/389ds/nunc-stans/ns_thrpool.c:248
 *
 * We have the following issue.
 * Thread A will attempt to dequeue the pointer at HEAD. Thread B is *also*
 * about to attempt the same dequeue. When Thread B issues the load barrier
 * Thread A has not yet dequeue, so element E1 is still the HEAD.
 *
 * Thread A now atomically dequeues the item E1. However, Thread B *still*
 * has not checked the value. Thread A now frees E1. Thread B in umpmc at line
 * 99 now does the check of next -> value. This causes the heap use after free
 *
 * This means that lfds isn't quite safe for this!
 *
 * The queue itself is stable and correct, the element isn't dequeued twice, it's
 * just that it's possible for a dequeued and freed value to be reffered to by
 * another thread. The solution I have for this issue is somewhat inspired by
 * https://aturon.github.io/blog/2015/08/27/epoch/
 *
 * In thread local storage, we keep the last few elements we have dequeued
 * with a flag to point between A/B. This gives us a small generation
 * ring buffer in essence. When we dequeue an element E1, we store the ptr
 * into the TLS. Then, we dequeue E2, and store it also into TLS. When we dequeue
 * E3, we now free E1, and store E3 in the TLS. E4 will free E2 and store E4.
 *
 * The benefit of this is that when we go to free E1, we can *guarantee* that
 * Nothing should still be using it, or reffering to it, since we have taken
 * The next element out of the queue also!
 */

static void
sds_lqueue_tprivate_cleanup(void *priv) {
#ifdef SDS_DEBUG
    sds_log("sds_lqueue_tprivate_cleanup", "Closing thread GC");
#endif
    sds_lqueue_gc *gc = (sds_lqueue_gc *)priv;
    /* For each remaining element. */
    for (size_t i = 0; i < GC_HISTORY; i++) {
        /* Do a GC on them. */
        if (gc->garbage[i] != NULL) {
            sds_free(gc->garbage[i]);
        }
    }
    /* Free the struct */
    sds_free(gc);
}

sds_result
sds_lqueue_init(sds_lqueue **q_ptr, void (*value_free_fn)(void *value)) {
#ifdef SDS_DEBUG
    sds_log("sds_lqueue_init", "Creating lock free queue");
#endif
    if (q_ptr == NULL) {
#ifdef SDS_DEBUG
        sds_log("sds_lqueue_init", "Invalid q_ptr");
#endif
        return SDS_NULL_POINTER;
    }
    struct lfds711_queue_umm_element *qe_dummy = sds_malloc(sizeof(struct lfds711_queue_umm_element));
    *q_ptr = sds_memalign(sizeof(sds_lqueue), LFDS711_PAL_ATOMIC_ISOLATION_IN_BYTES);
    lfds711_queue_umm_init_valid_on_current_logical_core(&((*q_ptr)->queue), qe_dummy, NULL);

    /* Create the thread local storage for GC */
    if (PR_NewThreadPrivateIndex(&((*q_ptr)->gc_index), sds_lqueue_tprivate_cleanup) != PR_SUCCESS) {
#ifdef SDS_DEBUG
        sds_log("sds_lqueue_init", "Unable to create private index");
#endif
        sds_free(*q_ptr);
        return SDS_UNKNOWN_ERROR;
    }
    (*q_ptr)->value_free_fn = value_free_fn;

    return SDS_SUCCESS;
}

sds_result
sds_lqueue_tprep(sds_lqueue *q) {
    /* Get ready to run on this core. It's essentially a load barrier or full barrier */
    LFDS711_MISC_MAKE_VALID_ON_CURRENT_LOGICAL_CORE_INITS_COMPLETED_BEFORE_NOW_ON_ANY_OTHER_LOGICAL_CORE;
    sds_lqueue_gc *gc = NULL;
    /* Check if we have been initialised. */
    gc = PR_GetThreadPrivate(q->gc_index);
    if (gc != NULL) {
        return SDS_SUCCESS;
    }
    /* Prepare the garbage collection. It is important to use Calloc here! */
    gc = sds_calloc(sizeof(sds_lqueue_gc));
    /* Set the current generation */
    gc->current_generation = 0;
    /* Store the GC into the thread local. */
    if (PR_SetThreadPrivate(q->gc_index, (void *)gc) != PR_SUCCESS) {
        return SDS_UNKNOWN_ERROR;
    }
    return SDS_SUCCESS;
}

sds_result
sds_lqueue_enqueue(sds_lqueue *q, void *elem) {
#ifdef SDS_DEBUG
        sds_log("sds_lqueue_enqueue", "<== lf Queue %p elem %p", q, elem);
#endif
    struct lfds711_queue_umm_element *qe = sds_malloc(sizeof(struct lfds711_queue_umm_element));
    LFDS711_QUEUE_UMM_SET_VALUE_IN_ELEMENT(*qe, elem);
    lfds711_queue_umm_enqueue(&(q->queue), qe);
    return SDS_SUCCESS;
}

sds_result
sds_lqueue_dequeue(sds_lqueue *q, void **elem) {
#ifdef SDS_DEBUG
        sds_log("sds_lqueue_dequeue", "==> lf Queue %p elem %p", q, elem);
#endif
    if (elem == NULL) {
        return SDS_NULL_POINTER;
    }
    struct lfds711_queue_umm_element *qe = NULL;
    sds_lqueue_gc *gc = PR_GetThreadPrivate(q->gc_index);
    if (gc == NULL) {
        return SDS_UNKNOWN_ERROR;
    }
    if (lfds711_queue_umm_dequeue(&(q->queue), &qe) && qe) {
        *elem = LFDS711_QUEUE_UMM_GET_VALUE_FROM_ELEMENT( *qe );
        if (gc->garbage[gc->current_generation] != NULL) {
            sds_free(gc->garbage[gc->current_generation]);
        }
        gc->garbage[gc->current_generation] = qe;
        gc->current_generation = (gc->current_generation + 1) % GC_HISTORY;
        return SDS_SUCCESS;
    }
    return SDS_LIST_EXHAUSTED;
}

void
sds_lqueue_dummy_cleanup(struct lfds711_queue_umm_state *qs __attribute__((unused)), struct lfds711_queue_umm_element *qe, enum lfds711_misc_flag dummy_element_flag __attribute__((unused))) {
    /* Should only be the dummy! */
    sds_free(qe);
}

sds_result
sds_lqueue_destroy(sds_lqueue *q) {
    /* All the threads should be shutdown, so they GC themself. */
    /* There is no guarantee we have been thread preped, so setup GC now if needed. */
    sds_lqueue_tprep(q);
    /* We just need to dequeue everything, and free it. */
    void *ptr = NULL;

    while (sds_lqueue_dequeue(q, &ptr) == SDS_SUCCESS) {
        if (q->value_free_fn != NULL) {
            q->value_free_fn(ptr);
        }
    }
    /* The final GC will be triggered on thread delete */
    /* Finally, use the lfds cleanup to free the dummy */
    lfds711_queue_umm_cleanup(&(q->queue), sds_lqueue_dummy_cleanup);
    sds_free(q);

    return SDS_SUCCESS;
}
#else
/* Fall back to our tqueue implementation. */
sds_result
sds_lqueue_init(sds_lqueue **q_ptr, void (*value_free_fn)(void *value)) {
    return sds_tqueue_init(q_ptr, value_free_fn);
}

sds_result
sds_lqueue_tprep(sds_lqueue *q __attribute__((unused))) {
    return SDS_SUCCESS;
}

sds_result
sds_lqueue_enqueue(sds_lqueue *q, void *elem) {
    return sds_tqueue_enqueue(q, elem);
}

sds_result
sds_lqueue_dequeue(sds_lqueue *q, void **elem) {
    return sds_tqueue_dequeue(q, elem);
}

sds_result
sds_lqueue_destroy(sds_lqueue *q) {
    return sds_tqueue_destroy(q);
}
#endif


