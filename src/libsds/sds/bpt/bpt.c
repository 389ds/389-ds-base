/** BEGIN COPYRIGHT BLOCK
 * Copyright (c) 2016, William Brown <william at blackhats dot net dot au>
 * Copyright (c) 2017, Red Hat, Inc
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/


#include "bpt.h"

sds_result
sds_bptree_init(sds_bptree_instance **binst_ptr, uint16_t checksumming, int64_t (*key_cmp_fn)(void *a, void *b), void (*value_free_fn)(void *value), void (*key_free_fn)(void *key), void *(*key_dup_fn)(void *key) ) {
    if (binst_ptr == NULL) {
#ifdef DEBUG
        sds_log("sds_btree_init", "Invalid pointer");
#endif
        return SDS_NULL_POINTER;
    }

    *binst_ptr = sds_memalign(sizeof(sds_bptree_instance), SDS_CACHE_ALIGNMENT);
    (*binst_ptr)->print_iter = 0;
    (*binst_ptr)->offline_checksumming = checksumming;
    (*binst_ptr)->search_checksumming = checksumming;
    (*binst_ptr)->key_cmp_fn = key_cmp_fn;
    (*binst_ptr)->value_free_fn = value_free_fn;
    (*binst_ptr)->key_free_fn = key_free_fn;
    (*binst_ptr)->key_dup_fn = key_dup_fn;

    (*binst_ptr)->root = sds_bptree_node_create();

    // Now update the checksums
#ifdef DEBUG
    if ((*binst_ptr)->offline_checksumming) {
        sds_bptree_crc32c_update_node((*binst_ptr)->root);
        sds_bptree_crc32c_update_instance(*binst_ptr);
    }
#endif

    return SDS_SUCCESS;
}

sds_result
sds_bptree_load(sds_bptree_instance *binst, void **keys, void **values, size_t count) {
    sds_result result = sds_bptree_map_nodes(binst, binst->root, sds_bptree_node_destroy);
    if (result != SDS_SUCCESS) {
        return result;
    }

    /* Convert the sorted arrays to a list of nodes */
    sds_bptree_node *left_node = sds_bptree_arrays_to_node_list(keys, values, count);
    /* Now build the tree! */
    return sds_bptree_node_list_to_tree(binst, left_node);
}

/* Searching functions */

sds_result
sds_bptree_search(sds_bptree_instance *binst, void *key) {
    return sds_bptree_search_internal(binst, binst->root, key);
}

sds_result
sds_bptree_retrieve(sds_bptree_instance *binst, void *key, void **target) {
    return sds_bptree_retrieve_internal(binst, binst->root, key, target);
}


/* Insertion functions */

sds_result
sds_bptree_insert(sds_bptree_instance *binst, void *key, void *value) {

    sds_result result = SDS_SUCCESS;
    sds_bptree_node *target_node = NULL;
    sds_bptree_node *next_node = NULL;
    void *next_key = key;

#ifdef DEBUG
    sds_log("sds_bptree_insert", "==> Beginning insert of %d", key);
#endif

    /* Check key is valid */
    if (key == NULL) {
        return SDS_INVALID_KEY;
    }

    /* Get the target node, and path to it */
    result = sds_bptree_search_node(binst, binst->root, key, &target_node);
    if (result != SDS_SUCCESS) {
        return result;
    }

    /* CHECK FOR DUPLICATE KEY HERE. */
    if (sds_bptree_node_contains_key(binst, target_node, key) == SDS_KEY_PRESENT) {
        return SDS_DUPLICATE_KEY;
    }

    /* At this point, the insert will happen. Prepare the key ... */
    void *insert_key = binst->key_dup_fn(key);

    /* Insert to the leaf. */
    if (target_node->item_count < SDS_BPTREE_DEFAULT_CAPACITY) {
        sds_bptree_leaf_insert(binst, target_node, insert_key, value);
    } else {
        /* We have to split the leaf now. */
        next_node = sds_bptree_node_create();
        next_node->level = target_node->level;
        next_node->parent = target_node->parent;
        sds_bptree_leaf_split_and_insert(binst, target_node, next_node, insert_key, value);
        /* This is the only duplication we need to do */
        next_key = binst->key_dup_fn(next_node->keys[0]);

        /* This now walks up the branches and inserts as needed. */

        result = sds_bptree_insert_leaf_node(binst, target_node, next_node, next_key);

    }

#ifdef DEBUG
    sds_log("sds_bptree_insert", "<== Finishing insert of %d", key);
#endif

    return result;
}

/* Deletion functions */

sds_result
sds_bptree_delete(sds_bptree_instance *binst, void *key) {
    sds_result result = SDS_SUCCESS;
    sds_bptree_node *target_node = NULL;
    sds_bptree_node *next_node = NULL;
    sds_bptree_node *deleted_node = NULL;

#ifdef DEBUG
    sds_log("sds_bptree_delete", "==> Beginning delete of %d", key);
#endif

    /* Check key is valid */
    if (key == NULL) {
        return SDS_INVALID_KEY;
    }

    /* Get the target node, and path to it */
    result = sds_bptree_search_node(binst, binst->root, key, &target_node);
    if (result != SDS_SUCCESS) {
        /* If an error occured, the search_node cleans up the path. */
        return result;
    }

    /* CHECK FOR DUPLICATE KEY HERE. */
    result = sds_bptree_node_contains_key(binst, target_node, key);
    if (result != SDS_KEY_PRESENT) {
        return result;
    }

    /* First, we handle the leaf. */
    /* Given this algo, first, delete the required key from the target_node. */
    sds_bptree_leaf_delete(binst, target_node, key);
    result = SDS_KEY_PRESENT;


    /*
     * If we are deleteing from a leaf, and the leaf has still got >= HALF - 1
     * (ie if we have 7 keys, and half is 4, we need 3or more keys to be valid)
     * then we are complete.
     *
     * If we will fall below that amount, and a sibling has >= HALF, we move
     * one of their elements to us.
     *
     * If the sibling has HALF - 1, then we MERGE with them
     *
     * If we are the root, and the sum our our children keys is less than
     * capacity, we remove the root, and compress the children to one root?
     *
     */

    /*
     * Delete is insane, so there are some values running about here. 
     *
     * key - The key we have been asked to delete.
     * next_key and next_node - If we have compacted, the key and node to remove..
                                only applies to the parent once.
     */

    /* Then, check the properties of the node are upheld. If not, work out
     * what needs doing.
     */
    next_node = target_node;
    target_node = target_node->parent;

    /* Is the node less than half? */
    if (target_node != NULL && next_node->item_count < SDS_BPTREE_HALF_CAPACITY) {
        /* Get our siblings. */
        sds_bptree_node *left = NULL;
        sds_bptree_node *right = NULL;
        sds_bptree_node_siblings(next_node, &left, &right);
#ifdef DEBUG
        sds_log("sds_bptree_delete", " %p -> %p -> %p", left, next_node, right);
        sds_log("sds_bptree_delete", " next_node->item_count = %d", next_node->item_count);
        if (right != NULL) {
            sds_log("sds_bptree_delete", " right->item_count = %d", right->item_count);
        }
        if (left != NULL) {
            sds_log("sds_bptree_delete", " left->item_count = %d", left->item_count);
        }
#endif
        if (right != NULL && right->item_count > SDS_BPTREE_HALF_CAPACITY) {
            /* Does right have excess keys? */
#ifdef DEBUG
            sds_log("sds_bptree_delete", "Right leaf borrow");
#endif
            sds_bptree_leaf_right_borrow(binst, next_node, right);
            /* We setup next_node to be right now, so that key fixing on the path works */
            next_node = right;
        } else if (left != NULL && left->item_count > SDS_BPTREE_HALF_CAPACITY) {
            /* Does left have excess keys? */
#ifdef DEBUG
            sds_log("sds_bptree_delete", "Left leaf borrow");
#endif
            sds_bptree_leaf_left_borrow(binst, left, next_node);
        } else if (right != NULL && right->item_count <= SDS_BPTREE_HALF_CAPACITY) {
            /* Does right want to merge? */
#ifdef DEBUG
            sds_log("sds_bptree_delete", "Right leaf contract");
#endif
            sds_bptree_leaf_compact(binst, next_node, right);
            /* Setup the branch delete properly */
            deleted_node = right;
        } else if (left != NULL && left->item_count <= SDS_BPTREE_HALF_CAPACITY) {
            /* Does left want to merge? */
#ifdef DEBUG
            sds_log("sds_bptree_delete", "Left leaf contract");
#endif
            sds_bptree_leaf_compact(binst, left, next_node);
            deleted_node = next_node;
        } else {
            /* Mate, if you get here you are fucked. */
            return SDS_INVALID_NODE;
        }


        /************** Now start on the branches ****************/

        while (target_node != NULL) {

            /*
             * We need to ensure that the key we are deleting is NOT in any other node on
             * the path. If it is, we must replace it, by the rules of the B+Tree.
             */

            /*
             * Dear william of the future.
             * The reason you do not need to perform a key fixup on leaf or branch delete
             * is because you only do a leaf compact to the left: So if you have:
             *       [  A  ]
             *       [ 1 2 ]
             *        /   \
             *     [ B ]  [ C ]
             * No MATTER what you delete from, B or C, B, will ALWAYS survive, and C will
             * be deleted. This means you DON'T need a keyfix in A -> B, because everything
             * merged in MUST be greater. It solves the problem by never creating it.
             *
             * PS: Get Char more snacks <3
             */

            if (deleted_node != NULL) {
                /* Make sure we delete this value from our branch */
#ifdef DEBUG
                sds_log("sds_bptree_delete", "Should be removing %p from branch %p here!", deleted_node, target_node);
#endif
                sds_bptree_branch_delete(binst, target_node, deleted_node);
                /* The deed is done, remove the next reference */
                deleted_node = NULL;
            } else {
                /* It means a borrow was probably done somewhere, so we need to fix the path */
#ifdef DEBUG
                sds_log("sds_bptree_delete", "Should be fixing %p key to child %p here!", target_node, next_node);
#endif
                sds_bptree_branch_key_fixup(binst, target_node, next_node);
            }
            /* Then, check the properties of the node are upheld. If not, work out
             * what needs doing.
             */
            next_node = target_node;
            target_node = target_node->parent;

    /* IMPROVE THIS!!! This could allow a key to bounce back and forth.
     * Instead, check if node < half and node->item_count + right/left->item_count < MAX
     */

            /* Is the node less than half? */
            if (target_node != NULL && next_node->item_count < SDS_BPTREE_HALF_CAPACITY) {
                /* Get our siblings. */
                sds_bptree_node *left = NULL;
                sds_bptree_node *right = NULL;
                sds_bptree_node_siblings(next_node, &left, &right);
                /* Note the conditions for HALF_CAPACITY change here due to 
                 * space requirements. we only merge if < half. IE for 7 keys
                 * we have 8 links. If we have a node at 3 keys, and one at 4 keys
                 * they have 4 and 5 links each. Too many! So we have to delay
                 * the merge by a fraction, to allow space for 3 keys and 3 keys.
                 *
                 */
#ifdef DEBUG
                sds_log("sds_bptree_delete", " %p -> %p -> %p", left, next_node, right);
                sds_log("sds_bptree_delete", " next_node->item_count = %d", next_node->item_count);
                if (right != NULL) {
                    sds_log("sds_bptree_delete", " right->item_count = %d", right->item_count);
                }
                if (left != NULL) {
                    sds_log("sds_bptree_delete", " left->item_count = %d", left->item_count);
                }
#endif

                if (right != NULL && right->item_count >= SDS_BPTREE_HALF_CAPACITY) {
                    /* Does right have excess keys? */
#ifdef DEBUG
                    sds_log("sds_bptree_delete", "Right branch borrow");
#endif
                    sds_bptree_branch_right_borrow(binst, next_node, right);
                    /* We setup next_node to be right now, so that key fixing on the path works */
                    next_node = right;
                } else if (left != NULL && left->item_count >= SDS_BPTREE_HALF_CAPACITY) {
                    /* Does left have excess keys? */
#ifdef DEBUG
                    sds_log("sds_bptree_delete", "Left branch borrow");
#endif
                    sds_bptree_branch_left_borrow(binst, left, next_node);
                } else if (right != NULL && right->item_count < SDS_BPTREE_HALF_CAPACITY) {
                    /* Does right want to merge? */
#ifdef DEBUG
                    sds_log("sds_bptree_delete", "Right branch contract");
#endif
                    sds_bptree_branch_compact(binst, next_node, right);
                    /* Setup the branch delete properly */
                    next_node = right;
                    deleted_node = right;
                } else if (left != NULL && left->item_count < SDS_BPTREE_HALF_CAPACITY) {
                    /* Does left want to merge? */
#ifdef DEBUG
                    sds_log("sds_bptree_delete", "Left branch contract");
#endif
                    sds_bptree_branch_compact(binst, left, next_node);
                    deleted_node = next_node;
                } else {
                    /* Mate, if you get here you are fucked. */
                    return SDS_INVALID_NODE;
                }
            } else if (target_node == NULL && next_node->item_count == 0) {
                /* It's time to compact the root! */
                /* We only have one child at this point, so they become the new root */
#ifdef DEBUG
                sds_log("sds_bptree_delete", "Should be deleting root here!");
                if (binst->root != next_node) {
                    result = SDS_UNKNOWN_ERROR;
                    goto fail;
                }
#endif
                sds_bptree_root_promote(binst, next_node);
            }

        } // While target node
    } // If under half capacity

#ifdef DEBUG
fail:
    sds_log("sds_bptree_insert", "<== Finishing delete of %d", key);
#endif

    return result;
}


sds_result
sds_bptree_destroy(sds_bptree_instance *binst) {
    // Remove all the other elements
    sds_result result = SDS_SUCCESS;
    result = sds_bptree_map_nodes(binst, binst->root, sds_bptree_node_destroy);
    // Finally remove the binst
    sds_free(binst);
    return result;
}


