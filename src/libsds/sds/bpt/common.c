/** BEGIN COPYRIGHT BLOCK
 * Copyright (c) 2016, William Brown <william at blackhats dot net dot au>
 * Copyright (c) 2017, Red Hat, Inc
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include "bpt.h"

/* Node manipulation functions */
sds_bptree_node *
sds_bptree_node_create() {
    sds_bptree_node *node = sds_memalign(sizeof(sds_bptree_node), SDS_CACHE_ALIGNMENT);
    // Without memset, we need to null the max link in a value
    node->values[SDS_BPTREE_DEFAULT_CAPACITY] = NULL;
    node->level = 0;
    node->item_count = 0;
    node->parent = NULL;
    node->txn_id = 0;

#ifdef DEBUG
    sds_log("sds_bptree_node_create", "Creating node_%p item_count=%d\n", node, node->item_count);
#endif

    return node;
}


sds_result
sds_bptree_node_destroy(sds_bptree_instance *binst, sds_bptree_node *node) {
#ifdef DEBUG
    sds_log("sds_bptree_node_destroy", "Freeing node_%p", node);
#endif
    for (size_t i = 0; i < node->item_count; i += 1) {
        if (node->level == 0) {
            void *value =  node->values[i];
            if (value != NULL && binst->value_free_fn != NULL) {
                binst->value_free_fn(value);
            }
        }
        binst->key_free_fn(node->keys[i]);
    }

    sds_free(node);
    // Since we updated the node id.
#ifdef DEBUG
    // binst->node_count -= 1;
    if (binst->offline_checksumming) {
        sds_bptree_crc32c_update_instance(binst);
    }
#endif
    return SDS_SUCCESS;
}

size_t
sds_bptree_node_key_lt_index(sds_bptree_instance *binst, sds_bptree_node *node, void *key) {
    /* This is a very busy part of the code. */
    size_t index = 0;
    for (;index < node->item_count; index++) {
        if (binst->key_cmp_fn(key, node->keys[index]) < 0) {
            return index;
        }
    }
    return index;
}

size_t
sds_bptree_node_key_eq_index(sds_bptree_instance *binst, sds_bptree_node *node, void *key) {
    size_t index = 0;
    int64_t result = 0;
    for (;index < node->item_count; index++) {
        result = binst->key_cmp_fn(key, node->keys[index]);
        if (result == 0) {
            return index;
        } else if (result < 0) {
            /* Short cut out if we don't exist */
            return node->item_count;
        }
    }
    return index;
}

inline static size_t __attribute__((always_inline))
sds_bptree_node_node_index(sds_bptree_node *parent, sds_bptree_node *child) {
    size_t index = 0;
    /* GCC has an issue where uint16_t + something becomes signed ... fml */
    for (; index < (size_t)(parent->item_count + 1); index++) {
        if (parent->values[index] == child) {
            return index;
        }
    }
    return index;
}

// How can we make this handle errors safely?
void
sds_bptree_node_node_replace(sds_bptree_node *target_node, sds_bptree_node *origin_node, sds_bptree_node *replace_node) {
#ifdef DEBUG
    sds_log("sds_bptree_node_node_replace", "Replace node_%p to overwrite node_%p in node_%p\n", origin_node, replace_node, target_node);
#endif
    size_t index = sds_bptree_node_node_index(target_node, origin_node);
    target_node->values[index] = replace_node;
    replace_node->parent = target_node;
}

void *
sds_bptree_node_leftmost_child_key(sds_bptree_node *parent) {
    sds_bptree_node *right_prime = parent;
    while (right_prime->level > 0) {
        right_prime = (sds_bptree_node *)right_prime->values[0];
    }
    return right_prime->keys[0];
}

sds_result
sds_bptree_node_contains_key(sds_bptree_instance *binst, sds_bptree_node *node, void *key) {
    /* Very busy part of the code. Could be improved? */
    if (sds_bptree_node_key_eq_index(binst, node, key) != node->item_count) {
            return SDS_KEY_PRESENT;
    }
    return SDS_KEY_NOT_PRESENT;
}

sds_result
sds_bptree_node_retrieve_key(sds_bptree_instance *binst, sds_bptree_node *node, void *key, void **target) {
    size_t index = sds_bptree_node_key_eq_index(binst, node, key);

    if (index == node->item_count) {
        return SDS_KEY_NOT_PRESENT;
    }
    *target = node->values[index];
    return SDS_KEY_PRESENT;
}

sds_bptree_node *
sds_bptree_node_min(sds_bptree_instance *binst) {
    sds_bptree_node *work_node = binst->root;
    while (work_node->level > 0) {
        work_node = (sds_bptree_node *)work_node->values[0];
    }
    return work_node;
}

void
sds_bptree_node_list_append(sds_bptree_node **node, void *key, void *value) {
    if ((*node)->item_count >= SDS_BPTREE_DEFAULT_CAPACITY) {
        sds_bptree_node *new_work_node = NULL;
        new_work_node = sds_bptree_node_create();
        (*node)->values[SDS_BPTREE_DEFAULT_CAPACITY] = (void *)new_work_node;
        (*node) = new_work_node;
    }
    (*node)->keys[(*node)->item_count] = key;
    (*node)->values[(*node)->item_count] = value;
    (*node)->item_count++;
}

sds_bptree_node *
sds_bptree_arrays_to_node_list(void **keys, void **values, size_t count) {
    /* Allocate the work node. It's first, so make it the left too. */
    sds_bptree_node *left_node = sds_bptree_node_create();
    sds_bptree_node *work_node = left_node;

    for (size_t index = 0; index < count; index++) {
        if (values != NULL) {
            sds_bptree_node_list_append(&work_node, keys[index], values[index]);
        } else {
            sds_bptree_node_list_append(&work_node, keys[index], NULL);
        }
    }
    return left_node;
}

sds_result
sds_bptree_node_list_to_tree(sds_bptree_instance *binst, sds_bptree_node *node) {
    /* First, push all our nodes to a list. This also counts them. */
    sds_bptree_node *next_node = (sds_bptree_node *)node->values[SDS_BPTREE_DEFAULT_CAPACITY];
    sds_bptree_node *target_node = node;
    void *next_key = NULL;

    binst->root = node;
#ifdef DEBUG
    if (binst->offline_checksumming) {
        sds_bptree_crc32c_update_node(node);
        sds_bptree_crc32c_update_instance(binst);
    }
#endif
    /* We actually have a list of nodes. Better start inserting! */
    while (next_node != NULL) {
        next_node->parent = target_node->parent;
        next_key = binst->key_dup_fn(next_node->keys[0]);
        sds_bptree_insert_leaf_node(binst, target_node, next_node, next_key);

        target_node = next_node;
        next_node = (sds_bptree_node *)next_node->values[SDS_BPTREE_DEFAULT_CAPACITY];
    }
    return SDS_SUCCESS;
}

void
sds_bptree_leaf_insert(sds_bptree_instance *binst, sds_bptree_node *node, void *key, void *new_value) {
    /* This is called when you know you have space already */
#ifdef DEBUG
    sds_log("sds_bptree_leaf_insert", "node_%p key %" PRIu64 " ", node, key);
#endif
    size_t index = sds_bptree_node_key_lt_index(binst, node, key);
    // Move everything else to the right
    if (node->item_count > 0) {
        for (size_t i = node->item_count; i > index; i--) {
            node->keys[i] = node->keys[i - 1];
            node->values[i] = node->values[i - 1];
        }
    }
    // Insert
    node->keys[index] = key;
    node->values[index] = new_value;
    node->item_count = node->item_count + 1;

    // Update the checksum.
#ifdef DEBUG
    if (binst->offline_checksumming) {
        sds_bptree_crc32c_update_node(node);
    }
#endif

}

sds_result
sds_bptree_insert_leaf_node(sds_bptree_instance *binst, sds_bptree_node *tnode, sds_bptree_node *nnode, void *nkey) {
    sds_bptree_node *target_node = tnode;
    sds_bptree_node *next_node = nnode;
    sds_bptree_node *right_node = NULL;
    void *next_key = nkey;
    /* Now work up the path to the branches. */
    /* The path != NULL is important, as if we are the root, we want to make a new root! */
    /*
     * How does this magic work? Well, because we did a split and insert, we now
     * have a new_node, but it's not in it's parent yet! IE
     *            [ Parent ]
     *             /
     * [ target_node ] -> [ new_node ]
     * Were target node the root,  we would just create the new root, stick the
     * values in and be done. But we need to get this into parent.
     * So by leaving new_node in that spot, the while loop triggers and we
     * try again with the new_node->keys[0] as the key, and new_node as
     * as the insertion target.
     */

    while (target_node->parent != NULL && next_node != NULL && next_key != NULL) {
        target_node = target_node->parent;
        /* Does the target have space? If so insert! */
        if (target_node->item_count < SDS_BPTREE_DEFAULT_CAPACITY) {
            // Insert the value in the correct place.
            /* It's a branch, and we are updating to say we have a new child */
            sds_bptree_branch_insert(binst, target_node, next_key, next_node);
            /* Done! Say we are. In theory, next_node is already null, but lets be explicit */
            next_node = NULL;
            next_key = NULL;
        } else {
            /* If the target doesn't have space, split, then insert */

            /*
             * So we have the current target_node, and we will split to the right
             * some new node. So we create the new node, then move half our
             * values to it.
             *
             * Now, we put our next_key and value *or* new_node into the old node
             * or the new node.
             */

            /* create new node */
            right_node = sds_bptree_node_create();
            right_node->level = target_node->level;
            right_node->parent = target_node->parent;

            /* The way we split a branch is a bit different for link management. */
            void *copy_key = next_key;
            sds_bptree_branch_split_and_insert(binst, target_node, right_node, copy_key, next_node, &next_key);

            next_node = right_node;
            /* don't alter next key here! This is set by branchnodesplit */
        }


    }
    /* Done looping, but we still have to insert the right_node we just made..
     * means we need a new root!
     */

    if (next_node != NULL) {
        /*
         * If we are the root, we need to make a new root now, and add our values.
         */
        sds_bptree_root_insert(binst, target_node, next_node, next_key);
    }
    return SDS_SUCCESS;
}

void
sds_bptree_branch_insert(sds_bptree_instance *binst, sds_bptree_node *node, void *key, sds_bptree_node *new_node) {
    /* Remember, we already checked for duplicate keys! */
#ifdef DEBUG
    sds_log("sds_bptree_branch_insert", "new_node %p key %" PRIu64 " to node %p", new_node, key, node);
#endif

    /* !!!!! STARTING TO CHANGE THE NODE !!!!!! */
    /* Find where our key belongs.
     * Now, we should always have space, but there is a different condition
     * for a branch compared to a leaf. If we have:
     * [ 2, 6, 8, NULL ]
     * If we are inserting 10, we need to put the new_node to the *RIGHT*.
     * If our key is LESS, then we insert to the LEFT.
     */
    size_t index = sds_bptree_node_key_lt_index(binst, node, key);

    // If index == node->item_count, nothing will move.
    if (node->item_count > 0) {
        for (size_t i = node->item_count; i > index; i--) {
            node->keys[i] = node->keys[i - 1];
            // Copy the value to the RIGHT of the key ....
            node->values[i + 1] = node->values[i];
        }
    }

    /* sds_bptree_node *current_min_node = (sds_bptree_node *)node->values[0];
    if (index > 0 || binst->key_cmp_fn(current_min_node->keys[0], new_node->keys[0]) < 0) {
    */
    // node->keys[index] = binst->key_dup_fn(key);
    /* This key is already duplicated. */
    node->keys[index] = key;
    node->values[index + 1] = (void *)new_node;
    node->item_count += 1;
    new_node->parent = node;

    // Update the checksum.
#ifdef DEBUG
    if (binst->offline_checksumming) {
        sds_bptree_crc32c_update_node(node);
        sds_bptree_crc32c_update_node(new_node);
    }
#endif

}

void
sds_bptree_leaf_split_and_insert(sds_bptree_instance *binst, sds_bptree_node *left_node, sds_bptree_node *right_node, void *key, void *new_value) {
    /* Remember, we already checked for duplicate keys! */
#ifdef DEBUG
    sds_log("sds_bptree_leaf_split_and_insert", "left %p -> right %p key %" PRIu64 " ", left_node, right_node, key);
#endif

    /* !!!!! STARTING TO CHANGE THE NODE !!!!!! */
    /*  Right node is always new! */
    /* Move values to it */
    /* This has a cost of average 3. */
    for (size_t i = 0; i < (SDS_BPTREE_HALF_CAPACITY - 1); i++) {
        /* Is memcpy faster? */
        right_node->keys[i] = left_node->keys[i + SDS_BPTREE_HALF_CAPACITY];
        right_node->values[i] = left_node->values[i + SDS_BPTREE_HALF_CAPACITY];
        right_node->item_count += 1;
        // Now clear the values out.
        left_node->keys[i + SDS_BPTREE_HALF_CAPACITY] = NULL;
        left_node->values[i + SDS_BPTREE_HALF_CAPACITY] = NULL;
        left_node->item_count -= 1;
    }

    /* Fix the linked list pointers */
    right_node->values[SDS_BPTREE_DEFAULT_CAPACITY] = left_node->values[SDS_BPTREE_DEFAULT_CAPACITY];
    left_node->values[SDS_BPTREE_DEFAULT_CAPACITY] = (void *)right_node;

    /* Pick the node we need to insert to */
    if (binst->key_cmp_fn(key, right_node->keys[0]) >= 1) {
        /* Insert to the right */
        // Update the checksum.
#ifdef DEBUG
        if (binst->offline_checksumming) {
            sds_bptree_crc32c_update_node(left_node);
        }
#endif
        sds_bptree_leaf_insert(binst, right_node, key, new_value);
    } else {
        /* Insert to the left */
        // Update the checksum.
#ifdef DEBUG
        if (binst->offline_checksumming) {
            sds_bptree_crc32c_update_node(right_node);
        }
#endif
        sds_bptree_leaf_insert(binst, left_node, key, new_value);
    }
}

void
sds_bptree_branch_split_and_insert(sds_bptree_instance *binst, sds_bptree_node *left_node, sds_bptree_node *right_node, void *key, sds_bptree_node *new_node, void **excluded_key) {
    /* !!!!! STARTING TO CHANGE THE NODE !!!!!! */
    /*  Right node is always new! */
    sds_bptree_node *rchild = NULL;
#ifdef DEBUG
    sds_log("sds_bptree_branch_split_and_insert", "left %p -> right %p key %" PRIu64 "", left_node, right_node, key);
#endif

    /* We are left!
     * In a left case, we move half the nodes over like a normal insert and split.
     * Then we remove the "last" node
     */
    /* Has a cost of about 4 times per call? */
    for (size_t i = 0; i < (SDS_BPTREE_HALF_CAPACITY - 1); i++) {
        right_node->keys[i] = left_node->keys[i + SDS_BPTREE_HALF_CAPACITY];
        // Move the node pointer to the right ...
        right_node->values[i + 1] = left_node->values[i + SDS_BPTREE_HALF_CAPACITY + 1];
        // Reset the parent for the right nodes.
        rchild = (sds_bptree_node *)right_node->values[i + 1];
        rchild->parent = right_node;
        right_node->item_count += 1;
#ifdef DEBUG
        if (binst->offline_checksumming) {
            sds_bptree_crc32c_update_node(rchild);
        }
#endif
        // Now clear the values out.
        left_node->keys[i + SDS_BPTREE_HALF_CAPACITY] = NULL;
        left_node->values[i + SDS_BPTREE_HALF_CAPACITY + 1] = NULL;
        left_node->item_count -= 1;
    }
    /* Finally, move the last pointer from the tail of left to right */
    right_node->values[0] = left_node->values[SDS_BPTREE_HALF_CAPACITY];
    rchild = (sds_bptree_node *)right_node->values[0];
    rchild->parent = right_node;
#ifdef DEBUG
    if (binst->offline_checksumming) {
        sds_bptree_crc32c_update_node(rchild);
    }
#endif
    left_node->values[SDS_BPTREE_HALF_CAPACITY] = NULL;
    /* And null the excluded key. */
    /* The excluded key will have been duped from insert, so we are just pushing
     * it up the tree at this point. No need to dup!
     */
    *excluded_key = left_node->keys[SDS_BPTREE_HALF_CAPACITY - 1];
#ifdef DEBUG
    sds_log("sds_bptree_branch_split_and_insert", "excluding %d", *excluded_key);
#endif
    left_node->keys[SDS_BPTREE_HALF_CAPACITY - 1] = NULL;
    left_node->item_count -= 1;

    if (binst->key_cmp_fn(key, *excluded_key) < 0) {
#ifdef DEBUG
        if (binst->offline_checksumming) {
            sds_bptree_crc32c_update_node(right_node);
        }
#endif
        /* Now trigger the insert to the left node. */
        sds_bptree_branch_insert(binst, left_node, key, new_node);
    } else {
#ifdef DEBUG
        if (binst->offline_checksumming) {
            sds_bptree_crc32c_update_node(left_node);
        }
#endif
        /* Now trigger the insert to the left node. */
        sds_bptree_branch_insert(binst, right_node, key, new_node);
    }
}

void
sds_bptree_root_insert(sds_bptree_instance *binst, sds_bptree_node *left_node, sds_bptree_node *right_node, void *key) {
#ifdef DEBUG
    sds_log("sds_bptree_root_insert", "left_node %p, key %d, right_node %p", left_node, key, right_node);
#endif
    sds_bptree_node *root_node = sds_bptree_node_create();
    /* On non debug, we only need to know if this is a leaf or not. */
    if (left_node->level == 0) {
        root_node->level = 1;
    } else {
        root_node->level = left_node->level;
    }

    /* Is already duplicated */
    root_node->keys[0] = key;
    root_node->values[0] = (void *)left_node;
    root_node->values[1] = (void *)right_node;
    root_node->item_count = 1;
    root_node->parent = NULL;
    left_node->parent = root_node;
    right_node->parent = root_node;
    binst->root = root_node;
    // Update the checksum.
#ifdef DEBUG
    if (binst->offline_checksumming) {
        sds_bptree_crc32c_update_node(root_node);
        sds_bptree_crc32c_update_node(left_node);
        sds_bptree_crc32c_update_node(right_node);
        sds_bptree_crc32c_update_instance(binst);
    }
#endif
}

void
sds_bptree_leaf_delete(sds_bptree_instance *binst, sds_bptree_node *node, void *key) {
#ifdef DEBUG
    sds_log("sds_bptree_leaf_delete", "deleting %d from %p", key, node);
#endif
    /* Find the value */
    size_t index = sds_bptree_node_key_eq_index(binst, node, key);

    /* extract the contents (if any) */
    void *value = node->values[index];
    if (value != NULL) {
        binst->value_free_fn(value);
    }
    /* Delete the key + value */
    binst->key_free_fn(node->keys[index]);
    /* Move remaining values and keys into place. */
    /* nearly a 5x cost in this call.... */
    node->item_count = node->item_count - 1;
    for (; index < (node->item_count); index++) {
        node->keys[index] = node->keys[index + 1];
        node->values[index] = node->values[index + 1];
    }
    /* Finally, NULL the left over two values. */
    node->keys[index] = NULL;
    node->values[index] = NULL;

#ifdef DEBUG
    if (binst->offline_checksumming) {
        sds_bptree_crc32c_update_node(node);
    }
#endif
}

void
sds_bptree_leaf_compact(sds_bptree_instance *binst, sds_bptree_node *left, sds_bptree_node *right) {

    /* Has a 5x cost */
    for (size_t i = 0; i < right->item_count; i++) {
        left->keys[left->item_count] = right->keys[i];
        left->values[left->item_count] = right->values[i];
        left->item_count++;
        right->keys[i] = NULL;
        right->values[i] = NULL;
    }
    left->values[SDS_BPTREE_DEFAULT_CAPACITY] = right->values[SDS_BPTREE_DEFAULT_CAPACITY];
    right->item_count = 0;
    sds_bptree_node_destroy(binst, right);

#ifdef DEBUG
    if (binst->offline_checksumming) {
        sds_bptree_crc32c_update_node(left);
    }
#endif
}

void
sds_bptree_leaf_right_borrow(sds_bptree_instance *binst, sds_bptree_node *left, sds_bptree_node *right) {
    /* Take from the right node, and put into the left. */
    left->keys[left->item_count] = right->keys[0];
    left->values[left->item_count] = right->values[0];
    left->item_count += 1;
#ifdef DEBUG
    assert(right->item_count > 0);
#endif
    for (size_t i = 0; i < (size_t)(right->item_count - 1); i++) {
        right->keys[i] = right->keys[i + 1];
        right->values[i] = right->values[i + 1];
    }
    right->item_count -= 1;
    right->keys[right->item_count] = NULL;
    right->values[right->item_count] = NULL;

#ifdef DEBUG
    if (binst->offline_checksumming) {
        sds_bptree_crc32c_update_node(left);
        sds_bptree_crc32c_update_node(right);
    }
#endif
}

void
sds_bptree_leaf_left_borrow(sds_bptree_instance *binst, sds_bptree_node *left, sds_bptree_node *right) {
    /* Take a node from the left and put it into the right. */
    for (size_t i = right->item_count; i > 0; i--) {
        right->keys[i] = right->keys[i - 1];
        right->values[i] = right->values[i - 1];
    }
    left->item_count -= 1;
    right->item_count += 1;
    right->keys[0] = left->keys[left->item_count];
    right->values[0] = left->values[left->item_count];
    left->keys[left->item_count] = NULL;
    left->values[left->item_count] = NULL;

#ifdef DEBUG
    if (binst->offline_checksumming) {
        sds_bptree_crc32c_update_node(left);
        sds_bptree_crc32c_update_node(right);
    }
#endif
}

void
sds_bptree_root_promote(sds_bptree_instance *binst, sds_bptree_node *root) {
    /* Current root is empty! We have one child, so we promote them. */
    binst->root = (sds_bptree_node *)root->values[0];
    sds_bptree_node_destroy(binst, root);
    binst->root->parent = NULL;
#ifdef DEBUG
    if (binst->offline_checksumming) {
        sds_bptree_crc32c_update_node(binst->root);
        sds_bptree_crc32c_update_instance(binst);
    }
#endif
}

void
sds_bptree_branch_delete(sds_bptree_instance *binst, sds_bptree_node *node, sds_bptree_node *delete_node) {

    /* This deletes by NODE not by KEY!!!! */
    size_t index = sds_bptree_node_node_index(node, delete_node);

    /* Most of the times, keys / values right associate. IE index key -> value + 1 */
    binst->key_free_fn(node->keys[index - 1]);

    /* Have to move everything down. */
    /* Remember, we belong to key index - 1 */
    for (; index < node->item_count; index++) {
        node->keys[index - 1] = node->keys[index];
        node->values[index] = node->values[index + 1];
    }
    /* Finally, null the last item. */
    node->item_count-- ;
    node->keys[node->item_count] = NULL;
    node->values[node->item_count + 1] = NULL;

#ifdef DEBUG
    if (binst->offline_checksumming) {
        sds_bptree_crc32c_update_node(node);
    }
#endif
}

void
sds_bptree_node_siblings(sds_bptree_node *target, sds_bptree_node **left, sds_bptree_node **right) {
    /* Searches for a NODE not a KEY */
    size_t index = sds_bptree_node_node_index(target->parent, target);

    if (index == 0) {
        *left = NULL;
        *right = target->parent->values[index + 1];
    } else if (index == target->parent->item_count) {
        *left = target->parent->values[index - 1];
        *right = NULL;
    } else {
        *left = target->parent->values[index - 1];
        *right = target->parent->values[index + 1];
    }

}

void
sds_bptree_branch_key_fixup(sds_bptree_instance *binst, sds_bptree_node *parent, sds_bptree_node *child) {
    /* Find the index of child in parent. */
    size_t index = sds_bptree_node_node_index(parent, child);
    /* We are the parent of child at some point. We need to fix our key relationship to them. */
    if (index == 0) {
        /* Left children never need key updates. */
        return;
    }
    /* Now, the index is one to the left, so sub 1. */
    /* IE [   1    2    3    4    ] */
    /*    [ A   B    C    D    E  ] */
    /* So node B, index is 1, but key is 1, so one less */
    index--;
    binst->key_free_fn(parent->keys[index]);
    parent->keys[index] = binst->key_dup_fn(sds_bptree_node_leftmost_child_key(child));

#ifdef DEBUG
    if (binst->offline_checksumming) {
        sds_bptree_crc32c_update_node(parent);
    }
#endif
}

void
sds_bptree_branch_compact(sds_bptree_instance *binst, sds_bptree_node *left, sds_bptree_node *right) {
    sds_bptree_node *rchild = NULL;
    /* Merge the right to the left. */
    /* We have to create the missing intermediate key. */
    left->keys[left->item_count] = binst->key_dup_fn(sds_bptree_node_leftmost_child_key(right));

    rchild = (sds_bptree_node *)right->values[0];
    rchild->parent = left;
#ifdef DEBUG
    if (binst->offline_checksumming) {
        sds_bptree_crc32c_update_node(rchild);
    }
#endif
    left->values[left->item_count + 1] = rchild;
    right->values[0] = NULL;
    left->item_count++;

    /* Has about a 3 times impact */
    for (size_t i = 0; i < right->item_count; i++) {
        left->keys[left->item_count] = right->keys[i];
        rchild = (sds_bptree_node *)right->values[i + 1];
        rchild->parent = left;
#ifdef DEBUG
        if (binst->offline_checksumming) {
            sds_bptree_crc32c_update_node(rchild);
        }
#endif
        left->values[left->item_count + 1] = rchild;
        left->item_count++;
        right->keys[i] = NULL;
        right->values[i + 1] = NULL;
    }

    right->item_count = 0;
    sds_bptree_node_destroy(binst, right);

#ifdef DEBUG
    if (binst->offline_checksumming) {
        sds_bptree_crc32c_update_node(left);
    }
#endif
}

void
sds_bptree_branch_right_borrow(sds_bptree_instance *binst, sds_bptree_node *left, sds_bptree_node *right) {
    sds_bptree_node *rchild = NULL;
    /* Take from the right, and give to the left. */
    left->keys[left->item_count] = binst->key_dup_fn(sds_bptree_node_leftmost_child_key(right));
    binst->key_free_fn(right->keys[0]);

    rchild = (sds_bptree_node *)right->values[0];
    rchild->parent = left;
    left->values[left->item_count + 1] = rchild;
    left->item_count += 1;
#ifdef DEBUG
    assert(right->item_count > 0);
#endif
    for (size_t i = 0; i < (size_t)(right->item_count - 1); i++) {
        right->keys[i] = right->keys[i + 1];
        right->values[i] = right->values[i + 1];
    }
    right->values[right->item_count -1] = right->values[right->item_count];

    right->item_count -= 1;
    right->keys[right->item_count] = NULL;
    right->values[right->item_count + 1] = NULL;
#ifdef DEBUG
    if (binst->offline_checksumming) {
        sds_bptree_crc32c_update_node(left);
        sds_bptree_crc32c_update_node(right);
        sds_bptree_crc32c_update_node(rchild);
    }
#endif
}

void
sds_bptree_branch_left_borrow(sds_bptree_instance *binst, sds_bptree_node *left, sds_bptree_node *right) {
    sds_bptree_node *rchild = NULL;
    /* Take from the left and give to the right. */
    for (size_t i = right->item_count; i > 0; i--) {
        right->keys[i] = right->keys[i - 1];
        right->values[i + 1] = right->values[i];
    }
    /* shuffle the last value over. */
    right->values[1] = right->values[0];

    left->item_count -= 1;
    right->item_count += 1;
    /* keys[0] will already be blank here. */
    
    right->keys[0] = binst->key_dup_fn(sds_bptree_node_leftmost_child_key((sds_bptree_node *)right->values[1]));
    right->values[0] = left->values[left->item_count + 1];

    rchild = (sds_bptree_node *)right->values[0];
    rchild->parent = right;

    /* Now free the key in the left. */
    binst->key_free_fn(left->keys[left->item_count]);
    left->keys[left->item_count] = NULL;
    left->values[left->item_count + 1] = NULL;
#ifdef DEBUG
    if (binst->offline_checksumming) {
        sds_bptree_crc32c_update_node(left);
        sds_bptree_crc32c_update_node(right);
        sds_bptree_crc32c_update_node(rchild);
    }
#endif
}


