/** BEGIN COPYRIGHT BLOCK
 * Copyright (c) 2016, William Brown <william at blackhats dot net dot au>
 * Copyright (c) 2017, Red Hat, Inc
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include "bpt.h"
/* This is just used by the sets to clone instances */

sds_result
sds_bptree_instance_clone(sds_bptree_instance *binst, sds_bptree_instance **binst_ptr) {
    if (binst_ptr == NULL) {
#ifdef SDS_DEBUG
        sds_log("sds_btree_init", "Invalid pointer");
#endif
        return SDS_NULL_POINTER;
    }

    *binst_ptr = sds_memalign(sizeof(sds_bptree_instance), SDS_CACHE_ALIGNMENT);
    (*binst_ptr)->print_iter = 0;
    (*binst_ptr)->offline_checksumming = binst->offline_checksumming;
    (*binst_ptr)->search_checksumming = binst->search_checksumming;
    (*binst_ptr)->key_cmp_fn = binst->key_cmp_fn;
    (*binst_ptr)->value_free_fn = binst->value_free_fn;
    (*binst_ptr)->key_free_fn = binst->key_free_fn;
    (*binst_ptr)->key_dup_fn = binst->key_dup_fn;

    // Now update the checksums
#ifdef SDS_DEBUG
    if ((*binst_ptr)->offline_checksumming) {
        sds_bptree_crc32c_update_instance(*binst_ptr);
    }
#endif
    return SDS_SUCCESS;
}

/* If we can, get the requested key */
sds_result
sds_bptree_list_advance(sds_bptree_node **item, size_t *index) {

#ifdef SDS_DEBUG
    sds_log("sds_bptree_list_advance", "%p current index is %" PRIu64"", *item, *index);
#endif
    /* Now, if we have the ability */
    if (*index < (size_t)((*item)->item_count - 1)) {
        *index += 1;
        return SDS_SUCCESS;
    } else {
        sds_bptree_node *next = (sds_bptree_node *)(*item)->values[SDS_BPTREE_DEFAULT_CAPACITY];
        if (next != NULL) {
            *index = 0;
            *item = next;
            return SDS_SUCCESS;
        } else {
            /* index is max, and no more nodes. */
            return SDS_LIST_EXHAUSTED;
        }
    }
}

/* Tree mapping functions */
/* Shouldn't this make a set of results? */
sds_result
sds_bptree_map(sds_bptree_instance *binst, void (*fn)(void *k, void *v)) {
    /* If this is the non-cow tree, this is easy. */
    /* Find the bottom left node, then iterate to the right! */
    sds_bptree_node *work_node = sds_bptree_node_min(binst);

    while (work_node != NULL) {
        for (size_t index = 0; index < work_node->item_count; index++) {
            fn(work_node->keys[index], work_node->values[index]);
        }
        work_node = (sds_bptree_node *)work_node->values[SDS_BPTREE_DEFAULT_CAPACITY];
    }
    return SDS_SUCCESS;
}

sds_result sds_bptree_filter(sds_bptree_instance *binst_a, int64_t (*fn)(void *k, void *v), sds_bptree_instance **binst_subset) {
    /* */
    sds_result result = sds_bptree_instance_clone(binst_a, binst_subset);
    if (result != SDS_SUCCESS) {
        return result;
    }

    sds_bptree_node *work_node = sds_bptree_node_min(binst_a);
    int64_t presult = 0;
    sds_bptree_node *result_root = sds_bptree_node_create();
    sds_bptree_node *result_ptr = result_root;

    while (work_node != NULL) {
        for (size_t index = 0; index < work_node->item_count; index++) {
            presult = fn(work_node->keys[index], work_node->values[index]);
            if (presult != 0) {
                // Add the item to the new set.
                /* WHAT DO WE DO WITH VALUES */
                void *key = binst_a->key_dup_fn(work_node->keys[index]);
                sds_bptree_node_list_append(&result_ptr, key, NULL);
            }
        }
        work_node = (sds_bptree_node *)work_node->values[SDS_BPTREE_DEFAULT_CAPACITY];
    }

    return sds_bptree_node_list_to_tree(*binst_subset, result_root);
}

static sds_result
sds_bptree_set_operation(sds_bptree_instance *binst_a, sds_bptree_instance *binst_b, sds_bptree_instance **binst_out, uint16_t both, uint16_t diff, uint16_t alist) {

    /* Based on a set of flags we choose to include the element from:
     * - Both lists
     * - If it is only in one list or the other.
     * - if it is only in A list.
     *
     */
    sds_result result = SDS_SUCCESS;
    /* Make sure that our function pointers are all the same .... */
    if (binst_a->key_cmp_fn != binst_b->key_cmp_fn || binst_a->value_free_fn != binst_b->value_free_fn ||
        binst_a->key_free_fn != binst_b->key_free_fn || binst_a->key_dup_fn != binst_b->key_dup_fn) {
        return SDS_INCOMPATIBLE_INSTANCE;
    }
    /* Make the output instance. */
    result = sds_bptree_instance_clone(binst_a, binst_out);
    if (result != SDS_SUCCESS) {
        return result;
    }

    int64_t (*key_cmp_fn)(void *a, void *b) = binst_a->key_cmp_fn;
    void *(*key_dup_fn)(void *key) = binst_a->key_dup_fn;

    /* Need a pointer to track node_a and index_a, vs node_b and index_b */
    sds_bptree_node *node_a = sds_bptree_node_min(binst_a);
    size_t index_a = 0;
    sds_result result_a = SDS_SUCCESS;
    sds_bptree_node *node_b = sds_bptree_node_min(binst_b);
    size_t index_b = 0;
    sds_result result_b = SDS_SUCCESS;
    /* As for the new instance, we can just keep the left node, and build */
    sds_bptree_node *result_root = sds_bptree_node_create();
    sds_bptree_node *result_ptr = result_root;

    /* Can't rely on keys being zero, so check item count */
    if (node_a->item_count == 0) {
        result_a = SDS_LIST_EXHAUSTED;
    }
    if (node_b->item_count == 0) {
        result_b = SDS_LIST_EXHAUSTED;
    }

    /* We have to handle two cases;
     * - both sets have "some" content, so we can actually do comparisons
     * - one set is empty, so we can *never* compare them.
     */

    if (result_a != SDS_LIST_EXHAUSTED && result_b != SDS_LIST_EXHAUSTED) {
        /* Both lists have at least *some* content, so this comparison works. */

        /* Now iterate over the values. */
        while (result_a == SDS_SUCCESS && result_b == SDS_SUCCESS) {
            int64_t cmp = key_cmp_fn(node_a->keys[index_a], node_b->keys[index_b]);
            if (cmp == 0) {
                /* These values are the same, advance! */
                if (both) {
                    void *key = key_dup_fn(node_a->keys[index_a]);
                    sds_bptree_node_list_append(&result_ptr, key, NULL);
                }
                result_a = sds_bptree_list_advance(&node_a, &index_a);
                result_b = sds_bptree_list_advance(&node_b, &index_b);
            } else if (cmp < 0) {
                /* If A is smaller, we advance a, and include the value */
                /* !! WHAT ABOUT VALUE DUPLICATION!!! */
                if (diff || alist) {
                    void *key = key_dup_fn(node_a->keys[index_a]);
                    sds_bptree_node_list_append(&result_ptr, key, NULL);
                }
                result_a = sds_bptree_list_advance(&node_a, &index_a);
            } else {
                /* !! WHAT ABOUT VALUE DUPLICATION!!! */
                if (diff) {
                    void *key = key_dup_fn(node_b->keys[index_b]);
                    sds_bptree_node_list_append(&result_ptr, key, NULL);
                }
                result_b = sds_bptree_list_advance(&node_b, &index_b);
            }
        }

        /* We have now exhausted a list. Which one? */
        while (result_a == SDS_SUCCESS) {
            int64_t cmp = key_cmp_fn(node_a->keys[index_a], node_b->keys[index_b]);
            /* We have exhausted B. Finish iterating */
            if (cmp == 0) {
                if (both) {
                    void *key = key_dup_fn(node_a->keys[index_a]);
                    /* !! WHAT ABOUT VALUE DUPLICATION!!! */
                    sds_bptree_node_list_append(&result_ptr, key, NULL);
                }
            } else if (cmp != 0) {
                if (diff || alist) {
                    void *key = key_dup_fn(node_a->keys[index_a]);
                    /* !! WHAT ABOUT VALUE DUPLICATION!!! */
                    sds_bptree_node_list_append(&result_ptr, key, NULL);
                }
            }
            result_a = sds_bptree_list_advance(&node_a, &index_a);
        }

        while (result_b == SDS_SUCCESS) {
            int64_t cmp = key_cmp_fn(node_a->keys[index_a], node_b->keys[index_b]);
            if (cmp == 0) {
                if (both) {
                    void *key = key_dup_fn(node_b->keys[index_b]);
                    /* !! WHAT ABOUT VALUE DUPLICATION!!! */
                    sds_bptree_node_list_append(&result_ptr, key, NULL);
                }
            } else if (cmp != 0) {
                if (diff) {
                    void *key = key_dup_fn(node_b->keys[index_b]);
                    /* !! WHAT ABOUT VALUE DUPLICATION!!! */
                    sds_bptree_node_list_append(&result_ptr, key, NULL);
                }
            }
            result_b = sds_bptree_list_advance(&node_b, &index_b);
        }
    } else {
        /* One of the lists *is* empty from the start, so just shortcut
         * as we can't do a comparison.
         */
        /* because one is empty the lists "always differ" */
        while (result_a == SDS_SUCCESS) {
            /* We have exhausted B. Finish iterating */
            if (diff || alist) {
                void *key = key_dup_fn(node_a->keys[index_a]);
                /* !! WHAT ABOUT VALUE DUPLICATION!!! */
                sds_bptree_node_list_append(&result_ptr, key, NULL);
            }
            result_a = sds_bptree_list_advance(&node_a, &index_a);
        }

        while (result_b == SDS_SUCCESS) {
            if (diff) {
                void *key = key_dup_fn(node_b->keys[index_b]);
                /* !! WHAT ABOUT VALUE DUPLICATION!!! */
                sds_bptree_node_list_append(&result_ptr, key, NULL);
            }
            result_b = sds_bptree_list_advance(&node_b, &index_b);
        }
    }

    /* Do a tree build from the results */
    /* All done! */
    return sds_bptree_node_list_to_tree(*binst_out, result_root);
}




sds_result
sds_bptree_difference(sds_bptree_instance *binst_a, sds_bptree_instance *binst_b, sds_bptree_instance **binst_difference) {
    return sds_bptree_set_operation(binst_a, binst_b, binst_difference, 0, 1, 0);
}

sds_result
sds_bptree_union(sds_bptree_instance *binst_a, sds_bptree_instance *binst_b, sds_bptree_instance **binst_union) {
    return sds_bptree_set_operation(binst_a, binst_b, binst_union, 1, 1, 0);
}

sds_result
sds_bptree_intersect(sds_bptree_instance *binst_a, sds_bptree_instance *binst_b, sds_bptree_instance **binst_intersect) {
    return sds_bptree_set_operation(binst_a, binst_b, binst_intersect, 1, 0, 0);
}

sds_result
sds_bptree_compliment(sds_bptree_instance *binst_a, sds_bptree_instance *binst_b, sds_bptree_instance **binst_compliment) {
    return sds_bptree_set_operation(binst_a, binst_b, binst_compliment, 0, 0, 1);
}

