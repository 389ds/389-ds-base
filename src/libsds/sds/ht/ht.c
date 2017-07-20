/* BEGIN COPYRIGHT BLOCK
 * Copyright (c) 2017, Red Hat, Inc
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include "ht.h"

sds_result
sds_ht_init(sds_ht_instance **ht_ptr,
            int64_t (*key_cmp_fn)(void *a, void *b),
            void (*value_free_fn)(void *value),
            void *(*key_dup_fn)(void *key),
            void (*key_free_fn)(void *key),
            uint64_t (*key_size_fn)(void *key))
{
    if (ht_ptr == NULL) {
#ifdef SDS_DEBUG
        sds_log("sds_ht_init", "Invalid pointer");
#endif
        return SDS_NULL_POINTER;
    }

    *ht_ptr = sds_calloc(sizeof(sds_ht_instance));
    (*ht_ptr)->key_cmp_fn = key_cmp_fn;
    (*ht_ptr)->key_size_fn = key_size_fn;
    (*ht_ptr)->key_dup_fn = key_dup_fn;
    (*ht_ptr)->key_free_fn = key_free_fn;
    (*ht_ptr)->value_free_fn = value_free_fn;
    // Need value dup also?

    (*ht_ptr)->root = sds_ht_node_create();

#ifdef SDS_DEBUG
    (*ht_ptr)->root->depth = 15;
    sds_ht_crc32c_update_node((*ht_ptr)->root);
    sds_ht_crc32c_update_instance(*ht_ptr);
#endif

    return SDS_SUCCESS;
}

sds_result
sds_ht_destroy(sds_ht_instance *ht_ptr)
{
    if (ht_ptr == NULL) {
        return SDS_NULL_POINTER;
    }

#ifdef SDS_DEBUG
    if (sds_ht_crc32c_verify_instance(ht_ptr) != SDS_SUCCESS) {
        return SDS_CHECKSUM_FAILURE;
    }
#endif
    // Free the tree
    sds_result result = sds_ht_map_nodes(ht_ptr, sds_ht_node_destroy);
    if (result != SDS_SUCCESS) {
#ifdef SDS_DEBUG
        sds_log("sds_ht_destroy", "Failed to destroy instance %d\n", result);
#endif
        return result;
    }
    // Free the instance
    sds_free(ht_ptr);
    // Done!
    return SDS_SUCCESS;
}
