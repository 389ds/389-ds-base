/** BEGIN COPYRIGHT BLOCK
 * Copyright (c) 2016, William Brown <william at blackhats dot net dot au>
 * Copyright (c) 2017, Red Hat, Inc
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include "queue.h"

/* Keep's your threads safer for longer! */
/* Want longer lasting threads? */


sds_result
sds_tqueue_init(sds_tqueue **q_ptr, void (*value_free_fn)(void *value)) {
#ifdef SDS_DEBUG
    sds_log("sds_tqueue_init", "Createing mutex locked queue");
#endif
    if (q_ptr == NULL) {
#ifdef SDS_DEBUG
        sds_log("sds_tqueue_init", "Invalid p_ptr");
#endif
        return SDS_NULL_POINTER;
    }
    *q_ptr = sds_malloc(sizeof(sds_tqueue));
    sds_result result = sds_queue_init(&(*q_ptr)->uq, value_free_fn);
    if (result != SDS_SUCCESS) {
        sds_free(*q_ptr);
        return result;
    }
    pthread_mutex_init(&(*q_ptr)->lock, NULL);
    return SDS_SUCCESS;
}

sds_result
sds_tqueue_enqueue(sds_tqueue *q, void *elem) {
    pthread_mutex_lock(&(q->lock));
    sds_result result = sds_queue_enqueue(q->uq, elem);
    pthread_mutex_unlock(&(q->lock));
    return result;
}

sds_result
sds_tqueue_dequeue(sds_tqueue *q, void **elem) {
    pthread_mutex_lock(&(q->lock));
    sds_result result = sds_queue_dequeue(q->uq, elem);
    pthread_mutex_unlock(&(q->lock));
    return result;
}

sds_result
sds_tqueue_destroy(sds_tqueue *q) {
    pthread_mutex_lock(&(q->lock));
    sds_result result = sds_queue_destroy(q->uq);
    pthread_mutex_unlock(&(q->lock));
    pthread_mutex_destroy(&(q->lock));
    sds_free(q);
    return result;
}




