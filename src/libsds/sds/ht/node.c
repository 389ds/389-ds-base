/* BEGIN COPYRIGHT BLOCK
 * Copyright (c) 2017, Red Hat, Inc
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include "ht.h"

sds_ht_node *
sds_ht_node_create(void)
{
    sds_ht_node *node = sds_calloc(sizeof(sds_ht_node));
#ifdef SDS_DEBUG
    sds_log("sds_ht_node_create", "Creating ht_node_%p", node);
#endif
    return node;
}

sds_ht_value *
sds_ht_value_create(void *key, void *value)
{
    sds_ht_value *ht_value = sds_calloc(sizeof(sds_ht_value));
#ifdef SDS_DEBUG
    sds_log("sds_ht_value_create", "Creating ht_value_%p", ht_value);
#endif
    ht_value->key = key;
    ht_value->value = value;
#ifdef SDS_DEBUG
    sds_ht_crc32c_update_value(ht_value);
#endif
    return ht_value;
}

void
sds_ht_value_destroy(sds_ht_instance *ht_ptr, sds_ht_value *value)
{
#ifdef SDS_DEBUG
    sds_log("sds_ht_value_destroy", "Destroying ht_value_%p", value);
#endif
    ht_ptr->key_free_fn(value->key);
    if (value->value) {
        ht_ptr->value_free_fn(value->value);
    }
    sds_free(value);
}

sds_result
sds_ht_node_destroy(sds_ht_instance *ht_ptr, sds_ht_node *node)
{
#ifdef SDS_DEBUG
    sds_log("sds_ht_node_destroy", "Destroying ht_node_%p", node);
#endif
    for (size_t i = 0; i < HT_SLOTS; i++) {
        sds_ht_slot slot = node->slots[i];
        if (slot.state == SDS_HT_VALUE) {
            sds_ht_value_destroy(ht_ptr, slot.slot.value);
        }
    }
    sds_free(node);
    return SDS_SUCCESS;
}
