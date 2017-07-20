/** BEGIN COPYRIGHT BLOCK
 * Copyright (c) 2016, William Brown <william at blackhats dot net dot au>
 * Copyright (c) 2017, Red Hat, Inc
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include "bpt.h"

/* Node checksumming functions. */
#ifdef SDS_DEBUG

sds_result
sds_bptree_crc32c_verify_instance(sds_bptree_instance *binst)
{
    // This starts the check *after* the checksum in the struct
    if (sds_crc32c(0, (const unsigned char *)binst + sizeof(uint32_t), sizeof(sds_bptree_instance) - sizeof(uint32_t)) == binst->checksum) {
        return SDS_SUCCESS;
    } else {
        return SDS_CHECKSUM_FAILURE;
    }
}

void
sds_bptree_crc32c_update_instance(sds_bptree_instance *binst)
{
    // This starts the check *after* the checksum in the struct
    binst->checksum = sds_crc32c(0, (const unsigned char *)binst + sizeof(uint32_t), sizeof(sds_bptree_instance) - sizeof(uint32_t));
}

sds_result
sds_bptree_crc32c_verify_node(sds_bptree_node *node)
{
    // This starts the check *after* the checksum in the struct
    uint32_t checksum = sds_crc32c(0, (const unsigned char *)node + sizeof(uint32_t), sizeof(sds_bptree_node) - sizeof(uint32_t));
    if (checksum == node->checksum) {
        return SDS_SUCCESS;
    } else {
        return SDS_CHECKSUM_FAILURE;
    }
}

void
sds_bptree_crc32c_update_node(sds_bptree_node *node)
{
    // printf("sds_bptree_update_crc_node: node_%p\n", node);
    node->checksum = sds_crc32c(0, (const unsigned char *)node + sizeof(uint32_t), sizeof(sds_bptree_node) - sizeof(uint32_t));
}

/*
sds_result
sds_bptree_crc32c_verify_value(sds_bptree_value *value) {
    // This starts the check *after* the checksum in the struct
    if (sds_crc32c(0, (const unsigned char *)value + sizeof(uint32_t), sizeof(sds_bptree_value) - sizeof(uint32_t)) == value->checksum) {
        // Now we check the data itself.
        if (sds_crc32c(0, (const unsigned char *)value->data, value->size) == value->data_checksum) {
            return SDS_SUCCESS;
        }
    }
    return SDS_CHECKSUM_FAILURE;
}

void
sds_bptree_crc32c_update_value(sds_bptree_value *value) {
    // This starts the check *after* the checksum in the struct
    value->data_checksum = sds_crc32c(0, (const unsigned char *)value->data, value->size);
    value->checksum = sds_crc32c(0, (const unsigned char *)value + sizeof(uint32_t), sizeof(sds_bptree_value) - sizeof(uint32_t));
}
*/


#endif /* DEBUG */
