/** BEGIN COPYRIGHT BLOCK
 * Copyright (c) 2016, William Brown <william at blackhats dot net dot au>
 * All rights reserved.
 *
 * License: License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#pragma once

#include "../bpt/bpt.h"

/* Transaction management */

sds_bptree_transaction *sds_bptree_txn_create(sds_bptree_cow_instance *binst);
sds_result sds_bptree_cow_txn_destroy_all(sds_bptree_cow_instance *binst);

/* Verification */

#ifdef DEBUG
sds_result sds_bptree_crc32c_verify_cow_instance(sds_bptree_cow_instance *binst);
void sds_bptree_crc32c_update_cow_instance(sds_bptree_cow_instance *binst);
sds_result sds_bptree_crc32c_verify_btxn(sds_bptree_transaction *btxn);
void sds_bptree_crc32c_update_btxn(sds_bptree_transaction *btxn);
#endif
sds_result sds_bptree_cow_verify_node(sds_bptree_instance *binst, sds_bptree_node *node);

/* txn node manipulation */

sds_bptree_node * sds_bptree_cow_node_prepare(sds_bptree_transaction *btxn, sds_bptree_node *node);
sds_bptree_node * sds_bptree_cow_node_create(sds_bptree_transaction *btxn);
void sds_bptree_cow_node_update(sds_bptree_transaction *btxn, sds_bptree_node *node, void *key, void *value);
void sds_bptree_cow_node_siblings(sds_bptree_node *target, sds_bptree_node **left, sds_bptree_node **right);

void sds_bptree_cow_leaf_compact(sds_bptree_transaction *btxn, sds_bptree_node *left, sds_bptree_node *right);
void sds_bptree_cow_root_promote(sds_bptree_transaction *btxn, sds_bptree_node *root);
void sds_bptree_cow_branch_compact(sds_bptree_transaction *btxn, sds_bptree_node *left, sds_bptree_node *right);

/* Insert algo parts */

void sds_bptree_cow_leaf_split_and_insert(sds_bptree_transaction *btxn, sds_bptree_node *left_node, void *key, void *value);

/* Misc */

sds_result sds_bptree_cow_display(sds_bptree_transaction *btxn);

sds_result sds_bptree_search_node_path(sds_bptree_transaction *btxn, void *key, sds_bptree_node **target_out_node);
