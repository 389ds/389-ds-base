/** BEGIN COPYRIGHT BLOCK
 * Copyright (c) 2016, William Brown <william at blackhats dot net dot au>
 * All rights reserved.
 *
 * License: License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include "bpt_cow.h"

void
sds_bptree_cow_leaf_compact(sds_bptree_transaction *btxn, sds_bptree_node *left, sds_bptree_node *right) {
    /* Append content of right to left, mark right for free. */

    /*
     * In cow this is a bit different.
     * 1) We take an un-copied right node and clone the values to left.
     * 2) We put right onto the owned list.
     * 3) The cow_delete fn, puts right as the node to delete so it's cleared from parent.
     *
     * WARNING: DO NOT FREE RIGHT NODE!!!! It's still alive in a past txn!!!
     */

    for (size_t i = 0; i < right->item_count; i++) {
        left->keys[left->item_count] = btxn->bi->key_dup_fn(right->keys[i]);
        if (right->values[i] != NULL && btxn->bi->value_dup_fn != NULL) {
            left->values[left->item_count] = btxn->bi->value_dup_fn(right->values[i]);
        } else {
            left->values[left->item_count] = NULL;
        }
        left->item_count++;
    }
    /*
     * THIS IS THE KEY CHANGE!!!
     * We can't delete right as it's refed in the past txn, but we need it to be
     * cleaned after this txn is commit and all old ros are free. So mark it as owned
     * and it should "just work". Also means that abort works correctly.
     */
    sds_bptree_node_list_push(&(btxn->owned), right);

#ifdef DEBUG
    if (btxn->bi->offline_checksumming) {
        sds_bptree_crc32c_update_node(left);
        // Update this becuase we are updating the owned node lists.
        sds_bptree_crc32c_update_btxn(btxn);
    }
#endif
}

void
sds_bptree_cow_branch_compact(sds_bptree_transaction *btxn, sds_bptree_node *left, sds_bptree_node *right) {
    /* REMEMBER: Left is COWed, right is NOT */
    /* Append content of right to left, mark right for deletion */

    /* We have to recreate the missing intermediate key. */
    left->keys[left->item_count] = btxn->bi->key_dup_fn(sds_bptree_node_leftmost_child_key(right));
    /* Node reference, so can just copy */
    left->values[left->item_count + 1] = right->values[0];
    left->item_count++;


    /* Has about a 3 times impact */
    for (size_t i = 0; i < right->item_count; i++) {
        left->keys[left->item_count] = btxn->bi->key_dup_fn(right->keys[i]);
        /* These are all node pointers, so we can just copy them as is. */
        left->values[left->item_count + 1] = right->values[i + 1];
        left->item_count++;
    }

    sds_bptree_node_list_push(&(btxn->owned), right);

#ifdef DEBUG
    if (btxn->bi->offline_checksumming) {
        sds_bptree_crc32c_update_node(left);
        // Update this becuase we are updating the owned node lists.
        sds_bptree_crc32c_update_btxn(btxn);
    }
#endif
}


void
sds_bptree_cow_root_promote(sds_bptree_transaction *btxn, sds_bptree_node *root) {
    /* Current root is empty! We have one child, so we promote them. */
    btxn->root = (sds_bptree_node *)root->values[0];
    sds_bptree_node_list_push(&(btxn->owned), root);
    btxn->root->parent = NULL;
#ifdef DEBUG
    if (btxn->bi->offline_checksumming) {
        sds_bptree_crc32c_update_node(btxn->root);
        sds_bptree_crc32c_update_btxn(btxn);
    }
#endif
}
