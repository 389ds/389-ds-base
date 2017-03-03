/** BEGIN COPYRIGHT BLOCK
 * Copyright (c) 2016, William Brown <william at blackhats dot net dot au>
 * Copyright (c) 2017, Red Hat, Inc
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include "bpt_cow.h"

/* Node checksumming functions. */
#ifdef DEBUG
sds_result
sds_bptree_crc32c_verify_btxn(sds_bptree_transaction *btxn) {
    uint32_t checksum = sds_crc32c(0, (const unsigned char *)btxn + sizeof(uint32_t), sizeof(sds_bptree_transaction) - sizeof(uint32_t));
    // Iterate over the node list, and add the checksums.
    sds_bptree_node_list *cur_list = btxn->owned;
    while (cur_list != NULL) {
        checksum = sds_crc32c(checksum, (const unsigned char *)cur_list + sizeof(uint32_t), sizeof(sds_bptree_node_list) - sizeof(uint32_t));
        cur_list = cur_list->next;
    }

    if (btxn->checksum == checksum) {
        return SDS_SUCCESS;
    } else {
        return SDS_CHECKSUM_FAILURE;
    }
}

void
sds_bptree_crc32c_update_btxn(sds_bptree_transaction *btxn) {

    uint32_t checksum = sds_crc32c(0, (const unsigned char *)btxn + sizeof(uint32_t), sizeof(sds_bptree_transaction) - sizeof(uint32_t));
    // Iterate over the node list, and add the checksums.
    sds_bptree_node_list *cur_list = btxn->owned;
    while (cur_list != NULL) {
        checksum = sds_crc32c(checksum, (const unsigned char *)cur_list + sizeof(uint32_t), sizeof(sds_bptree_node_list) - sizeof(uint32_t));
        cur_list = cur_list->next;
    }
    // We can only do this because we know the internal structure of this list.

    btxn->checksum = checksum;
}


sds_result
sds_bptree_crc32c_verify_cow_instance(sds_bptree_cow_instance *binst) {
    // This starts the check *after* the checksum in the struct
    if (sds_crc32c(0, (const unsigned char *)binst + sizeof(uint32_t), sizeof(sds_bptree_cow_instance) - sizeof(uint32_t)) == binst->checksum) {
        return SDS_SUCCESS;
    } else {
        return SDS_CHECKSUM_FAILURE;
    }
}

void
sds_bptree_crc32c_update_cow_instance(sds_bptree_cow_instance *binst) {
    // This starts the check *after* the checksum in the struct
    binst->checksum = sds_crc32c(0, (const unsigned char *)binst + sizeof(uint32_t), sizeof(sds_bptree_cow_instance) - sizeof(uint32_t));
}
#endif /* DEBUG */

sds_result
sds_bptree_cow_verify_node(sds_bptree_instance *binst, sds_bptree_node *node) {
    // - verify the hash of the node metadata
#ifdef DEBUG
    if (binst->offline_checksumming) {
        sds_result result = sds_bptree_crc32c_verify_node(node);
        if (result != SDS_SUCCESS) {
            sds_log("sds_bptree_cow_verify_node", "node_%p failed checksum validation", node);
            return result;
        }
    }
#endif

    // - item_count matches number of non-null keys.
    for (size_t i = 0; i < node->item_count; i++) {
        if (node->keys[i] == NULL)
        {
            sds_log("sds_bptree_cow_verify_node", "%d \n", node->item_count);
            return SDS_INVALID_KEY;
        }

    }


    // Check that our keys are in order in the node.
    // We only need to compare left to right, because if we have:
    // A B C D
    // If A is < B, C, D, then when we check B, we only need assert B < C, D, as A < B is already true
    for (size_t i = 0; i < node->item_count; i++) {
        for (size_t j = 0; j < i && i != j; j++) {
            // printf("cmp: ! node->keys[%d] %d > node->keys[%d] %d\n", j, node->keys[j], i, node->keys[i]);
            int64_t result = binst->key_cmp_fn(node->keys[i], node->keys[j]);
            if (result == 0) {
                return SDS_DUPLICATE_KEY;
            } else if (result < 0) {
                // If the key on the right is less than the key on the left
                return SDS_INVALID_KEY;
            }
        }
    }

    if (node->level == 0) {

        /* Verify the values in the node if we are a leaf. */
        /* Leaves must always adhere to the properties of the tree. IE half size */
        // WE CAN'T do this check, because we can't assert if we are the root or not ...
        /*
        if (node->level > 0 && node->item_count < (SDS_BPTREE_HALF_CAPACITY - 1)) {
            return SDS_INVALID_NODE;
        }
        */


        // Check that our highest element is smaller than binst->branch_factor[0]
        // Because of the above assertion, we now know that our node is in order
        // And given the next is also, we can assert ALL nodes are in key order.
        /* This doesn't work because we don't maintain node links. */
            // Is there a way to assert ordering?
        /*
        sds_bptree_node *rnode = (sds_bptree_node *)node->values[SDS_BPTREE_DEFAULT_CAPACITY];

        if (rnode != NULL) {
            int64_t result = binst->key_cmp_fn(node->keys[node->item_count - 1], rnode->keys[0]);
            if (result > 0) {
                return SDS_INVALID_KEY;
            } else if (result == 0) {
                return SDS_DUPLICATE_KEY;
            }
        }
        */

        // Is there an effective way to do a duplicate key check?
        // I think the above checks already assert there are no dupes in the tree.

    } else {
        // As a branch, to be valid we must have at LEAST 1 item and 2 links
        // The later check will validate the links for us :)
        if (node->item_count == 0) {
            return SDS_INVALID_NODE;
        }

        // Check that all our childrens parent refs are to us.
        // We can't do backref checks, because of the way that COW works
        /*
        for (int i = 0; i <= node->item_count; i++) {
            sds_bptree_node *cnode = (sds_bptree_node *)node->values[i];
            if (cnode->parent != node) {
                return SDS_INVALID_POINTER;
            }
        }

        if (binst->root == node) {
            if (node->parent != NULL) {
                return SDS_INVALID_POINTER;
            }
        }
        */

        // Check that for key at index i, link i key[0] is < key.
        // For key at index i, link i + 1, is > key
        // For one key, there will always be a left and right link. That's the way it is :)
        // So we can fail if one is NULL
        for (uint16_t i = 0; i < node->item_count; i++) {
            if (node->keys[i] != NULL) {
                sds_bptree_node *lnode = (sds_bptree_node *)node->values[i];
                sds_bptree_node *rnode = (sds_bptree_node *)node->values[i + 1];

                if (lnode == NULL || rnode == NULL) {
                    // We should have two children!
                    return SDS_INVALID_POINTER;
                }

                // Check that all left keys are LESS.
                sds_bptree_node_list *path = NULL;
                size_t j = 0;

                while (lnode != NULL) {
                    for (j = 0; j < lnode->item_count; j++) {
                        /* This checks that all left keys *and* their children are less */
                        int64_t result = binst->key_cmp_fn(lnode->keys[j], node->keys[i]);
                        if (result >= 0) {
#ifdef DEBUG
                            sds_log("sds_bptree_verify_node", "    fault is in node %p with left child %p", node, lnode);
#endif
                            return SDS_INVALID_KEY_ORDER;
                        }
                        if (lnode->level > 0) {
                            sds_bptree_node_list_push(&path, lnode->values[j]);
                        }
                    }
                    if (lnode->level > 0) {
                        sds_bptree_node_list_push(&path, lnode->values[j]);
                    }
                    lnode = sds_bptree_node_list_pop(&path);
                }
                // All right keys are greater or equal
                while (rnode != NULL) {
                    for (j = 0; j < rnode->item_count; j++) {
                        /* This checks that all right keys are greatr or equal and their children */
                        int64_t result = binst->key_cmp_fn(rnode->keys[j], node->keys[i]);
                        if (result < 0) {
#ifdef DEBUG
                            sds_log("sds_bptree_verify_node", "    fault is in node %p with right child %p", node, rnode);
#endif
                            return SDS_INVALID_KEY_ORDER;
                        }
                        if (rnode->level > 0) {
                            sds_bptree_node_list_push(&path, rnode->values[j]);
                        }
                    }
                    if (rnode->level > 0) {
                        sds_bptree_node_list_push(&path, rnode->values[j]);
                    }
                    rnode = sds_bptree_node_list_pop(&path);
                } // While rnode
            }
        } // end for

    } // end else is_leaf

    return SDS_SUCCESS;
}

