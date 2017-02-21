/** BEGIN COPYRIGHT BLOCK
 * Copyright (c) 2016, William Brown <william at blackhats dot net dot au>
 * All rights reserved.
 *
 * License: License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include "queue.h"

/* Keep's your threads safer for longer! */
/* Want longer lasting threads? */


sds_result
sds_tqueue_init(sds_tqueue **q_ptr, void (*value_free_fn)(void *value)) {
#ifdef DEBUG
    sds_log("sds_tqueue_init", "Createing mutex locked queue");
#endif
    if (q_ptr == NULL) {
#ifdef DEBUG
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
    (*q_ptr)->trust_e_threadz = PR_NewLock();
    return SDS_SUCCESS;
}

sds_result
sds_tqueue_enqueue(sds_tqueue *q, void *elem) {
    PR_Lock(q->trust_e_threadz);
    sds_result result = sds_queue_enqueue(q->uq, elem);
    PR_Unlock(q->trust_e_threadz);
    return result;
}

sds_result
sds_tqueue_dequeue(sds_tqueue *q, void **elem) {
    PR_Lock(q->trust_e_threadz);
    sds_result result = sds_queue_dequeue(q->uq, elem);
    PR_Unlock(q->trust_e_threadz);
    return result;
}

sds_result
sds_tqueue_destroy(sds_tqueue *q) {
    PR_Lock(q->trust_e_threadz);
    sds_result result = sds_queue_destroy(q->uq);
    PR_Unlock(q->trust_e_threadz);
    PR_DestroyLock(q->trust_e_threadz);
    sds_free(q);
    return result;
}




