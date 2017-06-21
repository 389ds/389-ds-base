/** BEGIN COPYRIGHT BLOCK
 * Copyright (c) 2016, William Brown <william at blackhats dot net dot au>
 * Copyright (c) 2017, Red Hat, Inc
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

/*
 * Contains the actual tree manipulation algorithms. This abstraction exists
 * so that code between the cow and non-cow version can be shared.
 */

#include "bpt.h"

sds_result
sds_bptree_search_node(sds_bptree_instance *binst, sds_bptree_node *root, void *key, sds_bptree_node **target_out_node) {
    sds_bptree_node *target_node = root;
    uint64_t i = 0;
    int64_t (*key_cmp_fn)(void *a, void *b) = binst->key_cmp_fn;

    /* We do this first, as we need the node to pass before we access it! */
#ifdef DEBUG
    if (binst->search_checksumming) {
        sds_result result = sds_bptree_crc32c_verify_instance(binst);
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
    while (target_node->level > 0) {
        while (i < target_node->item_count) {
            if (key_cmp_fn(key, (target_node)->keys[i]) < 0) {
                target_node = (sds_bptree_node *)target_node->values[i];
#ifdef DEBUG
                if (binst->search_checksumming) {
                    sds_result result = sds_bptree_crc32c_verify_node(target_node);
                    if (result != SDS_SUCCESS) {
                        return result;
                    }
                }
#endif
                i = 0;
                goto branch_loop;
            } else {
                i++;
            }
        }
        target_node = (sds_bptree_node *)target_node->values[target_node->item_count];
        i = 0;
    }
    *target_out_node = target_node;
    return SDS_SUCCESS;
}

sds_result
sds_bptree_search_internal(sds_bptree_instance *binst, sds_bptree_node *root, void *key) {

#ifdef DEBUG
    sds_log("sds_bptree_search_internal", "<== Beginning search of %d", key);
#endif

    sds_bptree_node *target_node;
#ifdef DEBUG
    sds_result result = sds_bptree_search_node(binst, root, key, &target_node);
    if (result != SDS_SUCCESS) {
        return result;
    }
#else
    sds_bptree_search_node(binst, root, key, &target_node);
#endif

    for (size_t i = 0; i < target_node->item_count; i++) {
        if (binst->key_cmp_fn(key, (target_node)->keys[i]) == 0) {
#ifdef DEBUG
            sds_log("sds_bptree_search_internal", "<== Completing search of %d", key);
#endif
            return SDS_KEY_PRESENT;
        }
    }
#ifdef DEBUG
    sds_log("sds_bptree_search_internal", "==> Failing search of %d", key);
#endif
    return SDS_KEY_NOT_PRESENT;
}


sds_result
sds_bptree_retrieve_internal(sds_bptree_instance *binst, sds_bptree_node *root, void *key, void **target) {
    // This is the public retrieve function
    // It's basically the same as search.
#ifdef DEBUG
    sds_log("sds_bptree_retrieve_internal", "==> Beginning retrieve of %d", key);
#endif
    sds_bptree_node *target_node = NULL;

#ifdef DEBUG
    if (binst->search_checksumming) {
        sds_result result = sds_bptree_crc32c_verify_instance(binst);
        if (result != SDS_SUCCESS) {
            return result;
        }
    }
#endif

#ifdef DEBUG
    sds_result result = sds_bptree_search_node(binst, root, key, &target_node);
    if (result != SDS_SUCCESS) {
        return result;
    }
#else
    sds_bptree_search_node(binst, root, key, &target_node);
#endif
    /* Now get the key from the node. */

#ifdef DEBUG
    sds_log("sds_bptree_retrieve_internal", "==> Completing retrieve of %d", key);
#endif
    return sds_bptree_node_retrieve_key(binst->key_cmp_fn, target_node, key, target);
}



