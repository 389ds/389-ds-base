/** BEGIN COPYRIGHT BLOCK
 * Copyright (c) 2016, William Brown <william at blackhats dot net dot au>
 * Copyright (c) 2017, Red Hat, Inc
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include "bpt_cow.h"

// This is almost the same as search node fast, but it returns the PATH that
// we took to get there. This is really critical for COW operations, as we can't
// use parent backrefs to determine this!

sds_result
sds_bptree_search_node_path(sds_bptree_transaction *btxn, void *key, sds_bptree_node **target_out_node)
{
    sds_bptree_node *parent_node = NULL;
    sds_bptree_node *target_node = btxn->root;
    size_t i = 0;
    int64_t (*key_cmp_fn)(void *a, void *b) = btxn->bi->key_cmp_fn;

/* We do this first, as we need the node to pass before we access it! */
#ifdef SDS_DEBUG
    if (btxn->bi->search_checksumming) {
        sds_result result = sds_bptree_crc32c_verify_btxn(btxn);
        if (result != SDS_SUCCESS) {
            return result;
        }
        result = sds_bptree_crc32c_verify_instance(btxn->bi);
        if (result != SDS_SUCCESS) {
            return result;
        }
        result = sds_bptree_crc32c_verify_node(target_node);
        if (result != SDS_SUCCESS) {
            return result;
        }
    }
#endif

branch_loop:
    while (target_node->level != 0) {
        target_node->parent = parent_node;
#ifdef SDS_DEBUG
        if (btxn->bi->search_checksumming) {
            sds_bptree_crc32c_update_node(target_node);
        }
#endif
        while (i < target_node->item_count) {
            if (key_cmp_fn(key, (target_node)->keys[i]) < 0) {
                parent_node = target_node;
                target_node = (sds_bptree_node *)target_node->values[i];
                i = 0;
                goto branch_loop;
            } else {
                i++;
            }
        }
        parent_node = target_node;
        target_node = (sds_bptree_node *)target_node->values[target_node->item_count];
        i = 0;
#ifdef SDS_DEBUG
        if (btxn->bi->search_checksumming) {
            sds_result result = sds_bptree_crc32c_verify_node(target_node);
            if (result != SDS_SUCCESS) {
                return result;
            }
        }
#endif
    }
    target_node->parent = parent_node;
#ifdef SDS_DEBUG
    if (btxn->bi->search_checksumming) {
        sds_bptree_crc32c_update_node(target_node);
    }
#endif
    *target_out_node = target_node;
    return SDS_SUCCESS;
}
