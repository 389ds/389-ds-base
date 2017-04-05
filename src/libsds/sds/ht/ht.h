/* BEGIN COPYRIGHT BLOCK
 * Copyright (c) 2017, Red Hat, Inc
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#pragma once

#include "../sds_internal.h"
#include <sds.h>

/* Internal tree functions */

/* Node manipulations */

sds_ht_node *sds_ht_node_create(void);
sds_result sds_ht_node_destroy(sds_ht_instance *ht_ptr, sds_ht_node *node);
sds_result sds_ht_verify_node(sds_ht_instance *ht_ptr, sds_ht_node *node);

sds_result sds_ht_map_nodes(sds_ht_instance *ht_ptr, sds_result (*map_fn)(sds_ht_instance *ht_ptr, sds_ht_node *node));

sds_ht_value *sds_ht_value_create(void *key, void *value);

void sds_ht_value_destroy(sds_ht_instance *ht_ptr, sds_ht_value *value);

/* verification */
void sds_ht_crc32c_update_node(sds_ht_node *node);
void sds_ht_crc32c_update_instance(sds_ht_instance *inst);
void sds_ht_crc32c_update_value(sds_ht_value *value);


sds_result sds_ht_crc32c_verify_node(sds_ht_node *node);
sds_result sds_ht_crc32c_verify_instance(sds_ht_instance *inst);



