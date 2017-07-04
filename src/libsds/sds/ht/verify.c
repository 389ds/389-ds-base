/* BEGIN COPYRIGHT BLOCK
 * Copyright (c) 2017, Red Hat, Inc
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include "ht.h"

#ifdef SDS_DEBUG

void
sds_ht_crc32c_update_node(sds_ht_node *node)
{
    node->checksum = sds_crc32c(0, (const unsigned char *)node + sizeof(uint32_t), sizeof(sds_ht_node) - sizeof(uint32_t));
}

sds_result
sds_ht_crc32c_verify_node(sds_ht_node *node) {
    if (sds_crc32c(0, (const unsigned char *)node + sizeof(uint32_t), sizeof(sds_ht_node) - sizeof(uint32_t)) == node->checksum) {
        return SDS_SUCCESS;
    } else {
        return SDS_CHECKSUM_FAILURE;
    }
}

void
sds_ht_crc32c_update_instance(sds_ht_instance *inst)
{
    inst->checksum = sds_crc32c(0, (const unsigned char *)inst + sizeof(uint32_t), sizeof(sds_ht_instance) - sizeof(uint32_t));
}

sds_result
sds_ht_crc32c_verify_instance(sds_ht_instance *inst) {
    if (sds_crc32c(0, (const unsigned char *)inst + sizeof(uint32_t), sizeof(sds_ht_instance) - sizeof(uint32_t)) == inst->checksum) {
        return SDS_SUCCESS;
    } else {
        return SDS_CHECKSUM_FAILURE;
    }
}

void
sds_ht_crc32c_update_value(sds_ht_value *value) {
    value->checksum = sds_crc32c(0, (const unsigned char *)value + sizeof(uint32_t), sizeof(sds_ht_value) - sizeof(uint32_t));
}

sds_result
sds_ht_crc32c_verify_value(sds_ht_value *value) {
    if (sds_crc32c(0, (const unsigned char *)value + sizeof(uint32_t), sizeof(sds_ht_value) - sizeof(uint32_t)) == value->checksum) {
        return SDS_SUCCESS;
    } else {
        return SDS_CHECKSUM_FAILURE;
    }
}

#endif

sds_result
sds_ht_verify_node(sds_ht_instance *ht_ptr, sds_ht_node *node)
{
    // Do we need our depth so we can check the hashes?
    // Count that all the counts match slots.
    size_t count = 0;
    for (size_t i = 0; i < HT_SLOTS; i++) {
        sds_ht_slot *slot = &(node->slots[i]);
        if (slot->state != SDS_HT_EMPTY) {
            count++;
        }

        if (slot->state == SDS_HT_EMPTY) {
            if (slot->slot.value != NULL) {
#ifdef SDS_DEBUG
                sds_log("sds_ht_verify_node", "Failing ht_node_%p - invalid empty node c_slot 0x%"PRIu64, node, i);
#endif
                return SDS_INVALID_NODE;
            }
        } else if (slot->state == SDS_HT_VALUE) {
            if (slot->slot.value == NULL) {
#ifdef SDS_DEBUG
                sds_log("sds_ht_verify_node", "Failing ht_node_%p - invalid value pointer of NULL c_slot 0x%"PRIu64, node);
#endif
                return SDS_INVALID_POINTER;
            }
#ifdef SDS_DEBUG
            if (sds_ht_crc32c_verify_value(slot->slot.value) != SDS_SUCCESS) {
                sds_log("sds_ht_verify_node", "Failing ht_node_%p - invalid value checksum", node);
                return SDS_CHECKSUM_FAILURE;
            }
#endif
            // Check the value is sane.
        } else {
            // It's a branch
            if (slot->slot.node == NULL ) {
#ifdef SDS_DEBUG
                sds_log("sds_ht_verify_node", "Failing ht_node_%p - invalid branch, can not be NULL", node);
#endif
                return SDS_INVALID_POINTER;
            }
        }

    }
    if (count != node->count) {
#ifdef SDS_DEBUG
        sds_log("sds_ht_verify_node", "Failing ht_node_%p - invalid item count, doesn't match", node);
#endif
        return SDS_INVALID_NODE;
    }

    // Check that our parent and parent slot match
    if (node->parent != NULL || node->parent_slot != 0) {
        if (node->count == 0) {
#ifdef SDS_DEBUG
            sds_log("sds_ht_verify_node", "Failing ht_node_%p - invalid item count of 0", node);
#endif
            return SDS_INVALID_NODE;
        }
        sds_ht_slot *ex_p_slot = &(node->parent->slots[node->parent_slot]);
        if (ex_p_slot->state != SDS_HT_BRANCH) {
#ifdef SDS_DEBUG
            sds_log("sds_ht_verify_node", "Failing ht_node_%p - invalid parent, slot state is not branch", node);
#endif
            return SDS_INVALID_POINTER;
        }
        if (ex_p_slot->slot.node != node) {
#ifdef SDS_DEBUG
            sds_log("sds_ht_verify_node", "Failing ht_node_%p - invalid parent, slot node pointer is not to us", node);
#endif
            return SDS_INVALID_POINTER;
        }
    }

#ifdef SDS_DEBUG
    sds_log("sds_ht_verify_node", "ht_node_%p depth= %"PRIu64" count=%"PRIu64, node, node->depth, node->count);
#endif
    return SDS_SUCCESS;
}

sds_result
sds_ht_verify(sds_ht_instance *ht_ptr)
{
#ifdef SDS_DEBUG
    //verify instance
    if (sds_ht_crc32c_verify_instance(ht_ptr) != SDS_SUCCESS) {
        return SDS_CHECKSUM_FAILURE;
    }
#endif
    // Check instance properties
    // Map all our nodes
    return sds_ht_map_nodes(ht_ptr, sds_ht_verify_node);
}

