/* BEGIN COPYRIGHT BLOCK
 * Copyright (c) 2017, Red Hat, Inc
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include "ht.h"

sds_result
sds_ht_map_nodes(sds_ht_instance *ht_ptr, sds_result (*map_fn)(sds_ht_instance *ht_ptr, sds_ht_node *node))
{
#ifdef SDS_DEBUG
    //verify instance
    if (sds_ht_crc32c_verify_instance(ht_ptr) != SDS_SUCCESS) {
        sds_log("sds_ht_map_nodes", "ht_ptr failed verification");
        return SDS_CHECKSUM_FAILURE;
    }
#endif
    // Create a queue
    sds_result result = SDS_SUCCESS;
    sds_queue *node_q = NULL;
    sds_queue_init(&node_q, NULL);
    sds_ht_node *work_node = ht_ptr->root;
    // while node is true
    while (work_node != NULL) {
#ifdef SDS_DEBUG
        if (sds_ht_crc32c_verify_node(work_node) != SDS_SUCCESS) {
            sds_log("sds_ht_map_nodes", "ht_node_%p failed verification", work_node);
            result = SDS_CHECKSUM_FAILURE;
            goto out;
        }
#endif
        // add nodes to the list
        for (size_t i = 0; i < HT_SLOTS; i++) {
            // * should this be a pointer? I think this copies ....
            sds_ht_slot *slot = &(work_node->slots[i]);
            if (slot->state == SDS_HT_BRANCH) {
                sds_queue_enqueue(node_q, slot->slot.node);
            }
        }
        // once done, apply to our node.
        sds_result internal_result = map_fn(ht_ptr, work_node);
        if (internal_result != SDS_SUCCESS) {
            result = internal_result;
#ifdef SDS_DEBUG
            sds_log("sds_ht_map_nodes", "Encountered an issue with ht_node_%p: %d\n", work_node, internal_result);
            goto out;
#endif
        }
        // And get the next node for us to work on.
        if (sds_queue_dequeue(node_q, (void **)&work_node) != SDS_SUCCESS) {
            // Queue is empty.
            work_node = NULL;
        }
    }

#ifdef SDS_DEBUG
out:
#endif
    sds_queue_destroy(node_q);
    return result;

}

