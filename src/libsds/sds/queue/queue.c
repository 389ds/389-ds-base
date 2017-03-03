/** BEGIN COPYRIGHT BLOCK
 * Copyright (c) 2016, William Brown <william at blackhats dot net dot au>
 * Copyright (c) 2017, Red Hat, Inc
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include "queue.h"

/* Create */
//__attribute__ ((visibility ("hidden")))

sds_result
sds_queue_init(sds_queue **q_ptr, void (*value_free_fn)(void *value)) {
    if (q_ptr == NULL) {
#ifdef DEBUG
        sds_log("sds_queue_init", "Invalid p_ptr");
#endif
        return SDS_NULL_POINTER;
    }
    *q_ptr = sds_malloc(sizeof(sds_queue));
    (*q_ptr)->head = NULL;
    (*q_ptr)->tail = NULL;
    (*q_ptr)->value_free_fn = value_free_fn;
    return SDS_SUCCESS;
}

/* Enqueue */

sds_result
sds_queue_enqueue(sds_queue *q, void *elem) {
#ifdef DEBUG
    sds_log("sds_queue_dequeue", "Queue %p - <== enqueuing", q);
#endif
    sds_queue_node *node = sds_malloc(sizeof(sds_queue_node));
#ifdef DEBUG
    sds_log("sds_queue_enqueue", "Queue %p - Queueing ptr %p to %p", q, elem, node);
#endif
    node->element = elem;
    node->prev = NULL;
    node->next = q->tail;
    if (q->tail != NULL) {
        q->tail->prev = node;
    } else {
        /* If tail is null, head must ALSO be null. */
#ifdef DEBUG
        sds_log("sds_queue_enqueue", "Queue %p - empty, adding %p to head and tail", q, node);
#endif
        q->head = node;
    }
    q->tail = node;
#ifdef DEBUG
    sds_log("sds_queue_enqueue", "Queue %p - complete head: %p tail: %p", q, q->head, q->tail);
#endif
    return SDS_SUCCESS;
}

/* Dequeue */

sds_result
sds_queue_dequeue(sds_queue *q, void **elem) {
#ifdef DEBUG
    sds_log("sds_queue_dequeue", "Queue %p - ==> dequeuing", q);
#endif
    if (elem == NULL) {
#ifdef DEBUG
        sds_log("sds_queue_dequeue", "Queue %p - NULL pointer for **elem", q);
#endif
        return SDS_NULL_POINTER;
    }
    if (q->head == NULL) {
#ifdef DEBUG
        sds_log("sds_queue_dequeue", "Queue %p - queue exhausted.", q);
#endif
        return SDS_LIST_EXHAUSTED;
    }
    sds_queue_node *node = q->head;
    *elem = node->element;
    q->head = node->prev;
    sds_free(node);
    if (q->head == NULL) {
        // If we have no head node, we also have no tail.
        q->tail = NULL;
    }
#ifdef DEBUG
    sds_log("sds_queue_dequeue", "Queue %p - complete head: %p tail: %p", q, q->head, q->tail);
#endif
    return SDS_SUCCESS;
}


/* Map, filter, reduce? */

/* Destroy */

sds_result
sds_queue_destroy(sds_queue *q) {
#ifdef DEBUG
    sds_log("sds_queue_destroy", "Queue %p - destroying", q);
#endif
    /* Map over the queue and free the elements. */
    sds_queue_node *node = q->head;
    sds_queue_node *prev = NULL;
    while (node != NULL) {
        prev = node->prev;
        if (q->value_free_fn != NULL) {
#ifdef DEBUG
            sds_log("sds_queue_destroy", "Queue %p - implicitly freeing %p", q, node->element);
#endif
            q->value_free_fn(node->element);
        }
        sds_free(node);
        node = prev;
    }
    sds_free(q);
    return SDS_SUCCESS;
}


