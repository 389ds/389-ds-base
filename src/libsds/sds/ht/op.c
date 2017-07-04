/** BEGIN COPYRIGHT BLOCK
 * Copyright (c) 2017, Red Hat, Inc
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include "ht.h"
#include <assert.h>

inline static size_t __attribute__((always_inline))
sds_ht_hash_slot(int64_t depth, uint64_t hash)
{
#ifdef SDS_DEBUG
    assert(depth <= 15 && depth >= 0);
#endif
    size_t c_slot = (hash >> (depth * 4)) & 0xF;
#ifdef SDS_DEBUG
    assert(c_slot < 16);
#endif
    return c_slot;
}

sds_result
sds_ht_insert(sds_ht_instance *ht_ptr, void *key, void *value)
{
#ifdef SDS_DEBUG
    sds_log("sds_ht_insert", "==> begin");
    if (sds_ht_crc32c_verify_instance(ht_ptr) != SDS_SUCCESS) {
        return SDS_CHECKSUM_FAILURE;
    }
#endif

    if (key == NULL) {
        return SDS_INVALID_KEY;
    }

    size_t key_size = ht_ptr->key_size_fn(key);
    if (key_size == 0) {
        return SDS_INVALID_KEY;
    }

    // Use an internal search to find the node and slot we need to occupy
    uint64_t hashout = sds_siphash13(key, ht_ptr->key_size_fn(key), ht_ptr->hkey);
#ifdef SDS_DEBUG
    sds_log("sds_ht_insert", "hash key %p -> 0x%"PRIx64, key, hashout);
#endif
    int64_t depth = 15;
    size_t c_slot = 0;
    sds_ht_node *work_node = ht_ptr->root;
    sds_ht_slot *slot = NULL;

    while (depth >= 0) {
        c_slot = sds_ht_hash_slot(depth, hashout);
#ifdef SDS_DEBUG
        if (sds_ht_crc32c_verify_node(work_node) != SDS_SUCCESS) {
            sds_log("sds_ht_insert", "ht_node_%p failed verification", work_node);
            return SDS_CHECKSUM_FAILURE;
        }

        sds_log("sds_ht_insert", "at ht_node_%p depth %"PRIu64" c_slot 0x%"PRIx64, work_node, depth, c_slot);
#endif
        slot = &(work_node->slots[c_slot]);
        // Now look at the slot, and see if it's full, empty or branch.
        if (slot->state == SDS_HT_EMPTY) {
#ifdef SDS_DEBUG
            sds_log("sds_ht_insert", "c_slot 0x%"PRIx64" state=EMPTY", c_slot);
#endif
            // This is where we can insert.
            work_node->count += 1;
            slot->state = SDS_HT_VALUE;
            // Remember to dup the key.
            slot->slot.value = sds_ht_value_create(ht_ptr->key_dup_fn(key), value);
#ifdef SDS_DEBUG
            sds_ht_crc32c_update_node(work_node);
            sds_log("sds_ht_insert", "<== complete");
#endif
            return SDS_SUCCESS;
        } else if (slot->state == SDS_HT_BRANCH) {
#ifdef SDS_DEBUG
            sds_log("sds_ht_insert", "c_slot 0x%"PRIx64" state=BRANCH", c_slot);
#endif
            depth--;
            work_node = slot->slot.node;
            // Keep looping!
        } else {
#ifdef SDS_DEBUG
            sds_log("sds_ht_insert", "ht_node_%p at c_slot 0x%"PRIx64" state=VALUE", work_node, c_slot);
#endif
            // Must be a value, let's break out and process it.
            if (ht_ptr->key_cmp_fn(key, slot->slot.value->key) == 0) {
                // Yep, it's a dup.
                return SDS_KEY_PRESENT;
#ifdef SDS_DEBUG
                sds_log("sds_ht_insert", "<== complete");
#endif
            }
            // Must be a new key. Lets make a new node, put it into the chain.
            if (depth > 0) {
                depth--;
                sds_ht_node *new_node = sds_ht_node_create();
                // Move our existing value to the new node.
                uint64_t ex_hashout = sds_siphash13(slot->slot.value->key, ht_ptr->key_size_fn(slot->slot.value->key), ht_ptr->hkey);
                size_t ex_c_slot = sds_ht_hash_slot(depth, ex_hashout);
                sds_ht_slot *ex_slot = &(new_node->slots[ex_c_slot]);
                ex_slot->state = SDS_HT_VALUE;
                ex_slot->slot.value = slot->slot.value;
#ifdef SDS_DEBUG
                sds_log("sds_ht_insert", "existing c_slot 0x%"PRIx64" to new ht_node_%p c_slot 0x%"PRIx64, c_slot, new_node, ex_c_slot);
                new_node->depth = depth;
#endif
                new_node->parent = work_node;
                new_node->parent_slot = c_slot;
                new_node->count = 1;
                // Now make the existing worknode slot point to our new leaf..
                slot->state = SDS_HT_BRANCH;
                slot->slot.node = new_node;
#ifdef SDS_DEBUG
                sds_ht_crc32c_update_node(work_node);
                sds_ht_crc32c_update_node(new_node);
#endif
                // The next work node is the new_node.
                work_node = new_node;
                // That's it!
            } else {
                // We are at the end of the tree, and have a hash collision.
                // append to the value chain.
                assert(1 == 0);
            }
        }
    }
    return SDS_UNKNOWN_ERROR;
}

sds_result
sds_ht_search(sds_ht_instance *ht_ptr, void *key, void **value)
{
    // Search the tree. if key is found, SDS_KEY_PRESENT and *value is set.
    // Else, SDS_KEY_NOT_PRESENT
#ifdef SDS_DEBUG
    sds_log("sds_ht_search", "==> begin");
    if (sds_ht_crc32c_verify_instance(ht_ptr) != SDS_SUCCESS) {
        return SDS_CHECKSUM_FAILURE;
    }
#endif

    if (key == NULL) {
        return SDS_INVALID_KEY;
    }

    if (value == NULL) {
        return SDS_INVALID_POINTER;
    }

    size_t key_size = ht_ptr->key_size_fn(key);
    if (key_size == 0) {
        return SDS_INVALID_KEY;
    }

    // Use an internal search to find the node and slot we need to occupy
    uint64_t hashout = sds_siphash13(key, ht_ptr->key_size_fn(key), ht_ptr->hkey);
#ifdef SDS_DEBUG
    sds_log("sds_ht_search", "hash key %p -> 0x%"PRIx64, key, hashout);
#endif
    int64_t depth = 15;
    size_t c_slot = 0;
    sds_ht_node *work_node = ht_ptr->root;
    sds_ht_slot *slot = NULL;

    while (depth >= 0) {
        c_slot = sds_ht_hash_slot(depth, hashout);
#ifdef SDS_DEBUG
        if (sds_ht_crc32c_verify_node(work_node) != SDS_SUCCESS) {
            sds_log("sds_ht_search", "ht_node_%p failed verification", work_node);
            sds_log("sds_ht_search", "==> complete");
            return SDS_CHECKSUM_FAILURE;
        }
        sds_log("sds_ht_search", "depth %"PRIu64" c_slot 0x%"PRIx64, depth, c_slot);
#endif
        slot = &(work_node->slots[c_slot]);

        if (slot->state == SDS_HT_BRANCH) {
            // Keep going ....
            work_node = slot->slot.node;
            depth--;
        } else if (slot->state == SDS_HT_VALUE) {
            // Check the key realy does match .....
            if (ht_ptr->key_cmp_fn(key, slot->slot.value->key) == 0) {
                // WARNING: If depth == 0, check for LL
                *value = slot->slot.value->value;
#ifdef SDS_DEBUG
                sds_log("sds_ht_search", "<== complete");
#endif
                return SDS_KEY_PRESENT;
            } else {
#ifdef SDS_DEBUG
                sds_log("sds_ht_search", "<== complete");
#endif
                return SDS_KEY_NOT_PRESENT;
            }
        } else {
            // We got to the hash point where this should be but it's not here ....
#ifdef SDS_DEBUG
            sds_log("sds_ht_search", "==> complete");
#endif
            return SDS_KEY_NOT_PRESENT;
        }
    }
    return SDS_UNKNOWN_ERROR;
}


inline static void __attribute__((always_inline))
sds_ht_node_cleanup(sds_ht_instance *ht_ptr, sds_ht_node *node)
{
    // Okay, we start at *node, and it only has 1 value left.
    sds_ht_node *work_node = node;
    sds_ht_node *parent_node = node->parent;
    while (work_node->count <= 1 && parent_node != NULL) {
#ifdef SDS_DEBUG
        sds_log("sds_ht_node_cleanup", "Cleaning ht_node_%p into parent ht_node_%p", node, parent_node);
        sds_result post_result = sds_ht_verify_node(ht_ptr, work_node);
        if (post_result != SDS_SUCCESS) {
            sds_log("sds_ht_delete", "ht_node_%p failed verification post delete!", work_node);
            sds_log("sds_ht_delete", "==> complete");
            assert(1 == 0);
        }
#endif
        // We need to know where we are in the parent.
        sds_ht_slot *ex_p_slot = &(parent_node->slots[work_node->parent_slot]);
#ifdef SDS_DEBUG
        sds_log("sds_ht_node_cleanup", "Slot %p of parent ht_node_%p", work_node->parent_slot, parent_node);
#endif

        // Get our remaining slot out. We don't know where it is though ...
        sds_ht_slot *ex_r_slot = NULL;
        size_t r_slot = 0;
        for (; r_slot < HT_SLOTS; r_slot++) {
            ex_r_slot = &(work_node->slots[r_slot]);
            if (ex_r_slot->state == SDS_HT_VALUE) {
                break;
            } else if (ex_r_slot->state == SDS_HT_BRANCH) {
                // We can't do anything to this, just bail.
                return;
            }
        }
        assert (r_slot < HT_SLOTS);

#ifdef SDS_DEBUG
        sds_log("sds_ht_node_cleanup", "Remaining slot %p of ht_node_%p", r_slot, work_node);
#endif

        // Now, put our remaining slot into the parent.
        ex_p_slot->state = SDS_HT_VALUE;
#ifdef SDS_DEBUG
        sds_log("sds_ht_node_cleanup", "Move slot %p of ht_node_%p to %p of ht_node_%p", r_slot, work_node, work_node->parent_slot, parent_node);
#endif
        ex_p_slot->slot.value = ex_r_slot->slot.value;
        // And clean the old node.
        ex_r_slot->slot.value = NULL;
        ex_r_slot->state = SDS_HT_EMPTY;
        // Now we can free our node.
        sds_ht_node_destroy(ht_ptr, work_node);

        work_node = parent_node;
        parent_node = work_node->parent;
#ifdef SDS_DEBUG
        sds_ht_crc32c_update_node(work_node);
#endif
    }
}


sds_result
sds_ht_delete(sds_ht_instance *ht_ptr, void *key)
{
    // Search the tree. if key is found, SDS_KEY_PRESENT and *value is set.
    // Else, SDS_KEY_NOT_PRESENT
#ifdef SDS_DEBUG
    sds_log("sds_ht_delete", "==> begin");
    if (sds_ht_crc32c_verify_instance(ht_ptr) != SDS_SUCCESS) {
        return SDS_CHECKSUM_FAILURE;
    }
#endif

    if (key == NULL) {
        return SDS_INVALID_KEY;
    }

    size_t key_size = ht_ptr->key_size_fn(key);
    if (key_size == 0) {
        return SDS_INVALID_KEY;
    }

    // Use an internal search to find the node and slot we need to occupy
    uint64_t hashout = sds_siphash13(key, ht_ptr->key_size_fn(key), ht_ptr->hkey);
#ifdef SDS_DEBUG
    sds_log("sds_ht_delete", "hash key %p -> 0x%"PRIx64, key, hashout);
#endif
    int64_t depth = 15;
    size_t c_slot = 0;
    sds_ht_node *work_node = ht_ptr->root;
    sds_ht_slot *slot = NULL;

    while (depth >= 0) {
        c_slot = sds_ht_hash_slot(depth, hashout);
#ifdef SDS_DEBUG
        sds_result result = sds_ht_verify_node(ht_ptr, work_node);
        if (result != SDS_SUCCESS) {
            sds_log("sds_ht_delete", "ht_node_%p failed verification", work_node);
            sds_log("sds_ht_delete", "==> complete");
            return result;
        }
        sds_log("sds_ht_delete", "depth %"PRIu64" c_slot 0x%"PRIx64, depth, c_slot);
#endif
        slot = &(work_node->slots[c_slot]);

        if (slot->state == SDS_HT_BRANCH) {
            // Keep going ....
            work_node = slot->slot.node;
            depth--;
        } else if (slot->state == SDS_HT_EMPTY) {
            // We got to the hash point where this should be but it's not here ....
#ifdef SDS_DEBUG
            sds_log("sds_ht_delete", "==> complete");
#endif
            return SDS_KEY_NOT_PRESENT;
        } else {
            if (ht_ptr->key_cmp_fn(key, slot->slot.value->key) == 0) {
                // WARNING: If depth == 0, check for LL
#ifdef SDS_DEBUG
                sds_log("sds_ht_delete", "deleting from ht_node_%p", work_node);
#endif
                // Free the value, this frees the key + value.
                sds_ht_value_destroy(ht_ptr, slot->slot.value);
                slot->slot.value = NULL;
                slot->state = SDS_HT_EMPTY;
                work_node->count--;
#ifdef SDS_DEBUG
                sds_ht_crc32c_update_node(work_node);
#endif
                // How much left in this node? if <= 1, need to start merging up.
                sds_ht_node_cleanup(ht_ptr, work_node);
#ifdef SDS_DEBUG
                sds_log("sds_ht_delete", "<== complete");
#endif
                return SDS_KEY_PRESENT;
            } else {
#ifdef SDS_DEBUG
                sds_log("sds_ht_delete", "<== complete");
#endif
                return SDS_KEY_NOT_PRESENT;
            }
        }
    }
    return SDS_UNKNOWN_ERROR;
}

