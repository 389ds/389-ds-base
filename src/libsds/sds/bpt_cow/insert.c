/** BEGIN COPYRIGHT BLOCK
 * Copyright (c) 2016, William Brown <william at blackhats dot net dot au>
 * All rights reserved.
 *
 * License: License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include "bpt_cow.h"

void
sds_bptree_cow_root_insert(sds_bptree_transaction *btxn, sds_bptree_node *left_node, sds_bptree_node *right_node, void *key)
{
#ifdef DEBUG
    sds_log("sds_bptree_cow_root_insert", "left_node %p, key %d, right_node %p", left_node, key, right_node);
#endif
    // Just make the new root, add the nodes, and update the root in the txn.
    sds_bptree_node *root_node = sds_bptree_cow_node_create(btxn);
    root_node->level = left_node->level + 1;
#ifdef DEBUG
    sds_log("sds_bptree_cow_root_insert", "New root level %d", root_node->level);
#endif
    root_node->keys[0] = key;
    root_node->values[0] = (void *)left_node;
    root_node->values[1] = (void *)right_node;
    root_node->item_count = 1;
    // Isn't this aready done in create?
    /* root_node->parent = NULL;
    left_node->parent = root_node;
    right_node->parent = root_node; */
    btxn->root = root_node;

#ifdef DEBUG
    if (btxn->bi->offline_checksumming) {
        sds_bptree_crc32c_update_node(root_node);
        sds_bptree_crc32c_update_node(left_node);
        sds_bptree_crc32c_update_node(right_node);
        sds_bptree_crc32c_update_instance(btxn->bi);
        sds_bptree_crc32c_update_btxn(btxn);
    }
#endif
}

void
sds_bptree_cow_leaf_node_insert(sds_bptree_transaction *btxn, sds_bptree_node *lnode, sds_bptree_node *rnode, void *key)
{

    sds_bptree_node *left_node = lnode;
    sds_bptree_node *right_node = NULL;
    sds_bptree_node *next_right_node = rnode;
    sds_bptree_node *parent_node = lnode->parent;
    void *next_key = key;

#ifdef DEBUG
    sds_log("sds_bptree_cow_leaf_node_insert", "parent_node %p, left_node %p, key %d, right_node %p", parent_node, left_node, key, next_right_node);
#endif

    /*
     * How does this magic work? Well, because we did a split and insert, we now
     * have a new_node, but it's not in it's parent yet! IE
     *            [ left_node->parent ]
     *             /
     *      [ left_node ] -> [ next_right_node ]
     * Were target node the root,  we would just create the new root, stick the
     * values in and be done. But we need to get this into parent.
     * So by leaving new_node in that spot, the while loop triggers and we
     * try again with the new_node->keys[0] as the key, and new_node as
     * as the insertion target.
     *
     * If we don't have the space, we split and insert as:
     *            [ left_node->parent ]  [ right_node ]
     *             /                        \
     *      [ left_node ] -> [ next_right_node ]
     * Then right_node is new, so we shift it to the next_right_node, and left parent
     * to the left as:
     *                  [ left_node->parent ]
     *                     /
     *            [ left_node ]  [ next_right_node ] <<-- To be inserted.
     *             /                   \
     *      [ left_node ] .......  [ next_right_node ] // This layer is done ....
     */

    while (parent_node != NULL && next_right_node != NULL && next_key != NULL) {
        left_node = parent_node;
        parent_node = left_node->parent;
        if (left_node->item_count < SDS_BPTREE_DEFAULT_CAPACITY) {
            // We have space in the parent. Too easy!!
            sds_bptree_branch_insert(btxn->bi, left_node, next_key, next_right_node);
            next_right_node = NULL;
            next_key = NULL;
        } else {
            // Do we create this in the next function?
            right_node = sds_bptree_cow_node_create(btxn);
            right_node->level = left_node->level;
            right_node->parent = left_node->parent;
            void *copy_key = next_key;
            sds_bptree_branch_split_and_insert(btxn->bi, left_node, right_node, copy_key, next_right_node, &next_key);
            next_right_node = right_node;
        }
    }

    if (next_right_node != NULL) {
        sds_bptree_cow_root_insert(btxn, left_node, next_right_node, next_key);
    }
}

void
sds_bptree_cow_leaf_split_and_insert(sds_bptree_transaction *btxn, sds_bptree_node *left_node, void *key, void *value)
{
#ifdef DEBUG
    sds_log("sds_bptree_cow_leaf_split_and_insert", "left_node %p, key %d, right_node TBA", left_node, key);
#endif
    void *next_key = NULL;
    // Make the new node for this txn.
    sds_bptree_node *right_node = sds_bptree_cow_node_create(btxn);
    right_node->level = left_node->level;
    right_node->parent = left_node->parent;
    // Split the contents up.

    for (size_t i = 0; i < (SDS_BPTREE_HALF_CAPACITY - 1); i++) {
        right_node->keys[i] = left_node->keys[i + SDS_BPTREE_HALF_CAPACITY];
        right_node->values[i] = left_node->values[i + SDS_BPTREE_HALF_CAPACITY];
        right_node->item_count += 1;
        // Now clear the values out.
        left_node->keys[i + SDS_BPTREE_HALF_CAPACITY] = NULL;
        left_node->values[i + SDS_BPTREE_HALF_CAPACITY] = NULL;
        left_node->item_count -= 1;
    }

    if (btxn->bi->key_cmp_fn(key, right_node->keys[0]) >= 1) {
#ifdef DEBUG
        if (btxn->bi->offline_checksumming) {
            sds_bptree_crc32c_update_node(left_node);
        }
#endif
        sds_bptree_leaf_insert(btxn->bi, right_node, key, value);
    } else {
#ifdef DEBUG
        if (btxn->bi->offline_checksumming) {
            sds_bptree_crc32c_update_node(right_node);
        }
#endif
        sds_bptree_leaf_insert(btxn->bi, left_node, key, value);
    }

    // Now we need to push the new right_node to our parents. Remember,
    // left_node is already clone preped, so we can just walk up the parents.

    // Dup the right_node key[0] for next key.
    next_key = btxn->bi->key_dup_fn(right_node->keys[0]);

    sds_bptree_cow_leaf_node_insert(btxn, left_node, right_node, next_key);
}


